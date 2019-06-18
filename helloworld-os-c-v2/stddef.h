// ============================================================================
//  stddef.h  —  Freestanding NULL / size_t / ptrdiff_t
//
//  Minimal stand-in for the C library header, needed because the kernel is
//  built with -nostdinc.
// ============================================================================
#ifndef STDDEF_H
#define STDDEF_H

// NULL pointer definition
#define NULL ((void*)0)

// Size type
typedef unsigned int size_t;

// Pointer difference type
typedef int ptrdiff_t;

#endif // STDDEF_H
