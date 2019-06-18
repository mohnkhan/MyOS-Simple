// ============================================================================
//  process.h  —  Process model interface (PCB, states, scheduler API)
//
//  Declares the process states, the saved CPU-context struct, and the Process
//  Control Block (process_t), plus the lifecycle/scheduling API implemented in
//  process.c. Limits: up to MAX_PROCESSES tasks, STACK_SIZE bytes per stack.
// ============================================================================
#ifndef PROCESS_H
#define PROCESS_H

#include "stdint.h"

#define MAX_PROCESSES 10
#define STACK_SIZE 4096
#define PROCESS_NAME_LEN 32

// Process states
typedef enum {
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_TERMINATED
} process_state_t;

// CPU context structure
typedef struct {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t eip;
    uint32_t eflags;
    uint32_t cs;
    uint32_t ds;
    uint32_t es;
    uint32_t fs;
    uint32_t gs;
    uint32_t ss;
} cpu_context_t;

// Process Control Block
typedef struct process_t {
    uint32_t pid;                    // Process ID
    char name[PROCESS_NAME_LEN];     // Process name
    process_state_t state;            // Current state
    cpu_context_t context;            // CPU context for context switching
    uint8_t* stack;                   // Stack pointer
    uint32_t stack_size;              // Stack size
    void (*entry_point)(void);        // Process entry point
    uint32_t time_slice;              // Time slice for scheduling
    uint32_t priority;                // Process priority
    struct process_t* next;           // Next process in queue
} process_t;

// Process management functions
void process_init(void);
uint32_t process_create(const char* name, void (*entry_point)(void), uint32_t priority);
void process_terminate(uint32_t pid);
void process_yield(void);
void process_scheduler(void);
process_t* process_get_current(void);
process_t* process_get_by_pid(uint32_t pid);
void process_list(void);
void process_kill(uint32_t pid);
void process_suspend(uint32_t pid);
void process_resume(uint32_t pid);

// Context switching (assembly)
extern void context_switch(cpu_context_t* old_context, cpu_context_t* new_context);

// Sample processes for testing
void process_counter(void);
void process_fibonacci(void);
void process_prime(void);

#endif // PROCESS_H
