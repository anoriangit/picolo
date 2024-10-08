#pragma once

#include "f_util.h"
#include "ff.h"

void SD_PrintError(FRESULT err);
long SDCardTest();

FRESULT SD_ListDir (const char *path);
FRESULT SD_ChangeDir (const char *path);

// sdcard.h