#pragma once

#define ED_LINE_MAX_LEN 128
#define ED_LINE_MAX_CHARS 40		// on screen display length
#define ED_MAX_LIST_LINES 512
#define ED_MAX_FILENAME 30
#define ED_VERSION_STR "1.0"

enum ED_MODES { ED_BASICMODE };
extern unsigned char ED_MODE;

struct TextLineNode {
	char *text;
	struct TextLineNode *next;
	struct TextLineNode *prev;
};

struct TextCursor {
	int row, col;
};

// new, list based, text cursor
struct TextPos {
	struct TextLineNode *node;
	char *pos;
};

struct TextBuffer {
	struct TextLineNode *list_head;
	struct TextLineNode *list_tail;
	struct TextPos pos;

	char source_filename[ED_LINE_MAX_CHARS + 1];
	char out_lines[ED_MAX_LIST_LINES][ED_LINE_MAX_CHARS + 1];
	int line_len[ED_MAX_LIST_LINES];	// lengths of out_lines
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
int e_Edit(char* filename);

// ed_edit.c
void e_InsertLineBefore(int r);
void e_RemoveLine(int r);
unsigned char e_HandleBackspace();
void e_InsertCharacter(int c);
unsigned char e_HandleDel();
void e_handleNewLine();

// ed_buffer.c
void e_InitBuffer(struct TextBuffer*);
void e_BufferAppendLine(struct TextBuffer* B, char *line);
struct TextLineNode *e_BufferFindNode(struct TextBuffer* B, int i);
struct TextPos e_BufferCursor2Pos(struct TextBuffer *B, struct TextCursor cursor);

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