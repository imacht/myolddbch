#ifdef WIN32
#include <windows.h>
#include <conio.h>
#else
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#endif
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <libgen.h>
#include <time.h>


static int send(void *data, int bytes);


static char *port;
static struct { FILE *file; int size, left; } ota;


// helpers

static int bail(const char *fmt, ...)
{
	va_list l;
	va_start(l, fmt);
	int r = vfprintf(stderr, fmt, l);
	va_end(l);
	exit(r);
}

static int sendf(const char *fmt, ...)
{
	char tmp[768], *p = tmp, c;

	va_list l;
	va_start(l, fmt);
	while (c = *fmt++) {
		if (c != '%') {
			*p++ = c;
			continue;
		}
		c = *fmt++;
		if (c == 'c') // character
			*p++ = va_arg(l, int);
		else if (c == 'h') { // hex data
			unsigned char *h = va_arg(l, unsigned char*);
			unsigned bytes = va_arg(l, unsigned);
			while (bytes--)
				p += sprintf(p, "%02X", *h++);
		}
	}
	va_end(l);

//*p = 0, fprintf(stderr, "> %s\n", tmp);
	return send(tmp, p - tmp);
}


// OTA

static void ota_wrote(unsigned bytes)
{
	if (!ota.file)
		return;

	if (ota.left -= bytes) {
		char tmp[32], get = ota.left < 32 ? ota.left : 32;
		fseek(ota.file, ota.size - ota.left, SEEK_SET);
		fread(tmp, 1, get, ota.file);
		sendf("ota.img=%h\n", tmp, get); // begets ota.wrote=bytes
	} else {
		fclose(ota.file);
		ota.file = 0;
		sendf("ota.done\n"); // begets ota.img={...}
	}

	printf("OTA: %i bytes left  \r", ota.left);
}

static void ota_heard(char rx)
{
	static char line[96], got;

	if (rx >= 32) {
		line[got++] = rx;
		return;
	}

	line[got] = 0;
	got = 0;
//fprintf(stderr, "< %s\n", line);

	if (strncmp(line, "ota.wrote=", 8) == 0)
		ota_wrote(strtol(line + 10, 0, 16));
}


// dispatch

static void heard_dbch(char rx)
{
	if (ota.file)
		ota_heard(rx);
	else if (rx && rx != 7)
		putchar(rx);
	fflush(stdout);
}

static void heard_stdin(char rx)
{
	if (!ota.file)
		sendf("%c", rx);
}


// platform-specific

#ifdef WIN32

static HANDLE hcom, ecom, tcom, scom;

static DWORD WINAPI thread(LPVOID pparam)
{
	setmode(fileno(stdout), O_BINARY);
	setmode(fileno(stderr), O_BINARY);

	while (1) {
		DWORD bytes = 0;
		char rx;
		OVERLAPPED o = {0, 0, 0, 0, ecom};
		if (ReadFile(hcom, &rx, 1, &bytes, &o) == 0 && GetLastError() == ERROR_IO_PENDING)
			GetOverlappedResult(hcom, &o, &bytes, TRUE);
		if (bytes == 1)
			heard_dbch(rx); // TODO safe to call from here?
	}
	puts("thread done");

	return 0;
}

static int send(void *data, int bytes)
{
	DWORD sent = 0;
	OVERLAPPED o = {0, 0, 0, 0, scom};
	if (WriteFile(hcom, &c, 1, &sent, &o) == 0 && GetLastError() == ERROR_IO_PENDING)
		GetOverlappedResult(hcom, &o, &sent, TRUE);
	return sent;
}

static void prep(void)
{
	char wport[64];
	sprintf(wport, "\\\\.\\%s", port ? port : "COM1");

	// open comport
	hcom = CreateFile(wport, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	if (hcom == INVALID_HANDLE_VALUE)
		bail("Couldn't open %s. Available ports -\n", wport);

	ecom = CreateEvent(0, FALSE, FALSE, 0);
	scom = CreateEvent(0, FALSE, FALSE, 0);
	DCB dcb;
	if (!GetCommState(hcom, &dcb))
		bail("GetCommState failed\n");
	dcb.BaudRate = 115200;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.StopBits = ONESTOPBIT;
	dcb.fRtsControl = RTS_CONTROL_DISABLE;
	dcb.fDtrControl = DTR_CONTROL_DISABLE;
	dcb.fOutxCtsFlow = dcb.fOutxDsrFlow = 0;
	dcb.fAbortOnError = 0;
	if (!SetCommState(hcom, &dcb))
		bail("SetCommState failed %i\n", GetLastError());
	if (!SetupComm(hcom, 4096, 4096))
		bail("SetupComm failed %i\n", GetLastError());
	COMMTIMEOUTS cto = {0, 0, 0, 0, 0};
	if (!SetCommTimeouts(hcom, &cto))
		bail("SetCommTimeouts failed %i\n", GetLastError());

	GetCommState(hcom,&dcb);
	fprintf(stderr, "baud=%i\n", dcb.BaudRate);

	TODO investigate no threads with WaitForMultipleObjects
	HANDLE in = GetStdHandle(STD_INPUT_HANDLE);

	// start read thread
	tcom = CreateThread(0, 0, thread, 0, 0, 0);
	if (tcom == INVALID_HANDLE_VALUE)
		bail("CreateThread failed %i\n", GetLastError());

	setmode(fileno(stdin), O_BINARY);
}
	
static int go(void)
{
	char c;
	// TODO need to wait for hcom and stdin and call heard_dbch from this thread?
	while ((c = getch()) != 3)
		heard_stdin(c);
	return 0;
}

#else

static struct pollfd pa[2];

static int send(void *data, int bytes)
{
	return write(pa[0].fd, data, bytes);
}

static void prep(void)
{
	pa[0].fd = open(port ? port : "/dev/ttyUSB0", O_RDWR);
	if (pa[0].fd < 0)
		bail("Couldn't open stty %i\n", errno);

	struct termios t = {
		.c_iflag = IGNBRK | IGNPAR,
		.c_cflag = B115200 | CS8 | CREAD | CLOCAL,
		.c_cc[VTIME] = 0,
		.c_cc[VMIN] = 1,
	};
	if (tcsetattr(pa[0].fd,TCSANOW, &t) < 0)
		bail("tcsetattr failed %i\n", errno);

	pa[1].fd = STDIN_FILENO;
	pa[0].events = pa[1].events = POLLIN;
}

static int go(void)
{
	while (poll(pa, 2, -1) >= 0) {
		char rx;
		if (pa[0].revents && read(pa[0].fd, &rx, 1) == 1)
			heard_dbch(rx);
		if (pa[1].revents && read(pa[1].fd, &rx, 1) == 1)
			heard_stdin(rx);
	}
	return 0;
}

#endif

int main(int argc, char **argv)
{
	char *a, *exe = *argv, opt = 0, sync = 0;
	while (a = *++argv) {
		if (opt == 0 && *a == '-') {
			opt = a[1];
			if (a[2] == 0) // argument is next
				continue;
			a += 2; // no space, argument follows
		}

		if (opt == 'p')
			port = a;
		else if (opt == 's' && a[0] == 'y')
			sync = 1;
		else if (opt == 'o') {
			if (!(ota.file = fopen(a, "rb")))
				return fprintf(stderr, "Couldn't open %s\n", a);
			fseek(ota.file, 0, SEEK_END);
			ota.size = ota.left = ftell(ota.file);
		} else
			return fprintf(stderr, "Usage: %s [-p port] [-o ota-file] [-sync]\n", basename(exe));
	}

	prep();
	if (sync) {
		time_t t = time(0);
		unsigned utc = t - 946684800;
		sendf("zcl time %i\n", utc);
	}
	if (ota.file) {
		sendf("ota.reset\n"); // begets ota.img={...}
		ota_wrote(0); // send 1st block
	}
	return go();
}
