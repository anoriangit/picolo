#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "pico/sem.h"

// tinyusb (disabled)
//#include "bsp/board.h"
//#include "tusb.h"

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

// picolo
#include "platform.h"
#include "display.h"
#include "conio.h"
#include "shell.h"
#include "sdcard.h"

// Z80
#include "z80_memory.h"
#include "zxe_driver.h"

#define START_DVI 0

// TMDS bit clock 252 MHz
// DVDD 1.2V (1.1V seems ok too)
#define VREG_VSEL VREG_VOLTAGE_1_20
#define DVI_TIMING dvi_timing_640x480p_60hz

#define LED_PIN PICO_DEFAULT_LED_PIN

#define BUTTON_PIN      16


#define PIO_IRQ_TO_USE 0
#define DMA_IRQ_TO_USE 0

struct dvi_inst dvi0;
struct semaphore dvi_start_sem;

/******************************************************************************
 * CORE1  code for 320x240 8bit rgb332 graphics mode
 */

uint8_t FRAMEBUF[D_FRAME_WIDTH * D_FRAME_HEIGHT];

#if 0

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

#endif

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

uint32_t tick_count = 0;
uint8_t clock_on = 0;

// 50hz regular updates
bool display_timer_callback(__unused struct repeating_timer *t) {

	// TEST TEST TEST: Z80
	// copy the last 40 bytes of the Z80 SRAM to the top most line 
	// of the display systems character display buffer
	memcpy((void*)DisplayGetCharbuf(), (void*)&SRAM[216], 40);

    //printf("Repeat at %lld\n", time_us_64());
	for(int scanline = 0; scanline < D_FRAME_HEIGHT; scanline++)
		RenderTextScanline(scanline);

   	// tinyusb disabled atm tuh_task();

	if(auto_repeat_key && (framecount > auto_repeat_timer  + 30) && (framecount % 2)) {
		ConStoreCharacter(auto_repeat_key);
	}

	framecount++;
	return true;
}

 
int __not_in_flash("main") main() {
//int main() {

	vreg_set_voltage(VREG_VSEL);
	sleep_ms(10);

	// Run system at TMDS bit clock
	set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

	// not using uart (we need the pins :) )
	//setup_default_uart();
	// Set GPI28 as TX and GPI29 as RX for UART0
    gpio_set_function(28, GPIO_FUNC_UART); // TX
    gpio_set_function(29, GPIO_FUNC_UART); // RX
    stdio_init_all();

	printf("------------------------\n");

	// switch on system LED to indicate we are running
	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);
	gpio_put(LED_PIN, true);


	// init tinyusb host stack on configured roothub port
  	//tusb_init();
 	//board_init();
  	//tuh_init(BOARD_TUH_RHPORT);

	// get PicoDVI up and running
	//printf("Configuring DVI\n");
#if START_DVI
	dvi0.timing = &DVI_TIMING;
	dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
	dvi0.scanline_callback = core1_scanline_callback;
	dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

	// Once we've given core 1 the framebuffer, it will just keep on displaying
	// it without any intervention from core 0
	sprite_fill8((uint8_t*)FRAMEBUF, 0x02, D_FRAME_WIDTH * D_FRAME_HEIGHT);

	// need to pass in the first two scan lines before we start the encoder on core1
	// or the display won't sync, still don't understand why
	uint8_t *bufptr = FRAMEBUF;
	queue_add_blocking_u32(&dvi0.q_colour_valid, &bufptr);
	bufptr += D_FRAME_WIDTH;
	queue_add_blocking_u32(&dvi0.q_colour_valid, &bufptr);

	// start core1 renderer
	//printf("Core 1 start\n");
	sem_init(&dvi_start_sem, 0, 1);
	hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
	multicore_launch_core1(core1_main);
	sem_release(&dvi_start_sem);
#endif

	// bring up the remainder of the system 

	//printf("LUA start\n");
    //lua_State *L = luaL_newstate();
    //luaL_openlibs(L);

    //printf("Free heap after init: %d\n", P_GetFreeHeap());
	//printf("Start rendering\n");

	DisplayOpen();
	ConOpen();

	//sd_init_driver();
	Con_printf("Picolo System v%s\n%d bytes free \n", PLATFORM_VERSION_STRING, P_GetFreeHeap());
	//Con_printf("card free:%ldMB\n", SDCardTest()/(1024*1024));
	Con_puts("ready\n");

	// drive text display and tuh at 50hz
    struct repeating_timer timer;
    //add_repeating_timer_ms(-20, display_timer_callback, NULL, &timer);


	// wire up some z80 signals for testing
#if 1
	zxe_init();
	#else
	// note: this includes the pin setup from above (amongst many other things)
	zxe_init();
#endif

	uint32_t n_loops = 0;
	uint32_t start_time_ms = time_us_32()/1000;

	printf("picolo startup completed\n");
	printf("------------------------\n");

// main loop
	while (1) {

#if 1		
		uint32_t pins = sio_hw->gpio_in;
		
		if(Z80_IORQ(pins) && Z80_WR(pins)) {

			uint8_t addr = pins & 0b11111111;
			
			// switch transceivers and get data bus byte
			sio_hw->gpio_set = ADDR_OE_LSB_MASK; 	// OE high (disable)
			sio_hw->gpio_clr = DATA_OE_MASK; 		// OE low (enable)
			
			asm volatile("nop");	// 3.968 ns delay
			asm volatile("nop");	// 5*3.968 = 19,84 ns delay
			asm volatile("nop");	
			asm volatile("nop");	
			asm volatile("nop");	

			// read pins again
			pins = sio_hw->gpio_in;
			uint8_t data = pins & 0b11111111;

			// switch back to address bus lsb
			sio_hw->gpio_clr = ADDR_OE_LSB_MASK; 	// OE low (enable)
			sio_hw->gpio_set = DATA_OE_MASK; 		// OE high (disable)

			printf("iorq|wr addr:%u data:%u\n", addr, data);

			do {
				pins = sio_hw->gpio_in;
			} while(Z80_IORQ(pins));

		}
#endif

#ifdef DEBUG_TIME
		n_loops++;
		uint32_t time_ms = time_us_32()/1000;
		uint32_t delta_time_ms = time_ms - start_time_ms;
		if(delta_time_ms >= 1000) {
			printf("loops/sec:%u\n", n_loops);
			start_time_ms = time_ms;
			n_loops = 0;
		}
#endif

		//		__wfi();

	}

	__builtin_unreachable();
}

