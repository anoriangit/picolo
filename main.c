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


#define START_DVI 0

// TMDS bit clock 252 MHz
// DVDD 1.2V (1.1V seems ok too)
#define VREG_VSEL VREG_VOLTAGE_1_20
#define DVI_TIMING dvi_timing_640x480p_60hz

#define LED_PIN PICO_DEFAULT_LED_PIN


#define PIO_IRQ_TO_USE 0
#define DMA_IRQ_TO_USE 0

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
    gpio_set_function(19, GPIO_FUNC_UART); // RX
    stdio_init_all();

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
    add_repeating_timer_ms(-20, display_timer_callback, NULL, &timer);

	
	// start the Z80 ticking: first install the memory handler
	// then start the clock
	int error = 0;
	process_z80();

	// 1000->21Hz (with 60 wait cycles in ztick.pio)
	// 5000->42Hz, 10000->80Hz 
	static const float tick_freq = 5000;		
	if(start_z80(tick_freq) < 0) {
		Con_printf("Error starting Z80 clock\n");
		error = 1;
	}

	uint32_t n_loops = 0;

	// main loop
	while (1) {

		// these can be used for debugging 
		// dma needs to be disabled
		// if this code AND dma are enabled bad things will happen
		/*
		uint32_t addr = pio_sm_get_blocking(z80_write_pio, z80_write_sm);
		//pio_sm_clear_fifos (z80_write_pio, z80_write_sm);
		uint32_t data = pio_sm_get_blocking(z80_write_pio, z80_write_sm);
		Con_printf("write 0x%0x:0x%0x\n", addr, data);
		*/

		uint32_t addr = pio_sm_get_blocking(z80_read_pio, z80_read_sm);
		uint32_t data = (SRAM[addr&0x000000ff])<<24;
		//pio_sm_put_blocking(z80_read_pio, z80_read_sm, data);
	
		uint32_t time_ms = time_us_32() / 1000;
		printf("READ time:%u 0x%08x:0x%08x\n", time_ms, addr, data);

#if 0

     	//pio_sm_set_consecutive_pindirs(z80_pio, z80_sm, 0, z80_ram_PIN_COUNT, false);
        
		uint32_t addr = pio_sm_get_blocking(z80_pio, z80_sm) >> 24;
        Con_printf("Addr: 0x%02x, Data: 0x%02x\n", addr, sram[addr]);

        //pio_sm_set_consecutive_pindirs(z80_pio, z80_sm, 0, z80_ram_PIN_COUNT, true);
       
		dma_channel_set_read_addr(z80_dma, &sram[addr], true);
#else
		if(!error) {

#if 0
		// semi automatic DMA variant: 
		// read address from rx fifo and then trigger read_ram_dma 

		// Wait for data in the RX FIFO
		uint32_t pin_states = pio_sm_get_blocking(z80_pio, z80_sm);

		// if ISR is right shift
		//uint8_t addr = pin_states >> 24;
		// if ISR is left shift
		//uint32_t addr = pin_states & 0x000000ff;
		// if ISR is left shift AND we receive a full address
		uint32_t addr = pin_states;

		// transfer the data byte from SRAM to the pio tx fifo, either via dma or manually
		/* full auto DMA variant does not need any of this */
		// write the address to read from to the read_ram dma channel and trigger it
		dma_channel_set_read_addr(read_ram_dma, (void*)((uint8_t*)addr), true);
#endif

#if 0
		// pure CPU "manual" variant
		// for left shift osr we need to move the data byte into the msb of the 32 bit word
		// uint32_t data = SRAM[addr] << 24;
		uint32_t data = (*((uint8_t*)addr)) << 24;
		pio_sm_put_blocking(z80_pio, z80_sm, data);		// move to tx fifo (pulled into osr)
#endif
		// DEBUG: Print the pin states
        //Con_printf("A0-A5: 0x%04x\n", pin_states);
		// static uint32_t last_time_ms;
		// uint32_t time_ms = time_us_32() / 1000;
      
#if 0	
		if(addr != last_addr+4) {
			Con_printf("%ums E: addr:%u last:%u diff:%u\n", time_ms, addr, last_addr, addr-last_addr);
		}
#endif

#if 0
		Con_printf("dT: %03u ms - ", time_ms-last_time_ms);
        Con_printf("A0-A7: %d\n", addr&0x000000ff);
#endif
	
	
#endif
	}
		/*
		char *line = ShellReadInput();	// does not block
		if(line) {
			// might start a shell process (such as editor) and not return for duration
			ShellProcessLine(line);		
		} */
		
//		__wfi();

	}

	__builtin_unreachable();
}

