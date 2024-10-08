/* -------------------------------------------------------
 * CHARACTER STREAMS
 * ------------------------------------------------------*/

#define CHARACTER_STREAM_BUFFER_SIZE 32

struct CharacterStream {
	char buffer[CHARACTER_STREAM_BUFFER_SIZE];
	char *write_pointer;
	char *read_pointer;
	int size_remaining;
};

extern void OpenCharacterStream(struct CharacterStream *stream);
extern void StreamRewind(struct CharacterStream *stream);
extern void StreamWriteCharacter(int c, struct CharacterStream *stream);
extern int StreamReadCharacter(struct CharacterStream *stream);

// cstream.h
