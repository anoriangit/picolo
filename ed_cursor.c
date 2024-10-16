
// ED
// m16 text editor
//
// ed-cursor.c  cursor management (display and text cursor)
// gs july-2020
// updated 2020-08-28 gs
// ----------------------------------------------------------------------------

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define CONIO_STDIO_OVERRIDES
#include "display.h"
#include "conio.h"
#include "ed.h"


// TEXT CURSOR movment
// NOTE: all of this assumes that the text cursor C's column will always be inside the display limits
// i.e this implies a fixed maximum line length that is smaller or equal to the display width
// This is a convenience hack on the part of the coder (because it makes stuff so much easier :D)
// but will probably need to be ammended at some point
void e_DoCursorDown() {

	//if (CB->C.row < CB->next_line - 1) {
	if (CB->pos.node->next != NULL) {
		CB->pos.node = CB->pos.node->next;
		
		CB->C.row++;
		int dcr, dcc;
		ConGetCursorPos(&dcr, &dcc);	// current display cursor row&column
		if (dcr == W.n_rows - 1) {
			// we need to scroll
			CharacterDisplayScrollUpRow(dcr + 1);
			ConClearCursorRow();
			ConSetCursorPos(dcr, 0);			// make sure we are on column 0
			printf("%s", CB->out_lines[CB->C.row]);
			ConSetCursorPos(dcr, dcc);			// move back to actual user column
		}
		else {
			// just a plain cursor move down
			ConCursorDown();
			dcr++;
		}
		// finally: correct cursor column if we are to the right of the line end now
		if (CB->C.col > CB->line_len[CB->C.row]) {
			CB->C.col = CB->line_len[CB->C.row];
			ConSetCursorPos(dcr, CB->C.col);	// cant't use CB->C.row as that might be outside the actual display area
		}
	}
}

// returns new text cursor row
void e_DoCursorUp() {
	if (CB->C.row > 0) {
		CB->C.row--;
		int dcr, dcc;
		ConGetCursorPos(&dcr, &dcc);	// current display cursor row&column
		if (dcr == 0) {
			// we need to scroll
			CharacterDisplayScrollDownRow(W.n_rows - 1);
			ConClearCursorRow();
			ConSetCursorPos(dcr, 0);			// make sure we are on column 0
			printf("%s", CB->out_lines[CB->C.row]);
			ConSetCursorPos(dcr, dcc);
		}
		else {
			// just a plain cursor move up
			ConCursorUp();
			dcr--;
		}
		// finally: correct cursor column if we are to the right of the line end now
		if (CB->C.col > CB->line_len[CB->C.row]) {
			CB->C.col = CB->line_len[CB->C.row];
			ConSetCursorPos(dcr, CB->C.col);	// cant't use CB->C.row as that might be outside the actual display area
		}
	}
}

void e_DoCursorRight() {
	if (CB->C.col <= CB->line_len[CB->C.row] && !(CB->C.row == CB->next_line - 1 && CB->C.col == CB->line_len[CB->C.row])) {
		CB->C.col++;
		ConCursorRight();
		// finally: correct cursor row if we are to the right of the line end now
		// skip to start of next line
		if (CB->C.col > CB->line_len[CB->C.row]) {
			e_DoCursorDown();
			int dcr, dcc;
			ConGetCursorPos(&dcr, &dcc);	// current display cursor row&column
			CB->C.col = 0;
			ConSetCursorPos(dcr, 0);
		}
	}
}

void e_DoCursorLeft() {
	if (CB->C.col >= 0 && !(CB->C.row == 0 && CB->C.col == 0)) {
		CB->C.col--;
		// correct cursor row if we stepped out of the line "left"
		// move to end of next line and correct CB->C.col & CB->C.row
		if (CB->C.col < 0) {
			e_DoCursorUp();
			CB->C.col = CB->line_len[CB->C.row];
			int dcr, dcc;
			ConGetCursorPos(&dcr, &dcc);				// current display cursor row&column
			ConSetCursorPos(dcr, CB->C.col);
		}
		else {
			ConCursorLeft();
		}
	}
}

void e_DoPageDown() {
	int dcr, dcc;
	ConGetCursorPos(&dcr, &dcc);

	// find the line that is the last one visible on screen
	int last_line_on_screen = (CB->C.row + (W.n_rows - dcr)) - 1;
	// do we have more lines following that? One full screen (or more) or less?
	int lines_available = CB->next_line - last_line_on_screen - 1;
	if (lines_available >= W.n_rows) {
		// display the next full page: set the text cursor to the top of that page temporarily
		CB->C.row = last_line_on_screen + 1;
		e_PrintPage();
		// now setup the cursor correctly: the screen cursor is supposed to stay where it is!
		// so we need to find out where the text cursor needs to be now
		CB->C.row += dcr;
	}
	else if(lines_available > 0) {
		// down as far as we can go (i.e lines are available)
		int first_line_on_screen = CB->C.row - dcr;
		int cur_text_row = CB->C.row;
		CB->C.row = first_line_on_screen + lines_available;
		e_PrintPage();

		// correct cursors
		CB->C.row = cur_text_row + lines_available;

	}

	if (dcc > CB->line_len[CB->C.row])
		CB->C.col = CB->line_len[CB->C.row];	// make sure cursor stays inside the line
	ConSetCursorPos(dcr, CB->C.col);
}

void e_DoPageUp() {
	int dcr, dcc;
	ConGetCursorPos(&dcr, &dcc);

	// find the line that is the first one visible on screen
	int first_line_on_screen = CB->C.row - dcr;
	// do we have more lines above that? One full screen (or more) or less?
	int lines_available = first_line_on_screen;
	if (lines_available >= W.n_rows) {
		// display the previous full page: set the text cursor to the top of that page temporarily
		CB->C.row = first_line_on_screen - W.n_rows;
		e_PrintPage();
		// now setup the cursor correctly: the screen cursor is supposed to stay where it is!
		// so we need to find out where the text cursor needs to be now
		CB->C.row += dcr;
	}
	else if (lines_available > 0) {
		// up as far as we can go (i.e lines are available)
		int cur_text_row = CB->C.row;
		CB->C.row = first_line_on_screen - lines_available;
		e_PrintPage();
		// correct cursors
		CB->C.row = cur_text_row - lines_available;
	}

	if (dcc > CB->line_len[CB->C.row])
		CB->C.col = CB->line_len[CB->C.row];	// make sure cursor stays inside the line
	ConSetCursorPos(dcr, CB->C.col);
}

// ed-cursor.c
