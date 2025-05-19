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

// picolo
#include "platform.h"
#include "display.h"
#include "conio.h"
#include "shell.h"
#include "sdcard.h"
#include "ztick.pio.h"
#include "zproc.pio.h"
#include "z80_ram.pio.h"

#define START_DVI 1

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

uint32_t tick_count = 0;
uint8_t clock_on = 0;

// 50hz regular updates
bool display_timer_callback(__unused struct repeating_timer *t) {
    //printf("Repeat at %lld\n", time_us_64());
	for(int scanline = 0; scanline < D_FRAME_HEIGHT; scanline++)
		RenderTextScanline(scanline);

   	tuh_task();

	if(auto_repeat_key && (framecount > auto_repeat_timer  + 30) && (framecount % 2)) {
		ConStoreCharacter(auto_repeat_key);
	}

	framecount++;
	return true;
}

uint ztick_sm;

// Z80 clock ticking
// Note the pio program currently is hard coded to tick the Z80 at 10 Hz
void start_z80() {

	static const uint clock_pin = 28;

	// 4000:tick at 5Hz (2000 for 10Hz) (15 ~ 30Hz) (5 ~ 300Hz) (1 ~ 1KHz)
	static const float pio_freq = 4000;		

    // Choose PIO instance (0 or 1)
    PIO pio = pio1;

    // Get first free state machine in PIO 1
    ztick_sm = pio_claim_unused_sm(pio, true);
	Con_printf("ztick_sm: %d\n", ztick_sm);

    // Add PIO program to PIO instruction memory. SDK will find location and
    // return with the memory offset of the program.
    uint offset = pio_add_program(pio, &ztick_program);
	Con_printf("ztick program offset: %d\n", offset);

	/*
	The PIO state machine’s clock frequency is:
	PIO frequency=System clock frequency / Clock divider
	*/

    // Calculate the PIO clock divider
    float div = (float)clock_get_hz(clk_sys) / pio_freq;
	Con_printf("ztick clock div: %f\n", div);

    // Initialize the program using the helper function in our .pio file
    ztick_program_init(pio, ztick_sm, offset, clock_pin, div);

    // Start running our PIO program in the state machine
    pio_sm_set_enabled(pio, ztick_sm, true);

}

PIO z80_pio;
uint z80_sm;
int z80_dma;

void process_z80() {
	// Choose PIO instance and claim a state machine
    z80_pio = pio1;
    z80_sm = pio_claim_unused_sm(z80_pio, true);
	Con_printf("zproc_sm: %d\n", z80_sm);

    // Load the PIO program
    uint offset = pio_add_program(z80_pio, &zproc_program);
	Con_printf("zproc program offset: %d\n", offset);

    // Initialize the PIO state machine
    zproc_program_init(z80_pio, z80_sm, offset);

    // Need to clear _input shift counter_, as well as FIFO, because there may be
    // partial ISR contents left over from a previous run. sm_restart does this.
    pio_sm_clear_fifos(z80_pio, z80_sm);
    pio_sm_restart(z80_pio, z80_sm);
 	
	// push pin mask to the tx fifo
    pio_sm_put_blocking(z80_pio, z80_sm, 0b00000000111);

    // Start running our PIO program in the state machine
    pio_sm_set_enabled(z80_pio, z80_sm, true);

}

// ------------------------------------------------
// new version of z80_ram

#define SRAM_SIZE 256
uint8_t sram[SRAM_SIZE];

void start_z80_ram() {

	Con_printf("Constants:\n PIN_COUNT=%d\n RD_PIN=%d\n ADDR_OE_PIN=%d\n DATA_OE_PIN=%d\n DELAY_COUNT=%d\n DELAY_LOOPS=%d\n",
           z80_ram_PIN_COUNT, z80_ram_RD_PIN, z80_ram_ADDR_OE_PIN, z80_ram_DATA_OE_PIN,
           z80_ram_DELAY_COUNT, z80_ram_DELAY_LOOPS);

    // Initialize SRAM
    for (int i = 0; i < SRAM_SIZE; i++) 
		sram[i] = 0x00;

	// example
    //sram[0] = 0xC3; sram[1] = 0x00; sram[2] = 0x00; // JP 0

    // RAM PIO (SM0)
    z80_pio = pio1;
    z80_sm = pio_claim_unused_sm(z80_pio, true);
	Con_printf("z80_ram sm: %d\n", z80_sm);

	uint offset_ram = pio_add_program(z80_pio, &z80_ram_program);
    z80_ram_program_init(z80_pio, z80_sm, offset_ram);

    // DMA
    z80_dma = dma_claim_unused_channel(true);
	Con_printf("z80_ram dma: %d\n", z80_sm);

    dma_channel_config dma_config = dma_channel_get_default_config(z80_dma);
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_8);
    channel_config_set_read_increment(&dma_config, false);
    channel_config_set_write_increment(&dma_config, false);
    channel_config_set_dreq(&dma_config, pio_get_dreq(z80_pio, z80_sm, false));
    dma_channel_configure(
        z80_dma,
        &dma_config,
        &z80_pio->txf[z80_sm],
        sram,
        1,
        false
    );

}

void shutdown(void) {
	pio_sm_set_enabled(pio1, ztick_sm, false);
	pio_sm_set_enabled(pio1, z80_sm, false);
}

int __not_in_flash("main") main() {
//int main() {

	atexit(shutdown);

	vreg_set_voltage(VREG_VSEL);
	sleep_ms(10);

	// Run system at TMDS bit clock
	set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

	// not using uart (we need the pins :) )
	//setup_default_uart();
    //stdio_init_all();

	// switch on system LED to indicate we are running
	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);
	gpio_put(LED_PIN, true);

	// clock the Z80 manually for now
	gpio_init(28);
	gpio_set_dir(28, GPIO_OUT);

	// init tinyusb host stack on configured roothub port
  	tusb_init();
 	board_init();
  	tuh_init(BOARD_TUH_RHPORT);

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

	// start the Z80
	start_z80();
	process_z80();
	//start_z80_ram();

	// main loop
	while (1) {

		// TEST TEST TEST

#if 0

     	//pio_sm_set_consecutive_pindirs(z80_pio, z80_sm, 0, z80_ram_PIN_COUNT, false);
        
		uint32_t addr = pio_sm_get_blocking(z80_pio, z80_sm) >> 24;
        Con_printf("Addr: 0x%02x, Data: 0x%02x\n", addr, sram[addr]);

        //pio_sm_set_consecutive_pindirs(z80_pio, z80_sm, 0, z80_ram_PIN_COUNT, true);
       
		dma_channel_set_read_addr(z80_dma, &sram[addr], true);
#else
		// Wait for data in the RX FIFO
        uint32_t pin_states = pio_sm_get_blocking(z80_pio, z80_sm);
	
		// reconfigure bus pins for output (true)
	    // pio_sm_set_consecutive_pindirs(z80_pio, z80_sm, 0, 8, true);

		// Extract the 6-bit value (GP0-GP5)
        //pin_states &= 0x3F; // Mask to 6 bits
		static uint8_t last_addr = 0;
		uint8_t addr = pin_states >> 24;

		// Print the pin states
        //Con_printf("A0-A5: 0x%04x\n", pin_states);
		static uint32_t last_time_ms;
		uint32_t time_ms = time_us_32() / 1000;
        
		if(addr != ++last_addr) {
			Con_printf("%ums Error: addr:%u last:%u\n", time_ms, addr, last_addr);
		}

		Con_printf("dT: %u ms - ", time_ms-last_time_ms);
        Con_printf("A0-A7: %d\n", addr);

		last_time_ms = time_ms;
		last_addr = addr;

		/*
		Con_printf("GP0-GP5 states: 0x%02x (GP0=%d, GP1=%d, GP2=%d, GP3=%d, GP4=%d, GP5=%d)\n",
               pin_states,
               (pin_states >> 0) & 1,
               (pin_states >> 1) & 1,
               (pin_states >> 2) & 1,
               (pin_states >> 3) & 1,
               (pin_states >> 4) & 1,
               (pin_states >> 5) & 1); */

#endif
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

