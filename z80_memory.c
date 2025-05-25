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
#include "zproc_read.pio.h"
#include "zproc_write.pio.h"


#ifndef USE_CONIO
#define Con_printf printf
#endif


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

	static const uint clock_pin = 22;

	// Choose PIO instance (0 or 1)
    PIO pio = pio1;

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

PIO z80_read_pio;
PIO z80_write_pio;

// Z80 read and write state machines
uint z80_read_sm;
uint z80_write_sm;

// Z80 read cycle dma channels
int read_addr_dma = -1;
int read_ram_dma = -1;

// Z80 write cycle dma channels
int read_addr_w_dma = -1;
int write_ram_dma = -1;

// Simulated RAM
#define SRAM_SIZE 256
// Align SRAM on 256-byte (8-bit) boundary
uint8_t SRAM[SRAM_SIZE] __attribute__((aligned(256))) = {
    0x21,0x0e,0x00,     // 0: ld hl, 14
    0x11,0xd8,0x00,     // 3: ld de, 216
    0x01,0x05,0x00,     // 6: ld bc, 5
    0xed,0xb0,          // 9: ldir
    0xc3,0x00,0x00,     // 11:jp 0
    0x48,0x65,0x6c,0x6c,0x6f    // 14: .db "Hello"
};

void __not_in_flash_func(z80_read_irq_handler)(void) {

    // Clear the interrupt request.
	pio_interrupt_clear(z80_read_pio, 0);

#if 1
    uint32_t addr = (uint32_t)dma_hw->ch[read_ram_dma].al3_read_addr_trig;
    Con_printf("read:%03u:0x%02x\n", addr&0x000000ff, SRAM[addr&0x000000ff]);
#endif

    /* grok version
    Neither the check for the interrupt no. (we are an exclusive handler)
    nor the fifo empty check (we are triggered from an irq statement in pio)
    are needed right now. 
    
    if (pio_interrupt_get(pio, 0)) {
        pio_interrupt_clear(pio, 0);
        if (!pio_sm_is_rx_fifo_empty(pio, sm_read)) {
            uint32_t addr = pio_sm_get(pio, sm_read) & 0xFF;
            printf("IRQ Read - Addr: 0x%02x\n", addr);
        }
    } */

}


void __not_in_flash_func(z80_write_irq_handler)(void) {

    // Clear the interrupt request.
	pio_interrupt_clear(z80_write_pio, 0);

    uint32_t addr = (uint32_t)dma_hw->ch[write_ram_dma].al2_write_addr_trig;
    uint8_t z_write_addr = addr&0x000000ff;
    uint8_t z_data_byte = z80_write_pio->rxf[z80_write_sm]&0x000000ff;
   
    Con_printf("write:%u:0x%x\n", z_write_addr, z80_write_pio->rxf[z80_write_sm]);
}

/*
void __not_in_flash_func(z80_write_irq1_handler)(void) {

    // Clear the interrupt request.
	pio_interrupt_clear(z80_write_pio, 1);

    uint8_t z_data_byte = z80_write_pio->rxf[z80_write_sm]&0x000000ff;
    Con_printf("write:%u:%u\n", z_write_addr, z_data_byte);
}
*/

int setup_dma() {

    // DMA channels for Z80 READ
	read_addr_dma = dma_claim_unused_channel(true);
   	Con_printf("DMA read_addr: %d\n", read_addr_dma);
    if (read_addr_dma == -1) {
        Con_printf("DMA channel error read_addr\n");
        return -1;
    }
    read_ram_dma = dma_claim_unused_channel(true);
   	Con_printf("DMA read_ram: %d\n", read_ram_dma);
    if (read_ram_dma == -1) {
        Con_printf("DMA channel error read_ram\n");
        return -1;
    }

    // DMA channels for Z80 WRITE
   	read_addr_w_dma = dma_claim_unused_channel(true);
   	Con_printf("DMA read_addr_w: %d\n", read_addr_w_dma);
    if (read_addr_w_dma == -1) {
        Con_printf("DMA channel error read_addr_w\n");
        return -1;
    }
    write_ram_dma = dma_claim_unused_channel(true);
   	Con_printf("DMA write_ram: %d\n", write_ram_dma);
    if (write_ram_dma == -1) {
        Con_printf("DMA channel error read_ram\n");
        return -1;
    }

    // DMA configuration for Z80 READ

    dma_channel_config cdmaReadRam = dma_channel_get_default_config(read_ram_dma);
    channel_config_set_transfer_data_size(&cdmaReadRam, DMA_SIZE_8);
    channel_config_set_read_increment(&cdmaReadRam, false);
    channel_config_set_write_increment(&cdmaReadRam, false);
    channel_config_set_dreq(&cdmaReadRam, pio_get_dreq(z80_read_pio, z80_read_sm, true)); // dreq true=send to sm, false=read from sm
    channel_config_set_chain_to(&cdmaReadRam, read_addr_dma);

	dma_channel_configure(
        read_ram_dma,
        &cdmaReadRam,
        &z80_read_pio->txf[z80_read_sm],		// write to
        SRAM,						// read from: this is provided by the chained read addr channel
        1,							// transfer count
        false
	); 

    // Read ADDRESS dma: the address to read from the simulated RAM is obtained from the FIFO
    // and injected into the read address trigger register of the lookup data DMA channel
    // chained.
    dma_channel_config cdmaReadAddr = dma_channel_get_default_config(read_addr_dma);
    channel_config_set_transfer_data_size(&cdmaReadAddr, DMA_SIZE_32);
    channel_config_set_read_increment(&cdmaReadAddr, false);
    channel_config_set_write_increment(&cdmaReadAddr, false);
    channel_config_set_dreq(&cdmaReadAddr, pio_get_dreq(z80_read_pio, z80_read_sm, false));
    channel_config_set_chain_to(&cdmaReadAddr, read_ram_dma);

	dma_channel_configure(
        read_addr_dma,
        &cdmaReadAddr,
        &dma_hw->ch[read_ram_dma].al3_read_addr_trig,	// write to
        &z80_read_pio->rxf[z80_read_sm],				// read from
        1,												// transfers count
        true
	);

    // ------------------------------------------------------------------------
    // DMA configuration for Z80 WRITE

    // Write RAM dma: the address of the data to read is injected from the
    // chained previous DMA channel (read_addr_dma) into the read address trigger register.
    // This DMA does the lookup and pushes the 8 bit result into the TX FIFO
#if 0
    dma_channel_config cdmaWriteRam = dma_channel_get_default_config(write_ram_dma);
    channel_config_set_transfer_data_size(&cdmaWriteRam, DMA_SIZE_8);
    channel_config_set_read_increment(&cdmaWriteRam, false);
    channel_config_set_write_increment(&cdmaWriteRam, false);
    channel_config_set_dreq(&cdmaWriteRam, pio_get_dreq(z80_write_pio, z80_write_sm, false)); // dreq true=send to sm, false=read from sm
    channel_config_set_chain_to(&cdmaWriteRam, read_addr_w_dma);

	dma_channel_configure(
        write_ram_dma,              // dma channel
        &cdmaWriteRam,              // config structure
        SRAM,                       // write to: actual address injected by read_addr_w
        &z80_write_pio->rxf[z80_write_sm],// read from: rx fifo (also trigger for channel as per dreq)
        1,							// transfer count
        false                       // don't enable yet
	); 

    // Read ADDRESS for write dma: the address to write to inside the simulated RAM is obtained from the FIFO
    // and injected into the read address trigger register of the lookup data DMA channel chained.
    dma_channel_config cdmaReadAddrW = dma_channel_get_default_config(read_addr_w_dma);
    channel_config_set_transfer_data_size(&cdmaReadAddrW, DMA_SIZE_32);
    channel_config_set_read_increment(&cdmaReadAddrW, false);
    channel_config_set_write_increment(&cdmaReadAddrW, false);
    channel_config_set_dreq(&cdmaReadAddrW, pio_get_dreq(z80_write_pio, z80_write_sm, false));
    channel_config_set_chain_to(&cdmaWriteRam, write_ram_dma);

    // grok: &dma_hw->ch[chan_write_data].al3_write_addr_trig, &pio->rxf[sm_write], 1, false);
	dma_channel_configure(
        read_addr_w_dma,                                // dma channel
        &cdmaReadAddrW,                                 // config struct
        &dma_hw->ch[write_ram_dma].al2_write_addr_trig, // write to
        &z80_write_pio->rxf[z80_write_sm],				// read from
        1,												// transfers count
        false                                           // enable
	);
 #endif
    return 0;
}

int process_z80() {

    // Initialize SRAM with a contiguous sequence of "JR 6" relative jumps
    //memset(SRAM,0,256);
#if 0
    for (int i = 0; i < SRAM_SIZE; i+=2){ 
		SRAM[i] = 0x18;
		SRAM[i+1] = 0x6;
	}
#endif

	// Choose PIO instance and claim a state machine
    z80_read_pio = pio1;
    z80_write_pio = pio0;
    
    z80_read_sm = pio_claim_unused_sm(z80_read_pio, true);
	Con_printf("zproc_read_sm: %d\n", z80_read_sm);
    z80_write_sm = pio_claim_unused_sm(z80_write_pio, true);
	Con_printf("zproc_write_sm: %d\n", z80_write_sm);

    // Load and initialize the READ PIO program
    int offset = pio_add_program(z80_read_pio, &zproc_read_program);
	if(offset >= 0)
		Con_printf("zproc_read program offset: %d\n", offset);
	else
		return offset;

    zproc_read_program_init(z80_read_pio, z80_read_sm, offset);

    // Load and initialize the WRITE PIO program
    // NOTE: the init() below totally messes up the read sm
    // cant do this!
#if 0
    offset = pio_add_program(z80_write_pio, &zproc_write_program);
	if(offset >= 0)
		Con_printf("zproc_write program offset: %d\n", offset);
	else
		return offset;

    zproc_write_program_init(z80_write_pio, z80_write_sm, offset);
#endif

    // Need to clear _input shift counter_, as well as FIFO, because there may be
    // partial ISR contents left over from a previous run. sm_restart does this.
    // NOTE: not sure about this?!?
    //pio_sm_clear_fifos(z80_pio, z80_read_sm);
    //pio_sm_restart(z80_pio, z80_read_sm);
 	
    if(setup_dma() != 0) {
        Con_printf("DMA setup error!\n");
        return -1;
    }

    // Get READ SM up and running 
    // push 24 most significant bits of the base address of SRAM
	pio_sm_put(z80_read_pio, z80_read_sm, (uintptr_t) SRAM>>8);
    pio_sm_set_enabled(z80_read_pio, z80_read_sm, true);

    // Get WRITE SM up and running 
    // push 24 most significant bits of the base address of SRAM
	//pio_sm_put(z80_write_pio, z80_write_sm, (uintptr_t) SRAM>>8);
    //pio_sm_set_enabled(z80_write_pio, z80_write_sm, true);

#if 1
	// pio irq times 2 for debugging
    // READ (pio1)
    pio_set_irq0_source_mask_enabled(z80_read_pio, 0b0000111100000000 /*3840*/, true);	// enables all 4 irq
    irq_set_exclusive_handler(PIO1_IRQ_0, z80_read_irq_handler);
    //pio_set_irqn_source_enabled(z80_pio, PIO_IRQ_TO_USE, pio_get_rx_fifo_not_empty_interrupt_source(z80_pio), true);
	irq_set_enabled(PIO1_IRQ_0, true);

    // WRITE (pio0)
	//pio_set_irq0_source_mask_enabled(z80_write_pio, 0b0000111100000000 /*3840*/, true);	// enables all 4 irq
    //irq_set_exclusive_handler(PIO0_IRQ_0, z80_write_irq_handler);
	//irq_set_enabled(PIO0_IRQ_0, true);

#endif

	return 0;
}

