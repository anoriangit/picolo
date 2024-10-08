#pragma once

#define LINE_MAX_CHARS 40
#define MAX_LIST_LINES 512
#define ED_MAX_FILENAME 30
#define ED_VERSION_STR "1.0"

enum ED_MODES { ED_BASICMODE };
extern unsigned char ED_MODE;

struct TextCursor {
	int row, col;
};

struct TextBuffer {
	char source_filename[LINE_MAX_CHARS + 1];
	char out_lines[MAX_LIST_LINES][LINE_MAX_CHARS + 1];
	int line_len[MAX_LIST_LINES];	// lengths of out_lines
	struct TextCursor C;			// the actual text cursor
	struct TextCursor SC;			// screen cursor: used to "remember" cursor position between buffer switches
	int next_line;
	int line_mark;
	unsigned char b_dirty;
};

struct Win {
	int n_cols;
	int n_rows;			// main display area row count excluding status row
	int n_total_rows;
	int status_row;		// the row index of the status line
};

extern struct Win W;
extern struct TextBuffer* BUFFERS[10];
extern struct TextBuffer* CB;
extern int CBI;
extern struct TextBuffer KILLBUF;

// ed.c
void e_PrintPage();
void e_InsertLineBefore(int r);
void e_RemoveLine(int r);
void e_InitBuffer(struct TextBuffer*);
int e_Edit(char* filename);

// ed_cursor.c
void e_DoCursorDown();
void e_DoCursorUp();
void e_DoCursorRight();
void e_DoCursorLeft();
void e_DoPageDown();
void e_DoPageUp();

// ed_status.c
void e_PrintStatusLine();
int e_GetStatusTextLen();
void e_ClearStatus();
void e_SetExtraStatusTextTemp(char* text);
void e_SetExtraStatusText(char* text);

// ed_copypaste.c
void e_DoLineYank();
void e_DoLineKill(unsigned char b_kill);

// ed.h