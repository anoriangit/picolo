#include <stdio.h>
#include <stdlib.h>
#include <pico/stdlib.h>	// for panic()
#include <string.h>
#include <malloc.h>

#include "ed.h"


static struct TextLineNode *_newTextLineNode(char *line);
static char* _newTextLine(char *line);

// Find a node in the list by index
// returns the node or NULL
struct TextLineNode *e_BufferFindNode(struct TextBuffer *B, int i) {
    int line = 0;
    struct TextLineNode *node = B->list_head;
    while(node) {
        if(line == i)
            break;
        // walk the list
        node = node->next;
		line++;
    }
    return node;
}

struct TextPos e_BufferCursor2Pos(struct TextBuffer *B, struct TextCursor cursor) {
    struct TextPos pos = {0};
    struct TextLineNode *node = e_BufferFindNode(B, cursor.row);
    if(node) {
        pos.node = node;
        pos.pos = (cursor.col < node->line.len) ? cursor.col : node->line.len;
    }
    return pos;
}

// Does all required memory allocations to create a TextLineNode
// from the passed in line of text and then appends it to the 
// end of the TextBuffer's list 
void e_BufferAppendLine(struct TextBuffer* B, char *line) {

    // both of the newXXX() calls used will panic() on malloc errors
	char *buf = _newTextLine(line);

	struct TextLineNode *node = B->list_tail;
	if(node && node == B->list_head && node->line.text == NULL) {
		// this is a buffer with just the intial empty dummy head node existing
		// insert the incoming text line into it
		node->line.text = buf;
		node->line.len = strlen(buf);
	} else {
		node = _newTextLineNode(buf);
	}

	
	node->prev = B->list_tail;
	if(B->list_tail == NULL) {
		// list is emtpty
		B->list_head = B->list_tail = node;
	} else {
		// link up to the end of the list
		B->list_tail->next = node;
           B->list_tail = node;
	}
    B->b_dirty = 1;
}

// Initialize a TextBuffer
// Does not allocate the memory for the buffer!
void e_InitBuffer(struct TextBuffer* b) {

    // new list based stuff: each buffer starts with on empty dummy line
    b->list_head = _newTextLineNode(NULL);
    b->list_tail = b->list_head;
    b->pos.node = b->list_head;
    b->pos.pos = b->list_head->line.len;


	b->C.col = 0;
	b->C.row = 0;
	b->SC.col = 0;
	b->SC.row = 0;
    
	b->b_dirty = 0;
	b->next_line = 0;

	b->source_filename[0] = 0;

	size_t size = sizeof(b->line_len);
	memset(b->line_len, 0, size);

	size = sizeof(b->out_lines);
	memset(b->out_lines, 0, size);
}


// allocate character buffer memory and copy text line into it
// enforces defined maximum length by truncating incoming text if required
// return allocated line buffer or NULL (out of memory)
static char *_newTextLine(char *text) {
	// enforce maximum length
	size_t len = strlen(text);
	if(len >= ED_LINE_MAX_LEN)
		len = ED_LINE_MAX_LEN;

	char *buf = malloc(len+1);		// make room for trailing 0
	if(!buf)
    	panic("out off memory allocating TextLineBuf");

	memcpy(buf, text, len);
	buf[len] = 0;

	return buf;
}

// allocate a TextLineNode, initialize its list related members
// and insert pointer to the text buffer
// returns TextLineNode pointer or NULL (out of memory)
static struct TextLineNode *_newTextLineNode(char *text) {
	struct TextLineNode *tl = malloc(sizeof(struct TextLineNode));
	if(tl) {
		tl->line.text = text;
		tl->line.len = (uint16_t) strlen(text);
		tl->next = NULL;
		tl->prev = NULL;
	} else{
	    panic("out off memory allocating TextLineNode");
	}
	return tl;
}

// ed_buffer.c
