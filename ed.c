// ----------------------------------------------------------------------------
// m16 text editor
// gs july-2020
// updated 2024-10-10 gs
// ----------------------------------------------------------------------------
//
// features:
// full screen 40*30 edit with scroll
// multi buffer editing (ctrl-b N)
// status line indicating file name (and change status) and current cursor row/col
// full cursor keys and home/end support
// backspace and del
// line kill and re-insert (yank) via ctrl-k and ctrl-y
// ctrl-q quits (and prompts for save if file was changed)
//
// limitations:
// insert mode only (no overwrite - I *never* use that anyway :P)
// pgup/pgdwn not yet implemented


#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <malloc.h>

#define CONIO_STDIO_OVERRIDES
#include "display.h"
#include "conio.h"
#include "ed.h"

unsigned char ED_MODE = ED_BASICMODE;


//static unsigned char list_dirty[ED_MAX_LIST_LINES];	// per line dirty flags

// This are global states used to signal that ED has saved a modified source or loaded a new file
// by setting it to 1. Whoever reads and processes this should then reset it to 0
unsigned char ED_FILE_SAVED = 0;
unsigned char ED_FILE_LOADED = 0;

// Hard coded display window specifics
// this can easily be expanded to use information from the display system
// should we ever want the editor to run in other modes than 0
struct Win W = { 40, 29, 30, 29 };

// Text Buffers
struct TextBuffer* BUFFERS[10] = { 0,0,0,0,0,0,0,0,0,0 };
struct TextBuffer* CB = NULL; // pointer to  current buffer
int CBI = 0;

struct TextBuffer KILLBUF;

// Add a line to a text buffer
static void _addListLine(struct TextBuffer* B, char* outline) {

	e_BufferAppendLine(B, outline);

	// obsolete soon...
	if (CB->next_line < ED_MAX_LIST_LINES) {
		strncpy(&B->out_lines[B->next_line][0], outline, ED_LINE_MAX_CHARS);
		B->line_len[B->next_line] = (int)strlen(outline);
		B->next_line++;
	}
}

// OBSOLETE
// pull in the source code from BASIC
// this is very similar to the BASIC list command in bas55/cmd.c
static int _getBasicSource() {
#if 0
	struct basic_line* p;
	// int indent = 0;
	char buf[ED_LINE_MAX_CHARS + 1];
	int lines = 0;

	for (p = s_line_list; p != NULL; p = p->next) {
		// indent for loops in the listing
		if (!strncmp(p->str, "FOR ", 4)) {
			//indent += 4;
			sprintf(buf, "%d %s", p->number, p->str);
		}
		else if (!strncmp(p->str, "NEXT ", 5)) {
			//indent -= 4;
			sprintf(buf, "%d %s", p->number, p->str);
			puts(buf);
		}
		else {
			sprintf(buf, "%d ", p->number);
			//for (int i = 0; i < indent; i++)
			//	strcat(buf, " ");
			strcat(buf, p->str);
		}
		_addListLine(CB, buf);
		lines++;
	}
	return lines;
    #endif
    return 0;
}

// display one complete page of text starting from the current text cursor row
void e_PrintPage() {
	//ClearDisplay(P_MemoryRead(P_MEM_SV_BGCOLOR));
    ClearTextDisplay();
	ConSetCursorPos(0, 0);

	struct TextLineNode *node = CB->pos.node;
	int line_count = 0;
	while(node && line_count < W.n_rows) {
		// this simpy truncates long lines
		// +1 accounts for the \n character being included in the length calculation
		Con_nprintf(ED_LINE_MAX_CHARS+1, "%s\n", node->text);
		node = node->next;
		line_count++;
	}
	
	/*	old way (based on fixed size text buffers)
	for (int row = CB->C.row; row < CB->C.row + W.n_rows && row < CB->next_line; row++) {
		printf("%s\n", CB->out_lines[row]);
	} */
}

// ----------------------------------------------------------------------------
// USER INTERACTION
// ----------------------------------------------------------------------------

// Put a (Y)es/(N)o/(A)bort prompt into the status line, wait for user response
// and return result: 'y', 'n' or 'a'
static char _PromptYNA(char* prologue) {
	char st[80];
	sprintf(st, "%s [(Y)es/(N)o/(A)bort]", prologue);
	e_SetExtraStatusText(st);
	e_PrintStatusLine();
	int k;
	do {
		k = getc(stdin);
		if (k < 97) k += 32;	// force lower case
	} while (k != 'y' && k != 'n' && k != 'a');
	return k;
}

// Minimalistic line editor: character entry and backspace (no cursors etc)
// Returns 1 if a name was entered or 0 if the user aborted or ended 
// their input with a 0 length name
//
// NOTE that right now this can only accept file names up to a length of 
// ED_MAX_FILENAME, which is rather limiting. 
// FIXME: implement side scrolling inside the input "field" 
static unsigned char _PromptFilename(char* fname, char *prompt) {
	e_SetExtraStatusText(prompt);
	e_PrintStatusLine();

	int dcr, dcc;
	ConGetCursorPos(&dcr, &dcc);
	ConSetCursorPos(W.status_row, e_GetStatusTextLen());

	char* cp = fname;
	int count = 0;
	int c;
	do {
		c = getc(stdin);
		// FIXME: allowable characters should be way more limited than this I think
		if ((c > 32 && c <= 122) || c == 8) {	// no spaces allowed, hence > 32
			if (c == 8) {
				if (cp > fname) {
					*(--cp) = 0;
					ConCursorLeft();
					putc(' ', stdout);
					ConCursorLeft();
					count--;
				}
			} else if (count < ED_MAX_FILENAME) {
				*cp++ = c;
				count++;
				putc(c, stdout);
			}
		}
	} while (c != 10 && c != 27);	// ESC and ENTER are the only way out
	
	*cp = 0;
	ConSetCursorPos(dcr, dcc);

	// non abortive exit from the loop and at least one character in the filename
	if (c == 10 && cp > fname)
		return 1;

	return 0;
}

static int _handleQuit() {

	if (CB->b_dirty) {
		int k = _PromptYNA("Save file?");
		switch (k) {
		case 'a':
			e_SetExtraStatusText(NULL);
			e_PrintStatusLine();
			return 255;				// return something that is neither ascii nor one of our meta chars
			break;
		case 'n':
			e_SetExtraStatusText(NULL);
			e_PrintStatusLine();
			return M16_CON_CTRL_Q;	// return quit signal
			break;
		case 'y': {
			char fname[FILENAME_MAX];
			if (CB->source_filename[0] == 0)
				if (_PromptFilename(&fname[0], "Save file:")) {	// NOTE that this input gets limited to ED_MAXFILENAME len 
					strcpy(CB->source_filename, fname);
				}

			//_setExtraStatusText(NULL);
			//_printStatusLine();
			return M16_CON_CTRL_S;	// return save signal
		}
			break;
		}
	}
	return M16_CON_CTRL_Q;
}

// ----------------------------------------------------------------------------
// FILE IO
// ----------------------------------------------------------------------------


// Save the file
// for BASIC mode (the only mode we currently support anyway :D actually)
// we run all lines through a basicifier (tm) that makes the acceptable 
// returns number of lines saved or negative error code
// 
static int _doSave() {
	if (CB->source_filename[0]) {
		FILE* fp = fopen(CB->source_filename, "w");
		if (fp) {
			int line = 0;
			for (; line < CB->next_line; line++) {
				fprintf(fp, "%s\n", &CB->out_lines[line]);
			}
			fclose(fp);
			return line;
		}
	}
	return -1;
}

// Load a basic source and add the lines to the m16basic core
// returns number of lines loaded or negative error code
static int _doLoad(char *filename) {
	char buf[ED_LINE_MAX_CHARS+3];			// leave space for trailing \r and/or \n + 0
	FILE* fp = fopen(filename, "r");
	if (fp) {
		int lines = 0;
		// NOTE that fgets() reads a trailing 0x0D+0x0A (cr&lf) as a single LF!
		// (at least the visual c std lib does)
		while (fgets(buf, ED_LINE_MAX_CHARS+2, fp)) {
			size_t len = strlen(buf);
			if (buf[len - 1] == 10) buf[len - 1] = 0;	// remove any trailing newline characters
			lines++;
			_addListLine(CB, buf);
		}
		fclose(fp);
		return lines;
	}
	return -1;
}

// ----------------------------------------------------------------------------
// Buffer handling
// ----------------------------------------------------------------------------

static void _setBufferFilename(struct TextBuffer* b, char* fname) {
	strncpy(b->source_filename, fname, sizeof(b->source_filename) - 1);
	b->source_filename[sizeof(b->source_filename)-1] = 0;
}

// Load a file into the current buffer:
// returns number of lines loaded or negative error code
static int _bufferLoad(char* filename) {
	struct TextBuffer* temp_buffer = malloc(sizeof(struct TextBuffer));
	assert(temp_buffer);
	e_InitBuffer(temp_buffer);
	struct TextBuffer* old_current = CB;
	CB = temp_buffer;
	// load into CB
	int line_count = _doLoad(filename);
	if (line_count > 0) {
		// load ok
		free(old_current);
		_setBufferFilename(CB, filename);
		BUFFERS[CBI] = CB;
	}
	else {
		free(temp_buffer);
		CB = old_current;
	}
	return line_count;
}

static void _DoBufferSwitch() {
	// prompt the user for buffer number
	e_SetExtraStatusText("Press key 0-9");
	e_PrintStatusLine();
	int k;
	do {
		k = getc(stdin);
	} while (!(k >= 48 && k <= 57) && k != 27);		// esc aborts
	e_SetExtraStatusText(NULL);
	if (k != 27) {
		k -= 48;	// turn into proper number
		// handle buffer switch
		if (BUFFERS[k] != NULL) {
			// target buffer already exists: we can simply switch the current one out
			// and the target in: just don't forget to save the current display cursor
			// so we can later restore it should we come back to this buffer
			int dcr, dcc;
			ConGetCursorPos(&dcr, &dcc);	// current display cursor row&column
			CB->SC.row = dcr;				// save it
			CB->SC.col = dcc;

			// switch to the target buffer
			CBI = k;
			CB = BUFFERS[k];

			// restore the display
			// Note: currently not full functional as we move to the new list based system
			// right now we simply init the text cursor to point at the first line (if any)
			// and display from there

			CB->pos = e_BufferCursor2Pos(CB, CB->SC);
			//CB->pos.node = CB->list_head;
			//CB->pos.pos = CB->pos.node->text;
			e_PrintPage();
			//CB->C.row = CB->SC.row; CB->C.col = CB->SC.col; // not needed anymore soon'ish...
			ConSetCursorPos(CB->SC.row, CB->SC.col);	// restore the buffer's screen cursor position

			/*
			int tmp = CB->C.row;
			CB->C.row = CB->C.row - CB->SC.row;	// temporarily move up so that the text cursor is at the top of the screen
			e_PrintPage();						// only cares about row, ignores col
			CB->C.row = tmp;					// restore text cursor row
			ConSetCursorPos(CB->SC.row, CB->SC.col);		// and restore the new buffer's screen cursor position
			*/
		}
		else if (k != CBI) {
			// initialize a new buffer
			CB = malloc(sizeof(struct TextBuffer));
			assert(CB);
			CBI = k;
			BUFFERS[k] = CB;
			e_InitBuffer(CB);
			CB->next_line = 1;		// new buffer will implicitly have one empty line

			CB->pos.node = CB->list_head;
			CB->pos.pos = CB->pos.node->text;

			e_PrintPage();
			ConSetCursorPos(0, 0);
		}
	}
}



// ----------------------------------------------------------------------------
// Editor init and main loop
// ----------------------------------------------------------------------------

static void _createHelpPage() {
	char line[80];
	
	//               |----------------------------------------| 
	_addListLine(CB, "----------------------------------------");
	_addListLine(CB, "ED HELP PAGE v."ED_VERSION_STR);
	_addListLine(CB, "----------------------------------------");
	_addListLine(CB, "");
	_addListLine(CB, "Welcome to ED, the Picolo Text Editor!");
	_addListLine(CB, " (Hit [Ctrl-q] at any time to quit)");
	_addListLine(CB, "");
	_addListLine(CB, "This document provides a short overview");
	_addListLine(CB, "over what ED can (and can't) do.");
	_addListLine(CB, "The EDitor is meant to provide the basic");
	_addListLine(CB, "tools needed for you to be able to");
	_addListLine(CB, "edit M16BASIC source code files directly");
	_addListLine(CB, "on the M16 Virtual Computer.");
	_addListLine(CB, "");
	_addListLine(CB, "[It can be used to edit all sorts of text");
	_addListLine(CB, "[files, though it has some inherent");
	_addListLine(CB, "limitations, like being limited to");
	_addListLine(CB, "80 characters per line]");
	_addListLine(CB, "");

	_addListLine(CB, "");
	_addListLine(CB, "TEXT ENTRY");
	_addListLine(CB, "");
	_addListLine(CB, "Just happily type away! All characters you enter will be inserted at");
	_addListLine(CB, "the current cursor position. Please note that there is no automatic");
	_addListLine(CB, "line wrap.");
	_addListLine(CB, "");
	_addListLine(CB, "[There currently is no overwrite mode, ED only supports insert mode]");
	_addListLine(CB, "");

	_addListLine(CB, "");
	_addListLine(CB, "CURSOR MOVEMENT");
	_addListLine(CB, "");
	sprintf(line, "You can move the text cursor using the keyboard cursor [%c%c%c%c] keys.", 129, 130, 131, 132);
	_addListLine(CB, line);
	_addListLine(CB, "The [Home] and [End] keys quickly jump to thestart or end of the");
	_addListLine(CB, "current line while [PageUp] and [PageDown] can be used");
	_addListLine(CB, "to quickly scroll through a document, page by page.");
	_addListLine(CB, "");

	_addListLine(CB, "");
	_addListLine(CB, "DELETING TEXT");
	_addListLine(CB, "");
	_addListLine(CB, "Use the [Backspace] key to delete characters left of the cursor and");
	_addListLine(CB, "the [Del] key to delete characters under the cursor.");
	_addListLine(CB, "You can use [Ctrl-k] (or [Ctrl-x] to delete the entire current line.");
	_addListLine(CB, "");
	_addListLine(CB, "[Most of the more powerful functionalities of ED are invoked by holding the");
	_addListLine(CB, "control key and pressing an additional key while doing so. The above");
	_addListLine(CB, "line \"kill\" function also copies the line into the internal clipboard");
	_addListLine(CB, "by the way, from where it can be reinserted somewhere else later on]");
	_addListLine(CB, "");

	_addListLine(CB, "");
	_addListLine(CB, "COPY & PASTE");
	_addListLine(CB, "");
	_addListLine(CB, "[Ctrl-c] copies the current line to the clip board while [Ctrl-x] or");
	_addListLine(CB, "or [Ctrl-k] also delete the entire line. You can reinsert the copied ");
	_addListLine(CB, "line at the current cursor row using [Ctrl-v] or [Ctrl-y].");
	_addListLine(CB, "Multiple lines can be copied at once by using the \"MARK\":");
	_addListLine(CB, "press [Ctrl-m] to set the mark at the current line and subsequent");
	_addListLine(CB, "copy or cut commands will work on all lines from the mark up to ");
	_addListLine(CB, "and including the cursor.");
	_addListLine(CB, "");

	_addListLine(CB, "");
	_addListLine(CB, "LOAD, SAVE & QUIT");
	_addListLine(CB, "");
	_addListLine(CB, "You can load a file into the current buffer (more on buffers later)");
	_addListLine(CB, "by hitting [Ctrl-l] and save the buffer with [Ctrl-s].");
	_addListLine(CB, "Quit ED by simply hitting [Ctrl-q]. If the current buffer was modified");
	_addListLine(CB, "you will be prompted as to whether you want it to be saved first.");
	_addListLine(CB, "");
	_addListLine(CB, "[BASIC and buffer 0: buffer 0 is special in that it automatically");
	_addListLine(CB, "loads the current BASIC source code directly from the M16BASIC core");
	_addListLine(CB, "when ED is started without a filename parameter and a valid BASIC");
	_addListLine(CB, "source is available. If you modify this file and save it");
	_addListLine(CB, "directly using [Ctrl-s] or indirectly when quitting ED, the");
	_addListLine(CB, "M16 console will attempt to automatically reload it into");
	_addListLine(CB, "BASIC (which might fail if there are any syntax errors)]");
	_addListLine(CB, "");

	_addListLine(CB, "");
	_addListLine(CB, "BUFFERS");
	_addListLine(CB, "");
	_addListLine(CB, "ED provides 10 buffers for you to work with it. You can load and");
	_addListLine(CB, "save files into these buffers, copy and paste text lines between them");
	_addListLine(CB, "and their content will also persist between invocations of ED,");
	_addListLine(CB, "provided that the M16 virtual computer is not restarted.");
	_addListLine(CB, "To change the current buffer simply hit [Ctrl-b] followed by a digit");
	_addListLine(CB, "key (0-9) that selects the buffer you want to switch to.");
	_addListLine(CB, "");

	_addListLine(CB, "");
	_addListLine(CB, "THE STATUS LINE");
	_addListLine(CB, "");
	_addListLine(CB, "The bottom line of the ED screen forms the status line. It provides");
	_addListLine(CB, "the following information (from left to right):");
	_addListLine(CB, "- [N]: the buffer indicator.");
	_addListLine(CB, "- filename.ext: buffer filename and extention (if set).");
	_addListLine(CB, "- *: the buffer dirty indicator (if the buffer was modified).");
	_addListLine(CB, "- l:x/y: the line indicator: the cursor is on line x of y lines.");
	_addListLine(CB, "- c:z: the column indicator: this is the column the cursor is on.");
	_addListLine(CB, "To the right of this information, which is always present, ED displays");
	_addListLine(CB, "things like filename prompts and various messages.");
	_addListLine(CB, "");
	_addListLine(CB, "[Feel free to use this text as a sandbox for trying out ED. No worries!");
	_addListLine(CB, "You can't damage the help file. Its not actually a file but gets");
	_addListLine(CB, "generated from code everytime ED is started]");


}

static int _Init() {

	// NOTE that buffers persist between calls to ED
	// allocate the default buffer (0) if it does not exist
	CB = BUFFERS[0];
	CBI = 0;
	if (!CB) {
		// This is the very first initialization after M16 startup
		// first we autogenerate HELP page in buffer 9
		BUFFERS[9] = malloc(sizeof(struct TextBuffer));
		assert(BUFFERS[9]);
		e_InitBuffer(BUFFERS[9]);
		CB = BUFFERS[9];	// make it current
		_createHelpPage();

		// now prepare buffer 0
		CB = malloc(sizeof(struct TextBuffer));
		assert(CB);
		BUFFERS[0] = CB;

		e_InitBuffer(CB);
		for (int i = 1; i < 9; i++)
			BUFFERS[i] = 0;

	}

    ClearTextDisplay();
	e_ClearStatus();
	e_SetExtraStatusTextTemp("[CTRL-b]-9: help");

}

// Just a simple filename extention test
// returns 1 if extention is .bas
// or 0 if not (or if filename is NULL or its len is 0)
unsigned char _IsBasic(char* filename) {
	if (!filename) return 0;
	if (*filename == 0) return 0;
	char* cp = filename + strlen(filename);
	while (cp > filename&&* cp != '.')
		cp--;
	if (*cp == '.') {
		char ext[4];
		strcpy(ext, cp + 1);
		strupr(ext);
		if (!strcmp(ext, "BAS"))
			return 1;
	}
	return 0;
}

// returns M16_CON_CTRL_Q if the editor was quit without a save
// and M16_CON_CTRL_S if the source file was modified and subsequently saved
int e_Edit(char* filename) {

	int c;

	// Coming out of Init() above CB will always be buffer 0
	// the only buffer that does not persist but always gets reloaded (if there is a 
	// BASIC source available in the M16 core of a filename is given)
	// or reset.

	_Init();

	if (!filename) {
		e_InitBuffer(CB);
#if 0
		if (_getBasicSource() > 0) {
			// try to pull already loaded source from BASIC
			// prepare the text buffer: this relies on m16_basic_source_filename
			// always being properly setup as long as there is a BASIC source available
			strcpy(CB->source_filename, m16_basic_source_filename);
			// FIXME: add error handling
			//return M16_CON_CTRL_Q;
		}
		else {
			// no source available in the basic core: start a new file in buffer 0
			// by simply moving the next_line index down by one line
			CB->next_line = 1;
		}
#endif
			CB->next_line = 1;
	}
	else {
		// load a file directly (this always goes into buffer 0)
		// we need to reset buffer 0 for this because the default behaviour of _Init()
		// is to leave used buffers untouched on subsequent invocations of ED (persistence)
		e_InitBuffer(CB);
		if (_doLoad(filename) < 0) {
			// FIXME: add error handling: at least some type of message?
			return M16_CON_CTRL_Q;
		}
		strcpy(CB->source_filename, filename);
		
		// Not triggering a load into basic anymore (only if ED is called without a filename and
		// there is a basic source)
		//if (_IsBasic(filename)) {
		//	m16_SetBasicSourceFilename(filename);
		//	ED_FILE_LOADED = 1;		// signal: "this is basic: please reload into core on exit" to the console
		//}
	}

	e_PrintPage();
	e_PrintStatusLine();
	ConSetCursorPos(0, 0);
	ConEchoOff();

	do {
		c = getc(stdin);
		switch (c) {
		case M16_CON_CTRL_L: {
			// load a file into this buffer
			if (CB->b_dirty) {
				// current file is dirty
				char k = _PromptYNA("Save current file first?");
				if (k == 'y') {
					// FIXME: error handling
					_doSave();
				}
			}
			char fname[ED_MAX_FILENAME + 1];
			if (_PromptFilename(fname, "Load file:")) {
				// try to load the file into the current buffer
				if (_bufferLoad(fname) > 0) {
					//if (CBI == 0 && _IsBasic(CB->source_filename)) {
					//	m16_SetBasicSourceFilename(CB->source_filename);
					//	ED_FILE_LOADED = 1;				// this signals "reload basic source to the console code
					//}
					e_SetExtraStatusTextTemp("Buffer loaded.");
					e_PrintPage();
					ConSetCursorPos(0, 0);
				}
			}
		}
			break;
		case M16_CON_CTRL_B:
			_DoBufferSwitch();
			break;
		case M16_CON_CTRL_S:
			if (CB->source_filename[0] == 0)
				_PromptFilename(CB->source_filename, "Save file:");
			if (CB->source_filename[0] != 0)
				if (_doSave() > 0) {
					//if (CBI == 0 && _IsBasic(CB->source_filename)) {
					//	m16_SetBasicSourceFilename(CB->source_filename);
					//	ED_FILE_SAVED = 1;				// this signals "reload basic source to the console code
					//}
					CB->b_dirty = 0;
					e_SetExtraStatusTextTemp("Buffer saved.");
				}
			break;
		case M16_CON_CTRL_Q:
			// M16_CON_CTRL_Q: ctrl-q means quit 
			// prompt user to save if dirty
			c = _handleQuit();
			if (c == M16_CON_CTRL_S) {
				c = M16_CON_CTRL_Q;
				if(_doSave() > 0) {
					//if (CBI == 0 && _IsBasic(CB->source_filename)) {
					//	m16_SetBasicSourceFilename(CB->source_filename);
					//	ED_FILE_SAVED = 1;				// this signals "reload basic source to the console code
					//}
				}
			}
			break;
		case M16_CON_HOME:
			ConSetCursorPos(ConGetCursorRow(), 0);
			CB->C.col = 0;
			break;
		case M16_CON_END:
			CB->C.col = CB->line_len[CB->C.row];
			if (CB->C.col >= ED_LINE_MAX_CHARS)		// make sure we don't step the cursor past column 80
				CB->C.col = ED_LINE_MAX_CHARS - 1;
			ConSetCursorPos(ConGetCursorRow(), CB->C.col);
			break;
		case M16_CON_CTRL_X:
		case M16_CON_CTRL_K:
			e_DoLineKill(1);
			break;
		case M16_CON_CTRL_C:
			e_DoLineKill(0);
			break;
		case M16_CON_CTRL_M: {
			CB->line_mark = CB->C.row + 1;	// the mark notes the actual line number, not the index!
			char msg[40];
			sprintf(msg, "Mark set on line %d.", CB->C.row+1);
			e_SetExtraStatusTextTemp(msg);
		}
		break;
		case M16_CON_CTRL_V:
		case M16_CON_CTRL_Y:
			e_DoLineYank();
			break;
		case M16_CON_PGUP:
			e_DoPageUp();
			break;
		case M16_CON_PGDOWN:
			e_DoPageDown();
			break;
		case M16_CON_CURSOR_UP:
			e_DoCursorUp();
			break;
		case M16_CON_CURSOR_DOWN:
			e_DoCursorDown();
			break;
		case M16_CON_CURSOR_LEFT:
			e_DoCursorLeft();
			break;
		case M16_CON_CURSOR_RIGHT:
			e_DoCursorRight();
			break;
		case 8:		// backspace
			e_HandleBackspace();
			break;
		case 127:	// DEL
			e_HandleDel();
			break;
		case 10:	// newline
			e_handleNewLine();
			break;
		default:
			if (c >= 32 && c <= 126) {
				e_InsertCharacter(c);
			}
		}

		e_PrintStatusLine();

	} while (c != EOF && c != M16_CON_CTRL_Q);

	ConSetCursorPos(W.status_row, 0);
	putc(10, stdin);
	ConEchoOn();

	return c;
}


// ed.c
