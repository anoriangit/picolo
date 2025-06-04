#pragma once

// GPIOs
#define Z80_IORQ_PIN    8
#define Z80_WR_PIN      9

#define ADDR_OE_LSB_PIN     10     // address bus transceiver OE
#define DATA_OE_PIN         11     // data bus read transceiver OE

// SIO MASKS
#define OE_ENABLE   0
#define OE_DISABLE  1

#define Z80_IORQ_MASK   (1<<Z80_IORQ_PIN) 
#define Z80_WR_MASK     (1<<Z80_WR_PIN) 

#define Z80_IORQ(p) 	(!(p & Z80_IORQ_MASK))
#define Z80_WR(p) 	    (!(p & Z80_WR_MASK))

#define ADDR_OE_LSB_MASK (1<<ADDR_OE_LSB_PIN)
#define DATA_OE_MASK    (1<<DATA_OE_PIN)


#define SRAM_SIZE 256
extern uint8_t SRAM[SRAM_SIZE];

extern PIO zxe_pio;
extern uint zxe_iord_sm;

// Z80 read cycle dma channels
//extern int read_addr_dma;
//extern int read_ram_dma;

// Z80 write cycle dma channels
//extern int read_addr_w_dma;
//extern int write_ram_dma;


void zxe_init();
void zxe_init_pio();

// zxe_driver.h