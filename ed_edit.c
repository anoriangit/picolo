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


// insert an empty new line before cursor row r
// this does an implicit down shift (scroll) of all lines starting from line r
void e_InsertLineBefore(int r) {
	if (CB->next_line < ED_MAX_LIST_LINES) {
		// still space left: shift everything down
		for (int source_line = CB->next_line - 1; source_line >= r; source_line--) {
			memcpy(&CB->out_lines[source_line + 1], &CB->out_lines[source_line], ED_LINE_MAX_CHARS);
			CB->line_len[source_line + 1] = CB->line_len[source_line];
			//memcpy(&list_lines[source_line], &list_lines[source_line + 1], ED_LINE_MAX_CHARS);
			// also NUMS in case we should determine we actually need the list lines and the num_lines arrays
		}
		// init the new line by clearing it out
		memset(&CB->out_lines[r], 0, ED_LINE_MAX_CHARS);
		CB->line_len[r] = 0;
		CB->next_line++;
	}
}

// remove line r by scrolling up everything below it
// NOTE that this does not remove the last existing line 
void e_RemoveLine(int r) {
	for (int source_line = r + 1; source_line < CB->next_line; source_line++) {
		memcpy(&CB->out_lines[source_line - 1], &CB->out_lines[source_line], ED_LINE_MAX_CHARS);
		CB->line_len[source_line - 1] = CB->line_len[source_line];
	}
	// clear the now obsolete previously last line
	if (CB->next_line > 1) {
		// don't delete the very last line: there always needs to be at least one line
		// because otherwise the screen cursor would sit in a void (on a line that 
		// does not exist.
		CB->next_line--;		
		memset(&CB->out_lines[CB->next_line], 0, ED_LINE_MAX_CHARS);
		CB->line_len[CB->next_line] = 0;
	}
	else if (CB->next_line == 1) {
		// clear line 0 but don't remove it (see above)
		memset(&CB->out_lines[0], 0, ED_LINE_MAX_CHARS);
		CB->line_len[0] = 0;
	}
}

// Insert a character into at the current text cursor position
// shifts the contents "under and to the right" of the cursor to the right
void e_InsertCharacter(int c) {
	char temp[ED_LINE_MAX_CHARS];
	size_t linesiz = CB->line_len[CB->C.row];
	if (linesiz < ED_LINE_MAX_CHARS) {
		// there is still some space left 
		size_t shift_size = linesiz - CB->C.col;					// count of characters to shift 
		if (shift_size) {
			memcpy(temp, &CB->out_lines[CB->C.row][CB->C.col], shift_size);		// need to make a temp copy first
			memcpy(&CB->out_lines[CB->C.row][CB->C.col + 1], temp, shift_size);	// because overlapping memcpy is bad
		}
		CB->out_lines[CB->C.row][CB->C.col] = c;

		// cursor might jump to the next line due to console auto cursor advance
		// we don't want that in ED
		int row = ConGetCursorRow();
		putc(c, stdout);
		if (ConGetCursorRow() != row)
			ConSetCursorPos(row, W.n_cols - 1);

		// write shifted part to the display: use display driver directly so we don't mess with the cursor
		if (shift_size) {
			for (int i = CB->C.col; i <= CB->C.col + shift_size; i++) {
				//SetCharacterPos(ConGetCursorRow(), i);
				PutCharacter(CB->out_lines[CB->C.row][i], ConGetCursorRow(), i);
			}
		}
		if(CB->C.col < ED_LINE_MAX_CHARS - 1)	// Make sure we don't move the cursor past column 80
			CB->C.col++;

		CB->line_len[CB->C.row]++;
		CB->b_dirty = 1;			// global file dirty flag
	}
}

// User has pressed the backspace key
// - delete character left of the cursor (if any) and move write pointer "left"
// - also shift everything to the right of the cursor too
unsigned char e_HandleBackspace() {
	size_t len = CB->line_len[CB->C.row];
	if (CB->C.col > 0) {
		if (len > CB->C.col) {
			// we're somewhere in the middle of the line
			CB->C.col -= 1;
			memcpy(&CB->out_lines[CB->C.row][CB->C.col], &CB->out_lines[CB->C.row][CB->C.col + 1], len - CB->C.col - 1);
			CB->out_lines[CB->C.row][len - 1] = 0;
			// write shifted part to the display: use display driver directly so we don't mess with the cursor
			int i;
			for (i = CB->C.col; i < len - 1; i++) {
				//SetCharacterPos(ConGetCursorRow(), i);
				PutCharacter(CB->out_lines[CB->C.row][i], ConGetCursorRow(), i);
			}
			//SetCharacterPos(ConGetCursorRow(), i);
			PutCharacter(' ', ConGetCursorRow(), i);
		}
		else {
			// behind end of line
			CB->out_lines[CB->C.row][--CB->C.col] = 0;
			//SetCharacterPos(ConGetCursorRow(), CB->C.col);
			PutCharacter(' ', ConGetCursorRow(), CB->C.col);
		}
		ConCursorLeft();
		CB->line_len[CB->C.row]--;
		//list_dirty[CB->C.row] = 1;	// line dirty flag
		CB->b_dirty = 1;			// global file dirty flag
		return 1;
	} else if (CB->C.col == 0) {
		// at the start of line and it is not line 1
		// delete this line and attach contents to the end of the previous line (make sure there is space, else abort)
		// scroll everything below prev_line up
		// Note that we don't allow line splits: either it all fits or we won't perform the process
		if (CB->C.row > 0) {
			size_t prev_len = CB->line_len[CB->C.row - 1];
			// Note that we don't allow line splits: either it all fits or we won't perform the process
			if (prev_len + len <= ED_LINE_MAX_CHARS) {
				memcpy(&CB->out_lines[CB->C.row - 1][prev_len], &CB->out_lines[CB->C.row], len);
				CB->line_len[CB->C.row - 1] += (int) len;
				e_RemoveLine(CB->C.row--);
				CB->C.col = (int)prev_len;		// adapt position of the text cursor

				// now deal with the display side of things
				int dcr, dcc;
				ConGetCursorPos(&dcr, &dcc);				// current display cursor row&column
				ConCursorUp();
				ConClearCursorRow();
				puts((const char*)&CB->out_lines[CB->C.row]);
				CharacterDisplayScrollUpRange(dcr + 1, W.status_row - 2);	// scroll up the stuff below
				int last_line_on_screen = CB->C.row + (W.n_rows - dcr);
				if (last_line_on_screen < CB->next_line) {
					ConSetCursorPos(W.n_rows - 1, 0);						// print the new last line
					ConClearCursorRow();
					puts((const char*)&CB->out_lines[last_line_on_screen]);
				}
				else {
					ConSetCursorPos(W.n_rows - 1, 0);						// clear the new last line
					ConClearCursorRow();
				}
				ConSetCursorPos(dcr - 1, (int)prev_len);							// put cursor back where it belongs
				CB->b_dirty = 1;			// global file dirty flag
				return 1;
			}
			else {
				// produce an error message here: line too long
			}
		} // end if(CB->C.row > 0)
	}
	return 0;
}

// User has pressed the DEL key
// - delete character under the cursor (if any)
// - shift everything to the right of the cursor left
// - cursor at end of line (with more lines following) needs special treatment
unsigned char e_HandleDel() {
	int len = CB->line_len[CB->C.row];
	if (len > CB->C.col) {
		// we're somewhere in the the line but not at the end: 
		// shift left every thing to our right, overwriting whats under the cursor
		memcpy(&CB->out_lines[CB->C.row][CB->C.col], &CB->out_lines[CB->C.row][CB->C.col + 1], len - CB->C.col - 1);
		CB->out_lines[CB->C.row][len - 1] = 0;	// re-terminate
		// write shifted part to the display: use display driver directly so we don't mess with the cursor
		int i;
		for (i = CB->C.col; i < len - 1; i++) {
			//SetCharacterPos(ConGetCursorRow(), i);
			PutCharacter(CB->out_lines[CB->C.row][i], ConGetCursorRow(), i);
		}
		//SetCharacterPos(ConGetCursorRow(), i);
		PutCharacter(' ', ConGetCursorRow(), i);

		CB->line_len[CB->C.row]--;
		//list_dirty[CB->C.row] = 1;	// line dirty flag
		CB->b_dirty = 1;			// global file dirty flag
		return 1;
	}
	else if (len == CB->C.col) {
		// At the end of the line and there are more lines following:
		// pull up the next line andd attach contents to the end of this line (make sure there is space, else abort)
		// scroll everything below prev_line up
		// Note that we don't allow line splits: either it all fits or we won't perform the process!
		if (CB->C.row < CB->next_line - 1) {
			int next_len = CB->line_len[CB->C.row + 1];
			if (next_len + len <= ED_LINE_MAX_CHARS) {
				memcpy(&CB->out_lines[CB->C.row][len], &CB->out_lines[CB->C.row+1], next_len);
				CB->line_len[CB->C.row] += next_len;
				e_RemoveLine(CB->C.row + 1);

				// now deal with the display side of things
				int dcr, dcc;
				ConGetCursorPos(&dcr, &dcc);				// current display cursor row&column
				//ConCursorUp();
				//ConClearCursorRow();
				ConSetCursorPos(dcr, 0);
				puts((const char*)&CB->out_lines[CB->C.row]);
				CharacterDisplayScrollUpRange(dcr + 2, W.status_row - 2);	// scroll up the stuff below
				int last_line_on_screen = CB->C.row + (W.n_rows - dcr) - 1;
				if (last_line_on_screen < CB->next_line) {
					ConSetCursorPos(W.n_rows - 1, 0);						// print the new last line
					ConClearCursorRow();
					puts((const char*)&CB->out_lines[last_line_on_screen]);
				}
				else {
					ConSetCursorPos(W.n_rows - 1, 0);						// clear the new last line
					ConClearCursorRow();
				}
				ConSetCursorPos(dcr, len);									// put cursor back where it belongs
				CB->b_dirty = 1;			// global file dirty flag
				return 1;
			}
			else {
				// produce an error message here: line too long 
			} 
		} // end if(CB->C.row > 0)  
	} 
	return 0;
}

// Three cases:
// 1 NL at line start: insert new empty line before current
// 2 NL at line end: insert new empty line behind current
// 3 NL in the middle of the line: split current line at cursor,
//		insert a new line after current and copy split off part into it
void e_handleNewLine() {

	// this will need space for a new line: if there is none we need to abort
	// should probably generate some kind of error in the status line here
	if (!(CB->next_line < ED_MAX_LIST_LINES))
		return;

	int dcr, dcc;
	ConGetCursorPos(&dcr, &dcc);				// current display cursor row&column

	if (dcc == 0) {
		// Case 1:
		if (dcr < W.n_rows - 1) {
			CharacterDisplayScrollDownRange(dcr + 1, W.status_row - 2);	// scroll down
			ConClearCursorRow();
			ConCursorDown();			// and finally step the cursor down on screen and in the text
		}
		else {
			// on bottom row
			CharacterDisplayScrollUpRow(dcr);		// scroll display up if we are on the bottom row
			ConCursorUp();							// this is truely ugly (but it works :P) 
			ConClearCursorRow();
			ConCursorDown();
		}
		e_InsertLineBefore(CB->C.row);	// create a new empty row inside the text data
		CB->C.row++;
	} else if (dcc == CB->line_len[CB->C.row]) {
		// Case 2:
		if (dcr < W.n_rows - 1) {
			ConSetCursorPos(dcr + 1, 0);
			CharacterDisplayScrollDownRange(dcr + 1, W.status_row - 2);	// push (scroll) down
			ConClearCursorRow();		// clear the cursor row on screen
		}
		else {
			// on bottom row
			CharacterDisplayScrollUpRow(dcr+1);		// scroll display up if we are on the bottom row
			ConSetCursorPos(dcr, 0);
			ConClearCursorRow();		// clear the cursor row on screen
		}
		CB->C.row++; CB->C.col = 0;
		e_InsertLineBefore(CB->C.row);	// create a new empty row inside the text data
	} else {
		// Case 3: split the line
		// NOTE that we only in this case set a line dirty flag (for the new line created from the split)
		// because there is no need to submit empty lines to BASIC
		// FIXME: this probably needs to be changed if and when we should decide to support other languages too)
		if (dcr < W.n_rows - 1) {
			ConSetCursorPos(dcr + 1, 0);
			CharacterDisplayScrollDownRange(dcr + 1, W.status_row - 2);	// push (scroll) down
		}
		else {
			// on bottom row
			CharacterDisplayScrollUpRow(dcr + 1);		// scroll display up if we are on the bottom row
			ConSetCursorPos(dcr, 0);
		}
		CB->C.row++;
		//list_dirty[CB->C.row] = 1;		// see NOTE on line dirty flags above please!

		e_InsertLineBefore(CB->C.row);	// create a new empty row inside the text data
		memcpy(&CB->out_lines[CB->C.row], &CB->out_lines[CB->C.row - 1][CB->C.col], CB->line_len[CB->C.row-1] - CB->C.col);
		CB->line_len[CB->C.row] = CB->line_len[CB->C.row - 1] - CB->C.col;	// line length = length of split off part


		CB->line_len[CB->C.row - 1] -= CB->line_len[CB->C.row];			// correct old le´n
		memset(&CB->out_lines[CB->C.row - 1][CB->line_len[CB->C.row-1]], 0, ED_LINE_MAX_CHARS - CB->line_len[CB->C.row - 1]);	// null out reminder
		ConCursorUp();									// need to correct display of remainder line
		ConClearCursorRow();							// clear
		puts((const char*)&CB->out_lines[CB->C.row - 1]);					// and re-output
		ConCursorDown();

		CB->C.col = 0;
		ConSetCursorPos(ConGetCursorRow(), 0);
		ConClearCursorRow();			// clear the new current cursor row on screen
		puts((const char*)&CB->out_lines[CB->C.row]);		// output the second part of the split line
		ConSetCursorPos(ConGetCursorRow(), 0);
	}

	CB->b_dirty = 1;	// global file dirty flag
}



// ed_edit.c
