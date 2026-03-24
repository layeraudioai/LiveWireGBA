# LiveWireGBA
-LiveWire is an upcoming game based off Guitar Hero that uses a new controller with 7 frets 3 strings (21 frets) and real guitar strings. 
--
-The strings (21 frets) join pads when pressed as they are conductive and carry the signal of the song through them at the same time. 
--
-This repsoitory is for the GBA de-make using the DS Guitar Hero Grip. 
--
-WIP
--
 How it works:
   - songs/ Directory: Place your MIDI (.mid) files here.
   - tools/midi_to_chart.py: A custom, zero-dependency Python script that parses MIDI files and converts them into C ChartNote triples. It automatically:
     - Maps MIDI ticks to GBA frames.
     - Maps MIDI pitch to the 5 lanes (pitch % 5).
     - Maps MIDI pitch to the corresponding NOTE_ frequency macro.
   - Makefile Integration: The Makefile now detects changes in the songs/ folder and automatically regenerates src/playlist.c before compiling the ROM.
   - Menu Support: The game dynamically loads whatever songs are in the playlist. If no songs are found, the menu remains empty.

  To add a song:
   1. Drop a MIDI file into gba-rom-template/songs/.
   2. Run make.
   3. The new song will automatically appear in the GBA's selection menu.
