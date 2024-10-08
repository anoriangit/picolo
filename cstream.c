

#include "cstream.h"


void StreamRewind(struct CharacterStream *stream) {
	stream->write_pointer = stream->buffer;
	stream->read_pointer = stream->buffer;
	stream->size_remaining = CHARACTER_STREAM_BUFFER_SIZE;
}

void OpenCharacterStream(struct CharacterStream *stream) {
	StreamRewind(stream);
}

// write a single character to a character stream
// will silently fail if the stream is full
void StreamWriteCharacter(int c, struct CharacterStream *stream) {
	if (stream->size_remaining) {
		*stream->write_pointer++ = c;
		stream->size_remaining--;
	}
}

// returns 0 if nothing to read
int StreamReadCharacter(struct CharacterStream *stream) {
	int c = 0;
	if (stream->read_pointer < stream->write_pointer) {
		c = *stream->read_pointer++;
		// we've reached the write pointer (stream has been completely consumed): rewind
		if (stream->read_pointer == stream->write_pointer) {
			StreamRewind(stream);
		}
	}
	return c;
}

// cstream.c