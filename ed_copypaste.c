// ED
// m16 text editor
//
// ed-copypaste.c  copy&paste (kill&yank) support
// gs july-2020
// updated 2020-08-29 gs
// ----------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>

#define CONIO_STDIO_OVERRIDES
#include "display.h"
#include "conio.h"
#include "ed.h"


static void _copyFromKillbuf(int line) {
	int index = KILLBUF.next_line - 1;
	memcpy(&CB->out_lines[line], &KILLBUF.out_lines[index], KILLBUF.line_len[index]);
	CB->line_len[line] = KILLBUF.line_len[index];
	CB->out_lines[line][CB->line_len[line]] = 0;
	KILLBUF.next_line--;
}

static void _copyToKillbuf(int line) {
	int index = KILLBUF.next_line;
	memcpy(&KILLBUF.out_lines[index], &CB->out_lines[line], CB->line_len[line]);
	KILLBUF.line_len[index] = CB->line_len[line];
	KILLBUF.next_line++;
}


// Ctrl-V handler
// Insert line(s) from killbuffer before the line that the cursor is on right now
void e_DoLineYank() {
	if (KILLBUF.next_line == 0)
		return;		// nothing there

	int dcr, dcc;
	ConGetCursorPos(&dcr, &dcc);									// current display cursor row&column

	// insert the lines from KILLBUF at cursor
	int count = KILLBUF.next_line;
	for (int line = 0; line < count; line++) {
		e_InsertLineBefore(CB->C.row);
		_copyFromKillbuf(CB->C.row);
	}
	KILLBUF.next_line = count;		// restore next_line so we can perform multiple yanks

	// now clean up the display
	int first_line_visible = CB->C.row - dcr;
	if (first_line_visible < 0) {
		dcr = CB->C.row;			// not enough lines of text available above the cursor: we need to move it
	}
	int tmp = CB->C.row;
	CB->C.row = CB->C.row - dcr;	// temporarily move up so that the text cursor is at the top of the screen
	e_PrintPage();					// moves screen cursor to 0,0 and when its done it will be at the end of the display
	CB->C.row = tmp;				// restore text cursor
	ConSetCursorPos(dcr, dcc <= CB->line_len[CB->C.row] ? dcc : CB->line_len[CB->C.row]); // and display cursor too

	// clean up
	CB->line_mark = 0;		// reset mark
	CB->b_dirty = 1;		// set global file dirty flag

}


// Ctrl-X (cut) handler
// Kill (delete) the current line(s) and put its contents into the kill-buffer
// The text cursor remains on the line it is on (because removing line x automatically
// makes line x the previous line)
// Handling the very last line in the text buffer and the top most screen line
// is a bit special
void e_DoLineKill(unsigned char b_kill) {

	int dcr, dcc;
	ConGetCursorPos(&dcr, &dcc);									// current display cursor row&column

	// reset the killbuffer so that subsequent kills don't add up
	e_InitBuffer(&KILLBUF);

	// determine the range of lines to kill (if there is a MARK set it might be more than one)
	// line mark will be 0 if not set or else matching the 1-based line number (NOT the index!)
	int end = (CB->line_mark > 0) ? CB->line_mark - 1 : CB->C.row;
	int start = CB->C.row;
	if (start > end) {
		int tmp = start;
		start = end;
		end = tmp;
	}
	for (int i = start; i <= end; i++) {
		if (b_kill) {
			_copyToKillbuf(start);	// copy to killbuf
			e_RemoveLine(start);	// NOTE that this scrolls all lines below UP (hence we always use start) 
		}
		else {
			_copyToKillbuf(i);	// copy to killbuf
		}
	}
	// now "fix" the display
	// ideally we want to keep the display cursor where it is and adapt the position of the text 
	// such that the new current C.row is where the display cursor is
	CB->C.row = start;		// move text cursor to the (new) start
	if (CB->C.row == CB->next_line) CB->C.row--;		// this will happen if the killed area included the very last line
	int first_line_visible = CB->C.row - dcr;
	if (first_line_visible < 0) {
		// not enough lines of text available above the cursor: we need to move it
		dcr = CB->C.row;
	}
	int tmp = CB->C.row;
	CB->C.row = CB->C.row - dcr;	// temporarily move up so that the text cursor is at the top of the screen
	e_PrintPage();					// moves screen cursor to 0,0 and when its done it will be at the end of the display
	CB->C.row = tmp;				// restore text cursor
	ConSetCursorPos(dcr, dcc <= CB->line_len[CB->C.row] ? dcc : CB->line_len[CB->C.row]); // and display cursor too

	// clean up: reset mark and set dirty flag if this actually was a kill and not just a copy
	if (b_kill) {
		CB->line_mark = 0;		// reset mark
		CB->b_dirty = 1;		// set global file dirty flag
	}
}

// ed-copypaste.c
