#ifndef cave_compiler_h
#define cave_compiler_h

#include "object.h"

ObjFunction* compile(const char* source);
ObjFunction* runtimeCompile(const char* source);

#endif