// ============================================================================
//  stdint.h  —  Freestanding fixed-width integer types (32-bit x86)
//
//  Minimal stand-in for the C library header. The kernel is built with
//  -nostdinc, so these typedefs (uint8_t..uint64_t, intptr_t, size_t, ...) are
//  provided locally for the 32-bit i386 target.
// ============================================================================
#ifndef STDINT_H
#define STDINT_H

// Basic fixed-width integer types for 32-bit x86
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;

// Pointer-sized integers
typedef unsigned int uintptr_t;
typedef signed int intptr_t;

// Size type
typedef unsigned int size_t;

#endif // STDINT_H
