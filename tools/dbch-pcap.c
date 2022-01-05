#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>


// Windows build for Marco?
// only exits when data is received / output to pipe fails?


static int p;


// helpers

static void bail(const char *fmt, ...)
{
	va_list l;
	va_start(l, fmt);
	vfprintf(stderr, fmt, l);
	va_end(l);

	exit(-1);
}

static int open_port(char *port)
{
	int p = open(port, O_RDWR);
	if (p < 0)
		bail("Couldn't open %s %i\n", port, errno);

	struct termios t = {
		.c_iflag = IGNBRK | IGNPAR,
		.c_cflag = B115200 | CS8 | CREAD | CLOCAL,
		.c_cc[VTIME] = 0,
		.c_cc[VMIN] = 1,
	};
	if (tcsetattr(p, TCSANOW, &t) < 0)
		bail("tcsetattr failed %i\n", errno);
	return p;
}

static int writef(int fd, const char *fmt, ...)
{
	va_list l;
	va_start(l, fmt);
	char tmp[96], r = vsnprintf(tmp, sizeof(tmp), fmt, l);
	va_end(l);

//fprintf(stderr, ">> %s\n", tmp);
	return write(fd, tmp, r);
}

static int pcap_header_out(void)
{
	struct {
		uint32_t magic_number;
		uint16_t vmajor, vminor;
		int32_t thiszone;
		uint32_t sigfigs, snaplen, network;
	} hdr = {0xA1B2C3D4, 2, 4, 0, 0, 128, 195}; // LINKTYPE_IEEE802_15_4_WITHFCS
	// TODO LINKTYPE_IEEE802_15_4_TAP with rssi / lqi in TLV, maybe channel / freq in first record?

	return write(STDOUT_FILENO, &hdr, sizeof(hdr));
}

static void pcap_record_out(char *payload, int bytes, uint32_t ts, int lqi, int rssi)
{
	static struct {
		time_t t;
		uint32_t ts;
	} first;
	if (first.t == 0) {
		first.t = time(0);
		first.ts = ts;
	}

	static uint32_t last, overflow;
	if (last && ts < last)
		overflow++;
	last = ts;

	uint64_t us = (ts | (uint64_t)overflow << 32) - first.ts;

	struct {
		uint32_t ts_sec, ts_usec;
		uint32_t incl_len, orig_len;
	} hdr = {
		first.t + us / 1000000,
		us % 1000000,
		bytes,
		bytes
	};
	write(STDOUT_FILENO, &hdr, sizeof(hdr));

	write(STDOUT_FILENO, payload, bytes);
}

static char* hex2bin(char *hex, char *bin)
{
	int odd = 0;
	while (isxdigit(*hex)) {
		int c = *hex++;
		if ((c -= '0') >= 10)
			c = c - 7 & ~32;
		*bin = *bin << 4 | c;
		odd ^= 1;
		if (!odd)
			bin++;
	}
	return hex;
}

static int process_rx(char *line)
{ // snf.rx={payload,lqi,rssi,timestamp}
	if (strncmp(line, "snf.rx={", 8))
		return 0;

	char payload[4096], *p = hex2bin(line + 8, payload);
	int digits = p - line - 8;
	if (digits & 1)
		return fprintf(stderr, "Odd payload digits in %s\n", line);
	if (*p++ != ',')
		return fprintf(stderr, "Missing comma after payload in %s\n", line);

	int lqi = strtol(p, &p, 10);
	if (*p++ != ',')
		return fprintf(stderr, "Missing comma after LQI in %s\n", line);

	int rssi = strtol(p, &p, 10);
	if (*p++ != ',')
		return fprintf(stderr, "Missing comma after RSSI in %s\n", line);

	uint32_t ts = strtoul(p, &p, 10);
	if (*p++ != '}')
		return fprintf(stderr, "Missing brace after timestamp in %s\n", line);
	pcap_record_out(payload, digits / 2, ts, lqi, rssi);

	return *p ? fprintf(stderr, "Guff after brace in %s\n", line) : 0;
}

static void process_line(char *l)
{
	static char rdy;

	if (rdy)
		process_rx(l);
	else if (strncmp(l, "ok", 2) == 0)
		rdy = pcap_header_out();
	else if (strncmp(l, "no", 2) == 0)
		bail("Couldn't start sniffing: %s\n", l);
	else
		fprintf(stderr, "UNKNOWN: %s\n", l);
}

static void process_byte(char c)
{
	static char rx[4096];
	static int got;

	if (c == 0 || c == '\n') {
		if (got > 0) {
			rx[got] = 0;
			process_line(rx);
		}
		got = 0;
	} else if (got >= 0) {
		rx[got] = c;
		if (++got == sizeof(rx))
			got = -1; // wait for line terminator
	}
}

static void stop(int signum)
{
	writef(p, "snf.stop\n");
	fprintf(stderr, "stopping on %i\n", signum);
	exit(0);
}


// exports

int main(int argc, char **argv)
{
	int chn = 0;
	char *port = "/dev/ttyUSB0", *a, rx;
	while (a = *++argv) {
		if (isdigit(a[0]))
			chn = atoi(a);
		else
			port = a;
	}
	if (chn == 0)
		bail("Usage: dbch-pcap channel [port] | wireshark-gtk -k -i -\n");

	p = open_port(port);
	writef(p, "snf.go=%x\n", chn);

	signal(SIGINT, stop);
	signal(SIGTERM, stop);
	signal(SIGPIPE, stop);

	while (read(p, &rx, 1) == 1)
		process_byte(rx);

	return 0;
}
