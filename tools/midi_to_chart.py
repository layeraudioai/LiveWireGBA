
import struct
import sys
import os
import re

# GBA Note Mapping
MIDI_NOTES = {
    i: f"NOTE_{['C','CS','D','DS','E','F','FS','G','GS','A','AS','B'][i%12]}{i//12 - 1}"
    for i in range(12, 128)
}

def get_note_macro(pitch):
    if pitch < 60: return "NOTE_C4"
    if pitch > 96: return "NOTE_C7"
    return MIDI_NOTES.get(pitch, "NOTE_C4")

def sanitize(name):
    return re.sub(r'[^a-zA-Z0-9_]', '_', name)

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

    if len(data) < 14: return None
    header, hlen, mtype, ntracks, division = struct.unpack('>4sIHHH', data[:14])
    if header != b'MThd': return None
    
    offset = 14
    all_raw_notes = []
    tempo_map = [(0, 500000)]
    
    ticks_per_quarter = 480
    if not (division & 0x8000):
        ticks_per_quarter = division

    for _ in range(ntracks):
        if offset + 8 > len(data): break
        header, tlen = struct.unpack('>4sI', data[offset:offset+8])
        offset += 8
        track_end = offset + tlen
        abs_tick = 0
        running_status = None
        
        while offset < track_end and offset < len(data):
            delta, offset = parse_variable_length(data, offset)
            abs_tick += delta
            
            if offset >= len(data): break
            status = data[offset]
            if status < 0x80: status = running_status
            else:
                offset += 1
                if status < 0xF0: running_status = status
            
            msg_type = status & 0xF0
            if msg_type == 0x90:
                pitch, velocity = data[offset:offset+2]
                offset += 2
                if velocity > 0: all_raw_notes.append((abs_tick, pitch))
            elif msg_type == 0x80 or msg_type == 0xA0 or msg_type == 0xB0 or msg_type == 0xE0: offset += 2
            elif msg_type == 0xC0 or msg_type == 0xD0: offset += 1
            elif status == 0xFF:
                meta_type = data[offset]
                offset += 1
                meta_len, offset = parse_variable_length(data, offset)
                if meta_type == 0x51:
                    tempo = struct.unpack('>I', b'\x00' + data[offset:offset+3])[0]
                    tempo_map.append((abs_tick, tempo))
                offset += meta_len
            elif status == 0xF0 or status == 0xF7:
                meta_len, offset = parse_variable_length(data, offset)
                offset += meta_len
        offset = track_end

    if not all_raw_notes: return None
    tempo_map.sort()
    all_raw_notes.sort()

    # Determine Global Pitch Range
    pitches = [n[1] for n in all_raw_notes]
    min_p, max_p = min(pitches), max(pitches)
    p_range = max(1, max_p - min_p)

    # Convert to (frame, lane, note)
    notes_flat = []
    for tick, pitch in all_raw_notes:
        current_tempo = 500000
        total_time_us = 0
        last_tick = 0
        for t_tick, t_tempo in tempo_map:
            if t_tick >= tick: break
            total_time_us += ((t_tick - last_tick) * current_tempo) / ticks_per_quarter
            current_tempo = t_tempo
            last_tick = t_tick
        total_time_us += ((tick - last_tick) * current_tempo) / ticks_per_quarter
        frame = int((total_time_us * 60) / 1000000)
        lane = int(((pitch - min_p) / p_range) * 4.99)
        notes_flat.append((frame, lane, get_note_macro(pitch)))
    
    return sorted(list(set(notes_flat)))

def main():
    songs_dir = sys.argv[1]
    output_file = sys.argv[2]
    
    riff_library = {} # tuple(notes) -> riff_id
    songs = [] # list of (title, list of (start_frame, riff_id))

    if os.path.exists(songs_dir):
        for file in sorted(os.listdir(songs_dir)):
            if not file.endswith('.mid'): continue
            notes = parse_midi(os.path.join(songs_dir, file))
            if not notes: continue

            # Chunking strategy: 480 frames per riff (~8 seconds)
            CHUNK_SIZE = 480 
            song_instances = []
            
            # Group notes into chunks
            chunks = {}
            for f, l, n in notes:
                c_idx = f // CHUNK_SIZE
                if c_idx not in chunks: chunks[c_idx] = []
                chunks[c_idx].append((f % CHUNK_SIZE, l, n))
            
            for c_idx in sorted(chunks.keys()):
                chunk_notes = tuple(sorted(chunks[c_idx]))
                if chunk_notes not in riff_library:
                    riff_library[chunk_notes] = len(riff_library)
                song_instances.append((c_idx * CHUNK_SIZE, riff_library[chunk_notes]))
            
            songs.append((os.path.splitext(file)[0], song_instances))

    with open(output_file, 'w') as out:
        out.write('#include "gba_helper.h"\n\n')
        out.write('typedef struct { u16 rel_time; u8 lane; u16 note; } RiffNote;\n')
        out.write('typedef struct { const RiffNote* notes; u16 note_count; } Riff;\n')
        out.write('typedef struct { u32 start_time; const Riff* riff; } RiffInstance;\n')
        out.write('typedef struct { const char* title; const RiffInstance* instances; u16 instance_count; } Song;\n\n')

        # Write Riff Data
        for chunk_notes, r_id in sorted(riff_library.items(), key=lambda x: x[1]):
            out.write(f'const RiffNote riff_data_{r_id}[] = {{\n')
            for rt, l, n in chunk_notes:
                out.write(f'    {{{rt}, {l}, {n}}},\n')
            out.write('};\n')
            out.write(f'const Riff riff_{r_id} = {{ riff_data_{r_id}, {len(chunk_notes)} }};\n\n')
        
        # Write Song Instances
        for title, instances in songs:
            var_name = sanitize(title)
            out.write(f'const RiffInstance inst_{var_name}[] = {{\n')
            for start, r_id in instances:
                out.write(f'    {{{start}, &riff_{r_id}}},\n')
            out.write('};\n')
            out.write(f'const Song song_{var_name} = {{ "{title}", inst_{var_name}, {len(instances)} }};\n\n')

        out.write('const Song playlist[] = {\n')
        for title, _ in songs:
            out.write(f'    song_{sanitize(title)},\n')
        out.write('};\n\n')
        out.write(f'const int SONG_COUNT = {len(songs)};\n')

if __name__ == '__main__':
    main()
