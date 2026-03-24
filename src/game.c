
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gba_helper.h"

// -----------------------------------------------------------------------------
// SONG CHART STRUCTURE
// -----------------------------------------------------------------------------
typedef struct {
    u32 time; // in frames
    u8 lane;  // 0-3 for frets, 4 for open
    u16 note; // Frequency to play
} ChartNote;

typedef struct {
    const char* title;
    const ChartNote* chart;
} Song;

// These are generated into playlist.c by the Makefile tool
extern const Song playlist[];
extern const int SONG_COUNT;

// -----------------------------------------------------------------------------
// ENGINE STATE
// -----------------------------------------------------------------------------
typedef enum { STATE_MENU, STATE_PLAYING } GameState;
GameState current_state = STATE_MENU;
int selected_song = 0;

#define MAX_NOTES_ON_SCREEN 64
#define HIT_ZONE_Y 135
#define HIT_THRESHOLD 12
#define SCROLL_SPEED 3

typedef struct {
    int lane;
    int y;
    u16 note;
    int active;
} Note;

Note active_notes[MAX_NOTES_ON_SCREEN];
int chart_ptr = 0;
u32 frame_count = 0;
int score = 0;

// -----------------------------------------------------------------------------
// SYSTEM HELPERS
// -----------------------------------------------------------------------------
void play_note(u16 frequency) {
    REG_SOUND1CNT_L = 0x0000;
    REG_SOUND1CNT_H = 0xF700; 
    REG_SOUND1CNT_X = 0x8000 | (frequency & 0x7FF);
}

void init_system() {
    REG_SOUNDCNT_X = 0x80;
    REG_SOUNDCNT_L = 0x77;
    REG_SOUNDCNT_H = 0x02;

    REG_DISPLAYCONTROL = VIDEO_MODE_0 | OBJ_ENABLE | OBJ_MAP_1D;

    // Palettes
    OBJ_PALETTE[1] = RGB(0, 31, 0);  // Green
    OBJ_PALETTE[2] = RGB(31, 0, 0);  // Red
    OBJ_PALETTE[3] = RGB(31, 31, 0); // Yellow
    OBJ_PALETTE[4] = RGB(0, 0, 31);  // Blue
    OBJ_PALETTE[5] = RGB(25, 0, 25); // Purple
    OBJ_PALETTE[6] = RGB(31, 31, 31); // White

    u16* sprite_vram = (u16*)OBJ_BASE_ADR;
    for (int s = 0; s < 5; s++) {
        u16 color_bits = (s + 1) | ((s + 1) << 4) | ((s + 1) << 8) | ((s + 1) << 12);
        for (int i = 0; i < 64; i++) sprite_vram[s * 64 + i] = color_bits;
    }
    for (int i = 0; i < 64; i++) {
        u16 px = 0;
        if (i < 4 || i > 60 || (i % 4 == 0) || (i % 4 == 3)) px = 0x6666;
        sprite_vram[5 * 64 + i] = px;
    }
}

void start_song(int index) {
    if (SONG_COUNT == 0) return;
    selected_song = index;
    chart_ptr = 0;
    frame_count = 0;
    score = 0;
    memset(active_notes, 0, sizeof(active_notes));
    for (int i = 0; i < 128; i++) OAM[i].attr0 = 160;
    current_state = STATE_PLAYING;
}

// -----------------------------------------------------------------------------
// UPDATE & DRAW
// -----------------------------------------------------------------------------
void update_playing(u16 keys_pressed, u16 grip_state) {
    frame_count++;
    const ChartNote* chart = playlist[selected_song].chart;

    // Spawn notes
    while (chart[chart_ptr].time != 0 && chart[chart_ptr].time <= frame_count) {
        for (int i = 0; i < MAX_NOTES_ON_SCREEN; i++) {
            if (!active_notes[i].active) {
                active_notes[i].active = 1;
                active_notes[i].lane = chart[chart_ptr].lane;
                active_notes[i].note = chart[chart_ptr].note;
                active_notes[i].y = -16;
                break;
            }
        }
        chart_ptr++;
    }

    // Move notes
    for (int i = 0; i < MAX_NOTES_ON_SCREEN; i++) {
        if (active_notes[i].active) {
            active_notes[i].y += SCROLL_SPEED;
            if (active_notes[i].y > 160) active_notes[i].active = 0;
        }
    }

    // Strumming logic
    u16 strum_keys = KEY_A | KEY_B | KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT;
    if (keys_pressed & strum_keys) {
        for (int i = 0; i < MAX_NOTES_ON_SCREEN; i++) {
            if (active_notes[i].active) {
                int dy = abs(active_notes[i].y - HIT_ZONE_Y);
                if (dy < HIT_THRESHOLD) {
                    int lane = active_notes[i].lane;
                    int correct = 0;
                    if (lane == 4) { if ((grip_state & 0x1E) == 0) correct = 1; }
                    else { if (grip_state & (1 << (lane + 1))) correct = 1; }

                    if (correct) {
                        active_notes[i].active = 0;
                        score += 10;
                        play_note(active_notes[i].note);
                    }
                }
            }
        }
    }

    // Draw notes
    for (int i = 0; i < MAX_NOTES_ON_SCREEN; i++) {
        if (active_notes[i].active) {
            int x = 60 + active_notes[i].lane * 30;
            if (active_notes[i].lane == 4) x = 60;
            OAM[i].attr0 = (active_notes[i].y & 0xFF);
            OAM[i].attr1 = (x & 0x1FF) | (1 << 14);
            OAM[i].attr2 = (active_notes[i].lane * 4);
        } else {
            OAM[i].attr0 = 160;
        }
    }

    // Hit Zone feedback
    for (int j = 0; j < 4; j++) {
        int x = 60 + j * 30;
        int active = (grip_state & (1 << (j + 1)));
        int obj_idx = MAX_NOTES_ON_SCREEN + j;
        OAM[obj_idx].attr0 = (HIT_ZONE_Y & 0xFF);
        OAM[obj_idx].attr1 = (x & 0x1FF) | (1 << 14);
        OAM[obj_idx].attr2 = active ? (j * 4) : (5 * 4);
    }

    if (keys_pressed & KEY_START) current_state = STATE_MENU;
}

void update_menu(u16 keys_pressed) {
    if (SONG_COUNT > 0) {
        if (keys_pressed & KEY_UP) selected_song = (selected_song - 1 + SONG_COUNT) % SONG_COUNT;
        if (keys_pressed & KEY_DOWN) selected_song = (selected_song + 1) % SONG_COUNT;
        if (keys_pressed & (KEY_A | KEY_START)) start_song(selected_song);
    }

    // Simple visual for menu (using note sprites as cursors)
    for (int i = 0; i < 128; i++) OAM[i].attr0 = 160;
    if (SONG_COUNT > 0) {
        OAM[0].attr0 = 40 + (selected_song * 20);
        OAM[0].attr1 = 40 | (1 << 14);
        OAM[0].attr2 = 0; // Green dot cursor
    }
}

// -----------------------------------------------------------------------------
// MAIN
// -----------------------------------------------------------------------------
int main(void) {
    init_system();
    u16 current_keys = 0, prev_keys = 0, grip_state = 0;

    while(1) {
        vsync();
        prev_keys = current_keys;
        current_keys = ~REG_KEYINPUT;
        u16 keys_pressed = current_keys & ~prev_keys;
        grip_state = ~(*GRIP_ADDR);

        if (current_state == STATE_MENU) {
            update_menu(keys_pressed);
        } else {
            update_playing(keys_pressed, grip_state);
        }
    }
    return 0;
}
