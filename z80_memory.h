#pragma once

#define SRAM_SIZE 256
extern uint8_t SRAM[SRAM_SIZE];

int process_z80();
int start_z80(float tick_freq);
