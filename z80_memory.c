#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

#include "platform.h"
#include "display.h"
#include "conio.h"
#include "shell.h"
#include "ztick.pio.h"
#include "zproc.pio.h"


#ifndef DMA_IRQ_PRIORITY
#define DMA_IRQ_PRIORITY PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY
#endif

#ifndef PIO_IRQ_PRIORITY
#define PIO_IRQ_PRIORITY PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY
#endif



uint ztick_sm;

// Z80 clock ticking
// Note that we are running this on PIO0 (which is also used by PICODVI)
int start_z80(float tick_freq) {

	static const uint clock_pin = 28;

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
    channel_config_set_chain_to(&cdmaLookup, read_addr_dma);

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
    channel_config_set_chain_to(&cdmaLookup, read_ram_dma);

	dma_channel_configure(
        read_addr_dma,
        &cdma,
        &dma_hw->ch[read_ram_dma].al3_read_addr_trig,	// write to
        &z80_pio->rxf[z80_sm],							// read from
        1,												// transfers count
        true
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
	pio_set_irq0_source_mask_enabled(z80_pio, 0b0000111100000000 /*3840*/, true);	// enables all 4 irq
    irq_set_exclusive_handler(PIO1_IRQ_0, dma_irq_handler_address);
    //pio_set_irqn_source_enabled(z80_pio, PIO_IRQ_TO_USE, pio_get_rx_fifo_not_empty_interrupt_source(z80_pio), true);
	irq_set_enabled(PIO1_IRQ_0, true);
#endif

	return 0;
}

