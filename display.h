
#pragma once

#define D_FRAME_WIDTH 320
#define D_FRAME_HEIGHT 240

#define D_CHAR_COLS 40
#define D_CHAR_ROWS 30

#define D_FONT_HEIGHT 8
#define D_FONT_FIRST_ASCII 32

#define D_ATTR_INVERSE    0b00000001
#define D_ATTR_FLASH      0b00000010

#define D_FLASH_DELAY 20      // delay in frames (30 = 1/2 sec at 60fps)

void DisplayOpen();
void ClearTextDisplay();

void PutCharacter(int c, int row, int col);
void SetAttribute(unsigned char a, int row, int col);
void UnSetAttribute(unsigned char a, int row, int col);

void RenderTextScanline(int y);

void CharacterDisplayScrollUp();
void CharacterDisplayScrollUpRow(int r);
void CharacterDisplayScrollDownRow(int r);
void CharacterDisplayScrollDownRange(int start_r, int end_r);
void CharacterDisplayScrollUpRange(int start_r, int end_r);

// display.h