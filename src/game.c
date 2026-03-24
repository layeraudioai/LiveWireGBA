
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gba_console.h>
#include <gba_video.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <gba_input.h>
#include "gba_helper.h"

// -----------------------------------------------------------------------------
// STRUCTURES
// -----------------------------------------------------------------------------
typedef struct {
    u8 rel_time;
    u8 lane;
    u16 note;
} __attribute__((packed)) RiffNote;

typedef struct {
    const RiffNote* notes;
    u16 note_count;
} Riff;

typedef struct {
    u32 start_time;
    const Riff* riff;
} RiffInstance;

typedef struct {
    const char* title;
    const RiffInstance* instances;
    u16 instance_count;
} Song;

extern const Song playlist[];
extern const int SONG_COUNT;

typedef enum { STATE_MENU, STATE_PLAYING } GameState;
GameState current_state = STATE_MENU;
int selected_song = 0;

#define MAX_NOTES_ON_SCREEN 64
#define MAX_ACTIVE_RIFFS 16
#define HIT_ZONE_Y 135
#define HIT_THRESHOLD 12
#define SCROLL_SPEED 3
#define MENU_COLS 4

typedef struct {
    int lane;
    int y;
    u16 note;
    int tile_idx;
    int active;
} Note;

typedef struct {
    const Riff* riff;
    u32 start_time;
    int note_ptr;
    int active;
} ActiveRiff;

Note active_notes[MAX_NOTES_ON_SCREEN];
ActiveRiff active_riffs[MAX_ACTIVE_RIFFS];
int song_inst_ptr = 0;
u32 frame_count = 0;
int score = 0;

// -----------------------------------------------------------------------------
// HELPERS
// -----------------------------------------------------------------------------
void play_note(u16 frequency) {
    REG_SOUND1CNT_L = 0x0000;
    REG_SOUND1CNT_H = 0xF700; 
    REG_SOUND1CNT_X = 0x8000 | (frequency & 0x7FF);
}

int get_pitch_group(u16 freq) {
    if (freq < 1700) return 0;
    if (freq < 1800) return 1;
    if (freq < 1875) return 2;
    if (freq < 1950) return 3;
    return 4;
}

void init_system() {
    irqInit();
    irqEnable(IRQ_VBLANK);
    REG_SOUNDCNT_X = 0x80;
    REG_SOUNDCNT_L = 0x77;
    REG_SOUNDCNT_H = 0x02;
    consoleInit(0, 31, 0, NULL, 0, 0);
    REG_DISPCNT = MODE_0 | BG0_ON | OBJ_ON | OBJ_1D_MAP;
    OBJ_PALETTE_RAM[1] = RGB(31, 31, 31);
    OBJ_PALETTE_RAM[2] = RGB(0, 31, 0);  
    OBJ_PALETTE_RAM[3] = RGB(31, 0, 0);  
    OBJ_PALETTE_RAM[4] = RGB(31, 31, 0); 
    OBJ_PALETTE_RAM[5] = RGB(0, 0, 31);  
    OBJ_PALETTE_RAM[6] = RGB(10, 10, 10); 
    OBJ_PALETTE_RAM[7]  = RGB(31, 31, 31); 
    OBJ_PALETTE_RAM[8]  = RGB(0, 20, 20); 
    OBJ_PALETTE_RAM[9]  = RGB(20, 0, 20); 
    OBJ_PALETTE_RAM[10] = RGB(31, 15, 0); 
    OBJ_PALETTE_RAM[11] = RGB(0, 10, 31); 
    u16* sprite_vram = (u16*)OBJ_BASE_ADR;
    for (int l = 0; l < 5; l++) {
        for (int p = 0; p < 5; p++) {
            int base_tile = (l * 5 + p) * 4;
            u16* tile_ptr = &sprite_vram[base_tile * 16];
            for (int y = 0; y < 16; y++) {
                for (int x = 0; x < 8; x++) {
                    int p1_x = x * 2, p2_x = x * 2 + 1;
                    int c1 = l + 1, c2 = l + 1;
                    if (y >= 4 && y < 12) {
                        if (p1_x >= 4 && p1_x < 12) c1 = p + 7;
                        if (p2_x >= 4 && p2_x < 12) c2 = p + 7;
                    }
                    tile_ptr[y * 8 + x] = c1 | (c2 << 4);
                }
            }
        }
    }
    for (int i = 0; i < 64; i++) {
        u16 px = 0x6666;
        if (i >= 8 && i < 56 && (i % 8 >= 1 && i % 8 < 7)) px = 0x0000;
        sprite_vram[100 * 16 + i] = px;
    }
}

void start_song(int index) {
    if (SONG_COUNT == 0) return;
    selected_song = index;
    song_inst_ptr = 0;
    frame_count = 0;
    score = 0;
    memset(active_notes, 0, sizeof(active_notes));
    memset(active_riffs, 0, sizeof(active_riffs));
    for (int i = 0; i < 128; i++) OAM[i].attr0 = ATTR0_HIDE;
    printf("\x1b[2J");
    current_state = STATE_PLAYING;
}

void spawn_note(int lane, u16 note) {
    int pitch_grp = get_pitch_group(note);
    int tile_idx = (lane * 5 + pitch_grp) * 4;
    for (int i = 0; i < MAX_NOTES_ON_SCREEN; i++) {
        if (!active_notes[i].active) {
            active_notes[i].active = 1;
            active_notes[i].lane = lane;
            active_notes[i].note = note;
            active_notes[i].tile_idx = tile_idx;
            active_notes[i].y = -16;
            return;
        }
    }
}

void update_playing(u16 keys_pressed, u16 grip_state) {
    frame_count++;
    const Song* song = &playlist[selected_song];
    printf("\x1b[0;0HScore: %d\nSong: %.20s", score, song->title);
    while (song_inst_ptr < song->instance_count && song->instances[song_inst_ptr].start_time <= frame_count) {
        for (int i = 0; i < MAX_ACTIVE_RIFFS; i++) {
            if (!active_riffs[i].active) {
                active_riffs[i].active = 1;
                active_riffs[i].riff = song->instances[song_inst_ptr].riff;
                active_riffs[i].start_time = song->instances[song_inst_ptr].start_time;
                active_riffs[i].note_ptr = 0;
                break;
            }
        }
        song_inst_ptr++;
    }
    for (int i = 0; i < MAX_ACTIVE_RIFFS; i++) {
        if (active_riffs[i].active) {
            const Riff* r = active_riffs[i].riff;
            u32 rel_frame = frame_count - active_riffs[i].start_time;
            while (active_riffs[i].note_ptr < r->note_count && r->notes[active_riffs[i].note_ptr].rel_time <= rel_frame) {
                spawn_note(r->notes[active_riffs[i].note_ptr].lane, r->notes[active_riffs[i].note_ptr].note);
                active_riffs[i].note_ptr++;
            }
            if (active_riffs[i].note_ptr >= r->note_count) active_riffs[i].active = 0;
        }
    }
    for (int i = 0; i < MAX_NOTES_ON_SCREEN; i++) {
        if (active_notes[i].active) {
            active_notes[i].y += SCROLL_SPEED;
            if (active_notes[i].y > 160) active_notes[i].active = 0;
        }
    }
    u16 strum_keys = KEY_A | KEY_B | KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT;
    if (keys_pressed & strum_keys) {
        for (int i = 0; i < MAX_NOTES_ON_SCREEN; i++) {
            if (active_notes[i].active) {
                int dy = abs(active_notes[i].y - HIT_ZONE_Y);
                if (dy < HIT_THRESHOLD) {
                    int lane = active_notes[i].lane;
                    int correct = 0;
                    if (lane == 0) { if ((grip_state & 0x1E) == 0) correct = 1; }
                    else { if (grip_state & (1 << lane)) correct = 1; }
                    if (correct) {
                        active_notes[i].active = 0;
                        score += 10;
                        play_note(active_notes[i].note); 
                    }
                }
            }
        }
    }
    for (int i = 0; i < MAX_NOTES_ON_SCREEN; i++) {
        if (active_notes[i].active) {
            int x = 50 + active_notes[i].lane * 35;
            OAM[i].attr0 = (active_notes[i].y & 0xFF) | ATTR0_SQUARE;
            OAM[i].attr1 = (x & 0x1FF) | ATTR1_SIZE_16;
            OAM[i].attr2 = active_notes[i].tile_idx;
        } else {
            OAM[i].attr0 = ATTR0_HIDE;
        }
    }
    for (int j = 0; j < 5; j++) {
        int pressed = (j == 0) ? ((grip_state & 0x1E) == 0) : (grip_state & (1 << j));
        int obj_idx = MAX_NOTES_ON_SCREEN + j;
        OAM[obj_idx].attr0 = (HIT_ZONE_Y & 0xFF) | ATTR0_SQUARE;
        OAM[obj_idx].attr1 = ((50 + j * 35) & 0x1FF) | ATTR1_SIZE_16;
        OAM[obj_idx].attr2 = pressed ? ((j * 5) * 4) : 100;
    }
    if (keys_pressed & KEY_SELECT) { printf("\x1b[2J"); current_state = STATE_MENU; }
}

void update_menu(u16 keys_pressed) {
    if (SONG_COUNT > 0) {
        if (keys_pressed & KEY_UP) { if (selected_song >= MENU_COLS) selected_song -= MENU_COLS; }
        if (keys_pressed & KEY_DOWN) { if (selected_song + MENU_COLS < SONG_COUNT) selected_song += MENU_COLS; }
        if (keys_pressed & KEY_LEFT) { if (selected_song % MENU_COLS > 0) selected_song--; }
        if (keys_pressed & KEY_RIGHT) { if (selected_song % MENU_COLS < MENU_COLS - 1 && selected_song + 1 < SONG_COUNT) selected_song++; }
        if (keys_pressed & (KEY_A | KEY_START)) start_song(selected_song);
    }
    printf("\x1b[0;0HSelect Song Grid:\n");
    for (int i = 0; i < SONG_COUNT && i < 32; i++) {
        int col = i % MENU_COLS, row = i / MENU_COLS;
        int x = 20 + col * 55, y = 35 + row * 35;
        OAM[i].attr0 = (y & 0xFF) | ATTR0_SQUARE;
        OAM[i].attr1 = (x & 0x1FF) | ATTR1_SIZE_16;
        OAM[i].attr2 = (i % 25) * 4;
        printf("\x1b[%d;%dH%.6s", (y/8)+2, (x/8), playlist[i].title);
        if (i == selected_song) {
            OAM[127].attr0 = (y & 0xFF) | ATTR0_SQUARE;
            OAM[127].attr1 = (x & 0x1FF) | ATTR1_SIZE_16;
            OAM[127].attr2 = 100;
        }
    }
}

int main(void) {
    init_system();
    u16 current_keys = 0, prev_keys = 0, grip_state = 0;
    while(1) {
        VBlankIntrWait();
        prev_keys = current_keys;
        current_keys = ~REG_KEYINPUT;
        u16 keys_pressed = current_keys & ~prev_keys;
        grip_state = ~(*GRIP_ADDR);
        if (current_state == STATE_MENU) update_menu(keys_pressed);
        else update_playing(keys_pressed, grip_state);
    }
    return 0;
}
