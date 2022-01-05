#include <dbch.h>
#include <em_emu.h>
#include <serial/ember-printf.h>


struct fifo {
	short head, tail;
	char fifo[0];
};

static char rx[sizeof(struct fifo) + HAL_SERIAL_APP_RX_QUEUE_SIZE];
static char tx[sizeof(struct fifo) + HAL_SERIAL_APP_TX_QUEUE_SIZE];
static char line[255], lstt;
static short lgot;


// helpers

static void push(void *fifo, char c, int size)
{
	struct fifo *f = fifo;
	f->fifo[f->tail] = c;
	if (++f->tail == size)
		f->tail = 0;
}

static char pop(void *fifo, int size)
{
	struct fifo *f = fifo;
	char c = f->fifo[f->head];
	if (++f->head == size)
		f->head = 0;
	return c;
}

static int send(char byte)
{
	struct fifo *f = (struct fifo*)tx;

	short next = (f->tail + 1) % HAL_SERIAL_APP_TX_QUEUE_SIZE;
	while (next == f->head) {
		USART0->IEN |= USART_IEN_TXBL;
		EMU_EnterEM1();
		halResetWatchdog();
	}

	f->fifo[f->tail] = byte;
	f->tail = next;
	USART0->IEN |= USART_IEN_TXBL;

	return 1;
}

static Ecode_t uart_flush(COM_Port_t flushVar, uint8_t *contents, uint8_t length)
{
	while (length--)
		send(*contents++);
	return EMBER_SUCCESS;
}


// ISRs

void USART0_TX_IRQHandler(void)
{
	struct fifo *f = (struct fifo*)tx;
	if (f->head == f->tail)
		USART0->IEN &= ~USART_IEN_TXBL;
	else if (USART0->STATUS & USART_STATUS_TXBL)
		USART0->TXDATA = pop(f, HAL_SERIAL_APP_TX_QUEUE_SIZE);
}

void USART0_RX_IRQHandler(void)
{
	push(rx, USART0->RXDATA, HAL_SERIAL_APP_RX_QUEUE_SIZE);
}


// COM

Ecode_t COM_Init(COM_Port_t port, COM_Init_t *init)
{
	if (port != APP_SERIAL)
		return EMBER_ERR_FATAL;

	GPIO_PinModeSet(BSP_SERIAL_APP_TX_PORT, BSP_SERIAL_APP_TX_PIN, gpioModePushPull, 1);
	GPIO_PinModeSet(BSP_SERIAL_APP_RX_PORT, BSP_SERIAL_APP_RX_PIN, gpioModeInputPull, 1);
	CMU_ClockEnable(cmuClock_USART0, true);

	USART0->CTRL = USART_CTRL_TXBIL_HALFFULL;
	USART0->CLKDIV = (((38400000 / HAL_SERIAL_APP_BAUD_RATE - 16) * 4 + 1) / 2) << 3;
	USART0->ROUTELOC0 = BSP_SERIAL_APP_TX_LOC << 8 | BSP_SERIAL_APP_RX_LOC;
	USART0->ROUTEPEN = 3; // enable RX and TX
	USART0->CMD = 0xC85; // reset

	NVIC_ClearPendingIRQ(USART0_RX_IRQn);
	NVIC_EnableIRQ(USART0_RX_IRQn);
	USART0->IEN = USART_IEN_RXDATAV;

	NVIC_ClearPendingIRQ(USART0_TX_IRQn);
	NVIC_EnableIRQ(USART0_TX_IRQn);

	return EMBER_SUCCESS;
}

Ecode_t COM_PrintfVarArg(COM_Port_t port, PGM_P fmt, va_list l)
{
	emPrintfInternal(uart_flush, comPortUsart0, fmt, l);
	return EMBER_SUCCESS; 
}


void COM_InternalPowerDown(bool idle)
{
}

void COM_InternalPowerUp(bool idle)
{
}

Ecode_t COM_InternalReceiveData(COM_Port_t port, uint8_t *data, uint32_t length)
{
	// for injecting data into RX buffer from JTAG?
	return ECODE_EMDRV_UARTDRV_OK;
}


// serial

EmberStatus emberSerialInit(uint8_t port, SerialBaudRate rate, SerialParity parity, uint8_t stopBits)
{
	return COM_Init((COM_Port_t)port, 0);
}

void emberSerialBufferTick(void)
{
	struct fifo *f = (struct fifo*)rx;
	while (lstt == 0 && f->head != f->tail) {
		char c = pop(f, HAL_SERIAL_APP_RX_QUEUE_SIZE);
		if (c == 8) {
			if (lgot)
				lgot--;
			continue;
		}
		if (c < 32)
			c = 0;

		if (line[lgot++] = c) {
			if (lgot == sizeof(line))
				lgot--;
			continue;
		}

		lgot = 0;
		if (doap_run(line) == 0)
			lstt = 'w'; // waiting for emberSerialReadByte
	}
}

EmberStatus emberSerialWaitSend(uint8_t port)
{
	while ((USART0->STATUS & 0x02020) != 0x02020)
		halResetWatchdog();
	return EMBER_SUCCESS;
}

EmberStatus emberSerialGuaranteedPrintf(uint8_t port, PGM_P fmt, ...)
{
	va_list l;
	va_start(l, fmt);
	emberSerialPrintfVarArg(port, fmt, l);
	va_end(l);
	return EMBER_SUCCESS;
}

EmberStatus emberSerialWriteData(uint8_t port, uint8_t *data, uint8_t length)
{
	return uart_flush(port, data, length);
}

EmberStatus emberSerialReadByte(uint8_t port, uint8_t *dataByte)
{
	if (lstt != 'w')
		return EMBER_SERIAL_RX_EMPTY;

	char c = line[lgot++];
	if (c)
		*dataByte = c;
	else {
		*dataByte = '\n';
		lstt = lgot = 0;
	}
	return EMBER_SUCCESS;
}

static int decimal(int64_t n, int digits)
{
	int r = 0;
	if (n < 0) {
		r += send('-');
		n = -n;
	}

	char c = '0' + n % 10;
	if ((n /= 10) || digits > 1)
		r += decimal(n, digits - 1);
	return r + send(c);
}

int uartf(const char *fmt, ...)
{
	int wrote = 0, got = 0;
	char stk[16], c;

	va_list l;
	va_start(l, fmt);
	while (c = *fmt++) {
		if (c != '%') {
			wrote += send(c);
			continue;
		}

		int mod = 0;
		while (*fmt >= '0' && *fmt <= '9')
			mod = mod * 10 - '0' + *fmt++;
		c = *fmt++;

		if (c == '%')
			wrote += send('%');
		else if (c == 'd')
			wrote += decimal(va_arg(l, int), mod);
		else if (c == 'u')
			wrote += decimal(va_arg(l, unsigned), mod);
		else if (c == 's') { // string
			char *s = va_arg(l, char*);
			if (s) while (*s)
				wrote += send(*s++);
		} else if (c == 'a') { // ass-backward address
			uint8_t *a = va_arg(l, uint8_t*);
			if (mod == 0)
				mod = 8;
			if (a) while (mod)
				wrote += uartf("%2x", a[--mod]);
		} else if (c == 'b') { // hex data
			uint8_t *p = va_arg(l, uint8_t*);
			if (mod == 0)
				mod = va_arg(l, int);
			if (p) while (mod--)
				wrote += uartf("%2x", *p++);
		} else if (c == 'x') { // hexadecimal
			static const char *hex = "0123456789ABCDEF";
			unsigned x = va_arg(l, unsigned);
			do {
				stk[got++] = hex[x & 15];
				x >>= 4;
			} while (x || got < mod);
		} else if (c == 'p') { // Pascal data
			uint8_t *p = va_arg(l, uint8_t*);
			uint8_t len = *p++;
			while (len) {
				wrote += uartf("0x%2x", *p++);
				if (--len)
					wrote += send(' ');
			}
		}

		while (got)
			wrote += send(stk[--got]);
	}
	va_end(l);

	return wrote;
}
