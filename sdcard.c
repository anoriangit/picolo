// *******************************************************************
// See FatFs - Generic FAT Filesystem Module, "Application Interface",
// http://elm-chan.org/fsw/ff/00index_e.html
//
// *******************************************************************


#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "display.h"
#include "conio.h"

// FatFS sd card support
#include "sdcard.h"
#include "hw_config.h"


// *******************************************************************
// hw_conf
// *******************************************************************

/* Configuration of hardware SPI object */
static spi_t spi = {
    .hw_inst = spi0,    // SPI component
    .sck_gpio = 2,      // GPIO number (not Pico pin number) (CLK pin on adafruit)
    .mosi_gpio = 3,     // connect to SI pin on adafruit breakout
    .miso_gpio = 4,     // connect to SO pin on adafruit breakout

    //.baud_rate = 125 * 1000 * 1000 / 8  // 15625000 Hz
    //.baud_rate = 125 * 1000 * 1000 / 6  // 20833333 Hz
    .baud_rate = 125 * 1000 * 1000 / 4  // 31250000 Hz
    //.baud_rate = 125 * 1000 * 1000 / 2  // 62500000 Hz
};

/* SPI Interface */
static sd_spi_if_t spi_if = {
    .spi = &spi,  // Pointer to the SPI driving this card
    .ss_gpio = 5  // The SPI slave select GPIO for this SD card (CS pin on adafruit)
};

/* Configuration of the SD Card socket object */
static sd_card_t sd_card = {
    .type = SD_IF_SPI,
    .spi_if_p = &spi_if  // Pointer to the SPI interface driving this card
};

/* ********************************************************************** */

size_t sd_get_num() { return 1; }

/**
 * @brief Get a pointer to an SD card object by its number.
 *
 * @param[in] num The number of the SD card to get.
 *
 * @return A pointer to the SD card object, or @c NULL if the number is invalid.
 */
sd_card_t *sd_get_by_num(size_t num) {
    if (0 == num) {
        // The number 0 is a valid SD card number.
        // Return a pointer to the sd_card object.
        return &sd_card;
    } else {
        // The number is invalid. Return @c NULL.
        return NULL;
    }
}


#define FS_SECTOR_SIZE  512

static FATFS FS = { 0 };    // set fs_type = 0 (not mounted)

void SD_PrintError(FRESULT err) {
        Con_printf("card error: %s (%d)\n", FRESULT_str(err), err);
}

static FRESULT _checkmount() {
    FRESULT fr = FR_OK;
    if(FS.fs_type == 0) {   // fs_type 0 means "not mounted"
        fr = f_mount(&FS, "sd", 1); 
        if (FR_OK != fr) {
            SD_PrintError(fr);
        }
    }
    return fr;
}

static DIR dir;
static FILINFO fno;

FRESULT SD_ChangeDir (const char *path) {
    if(_checkmount() != FR_OK) 
        return -1;
    FRESULT fr = f_chdir (path);
    return fr;
}

FRESULT SD_ListDir (const char *path)
{
    FRESULT res;
    int nfile, ndir;

    if(_checkmount() != FR_OK) 
        return -1;

    memset(&dir,0,sizeof(DIR));
    memset(&fno,0,sizeof(FILINFO));
    
    res = f_opendir(&dir, path);                       /* Open the directory */
    if (res == FR_OK) {
        nfile = ndir = 0;
        for (;;) {
            res = f_readdir(&dir, &fno);                   /* Read a directory item */
            if (res != FR_OK || fno.fname[0] == 0) break;  /* Error or end of dir */
            if (fno.fattrib & AM_DIR) {            /* Directory */
                printf("   <DIR>   %s\n", fno.fname);
                Con_printf("   <DIR>   %s\n", fno.fname);
                ndir++;
            } else {                               /* File */
                // gs: for some reason, packing the two items being printed
                // below into one single printf() statement fails hence the split
                printf("%10ld ", fno.fsize);
                Con_printf("%10ld ", fno.fsize);
                printf("%s\n", fno.fname);
                Con_printf("%s\n", fno.fname);
                nfile++;
            }
        }
        f_closedir(&dir);
        printf("%d dirs, %d files.\n", ndir, nfile);
        Con_printf("%d dirs, %d files.\n", ndir, nfile);
    } else {
        Con_printf("Failed to open \"%s\". (%u)\n", path, res);
    }
    return res;
}

long SDCardTest() {
    
    if(_checkmount() != FR_OK) 
        return -1;

    DWORD n_clusters;
    FATFS *fsp;
    FRESULT fr = f_getfree ("sd", &n_clusters, &fsp);

    if (FR_OK != fr) {
        panic("f_getfree error: %s (%d)\n", FRESULT_str(fr), fr);
    }

    return n_clusters*FS.csize*FS_SECTOR_SIZE;

    // Unmount the SD card
    //f_unmount("");
}

// sdcard.c
