
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
#include "zxe.pio.h"
#include "zxe_driver.h"


PIO zxe_pio;
uint zxe_iord_sm;

// Align SRAM on 256-byte (8-bit) boundary
uint8_t SRAM[SRAM_SIZE] __attribute__((aligned(256))) = {
    0x21,0x0e,0x00,     // 0: ld hl, 14
    0x11,0xd8,0x00,     // 3: ld de, 216
    0x01,0x28,0x00,     // 6: ld bc, 40
    0xed,0xb0,          // 9: ed b0 ldir
    0xc3,0x00,0x00,     // 11:jp 0
    0x48,0x65,0x6c,0x6c,0x6f    // 14: .db "Hello"
};


static void __not_in_flash_func(zxe_iord_irq_handler)(void) {
    // Clear the interrupt request.
	pio_interrupt_clear(zxe_pio, 0);
#if 1
    //uint32_t addr = (uint32_t)dma_hw->ch[read_ram_dma].al3_read_addr_trig;
    uint32_t data = zxe_pio->txf[zxe_iord_sm];
    printf("irq iord:0x%08x\n", data);
#endif

}

void zxe_init() {

    for(int i = Z80_BUS_PIN_0; i < Z80_NUM_BUS_PINS; i++) {
    	gpio_init(i);
	    gpio_set_dir(i, GPIO_IN);
	    gpio_set_pulls(i, false, true);	    // up, down
    }

    gpio_init(Z80_IORQ_PIN);
	gpio_set_pulls(zxe_IORQ_PIN, false, true);	// up, down
	gpio_set_dir(Z80_IORQ_PIN, GPIO_IN);
	
	gpio_init(Z80_WR_PIN);
	gpio_set_pulls(zxe_WR_PIN, false, true);	// up, down
	gpio_set_dir(Z80_WR_PIN, GPIO_IN);

	// transceiver OE
	gpio_init(ADDR_OE_MSB_PIN);
	gpio_set_dir(ADDR_OE_MSB_PIN, GPIO_OUT);
	gpio_set_pulls(ADDR_OE_MSB_PIN, true, false);	// up, down

    gpio_init(ADDR_OE_LSB_PIN);
	gpio_set_dir(ADDR_OE_LSB_PIN, GPIO_OUT);
	gpio_set_pulls(ADDR_OE_LSB_PIN, true, false);	// up, down

	gpio_init(DATA_OE_PIN);
	gpio_set_dir(DATA_OE_PIN, GPIO_OUT);
	gpio_set_pulls(DATA_OE_PIN, true, false);	// up, down

    // initially we want addr lsb enabled and addr msb + data disabled
	gpio_put(ADDR_OE_MSB_PIN, OE_DISABLE);
	gpio_put(ADDR_OE_LSB_PIN, OE_ENABLE);
	gpio_put(DATA_OE_PIN, OE_DISABLE);

}


void zxe_init_pio() {

	// Choose PIO instance and claim a state machine
    zxe_pio = pio1;
    
    zxe_iord_sm = pio_claim_unused_sm(zxe_pio, true);
	printf("zxe_iord_sm: %d\n", zxe_iord_sm);

    // Load and initialize the READ PIO program
    int offset = pio_add_program(zxe_pio, &zxe_program);
	if(offset >= 0)
		printf("zxe_program offset: %d\n", offset);
	else
		panic("zxe pio init failed");

    zxe_program_init(zxe_pio, zxe_iord_sm, offset);

    // Need to clear _input shift counter_, as well as FIFO, because there may be
    // partial ISR contents left over from a previous run. sm_restart does this.
    // NOTE: not sure about this?!?
    pio_sm_clear_fifos(zxe_pio, zxe_iord_sm);
    pio_sm_restart(zxe_pio, zxe_iord_sm);
 

    // Get SM up and running 
    pio_sm_set_enabled(zxe_pio, zxe_iord_sm, true);

#if 0
    // IORD (pio1)
    pio_set_irq0_source_mask_enabled(zxe_pio, 0b0000111100000000 /*3840*/, true);	// enables all 4 irq
    irq_set_exclusive_handler(PIO1_IRQ_0, zxe_iord_irq_handler);
    //pio_set_irqn_source_enabled(z80_pio, PIO_IRQ_TO_USE, pio_get_rx_fifo_not_empty_interrupt_source(z80_pio), true);
	irq_set_enabled(PIO1_IRQ_0, true);
#endif

}

// zxe_driver.c