#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdarg.h>

#include "display.h"
#include "conio.h"
#include "cstream.h"


static struct CharacterStream CSTDIN;

// CURSOR ROW & COL (character modes)
static int CROW = 0;
static int CCOL = 0;

static unsigned char CON_ECHO = 1;	// echo is on by default
static int IMPLICIT_CR = 1;			// enable impicit CR (on LF) by default
static int IMPLICIT_LF = 1;			// enable impicit LF (on CR) by default
static unsigned char cursor_enabled = 1;


/* --------------------------------------------------------------------------*
 * CURSOR                                                                    *
 * --------------------------------------------------------------------------*/

//static int last_cursor_row, last_cursor_col;
//static int cursor_on = 0;

void DrawCursor() {
    if(cursor_enabled)
        SetAttribute(D_ATTR_FLASH, CROW, CCOL);
}
void UnDrawCursor() {
    if(cursor_enabled)
        UnSetAttribute(D_ATTR_FLASH, CROW, CCOL);
}

void ConCursorOff() {
	UnDrawCursor();
	cursor_enabled = 0;
}

void ConCursorOn() {
	cursor_enabled = 1;
	DrawCursor();
}


int ConGetCursorRow() {
	return CROW;
}

int ConGetCursorColumn() {
	return CCOL;
}

void ConGetCursorPos(int *row, int *col) {
	*row = CROW;
	*col = CCOL;
}

void ConSetCursorPos(int row, int col) {
	UnDrawCursor();
	CROW = row;
	CCOL = col;
	DrawCursor();
}

void ConClearCursorRow() {
	ConCursorOff();
	for (int column = 0; column < D_CHAR_COLS; column++) {
		PutCharacter(' ', CROW, column);
	}
	ConCursorOn();
}


// Move Cursor UP
void ConCursorUp() {
	if (CROW > 0) {
		UnDrawCursor();
		CROW--;
		DrawCursor();
	}
}

// Move Cursor DOWN
void ConCursorDown() {
	if (CROW < D_CHAR_ROWS - 1) {
		UnDrawCursor();
		CROW++;
		DrawCursor();
	}
}

// moves the cursor left if possible
// does not clear what is "under the cursor" (as opposed to CursorBackspace())
void ConCursorLeft() {
	if (CCOL > 0) {
		UnDrawCursor();
		CCOL--;
		DrawCursor();
	}
}
// no auto-wrap into the next line
void ConCursorRight() {
	if (CCOL < D_CHAR_COLS - 1) {
		UnDrawCursor();
		CCOL++;
		DrawCursor();
	}
}

/* --------------------------------------------------------------------------*
 * CHARACTER OUTPUT                                                          *
 * --------------------------------------------------------------------------*/

// FIXME: obsolete
// call this direcly if you already have a cursor lock
static void _AdvanceRowInner() {
	CROW++;
	if (CROW == D_CHAR_ROWS) {
        // FIXME: implement
		CharacterDisplayScrollUp();
		CROW--;
	}
}

// automatically scrolls up
static void _AdvanceRow() {
    UnDrawCursor();
	_AdvanceRowInner();
    DrawCursor();
}

// move the cursor right: automatically skips to next row when 
// advancing past the right screen border
static void _AdvanceCursor() {
    UnDrawCursor();
	CCOL++;
	if (CCOL == D_CHAR_COLS) {
		CCOL = 0;
		_AdvanceRowInner();		// we already have the cursor locked
	}
    DrawCursor();
}

static void _CarriageReturn() {
    UnDrawCursor();
	CCOL = 0;
    DrawCursor();
}

// backspacing up to the previous row not supported so far
void _BackspaceCursor() {
    UnDrawCursor();
	CCOL--;
	if (CCOL < 0) {
		CCOL = 0;
	}
	// overwrite the current cursor position with a space: use the display driver
	// api directly because we don't want to change the cursor position with this write
	PutCharacter(' ', CROW, CCOL);
    DrawCursor();
}

// write a character to the display at CROW, CCOL
// handles \n \r and modifies CROW, CCOL accordingly
// TODO: implement \b and \t (backspace and tab)
void ConWriteCharacter(int c) {
	switch (c) {
	case 8:				// BACKSPACE
		_BackspaceCursor();
		break;
	case 13:			// CR
		_CarriageReturn();
		if (IMPLICIT_LF) _AdvanceRow();
		break;
	case 10:			// LF
		_AdvanceRow();
		if (IMPLICIT_CR) _CarriageReturn();
		break;
	default:
		PutCharacter(c, CROW, CCOL);
		_AdvanceCursor();
	}
}

/* --------------------------------------------------------------------------*
 * CONSOLE STDIO EMULATION                                                   *
 * --------------------------------------------------------------------------*/

// implements a minimal fputs() where stream is ignored (everything goes to the display)
int Con_fputs(const char *p, FILE *stream) {
	int count = 0;
	while (*p) {
		ConWriteCharacter(*p++);
		count++;
	}
	return count;
}

int Con_puts(const char* p) {
	return Con_fputs(p, stdout);
}

#define BUFFER_SIZE 512
static char buffer[BUFFER_SIZE];

// checks if stream is not stdout or stderr: assume its a file and 
// call into stdlib's vfprintf() in that case
// returns error codes returned from C stdio
int Con_fprintf(FILE *stream, const char* const format, ...) {
	va_list argp;
	int err = 0;

	va_start(argp, format);
	if (stream == stdout || stream == stderr) {
		err = vsnprintf(buffer, BUFFER_SIZE, format, argp);
		Con_fputs(buffer, 0);
	} else {
		err = vfprintf(stream, format, argp);
	}
	va_end(argp);
	return err;
}

int Con_printf(const char *format, ...) {
	va_list argp;
	va_start(argp, format);
	int err = vsnprintf(buffer, BUFFER_SIZE, format, argp);
	va_end(argp);
	Con_fputs(buffer, 0);
	return err;
}

// stream blah blah blah
int Con_putc(int c, FILE *Stream) {
	ConWriteCharacter(c);
	return c;
}

int Con_fputc(int c, FILE *Stream) {
	ConWriteCharacter(c);
	return c;
};

// Reads from our console stdin stream or passes through to C stdio (if stream is not stdin)
// blocks if no data is available 
int Con_getc(FILE* stream) {
	int c = EOF;
    //printf("entering Con_getc()\n");
    
	if (stream == stdin) {
		// wait for keyboard input to be available
		do {
			c = StreamReadCharacter(&CSTDIN);
		} while (c == 0);
	
		//if (CON_ECHO && !M16_META_CHAR(c))	// don't echo meta characters
			//ConWriteCharacter(c);
	}
	else {
		c = getc(stream);		// pass through to C stdio if not stdin
	} 
	return c;
}

// non-blocking getc (stdin only)
// returns 0 if no character is ready
int Con_getc_nb() {
	int c = StreamReadCharacter(&CSTDIN);
	
	//if (c && CON_ECHO && !M16_META_CHAR(c))	// don't echo meta characters
	//	ConWriteCharacter(c);

	return c;
}

// only stdin supported at the moment

void Con_flush(FILE* stream) {
	if (stream == stdin) {
		StreamRewind(&CSTDIN);
	}
}

// This must be called before any other call to a public function in this file
void ConOpen() {
	CROW = CCOL = 0;
    DrawCursor();
	OpenCharacterStream(&CSTDIN);
}

void ConClose() {
	//if (_STDINLOCK)
	//	SDL_DestroySemaphore(_STDINLOCK);
	//if(_CLOCK)
	//	SDL_DestroySemaphore(_CLOCK);
}

void ConReset() {
	CROW = CCOL = 0;
}

void ConEchoOn() {
	CON_ECHO = 1;
}

void ConEchoOff() {
	CON_ECHO = 0;
}

/* --------------------------------------------------------------------------*
 * Keyboard Input Interface                                                  *
 * --------------------------------------------------------------------------*/

// Feed external input from host app
// this can be extended to process more complex input (see micro-conio.c)
// but for now just characters will do
void ConStoreCharacter(int c) {

	if((c >= 32 && c < 127) || c == 13 || c == 8) {
		if(c == 13) 	// HID will deliver 13 (CR) for the return key
			c = 10;		// for our purposes we want 10 (LF) though
			
		StreamWriteCharacter(c, &CSTDIN);

		//putchar(c);
		if (CON_ECHO && !M16_META_CHAR(c))	// don't echo meta characters
			ConWriteCharacter(c);
	}
}

// conio.c
