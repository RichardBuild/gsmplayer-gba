#include <gba_affine.h>
#include <gba_video.h>
#include <gba_dma.h>
#include <gba_systemcalls.h>

#include "blend_defs.h"
#include "art.h"

// adapted from tonc
#define REG_BG_AFFINE		((BGAffineDest*)(REG_BASE+0x0000))	//!< Bg affine array

#define SHARED_CHAR_BASE 1
#define PALETTE_RESERVED_ENTRIES 16
#define PALETTE_TOTAL_BYTES 512
#define BITMAP_TOTAL_BYTES 16384

void setupArtBackdrop() {
    REG_BG2CNT = SCREEN_BASE(30)
        | CHAR_BASE(SHARED_CHAR_BASE)
        | BG_PRIORITY(3)
        | BG_256_COLOR
        | BG_SIZE_0;

    BGAffineSource bgAffineSrc;
    bgAffineSrc.theta = 0;
    // 2x display
    bgAffineSrc.sX = 1 << 7;
    bgAffineSrc.sY = 1 << 7;
    // Center on screen
    bgAffineSrc.tX = 120;
    bgAffineSrc.tY = 80;
    // Center image
    bgAffineSrc.x = 64 << 8;
    bgAffineSrc.y = 64 << 8;
    // Apply affine transform
    BgAffineSet(&bgAffineSrc, &(REG_BG_AFFINE[2]), 1);

    // Fill out screen entries for album art background (affine)
    // Affine backgrounds use 8-bit screen entries
    u16* screenEntries = SCREEN_BASE_BLOCK(30);
    for (u16 i = 0; i < 256; i += 2) {
        // We have to write 2 bytes at a time
        // because VRAM can only be written in
        // 2 or 4 byte chunks
        screenEntries[i >> 1] = i | ((i + 1) << 8);
    }

    // Blend album art background with white
    REG_BLDCNT = BLD_BUILD(
        BLD_BG2,
        0,
        2
    );
    REG_BLDY = BLDY_BUILD(12); // out of 15
}

void setupArtForeground() {
    REG_BG0CNT = SCREEN_BASE(29)
        | CHAR_BASE(SHARED_CHAR_BASE)
        | BG_PRIORITY(0)
        | BG_256_COLOR
        | BG_SIZE_0;
    REG_BG0HOFS = -8;
    REG_BG0VOFS = -8;

    // Fill out screen entries for album art foreground (regular)
    // Regular backgrounds use 16-bit screen entries
    // The album art only fills up one quadrant of the smallest bg
    // Album art is 128x128px, or 16x16 tiles
    // BG size is 256x256px, or 32x32 tiles
    // So we need to second half of each row in the top section
    // as transparent, then fill all of the rest as transparent.
    u16* screenEntries = SCREEN_BASE_BLOCK(29);
    u16 screenEntryIndex = 0;
    u16 tileIndex = 0;
    while (tileIndex < 256) {
        u16 sei = screenEntryIndex++;
        if ((sei >> 4) % 2 == 0) {
            screenEntries[sei] = tileIndex++;
        } else {
            // Use tile at beginning of next charblock as transparent
            // (we're assuming the tiles in charblock 3 are zeroed out)
            screenEntries[sei] = 256;
        }
    }
    // Fill out remaining screen entries in BG0 as transparent
    while (screenEntryIndex < 1024) {
        screenEntries[screenEntryIndex++] = 256;
    }
}

void swapArt(const void *art_data)
{
    const u16 *palette = (const u16 *)art_data;
    const u8 *bitmap = (const u8 *)art_data + PALETTE_TOTAL_BYTES;

    while (REG_DMA3CNT & DMA_ENABLE)
        VBlankIntrWait();
    dmaCopy(
        palette + PALETTE_RESERVED_ENTRIES,
        BG_PALETTE + PALETTE_RESERVED_ENTRIES,
        PALETTE_TOTAL_BYTES - PALETTE_RESERVED_ENTRIES * 2
    );
    while (REG_DMA3CNT & DMA_ENABLE)
        VBlankIntrWait();
    dmaCopy(bitmap, PATRAM8(SHARED_CHAR_BASE, 0), BITMAP_TOTAL_BYTES);
    while (REG_DMA3CNT & DMA_ENABLE)
        VBlankIntrWait();
}

void initArt()
{
    setupArtBackdrop();
    setupArtForeground();
}
