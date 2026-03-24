
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gba_helper.h"

// -----------------------------------------------------------------------------
// OPTIMIZED RIFF-BASED SONG STRUCTURE
// -----------------------------------------------------------------------------
typedef struct {
    u16 rel_time;
    u8 lane;
    u16 note;
} RiffNote;

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

// -----------------------------------------------------------------------------
// ENGINE STATE
// -----------------------------------------------------------------------------
typedef enum { STATE_MENU, STATE_PLAYING } GameState;
GameState current_state = STATE_MENU;
int selected_song = 0;

#define MAX_NOTES_ON_SCREEN 64
#define MAX_ACTIVE_RIFFS 16
#define HIT_ZONE_Y 135
#define HIT_THRESHOLD 12
#define SCROLL_SPEED 3

typedef struct {
    int lane;
    int y;
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
// GRAPHICS & SOUND HELPERS
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
    REG_SOUNDCNT_X = 0x80;
    REG_SOUNDCNT_L = 0x77;
    REG_SOUNDCNT_H = 0x02;
    REG_DISPLAYCONTROL = VIDEO_MODE_0 | OBJ_ENABLE | OBJ_MAP_1D;

    // Palette Reordered:
    // 1: White (Lane 0 - Open)
    // 2: Green (Lane 1)
    // 3: Red (Lane 2)
    // 4: Yellow (Lane 3)
    // 5: Blue (Lane 4)
    OBJ_PALETTE[1] = RGB(31, 31, 31);
    OBJ_PALETTE[2] = RGB(0, 31, 0);  
    OBJ_PALETTE[3] = RGB(31, 0, 0);  
    OBJ_PALETTE[4] = RGB(31, 31, 0); 
    OBJ_PALETTE[5] = RGB(0, 0, 31);  
    
    // Fret Gray
    OBJ_PALETTE[6] = RGB(10, 10, 10); 
    // Pitch Colors (Inner Box)
    OBJ_PALETTE[7]  = RGB(31, 31, 31); 
    OBJ_PALETTE[8]  = RGB(0, 20, 20);  
    OBJ_PALETTE[9]  = RGB(20, 0, 20);  
    OBJ_PALETTE[10] = RGB(31, 15, 0);  
    OBJ_PALETTE[11] = RGB(0, 10, 31);  

    u16* sprite_vram = (u16*)OBJ_BASE_ADR;
    for (int l = 0; l < 5; l++) {
        int lane_color = l + 1;
        for (int p = 0; p < 5; p++) {
            int pitch_color = p + 7;
            int base_tile = (l * 5 + p) * 4;
            u16* tile_ptr = &sprite_vram[base_tile * 16];
            for (int y = 0; y < 16; y++) {
                for (int x = 0; x < 8; x++) {
                    int p1_x = x * 2;
                    int p2_x = x * 2 + 1;
                    int c1 = lane_color, c2 = lane_color;
                    if (y >= 4 && y < 12) {
                        if (p1_x >= 4 && p1_x < 12) c1 = pitch_color;
                        if (p2_x >= 4 && p2_x < 12) c2 = pitch_color;
                    }
                    tile_ptr[y * 8 + x] = c1 | (c2 << 4);
                }
            }
        }
    }
    
    // Gray Fret
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
    for (int i = 0; i < 128; i++) OAM[i].attr0 = 160;
    current_state = STATE_PLAYING;
}

void spawn_note(int lane, u16 note) {
    int pitch_grp = get_pitch_group(note);
    int tile_idx = (lane * 5 + pitch_grp) * 4;
    for (int i = 0; i < MAX_NOTES_ON_SCREEN; i++) {
        if (!active_notes[i].active) {
            active_notes[i].active = 1;
            active_notes[i].lane = lane;
            active_notes[i].tile_idx = tile_idx;
            active_notes[i].y = -16;
            return;
        }
    }
}

void update_playing(u16 keys_pressed, u16 grip_state) {
    frame_count++;
    const Song* song = &playlist[selected_song];

    // 1. Check if new riff instances should start
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

    // 2. Process active riffs and spawn notes
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

    // 3. Move notes
    for (int i = 0; i < MAX_NOTES_ON_SCREEN; i++) {
        if (active_notes[i].active) {
            active_notes[i].y += SCROLL_SPEED;
            if (active_notes[i].y > 160) active_notes[i].active = 0;
        }
    }

    // 4. Strumming logic
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
                        play_note(NOTE_C4); 
                    }
                }
            }
        }
    }

    // 5. Draw notes
    for (int i = 0; i < MAX_NOTES_ON_SCREEN; i++) {
        if (active_notes[i].active) {
            int x = 50 + active_notes[i].lane * 35;
            OAM[i].attr0 = (active_notes[i].y & 0xFF);
            OAM[i].attr1 = (x & 0x1FF) | (1 << 14);
            OAM[i].attr2 = active_notes[i].tile_idx;
        } else {
            OAM[i].attr0 = 160;
        }
    }

    // 6. Draw frets
    for (int j = 0; j < 5; j++) {
        int x = 50 + j * 35;
        int pressed = 0;
        if (j == 0) pressed = ((grip_state & 0x1E) == 0);
        else pressed = (grip_state & (1 << j));
        
        int obj_idx = MAX_NOTES_ON_SCREEN + j;
        OAM[obj_idx].attr0 = (HIT_ZONE_Y & 0xFF);
        OAM[obj_idx].attr1 = (x & 0x1FF) | (1 << 14);
        OAM[obj_idx].attr2 = pressed ? ((j * 5) * 4) : 100;
    }

    if (keys_pressed & KEY_START) current_state = STATE_MENU;
}

void update_menu(u16 keys_pressed) {
    if (SONG_COUNT > 0) {
        if (keys_pressed & KEY_UP) selected_song = (selected_song - 1 + SONG_COUNT) % SONG_COUNT;
        if (keys_pressed & KEY_DOWN) selected_song = (selected_song + 1) % SONG_COUNT;
        if (keys_pressed & (KEY_A | KEY_START)) start_song(selected_song);
    }
    for (int i = 0; i < 128; i++) OAM[i].attr0 = 160;
    if (SONG_COUNT > 0) {
        OAM[0].attr0 = 40 + (selected_song * 20);
        OAM[0].attr1 = 40 | (1 << 14);
        OAM[0].attr2 = 0;
    }
}

int main(void) {
    init_system();
    u16 current_keys = 0, prev_keys = 0, grip_state = 0;
    while(1) {
        vsync();
        prev_keys = current_keys;
        current_keys = ~REG_KEYINPUT;
        u16 keys_pressed = current_keys & ~prev_keys;
        grip_state = ~(*GRIP_ADDR);
        if (current_state == STATE_MENU) update_menu(keys_pressed);
        else update_playing(keys_pressed, grip_state);
    }
    return 0;
}
