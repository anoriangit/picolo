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

#ifndef DMA_IRQ_PRIORITY
#define DMA_IRQ_PRIORITY PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY
#endif

#ifndef PIO_IRQ_PRIORITY
#define PIO_IRQ_PRIORITY PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY
#endif

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
// Note that we are running this on PIO0 (which is also used by PICODVI)
int start_z80(float tick_freq) {

	static const uint clock_pin = 27;

	// Choose PIO instance (0 or 1)
    PIO pio = pio0;

    // Get first free state machine in PIO 1
    ztick_sm = pio_claim_unused_sm(pio, true);
	Con_printf("ztick_sm: %d\n", ztick_sm);

    // Add PIO program to PIO instruction memory. SDK will find location and
    // return with the memory offset of the program.
    int offset = pio_add_program(pio, &ztick_program);
	if(offset >= 0)
		Con_printf("ztick program offset: %d\n", offset);
	else
		return offset;

	/*
	The PIO state machine’s clock frequency is:
	PIO frequency=System clock frequency / Clock divider
	*/

    // Calculate the PIO clock divider
    float div = (float)clock_get_hz(clk_sys) / tick_freq;
	Con_printf("ztick clock div: %f\n", div);

    // Initialize the program using the helper function in our .pio file
    ztick_program_init(pio, ztick_sm, offset, clock_pin, div);

    // Start running our PIO program in the state machine
    pio_sm_set_enabled(pio, ztick_sm, true);
	return 0;

}

PIO z80_pio;
uint z80_sm;
int read_addr_dma = -1;
int read_ram_dma = -1;

#define SRAM_SIZE 256
// Align SRAM on 256-byte (8-bit) boundary
uint8_t SRAM[SRAM_SIZE] __attribute__((aligned(256)));



void __not_in_flash_func(dma_irq_handler_address)(void) {

	// Clear the interrupt request for the dma channel
    // Clear the interrupt request.
    // dma_hw->ints1 = 1u << read_ram_dma;
	pio_interrupt_clear(z80_pio, 0);

    uint32_t addr = (uint32_t)dma_hw->ch[read_ram_dma].al3_read_addr_trig;
    // uint16_t value = *((uint16_t *)addr);
    // DPRINTF("DMA ADDR: $%x, VALUE: $%x\n", addr, value);

    Con_printf("PIO IRQ: SRAM addr:%u\n", addr&0x000000ff);
    //Con_printf(".");

    // Restart the DMA
    //    dma_channel_start(1);
}

int process_z80() {

    // Initialize SRAM with a contiguous sequence of "JR 6" relative jumps
    for (int i = 0; i < SRAM_SIZE; i+=2){ 
		SRAM[i] = 0x18;
		SRAM[i+1] = 0x6;
	}

	// Choose PIO instance and claim a state machine
    z80_pio = pio1;
    z80_sm = pio_claim_unused_sm(z80_pio, true);
	Con_printf("zproc_sm: %d\n", z80_sm);

    // Load the PIO program
    int offset = pio_add_program(z80_pio, &zproc_program);
	Con_printf("zproc program offset: %d\n", offset);

    // Initialize the PIO state machine
    zproc_program_init(z80_pio, z80_sm, offset);

    // Need to clear _input shift counter_, as well as FIFO, because there may be
    // partial ISR contents left over from a previous run. sm_restart does this.
    pio_sm_clear_fifos(z80_pio, z80_sm);
    pio_sm_restart(z80_pio, z80_sm);
 	
    // DMA
    // Claim the first available DMA channel for read_addr channel
	read_addr_dma = dma_claim_unused_channel(true);
   	Con_printf("DMA read_addr: %d\n", read_addr_dma);
    if (read_addr_dma == -1) {
        Con_printf("DMA channel error read_addr\n");
        return -1;
    }

    // Claim another available DMA channel for RAM lookup channel
    read_ram_dma = dma_claim_unused_channel(true);
   	Con_printf("DMA read_ram: %d\n", read_ram_dma);
    if (read_ram_dma == -1) {
        Con_printf("DMA channel error read_ram\n");
        return -1;
    }

    // DMA configuration
    // Lookup RAM dma: the address of the data to read is injected from the
    // chained previous DMA channel (read_addr_dma) into the read address trigger register.
    // This DMA does the lookup and pushes the 8 bit result into the TX FIFO

    dma_channel_config cdmaLookup = dma_channel_get_default_config(read_ram_dma);
    channel_config_set_transfer_data_size(&cdmaLookup, DMA_SIZE_8);
    channel_config_set_read_increment(&cdmaLookup, false);
    channel_config_set_write_increment(&cdmaLookup, false);
    channel_config_set_dreq(&cdmaLookup, pio_get_dreq(z80_pio, z80_sm, true)); // dreq true=send to sm, false=read from sm
    //channel_config_set_chain_to(&cdmaLookup, read_addr_dma);

	/*
	Configure all DMA parameters and optionally start transfer.
	static void dma_channel_configure (
		uint channel, 
		const dma_channel_config *config, 
		volatile void *write_addr, 
		const volatile void *read_addr, 
		uint transfer_count, 
		bool trigger)
	*/

	dma_channel_configure(
        read_ram_dma,
        &cdmaLookup,
        &z80_pio->txf[z80_sm],		// write to
        SRAM,						// read from: this is provided by the chained read addr channel
        1,							// transfer count
        false
	); 

    // Read ADDRESS dma: the address to read from the simulated RAM is obtained from the FIFO
    // and injected into the read address trigger register of the lookup data DMA channel
    // chained.
    dma_channel_config cdma = dma_channel_get_default_config(read_addr_dma);
    channel_config_set_transfer_data_size(&cdma, DMA_SIZE_32);
    channel_config_set_read_increment(&cdma, false);
    channel_config_set_write_increment(&cdma, false);
    channel_config_set_dreq(&cdma, pio_get_dreq(z80_pio, z80_sm, false));
    //channel_config_set_chain_to(&cdmaLookup, read_ram_dma);

	dma_channel_configure(
        read_addr_dma,
        &cdma,
        &dma_hw->ch[read_ram_dma].al3_read_addr_trig,	// write to
        &z80_pio->rxf[z80_sm],							// read from
        1,												// transfers count
        false
	);

	// push 24 most significant bits of the base address of SRAM
	pio_sm_put(z80_pio, z80_sm, (uintptr_t) SRAM>>8);

    // Start running our PIO program in the state machine
    pio_sm_set_enabled(z80_pio, z80_sm, true);

	// DEBUG: irq handler for read_ram_dma
	// can not get this to work at all :(
#if 0
    irq_add_shared_handler(DMA_IRQ_TO_USE, dma_irq_handler_address, DMA_IRQ_PRIORITY);
    irq_set_enabled(dma_get_irq_num(DMA_IRQ_TO_USE), true);
	dma_irqn_set_channel_enabled(DMA_IRQ_TO_USE, read_ram_dma, true);
#endif

#if 1
	// try a pio irq
	pio_set_irq0_source_mask_enabled(z80_pio, 3840, true);	// enables all 4 irq
    irq_set_exclusive_handler(PIO1_IRQ_0, dma_irq_handler_address);
    //pio_set_irqn_source_enabled(z80_pio, PIO_IRQ_TO_USE, pio_get_rx_fifo_not_empty_interrupt_source(z80_pio), true);
	irq_set_enabled(PIO1_IRQ_0, true);

#endif
	return 0;
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
	//gpio_init(28);
	//gpio_set_dir(28, GPIO_OUT);

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

	
	// start the Z80 ticking
	int error = 0;
	process_z80();

	// 1000->35Hz (with 60 wait cycles in ztick.pio)
	// 5000->124Hz, 10000->250Hz 
	static const float tick_freq = 1000;		
	if(start_z80(tick_freq) < 0) {
		Con_printf("Error starting Z80 clock\n");
		error = 1;
	}

	uint32_t n_loops = 0;

	// main loop
	while (1) {

		static uint8_t last_addr = 0;

#if 0

     	//pio_sm_set_consecutive_pindirs(z80_pio, z80_sm, 0, z80_ram_PIN_COUNT, false);
        
		uint32_t addr = pio_sm_get_blocking(z80_pio, z80_sm) >> 24;
        Con_printf("Addr: 0x%02x, Data: 0x%02x\n", addr, sram[addr]);

        //pio_sm_set_consecutive_pindirs(z80_pio, z80_sm, 0, z80_ram_PIN_COUNT, true);
       
		dma_channel_set_read_addr(z80_dma, &sram[addr], true);
#else
		if(!error) {
		
		// "Manual" variant (CPU does the transfer)
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
#if 1
		// semi automatic DMA variant: 
		// write the address to read from to the read_ram dma channel and trigger it
		dma_channel_set_read_addr(read_ram_dma, (void*)((uint8_t*)addr), true);
#else
		// pure CPU "manual" variant
		// for left shift osr we need to move the data byte into the msb of the 32 bit word
		// uint32_t data = SRAM[addr] << 24;
		uint32_t data = (*((uint8_t*)addr)) << 24;
		pio_sm_put_blocking(z80_pio, z80_sm, data);		// move to tx fifo (pulled into osr)
#endif
		// DEBUG: Print the pin states
        //Con_printf("A0-A5: 0x%04x\n", pin_states);
		static uint32_t last_time_ms;
		uint32_t time_ms = time_us_32() / 1000;
      
#if 0	
		if(addr != last_addr+4) {
			Con_printf("%ums E: addr:%u last:%u diff:%u\n", time_ms, addr, last_addr, addr-last_addr);
		}
#endif

#if 0
		Con_printf("dT: %03u ms - ", time_ms-last_time_ms);
        Con_printf("A0-A7: %d\n", addr&0x000000ff);
#endif
	
		last_time_ms = time_ms;
		last_addr = addr;
		n_loops++;
		if(!(n_loops%1000)) {
			Con_printf(".");
		}
	
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

