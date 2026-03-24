#ifndef GBA_HELPER_H
#define GBA_HELPER_H

#include <gba_types.h>
#include <gba_video.h>
#include <gba_input.h>
#include <gba_systemcalls.h>
#include <gba_interrupt.h>

typedef u16 COLOR;

#define SCREEN_WIDTH    240
#define SCREEN_HEIGHT   160

/* DS Guitar Hero Grip */
#define GRIP_ADDR    ((volatile u16*)0x08000000)
#define GRIP_GREEN   (1 << 1)
#define GRIP_RED     (1 << 2)
#define GRIP_YELLOW  (1 << 3)
#define GRIP_BLUE    (1 << 4)

/* Sound Registers */
#define REG_SOUNDCNT_L     *((volatile u16*)(0x04000080))
#define REG_SOUNDCNT_H     *((volatile u16*)(0x04000082))
#define REG_SOUNDCNT_X     *((volatile u16*)(0x04000084))
#define REG_SOUND1CNT_L    *((volatile u16*)(0x04000060))
#define REG_SOUND1CNT_H    *((volatile u16*)(0x04000062))
#define REG_SOUND1CNT_X    *((volatile u16*)(0x04000064))

/* Sprite/OAM Helpers */
typedef struct {
    u16 attr0;
    u16 attr1;
    u16 attr2;
    u16 dummy;
} __attribute__((packed, aligned(4))) OBJATTR;

#define OAM ((volatile OBJATTR*)0x07000000)
#define OBJ_BASE_ADR 0x06010000
#define OBJ_PALETTE_RAM ((u16*)0x05000200)

/* Sprite Attribute Constants */
#define ATTR0_SQUARE    0x0000
#define ATTR0_HIDE      0x0080
#define ATTR1_SIZE_16   0x4000

/* Note Frequencies */
#define NOTE_C4  1605
#define NOTE_CS4 1629
#define NOTE_D4  1652
#define NOTE_DS4 1673
#define NOTE_E4  1694
#define NOTE_F4  1714
#define NOTE_FS4 1732
#define NOTE_G4  1750
#define NOTE_GS4 1767
#define NOTE_A4  1783
#define NOTE_AS4 1798
#define NOTE_B4  1812
#define NOTE_C5  1826
#define NOTE_CS5 1839
#define NOTE_D5  1851
#define NOTE_DS5 1862
#define NOTE_E5  1873
#define NOTE_F5  1883
#define NOTE_FS5 1892
#define NOTE_G5  1901
#define NOTE_GS5 1910
#define NOTE_A5  1918
#define NOTE_AS5 1926
#define NOTE_B5  1933
#define NOTE_C6  1940
#define NOTE_CS6 1947
#define NOTE_D6  1953
#define NOTE_DS6 1959
#define NOTE_E6  1964
#define NOTE_F6  1969
#define NOTE_FS6 1974
#define NOTE_G6  1978
#define NOTE_GS6 1982
#define NOTE_A6  1986
#define NOTE_AS6 1990
#define NOTE_B6  1994
#define NOTE_C7  1997

static inline COLOR RGB(u32 red, u32 green, u32 blue) {   
  return red | (green<<5) | (blue<<10);   
}

#endif