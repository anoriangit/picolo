// ED
// m16 text editor
//
// ed-status.c  functions dealing with ED's status line
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

// this is how much space we have for the filename on the status line
#define STATUS_FNAME_LEN 20

// display the status line (and the separator one line above it)
#define EXTRA_STATUS_TEXT_LEN 20
static char status_text[LINE_MAX_CHARS + 1];
static char extra_status_text[EXTRA_STATUS_TEXT_LEN + 1];

static int TIMER_E = 0;

void e_ClearStatus() {
	TIMER_E = 0;
	status_text[0] = 0;
	extra_status_text[0] = 0;
}

// set extra status text and start the count down timer for its automatic removal
void e_SetExtraStatusTextTemp(char* text) {
	strncpy(extra_status_text, text, EXTRA_STATUS_TEXT_LEN);
	extra_status_text[EXTRA_STATUS_TEXT_LEN] = 0;	// just in case
	TIMER_E = 10;
}

void e_SetExtraStatusText(char* text) {
	if (text == NULL) {
		extra_status_text[0] = 0;
	}
	else {
		strncpy(extra_status_text, text, EXTRA_STATUS_TEXT_LEN);
		extra_status_text[EXTRA_STATUS_TEXT_LEN] = 0;
	}
}


static char* _MassageFilename(char* name) {
	static char fname[STATUS_FNAME_LEN + 1];
	int len = (int)strlen(name);
	char* start = name;
	char* cp = name + len;
	while (cp > name&&* cp != '.')			// walk back to try and find an extention
		cp--;
	if (*cp == '.') {						// cp is now either on a dot or at the start of name
		len = cp - start - 1;				// len pre extention excluding the dot
		if (len > STATUS_FNAME_LEN - 4) {	// too long: "massage"
			strncpy(fname, name, STATUS_FNAME_LEN - 5);
			fname[STATUS_FNAME_LEN - 5] = 0;
			strcat(fname, "~");
			strcat(fname, cp);
		}
		else {								// fits: just copy over
			strcpy(fname, name);
		}
	}
	else if (cp > name) {
		if (len > STATUS_FNAME_LEN) {
			strncpy(fname, name, STATUS_FNAME_LEN - 1);
			fname[STATUS_FNAME_LEN - 1] = 0;
			strcat(fname, "~");
		}
	}
	else
		fname[0] = 0;

	return fname;
}

void e_PrintStatusLine() {
	int dcr, dcc;
	ConGetCursorPos(&dcr, &dcc);
	ConSetCursorPos(W.status_row, 0);
	
    for (int i = 0; i < W.n_cols; i++) {
        // M16 was printing a line using graphics chars from tha Atari ST font here
    	//putc('-', stdout);			// 19 is a little off center (down), 18 would be centered
        SetAttribute(D_ATTR_INVERSE, W.status_row, i);
    }

    ConClearCursorRow();

	if (TIMER_E) {
		TIMER_E--;
		if (!TIMER_E)
			extra_status_text[0] = 0;
	}

	char* fname = _MassageFilename(CB->source_filename);
	snprintf(status_text, LINE_MAX_CHARS, "[%d]%s%c l:%d/%d c:%d  %s", CBI, fname, (CB->b_dirty ? '*' : ' '), CB->C.row + 1, CB->next_line, CB->C.col + 1, extra_status_text);
	puts(status_text);
	ConSetCursorPos(dcr, dcc);
}


int e_GetStatusTextLen() {
	return strlen(status_text);
}

// ed-status.c
