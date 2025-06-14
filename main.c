#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/spi.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "hardware/structs/bus_ctrl.h"
#include "pico/sem.h"

// tinyusb
#include "bsp/board.h"
#include "tusb.h"

// picodvi
#include "dvi.h"
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"
#include "tmds_encode.h"
#include "sprite.h"

// lua
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

// FatFS sd card support (carlk3)
// https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
#include "f_util.h"
#include "ff.h"
#include "hw_config.h"
#include "sd_card.h"

// SPI
#define SPI_PORT spi0
#define PIN_MISO 4 // RX (data input)
#define PIN_CS   5 // CSn
#define PIN_SCK  2 // SCK
#define PIN_MOSI 3 // TX (not used)
//#define SPI_BAUDRATE 25200000 // 25.2 MHz (nominal, matches master)
#define SPI_BAUDRATE 8000000

// picolo
#include "platform.h"
#include "display.h"
#include "conio.h"
#include "shell.h"
#include "sdcard.h"

// TMDS bit clock 252 MHz
// DVDD 1.2V (1.1V seems ok too)
#define VREG_VSEL VREG_VOLTAGE_1_20
#define DVI_TIMING dvi_timing_640x480p_60hz

#define LED_PIN PICO_DEFAULT_LED_PIN

struct dvi_inst dvi0;
struct semaphore dvi_start_sem;

/******************************************************************************
 * CORE1  code for 320x240 8bit rgb332 graphics mode
 */

uint8_t FRAMEBUF[D_FRAME_WIDTH * D_FRAME_HEIGHT];

void core1_main() {
	dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
	sem_acquire_blocking(&dvi_start_sem);
	dvi_start(&dvi0);
	dvi_scanbuf_main_8bpp(&dvi0);

	while (1) {
		__wfi();
    }
	__builtin_unreachable();

}

void core1_scanline_callback() {
	// Note first two scanlines are pushed before DVI start
	static uint scanline = 2;
	
	// Discard any scanline pointers passed back
	uint8_t *bufptr;
	while (queue_try_remove_u32(&dvi0.q_colour_free, &bufptr))
		;

	//RenderTextScanline(scanline);
	bufptr = &FRAMEBUF[D_FRAME_WIDTH * scanline];
	queue_add_blocking_u32(&dvi0.q_colour_valid, &bufptr);
	scanline = (scanline + 1) % D_FRAME_HEIGHT;
}


/******************************************************************************
 * CORE0 code
 */


static uint64_t framecount = 0;
static uint64_t auto_repeat_timer;
static int auto_repeat_key = 0;

void StartKeyRepeat(int k) {
	auto_repeat_key = k;
	auto_repeat_timer = framecount;
}

void StopKeyRepeat() {
	auto_repeat_key = 0;
}

bool RENDER_CONSOLE = 0;

// 50hz regular updates
bool display_timer_callback(__unused struct repeating_timer *t) {
    //printf("Repeat at %lld\n", time_us_64());

	if(RENDER_CONSOLE) {
		for(int scanline = 0; scanline < D_FRAME_HEIGHT; scanline++)
			RenderTextScanline(scanline);
	}

   	tuh_task();

	if(auto_repeat_key && (framecount > auto_repeat_timer  + 30) && (framecount % 2)) {
		ConStoreCharacter(auto_repeat_key);
	}

	framecount++;
    return true;
}

void spi_receive_isr() {
		uint32_t spi_data;
		int bytes_read = spi_read_blocking(SPI_PORT, 0x00, (uint8_t*)&spi_data, 3);
        //if (bytes_read == 4) {
		uint16_t addr = ((spi_data & 0x00ffff00) >> 8);
		uint16_t offset = addr - 16384;

		if(offset >= 6144)
			return;

#if 1
		 // Calculate components
    	int third = offset / 2048;
    	int line_in_char = (offset % 2048) / 256;
	    int char_row_in_third = ((offset % 2048) % 256) / 32;
    	int x_byte = offset & 0b0000000000011111; //offset % 32;

    	// Calculate Y coordinate
    	int y = (third * 64) + (char_row_in_third * 8) + line_in_char;

    	// Calculate X coordinate
    	int x = x_byte * 8;
#else

		// mask out Y			    15   10 76543210
		uint16_t y_7_6 = offset & 0b0001100000000000;		
		uint16_t y_2_0 = offset & 0b0000011100000000;
		uint16_t y_5_3 = offset & 0b0000000011100000;
		uint16_t y = (y_7_6>>5) | (y_5_3>>5) | (y_2_0>>5);
		uint16_t x = offset & 0b0000000000011111;		
#endif
		//Con_printf("offset %u x:%u y:%u\n", offset, x, y);

#if 1
		uint8_t *fp = FRAMEBUF + (y+24) * 320 + x + 32;

		uint8_t data = (spi_data & 0x000000ff); 

		uint8_t mask = 0b10000000;
		for(int i = 0; i < 8; i++) {
			if(data&mask) {
				*fp++ = 0;
			} else {
				*fp++ = 255;
			}
			mask = mask>>1;
		}
#endif
}

void init_spi() {

 	// Initialize SPI0 as slave
    uint actual_baudrate = spi_init(SPI_PORT, SPI_BAUDRATE);
    if (actual_baudrate == 0) {
        Con_printf("Error: Failed to initialize SPI\n");
    }
    //spi_set_format(SPI_PORT, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    spi_set_slave(SPI_PORT, true);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

	spi0_hw->imsc = 1 << 2;		// fire irq when fifo is 1/2 full (8 bits)
	irq_set_enabled(SPI0_IRQ, 1);
	spi0_hw->dr = 0;
	irq_set_exclusive_handler(SPI0_IRQ, spi_receive_isr);
	
    printf("Pico A (Slave): Waiting for SPI data at %u Hz\n", actual_baudrate);
}


int __not_in_flash("main") main() {
//int main() {

	vreg_set_voltage(VREG_VSEL);
	sleep_ms(10);

	// Run system at TMDS bit clock
	set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

	setup_default_uart();
    stdio_init_all();

	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);
	gpio_put(LED_PIN, true);

	// init tinyusb host stack on configured roothub port
  	tusb_init();
 	board_init();
  	tuh_init(BOARD_TUH_RHPORT);

	// get PicoDVI up and running
	printf("Configuring DVI\n");

	dvi0.timing = &DVI_TIMING;
	dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
	dvi0.scanline_callback = core1_scanline_callback;
	dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

	// Once we've given core 1 the framebuffer, it will just keep on displaying
	// it without any intervention from core 0
	sprite_fill8((uint8_t*)FRAMEBUF, 0x02, D_FRAME_WIDTH * D_FRAME_HEIGHT);

	// need to pass in the first two scan lines before we start the encoder on core1
	// or the display won't sync, still don't understand why
#if 1
	uint8_t *bufptr = FRAMEBUF;
	queue_add_blocking_u32(&dvi0.q_colour_valid, &bufptr);
	bufptr += D_FRAME_WIDTH;
	queue_add_blocking_u32(&dvi0.q_colour_valid, &bufptr);
#endif

	// start core1 renderer
	printf("Core 1 start\n");
	sem_init(&dvi_start_sem, 0, 1);
	hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
	multicore_launch_core1(core1_main);
	sem_release(&dvi_start_sem);

	// bring up the remainder of the system 

	//printf("LUA start\n");
    //lua_State *L = luaL_newstate();
    //luaL_openlibs(L);

    printf("Free heap after init: %d\n", P_GetFreeHeap());
	printf("Start rendering\n");

	DisplayOpen();
	ConOpen();

	// SPI
	init_spi();

	//sd_init_driver();
	Con_printf("Picolo System v%s\n%d bytes free \n", PLATFORM_VERSION_STRING, P_GetFreeHeap());
	//Con_printf("card free:%ldMB\n", SDCardTest()/(1024*1024));
	Con_puts("ready\n");

	// drive text display and tuh at 50hz
    struct repeating_timer timer;
    add_repeating_timer_ms(-20, display_timer_callback, NULL, &timer);

	while (1) {


		// TEST TEST TEST
#if 0
		uint32_t spi_data;
		int bytes_read = spi_read_blocking(SPI_PORT, 0x00, (uint8_t*)&spi_data, 2);
        //if (bytes_read == 4) {
			Con_printf("spi data: 0x%08x\n", spi_data);
		//}

#endif

		char *line = ShellReadInput();	// does not block
		if(line) {
			// might start a shell process (such as editor) and not return for duration
			ShellProcessLine(line);		
		}
		
//		__wfi();

	}

	__builtin_unreachable();
}

