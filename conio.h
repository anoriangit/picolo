#pragma once
#include <stdio.h>

// m16 console nonstandard codes (meta characters) for cursor keys and CTRL-key combos
#define M16_CON_CURSOR_LEFT		4
#define M16_CON_CURSOR_RIGHT	5
#define M16_CON_CURSOR_UP		6
#define M16_CON_CURSOR_DOWN		7
#define M16_CON_CTRL_Q			14
#define M16_CON_CTRL_S			15
#define M16_CON_CTRL_C			16
#define M16_CON_CTRL_X			17
#define M16_CON_CTRL_V			18
#define M16_CON_CTRL_K			19
#define M16_CON_CTRL_Y			20
#define M16_CON_CTRL_B			21
#define M16_CON_CTRL_L			22
#define M16_CON_CTRL_M			23
#define M16_CON_HOME			24
#define M16_CON_END				25
#define M16_CON_PGUP			26
#define M16_CON_PGDOWN			28
#define M16_META_CHAR(c) ((c >= 4 && c <= 7) || (c >= 14 && c <= 28))

#ifdef CONIO_STDIO_OVERRIDES
// latch in our stdlib replacements using simple defines
#undef fputs
#define fputs Con_fputs
#undef puts
#define puts Con_puts
#undef putc
#define putc Con_putc
#undef fputc
#define fputc Con_fputc
#undef fprintf
#define fprintf Con_fprintf
#undef printf
#define printf Con_printf
#undef getc
#define getc Con_getc
#endif

void ConOpen();
void ConClose();

// stdio style output
extern int Con_fputs(const char *p, FILE *stream);
extern int Con_puts(const char* p);
extern int Con_fprintf(FILE *stream, const char* const format, ...);
extern int Con_printf(const char *format, ...);
int Con_nprintf(size_t len, const char *format, ...);
extern int Con_putc(int c, FILE *Stream);
extern int Con_fputc(int c, FILE *Stream);

// input
extern int Con_getc(FILE* stream);
int Con_getc_nb();

// cursor
extern void ConCursorOff();
extern void ConCursorOn();
extern int ConGetCursorRow();
extern int ConGetCursorColumn();
extern void ConGetCursorPos(int *row, int *col);
extern void ConSetCursorPos(int row, int col);
extern void ConClearCursorRow();
extern void ConCursorUp();
extern void ConCursorDown();
extern void ConCursorLeft();
extern void ConCursorRight();

extern void DrawCursor();
extern void UnDrawCursor();

extern void ConEchoOn();
extern void ConEchoOff();

// keyboard input interface
void ConStoreCharacter(int c);

// conio.h