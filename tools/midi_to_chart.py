
import struct
import sys
import os

# GBA Note Mapping (2048 - (131072 / freq_hz))
# We'll map MIDI notes 21 (A0) to 108 (C8)
MIDI_NOTES = {
    60: "NOTE_C4", 61: "NOTE_CS4", 62: "NOTE_D4", 63: "NOTE_DS4", 64: "NOTE_E4",
    65: "NOTE_F4", 66: "NOTE_FS4", 67: "NOTE_G4", 68: "NOTE_GS4", 69: "NOTE_A4",
    70: "NOTE_AS4", 71: "NOTE_B4", 72: "NOTE_C5", 73: "NOTE_CS5", 74: "NOTE_D5",
    75: "NOTE_DS5", 76: "NOTE_E5", 77: "NOTE_F5", 78: "NOTE_FS5", 79: "NOTE_G5",
    80: "NOTE_GS5", 81: "NOTE_A5", 82: "NOTE_AS5", 83: "NOTE_B5", 84: "NOTE_C6",
    85: "NOTE_CS6", 86: "NOTE_D6", 87: "NOTE_DS6", 88: "NOTE_E6", 89: "NOTE_F6",
    90: "NOTE_FS6", 91: "NOTE_G6", 92: "NOTE_GS6", 93: "NOTE_A6", 94: "NOTE_AS6",
    95: "NOTE_B6", 96: "NOTE_C7"
}

def parse_variable_length(data, offset):
    val = 0
    while True:
        byte = data[offset]
        offset += 1
        val = (val << 7) | (byte & 0x7F)
        if not (byte & 0x80):
            break
    return val, offset

def parse_midi(filename):
    with open(filename, 'rb') as f:
        data = f.read()

    # MIDI Header (MThd)
    header, hlen, mtype, ntracks, division = struct.unpack('>4sIHHH', data[:14])
    if header != b'MThd': return None
    
    offset = 14
    notes = []
    
    # Simple single-track or multi-track parser (merging to one chart)
    for _ in range(ntracks):
        header, tlen = struct.unpack('>4sI', data[offset:offset+8])
        offset += 8
        track_end = offset + tlen
        abs_time = 0
        running_status = None
        
        while offset < track_end:
            delta, offset = parse_variable_length(data, offset)
            abs_time += delta
            
            status = data[offset]
            if status < 0x80:
                status = running_status
            else:
                offset += 1
                running_status = status
            
            msg_type = status & 0xF0
            channel = status & 0x0F
            
            if msg_type == 0x90: # Note On
                pitch, velocity = data[offset:offset+2]
                offset += 2
                if velocity > 0:
                    # Map to frames (assuming 60fps and 120bpm for now)
                    # frame = (abs_time / division) * (60 / (bpm/60)) = (abs_time/division) * (3600/bpm)
                    # We'll use a constant for simplicity, ideally we'd parse MIDI tempo
                    time_frames = int((abs_time / division) * 30) # Adjust multiplier for tempo
                    if pitch in MIDI_NOTES:
                        lane = pitch % 5 # Simple lane mapping: 0, 1, 2, 3, 4
                        notes.append((time_frames, lane, MIDI_NOTES[pitch]))
            elif msg_type == 0x80: # Note Off
                offset += 2
            elif msg_type == 0xB0 or msg_type == 0xE0: # Control change / Pitch bend
                offset += 2
            elif msg_type == 0xC0 or msg_type == 0xD0: # Patch change / Aftertouch
                offset += 1
            elif status == 0xFF: # Meta-event
                meta_type = data[offset]
                offset += 1
                meta_len, offset = parse_variable_length(data, offset)
                offset += meta_len
            elif status == 0xF0 or status == 0xF7: # SysEx
                meta_len, offset = parse_variable_length(data, offset)
                offset += meta_len
    
    return sorted(notes)

def main():
    songs_dir = sys.argv[1]
    output_file = sys.argv[2]
    
    with open(output_file, 'w') as out:
        out.write('#include "gba_helper.h"\n')
        out.write('typedef struct { u32 time; u8 lane; u16 note; } ChartNote;\n')
        out.write('typedef struct { const char* title; const ChartNote* chart; } Song;\n\n')
        
        playlist = []
        for file in os.listdir(songs_dir):
            if file.endswith('.mid'):
                name = os.path.splitext(file)[0]
                notes = parse_midi(os.path.join(songs_dir, file))
                if not notes: continue
                
                out.write(f'const ChartNote chart_{name}[] = {{\n')
                for t, l, n in notes:
                    out.write(f'    {{{t}, {l}, {n}}},\n')
                out.write('    {0, 0, 0}\n};\n\n')
                playlist.append((name, file))
        
        out.write('const Song playlist[] = {\n')
        for name, file in playlist:
            out.write(f'    {{"{name}", chart_{name}}},\n')
        out.write('};\n\n')
        out.write(f'const int SONG_COUNT = {len(playlist)};\n')

if __name__ == '__main__':
    main()
