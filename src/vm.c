#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "memory.h"
#include "compiler.h"
#include "debug.h"
#include "vm.h"

// From least sig on right
// CHECK IF TERNARY 0,1,2,3 is faster than a BITARRAY *********************************************
#define FOUR_TYPE_ID() \
    ((IS_STRING(vm.stackTop[-1]) * 3 | (IS_NUMBER(vm.stackTop[-1]) << 1 | IS_BOOL(vm.stackTop[-1]))) << 2) | \
    (IS_STRING(vm.stackTop[-2]) * 3 | IS_NUMBER(vm.stackTop[-2]) << 1 | IS_BOOL(vm.stackTop[-2]))
// End of FOUR_TYPE_ID

VM vm;

/*
    Native Functions
*/
static void runtimeError(const char* format, ...);
static bool call(ObjFunction* function, int argCount);
static InterpretResult run();

// Returns a number value of the clock
// Arity 0
static NativeFnReturn clockNative(int argCount, Value* args) {
    return NATIVE_SUCCESS(NUMBER_VAL((double)clock() / CLOCKS_PER_SEC));
}

// Returns a number value of the length of a string
// Arity 0
static NativeFnReturn lenNative(int argCount, Value* args) {
    if (!IS_STRING(args[0])) {
        runtimeError("Can only us len() on strings");
        return NATIVE_FAIL();
    }
    return NATIVE_SUCCESS(NUMBER_VAL(AS_STRING(args[0])->length));
}

// Returns enum value of the Value* type
// Arity 1
static NativeFnReturn typeNative(int argCount, Value* args) {
    int typeCode = args[0].type;
    if (IS_OBJ(args[0])) {
        typeCode += AS_OBJ(args[0])->type;
    }
    return NATIVE_SUCCESS(NUMBER_VAL(typeCode));
}

// Round
// Arity 1
static NativeFnReturn roundNative(int argCount, Value *args) {
    if (!IS_NUMBER(args[0])) {
        runtimeError("round: Expect round(number)");
        return NATIVE_FAIL();
    }
    return NATIVE_SUCCESS(NUMBER_VAL(round(AS_NUMBER(args[0]))));
}

// Sqrt
// Arity 1
static NativeFnReturn sqrtNative(int argCount, Value *args) {
    if (!IS_NUMBER(args[0])) {
        runtimeError("sqrt: Expect sqrt(number)");
        return NATIVE_FAIL();
    }
    return NATIVE_SUCCESS(NUMBER_VAL(sqrt(AS_NUMBER(args[0]))));
}

// pow
// Arity 2
static NativeFnReturn powNative(int argCount, Value *args) {
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) {
        runtimeError("pow: Expect pow(number, number)");
        return NATIVE_FAIL();
    }
    return NATIVE_SUCCESS(NUMBER_VAL(pow(AS_NUMBER(args[0]), AS_NUMBER(args[1]))));
}

// Sin
// Arity 1
static NativeFnReturn sinNative(int argCount, Value* args) {
    if (!IS_NUMBER(args[0])) {
        runtimeError("sin: Expect sin(number)");
        return NATIVE_FAIL();
    }
    return NATIVE_SUCCESS(NUMBER_VAL(sin(AS_NUMBER(args[0]))));
}

// Cos
// Arity 1
static NativeFnReturn cosNative(int argCount, Value* args) {
    if (!IS_NUMBER(args[0])) {
        runtimeError("cos: Expect cos(number)");
        return NATIVE_FAIL();
    }
    return NATIVE_SUCCESS(NUMBER_VAL(cos(AS_NUMBER(args[0]))));
}

// tan
// Arity 1
static NativeFnReturn tanNative(int argCount, Value* args) {
    if (!IS_NUMBER(args[0])) {
        runtimeError("tan: Expect tan(number)");
        return NATIVE_FAIL();
    }
    return NATIVE_SUCCESS(NUMBER_VAL(tan(AS_NUMBER(args[0]))));
}

// arcsin
// Arity 1
static NativeFnReturn asinNative(int argCount, Value* args) {
    if (!IS_NUMBER(args[0])) {
        runtimeError("asin: Expect asin(number)");
        return NATIVE_FAIL();
    }
    return NATIVE_SUCCESS(NUMBER_VAL(asin(AS_NUMBER(args[0]))));
}

// arccos
// Arity 1
static NativeFnReturn acosNative(int argCount, Value* args) {
    if (!IS_NUMBER(args[0])) {
        runtimeError("acos: Expect acos(number)");
        return NATIVE_FAIL();
    }
    return NATIVE_SUCCESS(NUMBER_VAL(acos(AS_NUMBER(args[0]))));
}

// arctan
// Arity 1
static NativeFnReturn atanNative(int argCount, Value* args) {
    if (!IS_NUMBER(args[0])) {
        runtimeError("atan: Expect atan(number)");
        return NATIVE_FAIL();
    }
    return NATIVE_SUCCESS(NUMBER_VAL(atan(AS_NUMBER(args[0]))));
}

// arctan2
// Arity 2
static NativeFnReturn atan2Native(int argCount, Value* args) {
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) {
        runtimeError("atan2: Expect atan2(number, number)");
        return NATIVE_FAIL();
    }
    return NATIVE_SUCCESS(NUMBER_VAL(atan2(AS_NUMBER(args[0]), AS_NUMBER(args[1]))));
}

// Saw wave
// Arity 1
static NativeFnReturn sawNative(int argCount, Value* args) {
    if (!IS_NUMBER(args[0])) {
        runtimeError("saw: Expect saw(number)");
        return NATIVE_FAIL();
    }
    Value result = NUMBER_VAL(1 - 2 * fmod(AS_NUMBER(args[0]), 1));
    return NATIVE_SUCCESS(result);
}

/* Randomness */

// Rand
// Arity 0
static NativeFnReturn randNative(int argCount, Value* args) {
    return NATIVE_SUCCESS(NUMBER_VAL(rand()));
}

// Random shared by a frame
static NativeFnReturn randfNative(int argCount, Value* args) {
    if (!IS_NUMBER(args[0])) {
        runtimeError("randf: Expect randf(number)");
        return NATIVE_FAIL();
    }
    int index = AS_NUMBER(args[0]);
    if (index < 0 || index >= WAVETABLE_MAX_FRAMES) {
        runtimeError("randf: Frame index out of bounds");
        return NATIVE_FAIL();
    }
    return NATIVE_SUCCESS(NUMBER_VAL(vm.wavetable.randf[index]));
}

// Random shared by an index
static NativeFnReturn randiNative(int argCount, Value* args) {
    if (!IS_NUMBER(args[0])) {
        runtimeError("randi: Expect randf(number)");
        return NATIVE_FAIL();
    }
    int index = AS_NUMBER(args[0]);
    if (index < 0 || index >= WAVETABLE_FRAME_LEN) {
        runtimeError("randi: Frame out of bounds");
        return NATIVE_FAIL();
    }
    return NATIVE_SUCCESS(NUMBER_VAL(vm.wavetable.randi[index]));
}

// Wavetable functions //
// Get value at MAIN_BUFFER_TIME (frame,index)
// Arity 2
static NativeFnReturn mainTimeNative(int argCount, Value* args) {
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) {
        runtimeError("main_t: Expect main_t(number, number)");
        return NATIVE_FAIL();
    }
    // Get the frame and bind it to the range [0,WAVETABLE_MAX_FRAMES)
    const int frame = ((int)AS_NUMBER(args[0])) & (WAVETABLE_MAX_FRAMES - 1);
    // Get the index and bind it to the range [0,WAVETABLE_FRAME_LEN)

    // Index is linearly interpolated
    const double rawIndex = AS_NUMBER(args[1]);
    const int indexLower = ((int)rawIndex) & (WAVETABLE_FRAME_LEN - 1);
    const int indexHigher = (indexLower + 1) & (WAVETABLE_FRAME_LEN - 1);

    // Linearly interpolate result
    const double indexRatio = rawIndex - (int)rawIndex;
    Value result = NUMBER_VAL(vm.wavetable.main_time[frame * WAVETABLE_FRAME_LEN + indexLower] * (1 - indexRatio)
                            + vm.wavetable.main_time[frame * WAVETABLE_FRAME_LEN + indexHigher] * (indexRatio));
    return NATIVE_SUCCESS(result);
}

// Get value at AUX1_BUFFER_TIME (frame,index)
// Arity 2
static NativeFnReturn aux1TimeNative(int argCount, Value* args) {
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) {
        runtimeError("aux1_t: Expect aux1_t(number, number)");
        return NATIVE_FAIL();
    }
    // Get the frame and bind it to the range [0,WAVETABLE_MAX_FRAMES)
    const int frame = ((int)AS_NUMBER(args[0])) & (WAVETABLE_MAX_FRAMES - 1);
    // Get the index and bind it to the range [0,WAVETABLE_FRAME_LEN)

    // Index is linearly interpolated
    const double rawIndex = AS_NUMBER(args[1]);
    const int indexLower = ((int)rawIndex) & (WAVETABLE_FRAME_LEN - 1);
    const int indexHigher = (indexLower + 1) & (WAVETABLE_FRAME_LEN - 1);

    // Linearly interpolate result
    const double indexRatio = rawIndex - (int)rawIndex;
    Value result = NUMBER_VAL(vm.wavetable.aux1_time[frame * WAVETABLE_FRAME_LEN + indexLower] * (1 - indexRatio)
                            + vm.wavetable.aux1_time[frame * WAVETABLE_FRAME_LEN + indexHigher] * (indexRatio));
    return NATIVE_SUCCESS(result);
}

// Check if inputted value is a proper buffer type
// Assumes provided value is a number
static bool invalidBuffType(Value buffer_type) {
    BufferType type = (BufferType)(int)AS_NUMBER(buffer_type);
    if (type < BUFFER_MAIN || type >= BUFFER_MAX) {
        return true;
    }
    return false;
}

// Normalize based on frame local max
static NativeFnReturn frameNormalizeNative(int argCount, Value* args) {
    // Type check
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2])) {
        runtimeError("frameNorm: expect editDC(number, number, number)");
        return NATIVE_FAIL();
    }
    // Check buffer type
    if (invalidBuffType(args[0])) {
        runtimeError("frameNorm: Invalid buffer type");
        return NATIVE_FAIL();
    }
    // Check bounds minFrame
    if (AS_NUMBER(args[1]) < 0 || AS_NUMBER(args[1]) > 255) {
        runtimeError("frameNorm: minFrame must be between [0, 255]");
        return NATIVE_FAIL();
    }
    // Check bounds maxFrame
    if (AS_NUMBER(args[2]) < 1 || AS_NUMBER(args[2]) > 256 || AS_NUMBER(args[2]) <= AS_NUMBER(args[1])) {
        runtimeError("frameNorm: maxFrame must be between [1, 256] and larger than minFrame");
        return NATIVE_FAIL();
    }

    BufferType bufferType = (BufferType)(int)AS_NUMBER(args[0]);
    int minFrame = (int)AS_NUMBER(args[1]);
    int maxFrame = (int)AS_NUMBER(args[2]);

    normalizeByFrame(&vm.wavetable, bufferType, minFrame, maxFrame);

    return NATIVE_SUCCESS(NIL_VAL);
}

// Import .wav file
// Arity 2
static NativeFnReturn wavImportNative(int argCount, Value* args) {
    if (!IS_STRING(args[1]) || !IS_NUMBER(args[0])) {
        runtimeError("importWav: Expect importWav(number, string)");
        return NATIVE_FAIL();
    }
    // Check buffer type
    if (invalidBuffType(args[0])) {
        runtimeError("importWav: Invalid buffer type");
        return NATIVE_FAIL();
    }
    // Import the wave
    bool importSuccess = importWav(&vm.wavetable, (BufferType)(int)AS_NUMBER(args[0]), AS_CSTRING(args[1]));
    if (!importSuccess) {
        runtimeError("importWav: Failed to import .wav file");
        return NATIVE_FAIL();
    }
    return NATIVE_SUCCESS(NIL_VAL);
}

// Export wavetable to .wav file
// Arity 4
static NativeFnReturn wavExportNative(int argCount, Value* args) {
    if (!IS_NUMBER(args[3]) || !IS_NUMBER(args[2]) || !IS_STRING(args[1]) || !IS_NUMBER(args[0])) {
        runtimeError("exportWav: Expect exportWav(number, string, number, number)");
        return NATIVE_FAIL();
    }
    // Check buffer type
    if (invalidBuffType(args[0])) {
        runtimeError("exportWav: Invalid buffer type");
        return NATIVE_FAIL();
    }
    // Check sample_size
    if (AS_NUMBER(args[2]) != 8 && AS_NUMBER(args[2]) != 16 && AS_NUMBER(args[2]) != 32) {
        runtimeError("exportWav: Expect sample_size to be 8, 16, or 32");
        return NATIVE_FAIL();
    }
    // Check num_frames
    if (AS_NUMBER(args[3]) <= 0 || AS_NUMBER(args[3]) > WAVETABLE_MAX_FRAMES) {
        runtimeError("exportWav: Expect num_frames to be in range [1,256]");
        return NATIVE_FAIL();
    }
    // Import the wave
    bool exportSuccess = exportWav(&vm.wavetable, (BufferType)(int)AS_NUMBER(args[0]), AS_CSTRING(args[1]), (int)AS_NUMBER(args[2]), (int)AS_NUMBER(args[3]));
    // Check if export worked
    if (!exportSuccess) {
        runtimeError("exportWav: Failed to export .wav file");
        return NATIVE_FAIL();
    }
    return NATIVE_SUCCESS(NIL_VAL);
}

// Checks variable type and ranges
// Returns true if it fails
static bool checkEditArgs(const char* funcName, Value* args, int minIndex, int maxIndex) {
    // Check types
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2])
        || !IS_NUMBER(args[3]) || !IS_NUMBER(args[4]) || !IS_STRING(args[5])) {
        runtimeError("%s: Expect %s(buffer, minFrame, maxFrame, minIndex, maxIndex, function)", funcName, funcName);
        return true;
    }
    // Check buffer type
    if (invalidBuffType(args[0])) {
        runtimeError("%s: Invalid buffer type", funcName);
        return true;
    }
    // Check bounds minFrame
    if (AS_NUMBER(args[1]) < 0 || AS_NUMBER(args[1]) > 255) {
        runtimeError("%s: minFrame must be between [0, 255]", funcName);
        return true;
    }
    // Check bounds maxFrame
    if (AS_NUMBER(args[2]) < 1 || AS_NUMBER(args[2]) > 256 || AS_NUMBER(args[2]) <= AS_NUMBER(args[1])) {
        runtimeError("%s: maxFrame must be between [1, 256] and larger than minFrame", funcName);
        return true;
    }
    // Check bounds minIndex
    if (AS_NUMBER(args[3]) < minIndex || AS_NUMBER(args[3]) > (maxIndex - 1)) {
        runtimeError("%s: minIndex must be between [%d, %d]", funcName, minIndex, maxIndex - 1);
        return true;
    }
    // Check maxIndex
    if (AS_NUMBER(args[4]) < (minIndex + 1) || AS_NUMBER(args[4]) > maxIndex || AS_NUMBER(args[4]) <= AS_NUMBER(args[3])) {
        runtimeError("%s: maxIndex must be between [%d, %d] and larger than minIndex", funcName, minIndex + 1, maxIndex);
        return true;
    }
    return false;
}

// Edit wavetable buffer, time domain
// (buffer 0, minFrame 1, maxFrame 2, minIndex 3, maxIndex 4, function 5)
// Arity 6
static NativeFnReturn editWaveNative(int argCount, Value* args) {
    if (checkEditArgs("editWav", args, 0, 2048)) {
        return NATIVE_FAIL();
    }

    // Toggle to time mode
    BufferType buffer_type = (BufferType)(int)AS_NUMBER(args[0]);
    setTimeMode(&vm.wavetable, buffer_type, true);

    // Compile function
    ObjFunction* waveFunction = runtimeCompile(AS_CSTRING(args[5]));
    // Check if compile failed
    if (waveFunction == NULL) {
        runtimeError("editWav: Failed wave function compiling");
        return NATIVE_FAIL();
    }

    // Push function
    push(OBJ_VAL(waveFunction));
    // Push frame arg location
    push(NUMBER_VAL(0));
    // Push index arg location
    push(NUMBER_VAL(0));
    // Set up call window
    call(waveFunction, 2);

    // Frame and Index pointer locations
    Value* frame_loc = vm.stackTop - 2;
    Value* index_loc = vm.stackTop - 1;
    // IP counter reset point
    uint8_t* reset_ip = vm.frames[vm.frameCount - 1].function->chunk.code;
    // Run and extract from waveFunction
    double* time_buffer = getTimeBuffer(&vm.wavetable, buffer_type);
    const int minFrame = (int)AS_NUMBER(args[1]);
    const int maxFrame = (int)AS_NUMBER(args[2]);
    const int minIndex = (int)AS_NUMBER(args[3]);
    const int maxIndex = (int)AS_NUMBER(args[4]);
    for (int frame = minFrame; frame < maxFrame; frame++) {
        // Edit current frame
        frame_loc->as.number = frame;
        for (int index = minIndex; index < maxIndex; index++) {
            // Reset frame->ip
            vm.frames[vm.frameCount - 1].ip = reset_ip;
            // Edit current index
            index_loc->as.number = index;

            // Run
            InterpretResult result = run();
            // Check if it ran okay
            if (result != INTERPRET_OK) {
                return NATIVE_FAIL();
            }

            // Update buffer
            time_buffer[frame * WAVETABLE_FRAME_LEN + index] = AS_NUMBER(vm.output);
        }
    }
    // Tear down call
    CallFrame frame = vm.frames[vm.frameCount-- - 1];
    vm.stackTop = frame.slots;
    // Free Memory
    freeChunk(&waveFunction->chunk);

    return NATIVE_SUCCESS(NIL_VAL);
}

// Edit wavetable buffer, freq domain, DC only
// (buffer 0, minFrame 1, maxFrame 2, function 3)
// Arity 6
static NativeFnReturn editDCNative(int argCount, Value* args) {
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2]) || !IS_STRING(args[3])) {
        runtimeError("editDC: expect editDC(number, number, number, string)");
        return NATIVE_FAIL();
    }
    // Check buffer type
    if (invalidBuffType(args[0])) {
        runtimeError("exportWav: Invalid buffer type");
        return NATIVE_FAIL();
    }
    // Check bounds minFrame
    if (AS_NUMBER(args[1]) < 0 || AS_NUMBER(args[1]) > 255) {
        runtimeError("editDC: minFrame must be between [0, 255]");
        return NATIVE_FAIL();
    }
    // Check bounds maxFrame
    if (AS_NUMBER(args[2]) < 1 || AS_NUMBER(args[2]) > 256 || AS_NUMBER(args[2]) <= AS_NUMBER(args[1])) {
        runtimeError("editDC: maxFrame must be between [1, 256] and larger than minFrame");
        return NATIVE_FAIL();
    }

    // Toggle to freq mode
    BufferType buffer_type = (BufferType)(int)AS_NUMBER(args[0]);
    setTimeMode(&vm.wavetable, buffer_type, false);

    // Compile function
    ObjFunction* waveFunction = runtimeCompile(AS_CSTRING(args[3]));
    // Check if compile failed
    if (waveFunction == NULL) {
        runtimeError("editDC: Failed wave function compiling");
        return NATIVE_FAIL();
    }

    // Push function
    push(OBJ_VAL(waveFunction));
    // Push frame arg location
    push(NUMBER_VAL(0));
    // Push index arg location
    push(NUMBER_VAL(0));
    // Set up call window
    call(waveFunction, 2);

    // Frame and Index pointer locations
    Value* frame_loc = vm.stackTop - 2;
    Value* index_loc = vm.stackTop - 1;
    // IP counter reset point
    uint8_t* reset_ip = vm.frames[vm.frameCount - 1].function->chunk.code;
    // Run and extract from waveFunction
    _Complex double* freq_buffer = getFreqBuffer(&vm.wavetable, buffer_type);
    const int minFrame = (int)AS_NUMBER(args[1]);
    const int maxFrame = (int)AS_NUMBER(args[2]);
    for (int frame = minFrame; frame < maxFrame; frame++) {
        // Edit current frame
        frame_loc->as.number = frame;
        // Reset frame->ip
         vm.frames[vm.frameCount - 1].ip = reset_ip;

        // Run
        InterpretResult result = run();
        // Check if it ran okay
        if (result != INTERPRET_OK) {
            return NATIVE_FAIL();
        }

        // Update buffer
        freq_buffer[frame * WAVETABLE_FRAME_LEN] = AS_NUMBER(vm.output) * WAVETABLE_FRAME_LEN;
    }
    // Tear down call
    CallFrame frame = vm.frames[vm.frameCount-- - 1];
    vm.stackTop = frame.slots;
    // Free Memory
    freeChunk(&waveFunction->chunk);

    return NATIVE_SUCCESS(NIL_VAL);
}

// Edit wavetable buffer, freq domain
// (buffer 0, minFrame 1, maxFrame 2, minIndex 3, maxIndex 4, function 5)
// Arity 6
static NativeFnReturn editFreqNative(int argCount, Value* args) {
    if (checkEditArgs("editFreq", args, 1, 1025)) {
        return NATIVE_FAIL();
    }

    // Toggle to freq mode
    BufferType buffer_type = (BufferType)(int)AS_NUMBER(args[0]);
    setTimeMode(&vm.wavetable, buffer_type, false);

    // Compile function
    ObjFunction* waveFunction = runtimeCompile(AS_CSTRING(args[5]));
    // Check if compile failed
    if (waveFunction == NULL) {
        runtimeError("editFreq: Failed wave function compiling");
        return NATIVE_FAIL();
    }

    // Push function
    push(OBJ_VAL(waveFunction));
    // Push frame arg location
    push(NUMBER_VAL(0));
    // Push index arg location
    push(NUMBER_VAL(0));
    // Set up call window
    call(waveFunction, 2);

    // Frame and Index pointer locations
    Value* frame_loc = vm.stackTop - 2;
    Value* index_loc = vm.stackTop - 1;
    // IP counter reset point
    uint8_t* reset_ip = vm.frames[vm.frameCount - 1].function->chunk.code;
    // Run and extract from waveFunction
    _Complex double* freq_buffer = getFreqBuffer(&vm.wavetable, buffer_type);
    const int minFrame = (int)AS_NUMBER(args[1]);
    const int maxFrame = (int)AS_NUMBER(args[2]);
    const int minIndex = (int)AS_NUMBER(args[3]);
    const int maxIndex = (int)AS_NUMBER(args[4]);
    for (int frame = minFrame; frame < maxFrame; frame++) {
        // Edit current frame
        frame_loc->as.number = frame;
        for (int index = minIndex; index < maxIndex; index++) {
            // Reset frame->ip
            vm.frames[vm.frameCount - 1].ip = reset_ip;
            // Edit current index
            index_loc->as.number = index;

            // Run
            InterpretResult result = run();
            // Check if it ran okay
            if (result != INTERPRET_OK) {
                return NATIVE_FAIL();
            }

            // Update buffer
            freq_buffer[frame * WAVETABLE_FRAME_LEN + WAVETABLE_FRAME_LEN - index] = -AS_NUMBER(vm.output) * WAVETABLE_FRAME_LEN * I;
            freq_buffer[frame * WAVETABLE_FRAME_LEN + index] = AS_NUMBER(vm.output) * WAVETABLE_FRAME_LEN * I;
        }
    }
    // Tear down call
    CallFrame frame = vm.frames[vm.frameCount-- - 1];
    vm.stackTop = frame.slots;
    // Free Memory
    freeChunk(&waveFunction->chunk);

    return NATIVE_SUCCESS(NIL_VAL);
}

// Edit wavetable buffer, freq domain, Phase
// (buffer 0, minFrame 1, maxFrame 2, minIndex 3, maxIndex 4, function 5)
// Function values should range between [0,2*M_PI)
// Arity 6
static NativeFnReturn editPhaseNative(int argCount, Value* args) {
    if (checkEditArgs("editPhase", args, 1, 1025)) {
        return NATIVE_FAIL();
    }

    // Toggle to freq mode
    BufferType buffer_type = (BufferType)(int)AS_NUMBER(args[0]);
    setTimeMode(&vm.wavetable, buffer_type, false);

    // Compile function
    ObjFunction* waveFunction = runtimeCompile(AS_CSTRING(args[5]));
    // Check if compile failed
    if (waveFunction == NULL) {
        runtimeError("editPhase: Failed wave function compiling");
        return NATIVE_FAIL();
    }

    // Push function
    push(OBJ_VAL(waveFunction));
    // Push frame arg location
    push(NUMBER_VAL(0));
    // Push index arg location
    push(NUMBER_VAL(0));
    // Set up call window
    call(waveFunction, 2);

    // Frame and Index pointer locations
    Value* frame_loc = vm.stackTop - 2;
    Value* index_loc = vm.stackTop - 1;
    // IP counter reset point
    uint8_t* reset_ip = vm.frames[vm.frameCount - 1].function->chunk.code;
    // Run and extract from waveFunction
    _Complex double* freq_buffer = getFreqBuffer(&vm.wavetable, buffer_type);
    const int minFrame = (int)AS_NUMBER(args[1]);
    const int maxFrame = (int)AS_NUMBER(args[2]);
    const int minIndex = (int)AS_NUMBER(args[3]);
    const int maxIndex = (int)AS_NUMBER(args[4]);
    for (int frame = minFrame; frame < maxFrame; frame++) {
        // Edit current frame
        frame_loc->as.number = frame;
        for (int index = minIndex; index < maxIndex; index++) {
            // Reset frame->ip
            vm.frames[vm.frameCount - 1].ip = reset_ip;
            // Edit current index
            index_loc->as.number = index;

            // Calculate index low and high
            const int index_low = frame * WAVETABLE_FRAME_LEN + index;
            const int index_high = frame * WAVETABLE_FRAME_LEN + WAVETABLE_FRAME_LEN - index;

            // Calculate magnitude
            double _Complex raw_value = freq_buffer[index_low];
            const double magnitude = csqrt(pow(creal(raw_value), 2) + pow(cimag(raw_value), 2));

            // Run
            InterpretResult result = run();
            // Check if it ran okay
            if (result != INTERPRET_OK) {
                return NATIVE_FAIL();
            }

            // Update buffer
            const double phase = AS_NUMBER(vm.output);
            freq_buffer[index_low] = -sin(phase) * magnitude - cos(phase) * magnitude * I;
            freq_buffer[index_high] = -sin(phase) * magnitude + cos(phase) * magnitude * I;
        }
    }
    // Tear down call
    CallFrame frame = vm.frames[vm.frameCount-- - 1];
    vm.stackTop = frame.slots;
    // Free Memory
    freeChunk(&waveFunction->chunk);

    return NATIVE_SUCCESS(NIL_VAL);
}
// End of Native Functions

/*
    Native variables
*/
static void makeNativeVariable(const char* name, Value value) {
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    push(value);
    tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

static void defineNativeVariables() {
    /*
        Math concepts
    */
    // Pi
    makeNativeVariable("M_PI", NUMBER_VAL(M_PI));
    /*
        Object Types
    */
    // Boolean
    makeNativeVariable("BOOL_T", NUMBER_VAL(VAL_BOOL));
    // Double
    makeNativeVariable("NUMBER_T", NUMBER_VAL(VAL_NUMBER));
    // Nil
    makeNativeVariable("NIL_T", NUMBER_VAL(VAL_NIL));
    // Function
    makeNativeVariable("FUNC_T", NUMBER_VAL(VAL_OBJ + OBJ_FUNCTION));
    // Native
    makeNativeVariable("NATIVE_T", NUMBER_VAL(VAL_OBJ + OBJ_NATIVE));
    // String
    makeNativeVariable("STR_T", NUMBER_VAL(VAL_OBJ + OBJ_STRING));

    /* 
        Random
    */
    // RAND_MAX
    makeNativeVariable("RAND_MAX", NUMBER_VAL(RAND_MAX));

    /* 
        Wavetable buffer enum
    */
    // Main
    makeNativeVariable("MAIN_B", NUMBER_VAL(BUFFER_MAIN));
    // Aux1
    makeNativeVariable("AUX1_B", NUMBER_VAL(BUFFER_AUX1));

    /*
        Wavetable constants
    */
    // Max frames
    makeNativeVariable("FRAME_MAX", NUMBER_VAL(WAVETABLE_MAX_FRAMES - 1));
    // Last frame
    makeNativeVariable("FRAME_LAST", NUMBER_VAL(WAVETABLE_MAX_FRAMES));
    // Max indeces
    makeNativeVariable("FRAME_LEN", NUMBER_VAL(WAVETABLE_FRAME_LEN));
    // Export qualities
    // High
    makeNativeVariable("HIGH_Q", NUMBER_VAL(32));
    // Medium
    makeNativeVariable("MED_Q", NUMBER_VAL(16));
    // Low / Experimental
    makeNativeVariable("LOW_Q", NUMBER_VAL(8));
}
// End of native variables

static void resetStack() {
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
}

// Raise a runtime
static void runtimeError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    // Stack trace error reporting
    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm.frames[i];
        ObjFunction* function = frame->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        // Print line
        fprintf(stderr, "[line %d] in ", getLine(&function->chunk.lines, instruction));
        // Print function name
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    resetStack();
}

/*
    Define a native function
*/
static void defineNative(const char* name, NativeFn function, int arity) {
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    push(OBJ_VAL(newNative(function, arity)));
    tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

void initVM() {
    resetStack();
    vm.objects = NULL;
    initTable(&vm.globals);
    initTable(&vm.strings);

    /* Init Native Functions */
    defineNative("clock", clockNative, 0);
    defineNative("len", lenNative, 1);
    defineNative("type", typeNative, 1);
    defineNative("round", roundNative, 1);
    defineNative("sqrt", sqrtNative, 1);
    defineNative("pow", powNative, 2);
    defineNative("sin", sinNative, 1);
    defineNative("cos", cosNative, 1);
    defineNative("tan", tanNative, 1);
    defineNative("asin", asinNative, 1);
    defineNative("acos", acosNative, 1);
    defineNative("atan", atanNative, 1);
    defineNative("atan2", atan2Native, 2);
    defineNative("saw", sawNative, 1);
    defineNative("rand", randNative, 0);
    /* Init Native Variables */
    defineNativeVariables();

    // Init rand
    srand(time(NULL));
    rand();

    /* Init wavetable */
    // Get frame rand values
    int* randf = (int*)malloc(sizeof(int) * WAVETABLE_MAX_FRAMES);
    for (int frame = 0; frame < WAVETABLE_MAX_FRAMES; frame++) {
        randf[frame] = rand();
    }
    // Get index rand values
    int* randi = (int*)malloc(sizeof(int) * WAVETABLE_FRAME_LEN);
    for (int index = 0; index < WAVETABLE_FRAME_LEN; index++) {
        randi[index] = rand();
    }
    initWavetable(&vm.wavetable, "untitled", 256, 44100, 16, 1, randf, randi);
    /* Wavetable native functions */
    defineNative("main_t", mainTimeNative, 2);
    defineNative("aux1_t", aux1TimeNative, 2);
    defineNative("frameNorm", frameNormalizeNative, 3);
    defineNative("randf", randfNative, 1);
    defineNative("randi", randiNative, 1);
    defineNative("importWav", wavImportNative, 2);
    defineNative("exportWav", wavExportNative, 4);
    defineNative("editWav", editWaveNative, 6);
    defineNative("editDC", editDCNative, 4);
    defineNative("editFreq", editFreqNative, 6);
    defineNative("editPhase", editPhaseNative, 6);
}

void freeVM() {
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    freeWavetable(&vm.wavetable);
    freeObjects();
}

// Push new value onto the stack
void push(Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

// Pop value off the stack
Value pop() {
    vm.stackTop--;
    return *vm.stackTop;
}

// Look at value 'distance' from the top of the stack
static Value peek(int distance) {
    return vm.stackTop[-1 - distance];
}

// Call a function
static bool call(ObjFunction* function, int argCount) {
    // Check arg counts
    if (argCount != function->arity) {
        runtimeError("Expected %d arguments but got %d", function->arity, argCount);
        return false;
    }

    // Check stack depth
    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow");
        return false;
    }

    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->function = function;
    frame->ip = function->chunk.code;
    frame->slots = vm.stackTop - argCount - 1;
    return true;
}

// Check function call
static bool callValue(Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_FUNCTION:
                return call(AS_FUNCTION(callee), argCount);
            case OBJ_NATIVE: {
                NativeFn native = AS_NATIVE(callee)->function;
                int arity = AS_NATIVE(callee)->arity;
                // Check arity
                if (argCount != arity) {
                    runtimeError("Expected %d arguments but got %d", arity, argCount);
                    return false;
                }
                // Run function
                NativeFnReturn result = native(argCount, vm.stackTop - argCount);
                if (result.failed) {
                    return false;
                }
                vm.stackTop -= argCount + 1;
                push(result.value);
                return true;
            }
            default:
                break; /// Non-callable object
        }
    }
    runtimeError("Can only call functions and classes");
    return false;
}

// Cave's false logic
// False if value == 0 || or value == false || or value == nil
static bool isFalse(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value)) || ((IS_NUMBER(value) && !AS_NUMBER(value))) || ((IS_STRING(value) && AS_STRING(value)->length == 0));
}

// Concatenate two strings
static void concatenate() {
    ObjString* b = AS_STRING(pop());
    ObjString* a = AS_STRING(pop());

    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString* result = takeString(chars, length);
    push(OBJ_VAL(result));
}

// Multiply a string
static void multiplyStringA(int times) {
    ObjString* a = AS_STRING(pop());

    if (times == 1) {
        push(OBJ_VAL(a));
    } else {
        int length = a->length * (times > 0 ? times : 0);
        char* chars = ALLOCATE(char, length + 1);
        for (int offset = 0; offset < length; offset += a->length)
            memcpy(chars + offset, a->chars, a->length);
        chars[length] = '\0';

        ObjString* result = takeString(chars, length);
        push(OBJ_VAL(result));
    }
}

// Multiply b string
static void multiplyStringB(int times) {
    ObjString* b = AS_STRING(pop());
    vm.stackTop--;

    if (times == 1) {
        push(OBJ_VAL(b));
    } else {
        int length = b->length * times;
        char* chars = ALLOCATE(char, length + 1);
        for (int offset = 0; offset < length; offset += b->length)
            memcpy(chars + offset, b->chars, b->length);
        chars[length] = '\0';

        ObjString* result = takeString(chars, length);
        push(OBJ_VAL(result));
    }
}

// Add turns the top value on the stack into a string
static void stringify() {
    Value b = vm.stackTop[-1];

    // Convert b into 
    if (IS_NUMBER(b)) {
        #define VALUE_B_LENGTH 25
        // Turn double into string
        char* buffer = ALLOCATE(char, VALUE_B_LENGTH);
        int len = snprintf(buffer, VALUE_B_LENGTH - 1, "%g", AS_NUMBER(b));
        buffer[len] = '\0';

        // Extract string
        vm.stackTop[-1] = OBJ_VAL(takeString(buffer, len));
        #undef VALUE_B_LENGTH

    } else if (IS_BOOL(b)) {
        // Get proper bool string
        if (isFalse(b))
            vm.stackTop[-1] = OBJ_VAL(copyString("false", 5));
        else
            vm.stackTop[-1] = OBJ_VAL(copyString("true", 4));
    } else if (IS_NIL(b)) {
        // Turn int "nil"
        vm.stackTop[-1] = OBJ_VAL(copyString("nil", 3));
    }
}

// Push a substring of an object onto the stack
static void pushIndexRange(char* str, int len, int start, int end, int interval) {
    // Allocate buffer
    char* buffer = ALLOCATE(char, len + 1);

    // Make substr
    int count = 0;
    if (interval > 0) {
        // Do not start substring out of bounds
        if (0 <= start && start < len) {
            // Bind end to len
            if (end > len) {
                end = len;
            }
            for (int i = start; i < end; i+=interval) {
                buffer[count++] = str[i];
            }
        }
    } else {
        // Do not start substring out of bounds
        if (0 <= start && start < len) {
            // Bind end to 0
            if (end < -1) {
                end = -1;
            }
            for (int i = start; i > end; i+=interval) {
                buffer[count++] = str[i];
            }
        }
    }

    buffer[count] = '\0';
    push(OBJ_VAL(takeString(buffer, count)));
}

// Define a global variable
static void defGlobal(ObjString* name) {
    tableSet(&vm.globals, name, peek(0));
    pop();
}

static InterpretResult run() {
#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_LONG() (frame->ip += 3, ((frame->ip[-3]) << 16 | (frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->function->chunk.constants.values[READ_BYTE()])
#define READ_CONSTANT_LONG() (frame->function->chunk.constants.values[READ_LONG()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define READ_STRING_LONG() AS_STRING(READ_CONSTANT_LONG())
#define BINARY_OP(valueType, op) \
    do { \
        if (!(IS_NUMBER(peek(0)) || IS_BOOL(peek(0))) || !(IS_NUMBER(peek(1)) || IS_BOOL(peek(1)))) { \
            runtimeError("Operands must be numbers or bools"); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        int option = IS_NUMBER(peek(0)) << 1 | IS_NUMBER(peek(1)); \
        switch(option) { \
            case 0: \
                vm.stackTop -= 2; \
                push(BOOL_VAL(!isFalse(BOOL_VAL(AS_BOOL(vm.stackTop[0]) op AS_BOOL(vm.stackTop[1]))))); \
                break; \
            case 1: \
                vm.stackTop -= 2; \
                push(valueType(AS_NUMBER(vm.stackTop[0]) op AS_BOOL(vm.stackTop[1]))); \
                break; \
            case 2: \
                vm.stackTop -= 2; \
                push(valueType(AS_BOOL(vm.stackTop[0]) op AS_NUMBER(vm.stackTop[1]))); \
                break; \
            case 3: \
                vm.stackTop -= 2; \
                push(valueType(AS_NUMBER(vm.stackTop[0]) op AS_NUMBER(vm.stackTop[1]))); \
                break; \
        } \
    } while (false);

    // Set up program counter and frame
    CallFrame* frame = &vm.frames[vm.frameCount - 1];

    // Debug run tracing
    for (;;) {
    #ifdef DEBUG_TRACE_EXECUTION
        printf("          ");
            for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
                printf("[ ");
                printValue(*slot);
                printf(" ]");
            }
        printf("\n");
        disassembleInstruction(&frame->function->chunk, (int)(frame->ip - frame->function->chunk.code));
    #endif

        uint8_t instruction;
        // Interpret current instruction and increment
        switch (instruction = READ_BYTE()) {
            // Equalities
            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_NOT_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(!valuesEqual(a, b)));
                break;
            }

            // Comparison
            case OP_GREATER:        BINARY_OP(BOOL_VAL, >); break;
            case OP_GREATER_EQUAL:  BINARY_OP(BOOL_VAL, >=); break;
            case OP_LESS:           BINARY_OP(BOOL_VAL, <); break;
            case OP_LESS_EQUAL:     BINARY_OP(BOOL_VAL, <=); break;

            // Binary Arithmatic Operations
            case OP_ADD: {
                    int option = FOUR_TYPE_ID();
                    switch(option) {
                        case 5:
                            vm.stackTop -= 2;
                            push(BOOL_VAL(!isFalse(BOOL_VAL(AS_BOOL(vm.stackTop[0]) + AS_BOOL(vm.stackTop[1])))));
                            break;
                        case 6:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_NUMBER(vm.stackTop[0]) + AS_BOOL(vm.stackTop[1])));
                            break;
                        case 9:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_BOOL(vm.stackTop[0]) + AS_NUMBER(vm.stackTop[1])));
                            break;
                        case 10:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_NUMBER(vm.stackTop[0]) + AS_NUMBER(vm.stackTop[1])));
                            break;
                        case 15:
                            concatenate();
                            break;
                        /* Errors */
                        case 0:
                        case 1:
                        case 2:
                        case 3:
                        case 4:
                        case 8:
                        case 12:
                            runtimeError("Cannot add nil or functions");
                            return INTERPRET_RUNTIME_ERROR;
                        case 7:
                        case 11:
                        case 13:
                        case 14:
                            runtimeError("Can only concat two strings");
                            return INTERPRET_RUNTIME_ERROR;
                    }
                }
                break;
            // Subtraction
            case OP_SUBTRACT: {
                    int option = FOUR_TYPE_ID();
                    switch(option) {
                        case 5:
                            vm.stackTop -= 2;
                            push(BOOL_VAL(!isFalse(BOOL_VAL(AS_BOOL(vm.stackTop[0]) - AS_BOOL(vm.stackTop[1])))));
                            break;
                        case 6:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_NUMBER(vm.stackTop[0]) - AS_BOOL(vm.stackTop[1])));
                            break;
                        case 9:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_BOOL(vm.stackTop[0]) - AS_NUMBER(vm.stackTop[1])));
                            break;
                        case 10:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_NUMBER(vm.stackTop[0]) - AS_NUMBER(vm.stackTop[1])));
                            break;
                        /* Errors */
                        case 0:
                        case 1:
                        case 2:
                        case 3:
                        case 4:
                        case 8:
                        case 12:
                            runtimeError("Cannot subtract nil or functions");
                            return INTERPRET_RUNTIME_ERROR;
                        case 7:
                        case 11:
                        case 13:
                        case 14:
                        case 15:
                            runtimeError("Cannot subtract strings");
                            return INTERPRET_RUNTIME_ERROR;
                    }
                }
                break;
            // Multiplication
            case OP_MULTIPLY: {
                    int option = FOUR_TYPE_ID();
                    switch(option) {
                        // Basic number and bool combos
                        case 5:
                            vm.stackTop -= 2;
                            push(BOOL_VAL(!isFalse(BOOL_VAL(AS_BOOL(vm.stackTop[0]) * AS_BOOL(vm.stackTop[1])))));
                            break;
                        case 6:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_NUMBER(vm.stackTop[0]) * AS_BOOL(vm.stackTop[1])));
                            break;
                        case 9:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_BOOL(vm.stackTop[0]) * AS_NUMBER(vm.stackTop[1])));
                            break;
                        case 10:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_NUMBER(vm.stackTop[0]) * AS_NUMBER(vm.stackTop[1])));
                            break;
                        case 7:
                            multiplyStringA(AS_BOOL(pop()));
                            break;
                        case 11:
                            multiplyStringA((int)AS_NUMBER(pop()));
                            break;
                        case 13:
                            multiplyStringB(AS_BOOL(vm.stackTop[-2]));
                            break;
                        case 14:
                            multiplyStringB((int)AS_NUMBER(vm.stackTop[-2]));
                            break;
                        /* Errors */
                        case 15: // Str * str
                            runtimeError("Can only multiply string by a number or bool");
                            return INTERPRET_RUNTIME_ERROR;
                        // Nil combos
                        case 0:
                        case 1:
                        case 2:
                        case 3:
                        case 4:
                        case 8:
                        case 12:
                            runtimeError("Cannot multiply by nil or functions");
                            return INTERPRET_RUNTIME_ERROR;
                    }
                }
                break;
            // Division
            case OP_DIVIDE: {
                    int option = FOUR_TYPE_ID();
                    switch(option) {
                        case 5:
                            vm.stackTop -= 2;
                            // FIX DIVISION BY FALSE
                            push(BOOL_VAL(!isFalse(BOOL_VAL(AS_BOOL(vm.stackTop[0]) / 1))));
                            break;
                        case 6:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_NUMBER(vm.stackTop[0]) / AS_BOOL(vm.stackTop[1])));
                            break;
                        case 9:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_BOOL(vm.stackTop[0]) / AS_NUMBER(vm.stackTop[1])));
                            break;
                        case 10:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_NUMBER(vm.stackTop[0]) / AS_NUMBER(vm.stackTop[1])));
                            break;
                        /* Errors */
                        case 0:
                        case 1:
                        case 2:
                        case 3:
                        case 4:
                        case 8:
                        case 12:
                            runtimeError("Cannot divide by nil or functions");
                            return INTERPRET_RUNTIME_ERROR;
                        case 7:
                        case 11:
                        case 13:
                        case 14:
                        case 15:
                            runtimeError("Cannot divide strings");
                            return INTERPRET_RUNTIME_ERROR;
                    }
                }
                break;
            // Mod Operation
            case OP_MOD: {
                    int option = FOUR_TYPE_ID();
                    switch(option) {
                        case 5:
                            vm.stackTop -= 2;
                            push(BOOL_VAL(0));
                            break;
                        case 6:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(fmod(AS_NUMBER(vm.stackTop[0]), AS_BOOL(vm.stackTop[1]))));
                            break;
                        case 9:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(fmod(AS_BOOL(vm.stackTop[0]), AS_NUMBER(vm.stackTop[1]))));
                            break;
                        case 10:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(fmod(AS_NUMBER(vm.stackTop[0]), AS_NUMBER(vm.stackTop[1]))));
                            break;
                        /* Errors */
                        case 0:
                        case 1:
                        case 2:
                        case 3:
                        case 4:
                        case 8:
                        case 12:
                            runtimeError("Cannot mod by or functions");
                            return INTERPRET_RUNTIME_ERROR;
                        case 7:
                        case 11:
                        case 13:
                        case 14:
                        case 15:
                            runtimeError("Cannot mod strings");
                            return INTERPRET_RUNTIME_ERROR;
                    }
                }
                break;
            
            // Str Interpolation
            case OP_INTERPOLATE_STR: {
                // If both strings concatenate
                if (!IS_STRING(vm.stackTop[-1])) 
                    stringify();
                concatenate();
                break;
            }

            // Not Value
            case OP_NOT:
                push(BOOL_VAL(isFalse(pop())));
                break;
            // Negate Value
            case OP_NEGATE:
                if (!IS_NUMBER(peek(0))) {
                    runtimeError("Operand must be a number");
                    return INTERPRET_RUNTIME_ERROR;
                }
                (vm.stackTop-1)->as.number = -AS_NUMBER(*(vm.stackTop-1));
                break;

            // Constant
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            // Constant long
            case OP_CONSTANT_LONG: {
                Value constant = READ_CONSTANT_LONG();
                push(constant);
                break;
            }

            // User literals
            case OP_NIL: push(NIL_VAL); break;
            case OP_TRUE: push(BOOL_VAL(1)); break;
            case OP_FALSE: push(BOOL_VAL(0)); break;

            // Variables //
            // Globals
            case OP_GET_GLOBAL: {
                ObjString* name = READ_STRING();
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    runtimeError("Undefined variable '%s'", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
            }
            case OP_GET_GLOBAL_LONG: {
                ObjString* name = READ_STRING_LONG();
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    runtimeError("Undefined variable '%s'", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
            }
            case OP_GET_GLOBAL_STACK: {
                if (!IS_STRING(peek(0))) {
                    runtimeError("Can only use strings to access global variables");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjString* name = AS_STRING(pop());
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    push(NIL_VAL);
                } else {
                    push(value);
                }
                break;
            }
            case OP_GET_GLOBAL_STACK_POPLESS: {
                if (!IS_STRING(peek(0))) {
                    runtimeError("Can only use strings to access global variables");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjString* name = AS_STRING(peek(0));
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    push(NIL_VAL);
                } else {
                    push(value);
                }
                break;
            }
            case OP_SET_GLOBAL: {
                ObjString* name = READ_STRING();
                if  (tableSet(&vm.globals, name, peek(0))) {
                    tableDelete(&vm.globals, name);
                    runtimeError("Undefined variable '%s'", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SET_GLOBAL_LONG: {
                ObjString* name = READ_STRING_LONG();
                if  (tableSet(&vm.globals, name, peek(0))) {
                    tableDelete(&vm.globals, name);
                    runtimeError("Undefined variable '%s'", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SET_GLOBAL_STACK: {
                if (!IS_STRING(peek(1))) {
                    runtimeError("Can only use strings to set global variables");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjString* name = AS_STRING(peek(1));
                if  (tableSet(&vm.globals, name, peek(0))) {
                    tableDelete(&vm.globals, name);
                    runtimeError("Undefined variable '%s'", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                pop();
                break;
            }
            case OP_DEFINE_GLOBAL: defGlobal(READ_STRING()); break;
            case OP_DEFINE_GLOBAL_LONG: defGlobal(READ_STRING_LONG()); break;
            case OP_DEFINE_GLOBAL_STACK: {
                if (!IS_STRING(peek(1))) {
                    runtimeError("Can only use strings to define global variables");
                    return INTERPRET_RUNTIME_ERROR;
                }
                defGlobal(AS_STRING(peek(1)));
                pop();
                break;
            }

            // Locals
            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(frame->slots[slot]);
                break;
            }
            case OP_GET_LOCAL_LONG: {
                uint32_t slot = READ_LONG();
                push(frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = peek(0);
                break;
            }
            case OP_SET_LOCAL_LONG: {
                uint32_t slot = READ_LONG();
                frame->slots[slot] = peek(0);
                break;
            }

            
            // Pop onces
            case OP_POP: pop(); break;
            // Pop n times
            case OP_POPN: vm.stackTop -= READ_LONG(); break;

            //// Statements ////
            case OP_PRINT: {
                printValue(pop());
                printf("\n");
                break;
            }

            //// Control Flow ////
            case OP_JUMP_IF_FALSE: {
                uint16_t loc = READ_SHORT();
                if (isFalse(peek(0))) frame->ip += loc;
                break;
            }
            case OP_JUMP_IF_TRUE: {
                uint16_t loc = READ_SHORT();
                if (!isFalse(peek(0))) frame->ip += loc;
                break;
            }
            case OP_JUMP: {
                frame->ip += READ_SHORT();
                break;
            }
            case OP_JUMP_NPOP: {
                int jump = READ_SHORT();
                vm.stackTop -= READ_LONG();
                frame->ip += jump;
                break;
            }
            case OP_LOOP: {
                frame->ip -= READ_SHORT();
                break;
            }
            case OP_LOOP_IF_TRUE: {
                uint16_t loc = READ_SHORT();
                if (!isFalse(peek(0))) frame->ip -= loc;
                // Pop condition check value from the stack
                pop();
                break;
            }

            // Function Stuff //
            case OP_CALL: {
                int argCount = READ_BYTE();
                if (!callValue(peek(argCount), argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                // Change frame
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }

            // Indexing //

            // str, index
            case OP_INDEX: {
                // Type check
                if (!IS_STRING(peek(1))) {
                    runtimeError("Can only index strings");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (!IS_NUMBER(peek(0))) {
                    runtimeError("Index must be a number");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                int index = (int) AS_NUMBER(peek(0));
                char* str = AS_CSTRING(peek(1));
                int len = AS_STRING(peek(1))->length;
                // Pop arguments
                vm.stackTop -= 2;
                // Wrap around
                if (index < 0) {
                    index += len;
                }
                // Bounds check
                if (index < 0 || index >= len) {
                    runtimeError("Index out of bounds");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(OBJ_VAL(copyString(str + index, 1)));
                break;
            }

            // str, start, end
            case OP_INDEX_RANGE: {
                // Type check
                if (!IS_STRING(peek(2))) {
                    runtimeError("Can only index strings");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (!IS_NUMBER(peek(1)) && !IS_NIL(peek(1)) || !IS_NUMBER(peek(0)) && !IS_NIL(peek(0))) {
                    runtimeError("Index ranges must be nil or a number");
                    return INTERPRET_RUNTIME_ERROR;
                }
                char* str = AS_CSTRING(peek(2));
                int len = AS_STRING(peek(2))->length;
                int startIndex = IS_NUMBER(peek(1))?AS_NUMBER(peek(1)):0;
                int endIndex = IS_NUMBER(peek(0))?AS_NUMBER(peek(0)):len;
                int interval = 1;
                // Wrap indexes
                if (startIndex < 0) {
                    startIndex += len;
                }
                if (endIndex < 0) {
                    endIndex += len;
                }
                // Pop values
                vm.stackTop -= 3;
                // Get substr
                pushIndexRange(str, len, startIndex, endIndex, interval);
                break;
            }

            // Str, start, end, interval
            case OP_INDEX_RANGE_INTERVAL: {
                // Type check
                if (!IS_STRING(peek(3))) {
                    runtimeError("Can only index strings");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (!IS_NUMBER(peek(2)) && !IS_NIL(peek(2)) || !IS_NUMBER(peek(1)) && !IS_NIL(peek(1)) || !IS_NUMBER(peek(0)) && !IS_NIL(peek(0))) {
                    runtimeError("Index ranges and interval must be nil or a number");
                    return INTERPRET_RUNTIME_ERROR;
                }
                char* str = AS_CSTRING(peek(3));
                int len = AS_STRING(peek(3))->length;
                // Determine interval and defaults
                int interval = IS_NUMBER(peek(0)) ? (int)AS_NUMBER(peek(0)) : 1;
                int startIndex, endIndex;
                // Get startIndex
                if (IS_NUMBER(peek(2))) {
                    startIndex = (int)AS_NUMBER(peek(2));
                    if (startIndex < 0) {
                        startIndex += len;
                    }
                } else {
                    startIndex = interval > 0 ? 0 : len - 1;;
                }
                // Get endIndex
                if (IS_NUMBER(peek(1))) {
                    endIndex = (int)AS_NUMBER(peek(1));
                    if (endIndex < 0) {
                        endIndex += len;
                    }
                } else {
                    endIndex = interval > 0 ? len : -1;
                }

                // Pop values
                vm.stackTop -= 4;
                // Check interval
                if (interval == 0) {
                    runtimeError("Interval cannot be '0'");
                    return INTERPRET_RUNTIME_ERROR;
                }
                // Get substr
                pushIndexRange(str, len, startIndex, endIndex, interval);
                break;
            }

            // Ascend outside the VM //
            case OP_EXTRACT: {
                // Rip the top value on the stack out
                vm.output = pop();
                // Exit interpreter
                return INTERPRET_OK;
            }
            
            // Returns value from func
            case OP_RETURN: {
                Value result = pop();
                vm.frameCount--;
                // Check if in final frame
                if (vm.frameCount == 0) {
                    pop();
                    // Exit interpreter
                    return INTERPRET_OK;
                }

                vm.stackTop = frame->slots;
                push(result);
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }

            default: {
                runtimeError("Unrecognized bytecode");
                return INTERPRET_RUNTIME_ERROR;
            }
        }
    }
#undef READ_LONG
#undef READ_SHORT
#undef READ_BYTE
#undef READ_CONSTANT_LONG
#undef READ_CONSTANT
#undef READ_STRING_LONG
#undef READ_STRING
#undef BINARY_OP
}

// Interpret a chunk
InterpretResult interpret(const char* source) {
    // Reset Stack
    resetStack();
    // Compile source
    ObjFunction* function = compile(source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    // Push script frame onto stack
    push(OBJ_VAL(function));
    call(function, 0);

    // Run
    InterpretResult result = run();

    return result;
}

#undef FOUR_TYPE_ID