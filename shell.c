
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "display.h"
#include "conio.h"
#include "sdcard.h"
#include "ed.h"

#define CMD_LINE_MAX_CHARS 128
#define CMD_MAX_CHARS  16

/* Number of elements in an array. */
#define NELEMS(v) (sizeof(v) / sizeof(v[0]))

static char editor_line[CMD_LINE_MAX_CHARS + 1];	// editor_line: the line editor is working on this
static char *line = editor_line;
static int line_write_index=0;					// where the next character will be written to editor_line

static unsigned char line_edit_mode = 0;


// shell commands
struct cmd_arg {
	const char *str;
	size_t len;
};

typedef void (*cmd_f)(struct cmd_arg *, int);

struct command {
	const char *name;
	cmd_f fun;
	unsigned char nargs;
	unsigned char nextra_args;
};

static struct cmd_arg args[2];
static char name[CMD_MAX_CHARS + 1];


static const char *s_help[] = {
"ED [file]   - Edit [file]",
"RUN [file]  - Compile and run [file]",
"COMPILE (C) - Compile program",
"DIR [path]  - List current dir or [path]",
"CD path     - Change current directory",
};

/******************************************************************************
 * COMMANDS
 */
static void _help_cmd(struct cmd_arg *args, int nargs) {
	int i;
	for (i = 0; i < NELEMS(s_help); i++) {
		Con_printf("%s\n", s_help[i]);
	}
}

static void _dir_cmd(struct cmd_arg *args, int nargs) {
   	struct cmd_arg *arg0 = NULL;
    FRESULT fr = FR_OK;

    if(nargs == 1) {
   	    arg0 = &args[0];
        fr = SD_ListDir (arg0->str);
    } else {
        fr = SD_ListDir ("");
    }
    if(fr != FR_OK)
        SD_PrintError(fr);
}

static void _cd_cmd(struct cmd_arg *args, int nargs) {
   	struct cmd_arg *arg0 = &args[0];

	printf("changing dir to:%s\n",arg0->str);
    FRESULT fr = SD_ChangeDir(arg0->str);
    if(fr != FR_OK)
        SD_PrintError(fr);
}

static void _ed_cmd(struct cmd_arg *args, int nargs) {
	e_Edit(NULL);
}

static const struct command s_commands[] = {
	{ "HELP", _help_cmd, 0, 1 },
	{ "DIR",  _dir_cmd, 0, 1 },
	{ "CD",   _cd_cmd, 1, 1 },
	{ "ED",   _ed_cmd, 0, 1 },
};

/******************************************************************************
 * COMMAND PARSER
 */

/* Finds a command and returns the command index or -1 */
static int find_cmd(const char *str)
{
	int i;

	for (i = 0; i < NELEMS(s_commands); i++)
		if (strcmp(str, s_commands[i].name) == 0)
			return i;

	return -1;
}


static void _parse_token(const char *str, size_t *start, size_t *tok_len, size_t *parse_len) {

	size_t i;

	i = 0;
	while (isspace(str[i]))
		i++;

	if (str[i] == '\"') {
		i++;
		*start = i;
		while (str[i] != '\"' && str[i] != '\0')
			i++;
		*tok_len = i - *start;
		if (str[i] == '\"')
			i++;
	} else {
		*start = i;
		while (str[i] != '\0' && str[i] != '\"' && !isspace(str[i]))
				i++;
		*tok_len = i - *start;
	}

	while (isspace(str[i]))
		i++;
	*parse_len = i;
}

static size_t min_size(size_t a, size_t b) {
	if (a <= b)
		return a;
	else
		return b;
}

static void copy_to_str(char *dst, const char *src, size_t len) {
	memcpy(dst, src, len);
	dst[len] = '\0';
}

static int collect_args(struct cmd_arg *args, size_t nargs, size_t nextra_args,
    const char *from, size_t *ncollected)
{
	size_t start_offs, tok_len, parse_len;
	size_t i, n;

	i = 0;
	n = nargs + nextra_args;
	_parse_token(from, &start_offs, &tok_len, &parse_len);
	while (tok_len != 0 && i < n) {
		args[i].str = from + start_offs;
		args[i].len = tok_len;
		from += parse_len;
		i++;
		_parse_token(from, &start_offs, &tok_len, &parse_len);
	}

	if (tok_len == 0 && i >= nargs && i <= n) {
		*ncollected = i;
		return 0;
	} else {
		return -1;
	}
}

void ShellProcessLine(char *lp) {

	//printf("line: %s\n", line);

	int i;
	size_t start, tok_len, parse_len, collected;

	_parse_token(lp, &start, &tok_len, &parse_len);
	copy_to_str(name, lp + start, min_size(tok_len, (size_t) CMD_MAX_CHARS));
	//toupper_str(name);
    strupr(name);

	i = find_cmd(name);
	if (i < 0) {
        Con_printf("Command not found\n");
		return;
	}

    //Con_printf("Command: %s\n", name);
	if (collect_args(args, s_commands[i].nargs, s_commands[i].nextra_args,
		lp + parse_len, &collected) != 0) {
        Con_printf("Syntax error\n");
		return;
	} 

	s_commands[i].fun(args, collected); 
}

/******************************************************************************
 * INPUT HANDLING
 */

// forward declarations
static void _InsertCharacter(int c);
static unsigned char _HandleBackspace();

// eat keyboard input and process it
char *ShellReadInput() {

	int c;

	// editor states
	static unsigned char b_dirty = 0;	// has the user made any changes (or inputs) yet

    if(line) {
		// reset old editor line
		memset(editor_line, 0, CMD_LINE_MAX_CHARS);
		line_write_index = 0;
		b_dirty = 0;
        line = NULL;
    }

	c = Con_getc_nb(stdin);
    //putchar(c);

	switch (c) {
	case M16_CON_CURSOR_LEFT:
		if (line_write_index > 0) {
			line_write_index--;
			ConCursorLeft();
		} 
		break;
	case M16_CON_CURSOR_RIGHT:
		if (line_write_index < CMD_LINE_MAX_CHARS - 1 && line_write_index < strlen(editor_line)) {
			// standard cursor right while editing
			line_write_index++;
			ConCursorRight();
		}
		break;
	case 8:		// backspace
		if (_HandleBackspace())
			b_dirty = 1;	// if the backspacing actually resulted in a change: raise dirty flag
		break;
	case 10:	// new line
		if (b_dirty) {
            line = editor_line;
		}
		break;
	default:
		if (c >= 32 && c <= 126) {
			_InsertCharacter(c);
			b_dirty = 1;
		}
	}
	return line;
}

// user has pressed the backspace key
// - delete character before the write pointer (if any) and move write pointer "left"
// - also shift everything to the right of the cursor too
static unsigned char _HandleBackspace() {
	if (line_write_index > 0) {
		size_t len = strlen(editor_line);
		if (len > line_write_index) {
			line_write_index -= 1;
			memcpy(&editor_line[line_write_index], &editor_line[line_write_index+1], len - line_write_index - 1);
			editor_line[len - 1] = 0;
			// write shifted part to the display: use display driver directly so we don't mess with the cursor
			int i;
			for (i = line_write_index; i < len - 1; i++) {
				//SetCharacterPos(ConGetCursorRow() , i);
				PutCharacter(editor_line[i], ConGetCursorRow(), i);
			}
			//SetCharacterPos(ConGetCursorRow(), i);
			PutCharacter(' ', ConGetCursorRow(), i);
		}
		else {
			editor_line[--line_write_index] = 0;
		}
		return 1;
	}
	return 0;
}

// insert a character into editor_line
// shifts the contents "under and to the right" of the cursor to the right
static void _InsertCharacter(int c) {
	char temp[CMD_LINE_MAX_CHARS];
	size_t linesiz = strlen(editor_line);
	if (linesiz < CMD_LINE_MAX_CHARS) {
		// there is still some space left 
		size_t shift_size = linesiz - line_write_index;		// count of characters to shift 
		if (shift_size) {
			memcpy(temp, &editor_line[line_write_index ], shift_size);		// need to make a temp copy first
			memcpy(&editor_line[line_write_index + 1], temp, shift_size);	// because overlapping memcpy is bad
		}
		editor_line[line_write_index] = c;
		
		// write shifted part to the display: use display driver directly so we don't mess with the cursor
		if (shift_size) {
			for (int i = line_write_index; i <= line_write_index + shift_size; i++) {
				//SetCharacterPos(ConGetCursorRow(), i);
				PutCharacter(editor_line[i], ConGetCursorRow(), i);
			}
		}

		line_write_index++;
	}
}

// shell.c