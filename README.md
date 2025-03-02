CAV/CAVE (From WAV/WAVE but starts with a C)

This is a modified version of CLOX, a virtual machine designed by Robert Nystrom on https://craftinginterpreters.com/, that specializes in
the design of .WAV files for wavetable synthesizers. See testing folder for examples.

Language Grammar:

TODO

Example:


Save the following to file "test.cave"
and then run "cave.exe .\test.cave" in desired output directory

/*
    When providing a string equation for a native function, all globals and functions can be used
    The local variables "index" and "frame" will be accessible
    "index": current index in the current frame
        ex: sin(M_PI * 2 * index / FRAME_LEN) will create a sin with 1 period per frame
	"frame": current frame being processed
	    ex: (sin(M_PI * 2 * index / FRAME_LEN * (1 + 6 * frame / FRAME_MAX)))
		will create a sin wav that goes from 1 period per frame at frame 1
		to 6 periods per a frame at frame 256
*/

// Edit Time Domain Call arguments "editWav"
// Target buffer (MAIN_B | AUX1_B), Sample bit size (8 | 16 | 32), Minframe [0-255], Maxframe [1-256], Min index [0-2047]
// Max partial [1-2048], Formula for for y(t) as a string
editWav(MAIN_B, 0, 256, 0, 2048, "sin(2*M_PI*index/FRAME_LEN)");

// Variables are instantiated with "var" identifier '=' value
for (var i = 1; i < 256; i+=1) {
	// String interpolation in the format "i is: ${i}" will output "i is 1" in the first loop
	// Caution string interpolation can be memory intensive if used in long loops (hundreds of thousands of iterations)
	editWav(MAIN_B, i, 256, 0, 2048, "sin(2*M_PI*index/FRAME_LEN + ${1 + 0.999*(i)/255} * main_t(frame, index))");
}

// Edit DC function call arguments
// Target buffer, minframe, maxframe, function as a string
editDC(MAIN_B, 0, 256, "0");

// Export wav function call arguments
// Target buffer (MAIN_B | AUX1_B), Sample Bit size (8 | 16 | 32), Num of Frames ()
exportWav(MAIN_B, "../tests/inception-freq.wav", 32, 256); 

// Edit Phase Call arguments
// Target buffer (MAIN_B | AUX1_B), Sample bit size (8 | 16 | 32), Minframe [0-255], Maxframe [1-256], Min partial [1-1024]
// Max partial [2-1025], Formula for phase as a string
editPhase(MAIN_B, 0, 256, 1, 1025, "M_PI");

// Normalize each frame to each local max (all frames in target range will have an abs max of 1)
// Target buffer, start frame, end frame
frameNorm(MAIN_B, 0, 256);


exportWav(MAIN_B, "../tests/inception-freq-with-phase.wav", 32, 256);
exportWav(MAIN_B, "../tests/inception-freq-with-phase-less-frames.wav", 32, 16);
