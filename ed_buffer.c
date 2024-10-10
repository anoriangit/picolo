#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "ed.h"


static struct TextLineNode *_newTextLineNode(char *line);
static char* _newTextLineBuf(char *line);
static void _panic(char *msg);

// Find a node in the list by index
// returns the node or NULL
struct TextLineNode *e_BufferFindNode(struct TextBuffer* B, int i) {
    int count = 0;
    struct TextLineNode *node = B->list_head;
    while(node) {
        if(count == i)
            break;
        // walk the list
        node = node->next;
    }
    return node;
}

// Does all required memory allocations to create a TextLineNode
// from the passed in line of text and then appends it to the 
// end of the TextBuffer's list 
void e_BufferAppendLine(struct TextBuffer* B, char *line) {
	char *buf = _newTextLineBuf(line);
	struct TextLineNode *tl = _newTextLineNode(buf);
	if(tl && buf) {
		tl->text = buf;
		tl->prev = B->list_tail;
		if(B->list_tail == NULL) {
			// list is emtpty
			B->list_head = B->list_tail = tl;
		} else {
			// link up to the end of the list
			B->list_tail->next = tl;
            B->list_tail = tl;
		}
        B->b_dirty = 1;
	} else {
			_panic("out of memory in line buffer.");
	}
}

// Initialize a TextBuffer
// Does not allocate the memory for the buffer!
void e_InitBuffer(struct TextBuffer* b) {
	b->C.col = 0;
	b->C.row = 0;
	b->SC.col = 0;
	b->SC.row = 0;

    b->list_head = NULL;
    b->list_tail = NULL;
    
	b->b_dirty = 0;
	b->next_line = 0;

	b->source_filename[0] = 0;

	size_t size = sizeof(b->line_len);
	memset(b->line_len, 0, size);

	size = sizeof(b->out_lines);
	memset(b->out_lines, 0, size);
}


static void _panic(char *msg) {
	printf("PANIC: %s\n", msg);
}

// allocate character buffer memory and copy text line into it
// enforces defined maximum length by truncating incoming text if required
// return allocated line buffer or NULL (out of memory)
static char* _newTextLineBuf(char *line) {
	// enforce maximum length
	size_t len = strlen(line);
	if(len >= ED_LINE_MAX_LEN)
		len = ED_LINE_MAX_LEN;

	char *buf = malloc(len+1);		// make room for trailing 0
	if(buf) {
		memcpy(buf, line, len);
		buf[len] = 0;
	}
	return buf;
}

// allocate a TextLineNode, initialize its list related members
// and insert pointer to the text buffer
// returns TextLineNode pointer or NULL (out of memory)
static struct TextLineNode *_newTextLineNode(char *line) {
	struct TextLineNode *tl = malloc(sizeof(struct TextLineNode));
	if(tl) {
		tl->text = line;
		tl->next = NULL;
		tl->prev = NULL;
	}
	return tl;
}

// ed_buffer.c
