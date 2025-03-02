#include <stdio.h>
#include <string.h>

#include "object.h"
#include "memory.h"
#include "value.h"

void initValueArray(ValueArray* array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

// Write constant to array, not adding duplicates
uint32_t writeValueArray(ValueArray* array, Value value) {
    // Check if constant is already in array
    ValueType type = value.type;
    /*switch(type) {
        case VAL_BOOL: {
            for (int pos = 0; pos < array->capacity; pos++) {
                if (type == array->values[pos].type && AS_BOOL(value) == AS_BOOL(array->values[pos])) {
                    return (uint32_t)pos;
                } 
            }
            break;
        }
        case VAL_NIL: {
            for (int pos = 0; pos < array->capacity; pos++) {
                if (type == array->values[pos].type) {
                    return (uint32_t)pos;
                } 
            }
            break;
        }
        case VAL_NUMBER: {
            for (int pos = 0; pos < array->capacity; pos++) {
                if (type == array->values[pos].type && AS_NUMBER(value) == AS_NUMBER(array->values[pos])) {
                    return (uint32_t)pos;
                } 
            }
            break;
        }
        case VAL_OBJ: {
            uint32_t hash = AS_STRING(value)->hash;
            int length = AS_STRING(value)->length;
            char* chars = AS_STRING(value)->chars;
            for (int pos = 0; pos < array->capacity; pos++) {
                if (type == array->values[pos].type) {
                    ObjString* str = AS_STRING(array->values[pos]);
                    if (length == str->length && hash == str->hash
                        && memcmp(chars, str->chars, length) == 0) {
                        return (uint32_t)pos;
                    }
                }
            }
            break;
        }
    }*/
    
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(Value, array->values, oldCapacity, array->capacity);
    }

    array->values[array->count] = value;
    return (uint32_t)array->count++;
}


void freeValueArray(ValueArray* array) {
    FREE_ARRAY(Value, array->values, array->capacity);
    initValueArray(array);
}

void printValue(Value value) {
    switch (value.type) {
        case VAL_BOOL:
            printf(AS_BOOL(value) ? "true" : "false");
            break;
        case VAL_NIL: printf("nil"); break;
        case VAL_NUMBER: printf("%g", AS_NUMBER(value)); break;
        case VAL_OBJ: printObject(value); break;
    }
}


// Defines Cave's equality rules
bool valuesEqual(Value a, Value b) {
    if (IS_NIL(a)) return IS_NIL(b);
    if (IS_OBJ(a)) {
        if (IS_OBJ(b)) {
            return AS_OBJ(a) == AS_OBJ(b);

        } else {
            return false;
        }
    }
    int option = IS_NUMBER(a) << 1 | IS_NUMBER(b);
    switch(option) {
        case 0:     return AS_BOOL(a) == AS_BOOL(b);
        case 1:     return AS_BOOL(a) == AS_NUMBER(b);
        case 2:     return AS_NUMBER(a) == AS_BOOL(b);
        case 3:     return AS_NUMBER(a) == AS_NUMBER(b);
        default:    return false; // Unreachable
    }
}