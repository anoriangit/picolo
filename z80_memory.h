#pragma once

#define SRAM_SIZE 256
extern uint8_t SRAM[SRAM_SIZE];

extern PIO z80_read_pio;
extern PIO z80_write_pio;

// Z80 read and write state machines
extern uint z80_read_sm;
extern uint z80_write_sm;

// Z80 read cycle dma channels
extern int read_addr_dma;
extern int read_ram_dma;

// Z80 write cycle dma channels
extern int read_addr_w_dma;
extern int write_ram_dma;

int process_z80();
int start_z80(float tick_freq);
