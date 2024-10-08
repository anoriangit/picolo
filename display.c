
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "display.h"
#include "conio.h"
#include "besciifont.h"

extern uint8_t FRAMEBUF[];


// this basically is our "character screen": 40x30 at 320x240 pixel resolution when using an 8x8 font
static char charbuf[D_CHAR_ROWS * D_CHAR_COLS];
static uint8_t attrbuf[D_CHAR_ROWS * D_CHAR_COLS];
static uint8_t masks[8] = { 128,64,32,16,8,4,2,1};
static unsigned long frame = 0;
static uint8_t flash = 0;

void ClearTextDisplay() {
	memset(charbuf, 32, D_CHAR_ROWS*D_CHAR_COLS);
	memset(attrbuf, 0, D_CHAR_ROWS*D_CHAR_COLS);
}

void DisplayOpen() {
	ClearTextDisplay();
}

// Put a character into our character display buffer at row,col
// NOTE: this does no boundaries checking whatsoever
void PutCharacter(int c, int row, int col) {
		char *CPTR = charbuf + D_CHAR_COLS * row + col;
		*CPTR = (uint8_t)c;
}

void SetAttribute(uint8_t a, int row, int col) {
		uint8_t *APTR = attrbuf + D_CHAR_COLS * row + col;
		*APTR |= a;
}

void UnSetAttribute(uint8_t a, int row, int col) {
		uint8_t *APTR = attrbuf + D_CHAR_COLS * row + col;
		*APTR &= ~a;
}

// called from core1 scanline callback: pretty timing critical
void inline RenderTextScanline(int y) {


    int char_row = y / D_FONT_HEIGHT;
    int offset = y % D_FONT_HEIGHT;
    int index = char_row * D_CHAR_COLS;

    int fb_offset = y * D_FRAME_WIDTH;

    for(int i = 0; i < D_CHAR_COLS; i++) {
        char c = charbuf[index+i] - D_FONT_FIRST_ASCII;
		uint8_t a = attrbuf[index+i];
        uint8_t src_pixels = bescii[c * D_FONT_HEIGHT + offset];
        
		if((a & D_ATTR_INVERSE) || ((a & D_ATTR_FLASH) && flash)) {
			// inverse video
			src_pixels = src_pixels ^ 0xff;	// invert by xor
        }

        // scan the 8 glyph bits
    	for (int bit = 0; bit < 8; bit++) {
			// scan the bits inside the glyph byte and write pixels accordingly
            FRAMEBUF[fb_offset] = 0x03;
			if (src_pixels & masks[bit]) {
                   FRAMEBUF[fb_offset] = 0xff;
			} 
            fb_offset++;
		}
    }

    // drive this at 50hz or so for a sensible cursor blink rate
    // drive frame count and flash attribute
   	if(!(y%D_FRAME_HEIGHT)) { 
		frame++;
		if(!(frame%D_FLASH_DELAY))
			flash = !flash;
	}

}

// ----------------------------------------------------------------------------
// SCROLLING
// ----------------------------------------------------------------------------

// Scrolls both, the characters and the attributes buffers
void CharacterDisplayScrollUp() {
	char *dp;
	char *sp = NULL;
	
	uint8_t *asp = NULL;

	UnDrawCursor();

	// Note that this implementation relies on the width specifications matching 
	// the buffer width in bytes (i.e one character code stored being one byte too)
	for (int row = 1; row < D_CHAR_ROWS; row++) {
		dp = &charbuf[(row - 1) * D_CHAR_COLS];
		sp = &charbuf[row * D_CHAR_COLS];
		memcpy(dp, sp, D_CHAR_COLS);

		// scroll attributes too
		uint8_t *adp = &attrbuf[(row - 1) * D_CHAR_COLS];
		asp = &attrbuf[row * D_CHAR_COLS];
		memcpy(adp, asp, D_CHAR_COLS);
	}
	// clear bottom line
	memset(sp, 32, D_CHAR_COLS);
	memset(asp, 0, D_CHAR_COLS);

	DrawCursor();

}

// scroll everything above r (exclusive) upwards
void CharacterDisplayScrollUpRow(int r) {
	uint8_t *dp;
	uint8_t *sp = NULL;

	UnDrawCursor();

	for (int row = 1; row < r; row++) {
		dp = (uint8_t*) &charbuf[(row - 1) * D_CHAR_COLS];
		sp = (uint8_t*) &charbuf[row * D_CHAR_COLS];
		memcpy(dp, sp, D_CHAR_COLS);

		dp = &attrbuf[(row - 1) * D_CHAR_COLS];
		sp = &attrbuf[row * D_CHAR_COLS];
		memcpy(dp, sp, D_CHAR_COLS);
	}
	// clear bottom line
	//memset(sp, 0, DISPLAY_DATA.display_mode.width);
	DrawCursor();
}

// scroll down, starting at top of display down to row r (inclusive)
void CharacterDisplayScrollDownRow(int r) {
	char *dp;
	char *sp = NULL;

	UnDrawCursor();

	// Note that this algorithm relies on the width specifications matching 
	// the buffer width in bytes (i.e one character code stored being one byte too)
	for (int row = r; row > 0; row--) {
		dp = &charbuf[row * D_CHAR_COLS];
		sp = &charbuf[(row - 1) * D_CHAR_COLS];
		memcpy(dp, sp, D_CHAR_COLS);

		dp = (char*) &attrbuf[row * D_CHAR_COLS];
		sp = (char*) &attrbuf[(row - 1) * D_CHAR_COLS];
		memcpy(dp, sp, D_CHAR_COLS);
	}

	DrawCursor();

}


// start and end rows are inclusive
void CharacterDisplayScrollDownRange(int start_r, int end_r) {

	if (end_r == start_r)
		return;				// nothing to do

	uint8_t *dp;
	uint8_t *sp = NULL;

	UnDrawCursor();

	for (int row = end_r; row >= start_r; row--) {
		dp = (uint8_t*) & charbuf[row * D_CHAR_COLS];
		sp = (uint8_t *)&charbuf[(row - 1) * D_CHAR_COLS];
		memcpy(dp, sp, D_CHAR_COLS);

		dp = (uint8_t *)&attrbuf[row * D_CHAR_COLS];
		sp = (uint8_t *)&attrbuf[(row - 1) * D_CHAR_COLS];
		memcpy(dp, sp, D_CHAR_COLS);
	}

	DrawCursor();
}

// start and end rows are inclusive
void CharacterDisplayScrollUpRange(int start_r, int end_r) {
	char *dp;
	char *sp = NULL;

	UnDrawCursor();

	for (int row = start_r; row <= end_r; row++) {
		dp = &charbuf[(row - 1) * D_CHAR_COLS];
		sp = &charbuf[row * D_CHAR_COLS];
		memcpy(dp, sp, D_CHAR_COLS);

		dp = (char*) &attrbuf[(row - 1) * D_CHAR_COLS];
		sp = (char*) &attrbuf[row * D_CHAR_COLS];
		memcpy(dp, sp, D_CHAR_COLS);
	}

	DrawCursor();
}


// display.c