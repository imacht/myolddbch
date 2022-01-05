#include <dbch.h>


static uint8_t capacity;


// primatives

static void assert_ss(void)
{
	GPIO_PinOutClear(BSP_USART1_CS_PORT, BSP_USART1_CS_PIN);
}

static void negate_ss(void)
{
	GPIO_PinOutSet(BSP_USART1_CS_PORT, BSP_USART1_CS_PIN);
}

static uint8_t transact(uint8_t tx)
{
	return USART_SpiTransfer(USART1, tx);
}

static void begin(uint32_t bigendian, int bytes)
{
	assert_ss();
	while (bytes--) {
		transact(bigendian >> 24);
		bigendian <<= 8;
	}
}


// helpers

static void spi_wait_done(void)
{
	while (1) {
		begin(0x05000000, 1);
		uint8_t rx = transact(0);
		negate_ss();
		if (~rx & 1)
			return;
	}
}

static void spi_write_enable(void)
{
	spi_wait_done();
	begin(0x06000000, 1);
	negate_ss();
}

static void spi_erase(uint32_t addr)
{
	spi_write_enable();
	begin(0x20000000 | addr, 4);
	negate_ss();
}

static void spi_write(uint32_t addr, int bytes, uint8_t *data)
{
	while (bytes) {
		spi_write_enable();
		int seg = 256 - addr % 256;
		if (seg > bytes)
			seg = bytes;
		begin(0x02000000 | addr, 4);
		addr += seg;
		bytes -= seg;
		while (seg--)
			transact(*data++);
		negate_ss();
	}
}

static int prep(void)
{
	if (capacity == 0) {
		GPIO_PinModeSet(BSP_USART1_MOSI_PORT, BSP_USART1_MOSI_PIN, gpioModePushPull, 0);
		GPIO_PinModeSet(BSP_USART1_MISO_PORT, BSP_USART1_MISO_PIN, gpioModeInput, 0);
		GPIO_PinModeSet(BSP_USART1_CLK_PORT, BSP_USART1_CLK_PIN, gpioModePushPull, 0);
		GPIO_PinModeSet(BSP_USART1_CS_PORT, BSP_USART1_CS_PIN, gpioModePushPull, 1);
		CMU_ClockEnable(cmuClock_USART1, true);

		USART1->CTRL = USART_CTRL_MSBF | USART_CTRL_SYNC;
		USART1->CLKDIV = 256 * (38400000 / (2 * HAL_USART1_FREQUENCY) - 1); // br = 38400000 / (2(1 + USARTn_CLKDIV / 256)) = 6400000Hz
		USART1->FRAME = USART_FRAME_DATABITS_EIGHT;
		USART1->ROUTELOC0 = BSP_USART1_CLK_LOC << 24 | BSP_USART1_MOSI_LOC << 8 | BSP_USART1_MISO_LOC;
		USART1->ROUTEPEN = USART_ROUTEPEN_CLKPEN | USART_ROUTEPEN_TXPEN | USART_ROUTEPEN_RXPEN;
		USART1->CMD = USART_CMD_CLEARRX | USART_CMD_CLEARTX | USART_CMD_RXBLOCKDIS | USART_CMD_MASTEREN | USART_CMD_TXEN | USART_CMD_RXEN;

		begin(0x9F000000, 1);
		transact(0); // manu
		transact(0); // type
		capacity = transact(0);
		negate_ss();
	}

	return capacity == 255 ? 0 : 1 << capacity;
}


// exports

int ota_store(int addr, uint8_t *data, int bytes)
{
	prep();

	int left = bytes;
	while (left) {
		int seg = 4096 - addr % 4096;
		if (seg == 4096)
			spi_erase(addr);
		if (seg > left)
			seg = left;
		spi_write(addr, seg, data);
		addr += seg;
		data += seg;
		left -= seg;
	}
	return bytes - left;
}

void ota_fetch(void *data, int addr, int bytes)
{
	prep();

	uint8_t *out = data;
	begin(0x03000000 | addr, 4);
	while (bytes--)
		*out++ = transact(0);
	negate_ss();
}

int ota_capacity(void)
{
	prep();

	if (capacity == 0 || capacity == 255)
		return 0;
	return 1 << capacity;
}
