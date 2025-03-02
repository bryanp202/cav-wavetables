CAV/CAVE (From WAV/WAVE but starts with a C)<br>

This is a modified version of CLOX, a virtual machine designed by Robert Nystrom on https://craftinginterpreters.com/, that specializes in<br>
the design of .WAV files for wavetable synthesizers. See testing folder for examples.<br>

Language Grammar:<br>
<br>
TODO<br>
<br>
Example:<br>
<br>
<br>
Save the following to file "test.cave"<br>
and then run "cave.exe .\test.cave" in desired output directory<br>

/\*<br>
    When providing a string equation for a native function, all globals and functions can be used<br>
    The local variables "index" and "frame" will be accessible<br><br>
    "index": current index in the current frame<br>
        ex: sin(M_PI * 2 * index / FRAME_LEN) will create a sin with 1 period per frame<br><br>
    "frame": current frame being processed<br>
	ex: (sin(M_PI * 2 * index / FRAME_LEN * (1 + 6 * frame / FRAME_MAX)))<br>
	will create a sin wav that goes from 1 period per frame at frame 1<br>
	to 6 periods per a frame at frame 256<br>
\*/<br>
<br>
// Edit Time Domain Call arguments "editWav"<br>
// Target buffer (MAIN_B | AUX1_B), Sample bit size (8 | 16 | 32), Minframe [0-255], Maxframe [1-256], Min index [0-2047]<br>
// Max partial [1-2048], Formula for for y(t) as a string<br>
editWav(MAIN_B, 0, 256, 0, 2048, "sin(2*M_PI*index/FRAME_LEN)");<br>
<br>
// Variables are instantiated with "var" identifier '=' value<br>
for (var i = 1; i < 256; i+=1) {<br>
	// String interpolation in the format "i is: ${i}" will output "i is 1" in the first loop<br>
	// Caution string interpolation can be memory intensive if used in long loops (hundreds of thousands of iterations)<br>
	editWav(MAIN_B, i, 256, 0, 2048, "sin(2*M_PI*index/FRAME_LEN + ${1 + 0.999*(i)/255} * main_t(frame, index))");<br>
}<br>
<br>
// Edit DC function call arguments<br>
// Target buffer, minframe, maxframe, function as a string<br>
editDC(MAIN_B, 0, 256, "0");<br>
<br>
// Export wav function call arguments<br>
// Target buffer (MAIN_B | AUX1_B), Sample Bit size (8 | 16 | 32), Num of Frames ()<br>
exportWav(MAIN_B, "../tests/inception-freq.wav", 32, 256);<br>
<br>
// Edit Phase Call arguments<br>
// Target buffer (MAIN_B | AUX1_B), Sample bit size (8 | 16 | 32), Minframe [0-255], Maxframe [1-256], Min partial [1-1024]<br>
// Max partial [2-1025], Formula for phase as a string<br>
editPhase(MAIN_B, 0, 256, 1, 1025, "M_PI");<br>
<br>
// Normalize each frame to each local max (all frames in target range will have an abs max of 1)<br>
// Target buffer, start frame, end frame<br>
frameNorm(MAIN_B, 0, 256);<br>
<br>

exportWav(MAIN_B, "../tests/inception-freq-with-phase.wav", 32, 256);<br>
exportWav(MAIN_B, "../tests/inception-freq-with-phase-less-frames.wav", 32, 16);<br>
