// ============================================================================
//  process.c  —  Cooperative process model and round-robin scheduler
//
//  A minimal multitasking layer. Maintains a fixed process table (PID, name,
//  state, saved CPU context, statically allocated 4 KiB stack, priority) and a
//  singly-linked ready queue. Scheduling is cooperative round-robin: a task
//  runs until it calls process_yield(); there is no timer preemption, and the
//  context switch is modeled rather than performed (see process_scheduler).
//  Ships three sample workloads: counter, Fibonacci, and prime finder.
// ============================================================================
#include "process.h"
#include "kernel.h"
#include "stddef.h"

// Process table
static process_t process_table[MAX_PROCESSES];
static process_t* current_process = NULL;
static process_t* ready_queue = NULL;
static uint32_t next_pid = 1;
static int process_count = 0;

// Memory allocation for stacks (simplified)
static uint8_t process_stacks[MAX_PROCESSES][STACK_SIZE] __attribute__((aligned(16)));
static int stack_allocation[MAX_PROCESSES] = {0};

// Helper function to allocate stack
static uint8_t* allocate_stack(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!stack_allocation[i]) {
            stack_allocation[i] = 1;
            return process_stacks[i];
        }
    }
    return NULL;
}

// Helper function to free stack
static void free_stack(uint8_t* stack) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_stacks[i] == stack) {
            stack_allocation[i] = 0;
            break;
        }
    }
}

// Helper function to copy string
static void str_copy(char* dest, const char* src, int max_len) {
    int i;
    for (i = 0; i < max_len - 1 && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

// Initialize process management system
void process_init(void) {
    // Clear process table
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_table[i].pid = 0;
        process_table[i].state = PROCESS_TERMINATED;
        process_table[i].stack = NULL;
        process_table[i].next = NULL;
    }
    
    // Create idle process (PID 0)
    process_table[0].pid = 0;
    str_copy(process_table[0].name, "idle", PROCESS_NAME_LEN);
    process_table[0].state = PROCESS_READY;
    process_table[0].priority = 0;
    process_table[0].time_slice = 1;
    
    current_process = &process_table[0];
    ready_queue = &process_table[0];
    process_count = 1;
    
    print_string("Process management initialized\n");
}

// Create a new process
uint32_t process_create(const char* name, void (*entry_point)(void), uint32_t priority) {
    if (process_count >= MAX_PROCESSES) {
        print_string("Error: Process table full\n");
        return 0;
    }
    
    // Find free slot in process table
    int slot = -1;
    for (int i = 1; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROCESS_TERMINATED) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        print_string("Error: No free process slot\n");
        return 0;
    }
    
    // Allocate stack
    uint8_t* stack = allocate_stack();
    if (!stack) {
        print_string("Error: No stack available\n");
        return 0;
    }
    
    // Initialize PCB
    process_t* proc = &process_table[slot];
    proc->pid = next_pid++;
    str_copy(proc->name, name, PROCESS_NAME_LEN);
    proc->state = PROCESS_READY;
    proc->stack = stack;
    proc->stack_size = STACK_SIZE;
    proc->entry_point = entry_point;
    proc->priority = priority;
    proc->time_slice = 5 - priority; // Higher priority = more time
    proc->next = NULL;
    
    // Initialize stack and context
    proc->context.esp = (uint32_t)(stack + STACK_SIZE - 4);
    proc->context.ebp = proc->context.esp;
    proc->context.eip = (uint32_t)entry_point;
    proc->context.eflags = 0x200; // Enable interrupts
    
    // Add to ready queue
    if (!ready_queue) {
        ready_queue = proc;
    } else {
        process_t* p = ready_queue;
        while (p->next) {
            p = p->next;
        }
        p->next = proc;
    }
    
    process_count++;
    
    print_string("Process created: ");
    print_string(name);
    print_string(" (PID: ");
    print_int(proc->pid);
    print_string(")\n");
    
    return proc->pid;
}

// Terminate a process
void process_terminate(uint32_t pid) {
    process_t* proc = process_get_by_pid(pid);
    if (!proc || proc->pid == 0) {
        print_string("Error: Cannot terminate process\n");
        return;
    }
    
    // Remove from ready queue
    if (ready_queue == proc) {
        ready_queue = proc->next;
    } else {
        process_t* p = ready_queue;
        while (p && p->next != proc) {
            p = p->next;
        }
        if (p) {
            p->next = proc->next;
        }
    }
    
    // Free resources
    if (proc->stack) {
        free_stack(proc->stack);
        proc->stack = NULL;
    }
    
    proc->state = PROCESS_TERMINATED;
    proc->pid = 0;
    process_count--;
    
    print_string("Process terminated (PID: ");
    print_int(pid);
    print_string(")\n");
    
    // If this was the current process, switch to another
    if (proc == current_process) {
        process_scheduler();
    }
}

// Get process by PID
process_t* process_get_by_pid(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid == pid) {
            return &process_table[i];
        }
    }
    return NULL;
}

// Get current process
process_t* process_get_current(void) {
    return current_process;
}

// Simple round-robin scheduler
void process_scheduler(void) {
    if (!ready_queue) {
        return;
    }
    
    process_t* next = ready_queue;
    
    // Move current to end of queue if still ready
    if (current_process && current_process->state == PROCESS_READY) {
        // Remove from front
        ready_queue = ready_queue->next;
        
        // Add to end
        process_t* p = ready_queue;
        if (p) {
            while (p->next) {
                p = p->next;
            }
            p->next = current_process;
            current_process->next = NULL;
        } else {
            ready_queue = current_process;
        }
    }
    
    // Select next process
    next = ready_queue;
    if (next && next != current_process) {
        process_t* old = current_process;
        current_process = next;
        current_process->state = PROCESS_RUNNING;
        
        // Context switch would happen here in real implementation
        // context_switch(&old->context, &current->context);
    }
}

// Yield CPU to next process
void process_yield(void) {
    process_scheduler();
}

// List all processes
void process_list(void) {
    print_string("\n=== Process List ===\n");
    print_string("PID\tName\t\tState\t\tPriority\n");
    print_string("---\t----\t\t-----\t\t--------\n");
    
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid > 0 || i == 0) {
            print_int(process_table[i].pid);
            print_string("\t");
            print_string(process_table[i].name);
            print_string("\t\t");
            
            switch (process_table[i].state) {
                case PROCESS_READY:
                    print_string("READY");
                    break;
                case PROCESS_RUNNING:
                    print_string("RUNNING");
                    break;
                case PROCESS_BLOCKED:
                    print_string("BLOCKED");
                    break;
                case PROCESS_TERMINATED:
                    print_string("TERMINATED");
                    break;
            }
            
            print_string("\t\t");
            print_int(process_table[i].priority);
            print_string("\n");
        }
    }
    print_string("Total processes: ");
    print_int(process_count);
    print_string("\n\n");
}

// Kill a process
void process_kill(uint32_t pid) {
    process_terminate(pid);
}

// Suspend a process
void process_suspend(uint32_t pid) {
    process_t* proc = process_get_by_pid(pid);
    if (proc && proc->pid != 0) {
        proc->state = PROCESS_BLOCKED;
        print_string("Process suspended (PID: ");
        print_int(pid);
        print_string(")\n");
        
        if (proc == current_process) {
            process_scheduler();
        }
    }
}

// Resume a process
void process_resume(uint32_t pid) {
    process_t* proc = process_get_by_pid(pid);
    if (proc && proc->state == PROCESS_BLOCKED) {
        proc->state = PROCESS_READY;
        
        // Add back to ready queue
        if (!ready_queue) {
            ready_queue = proc;
        } else {
            process_t* p = ready_queue;
            while (p->next) {
                p = p->next;
            }
            p->next = proc;
        }
        proc->next = NULL;
        
        print_string("Process resumed (PID: ");
        print_int(pid);
        print_string(")\n");
    }
}

// Sample process: Counter
void process_counter(void) {
    int count = 0;
    while (1) {
        print_string("[Counter] Count: ");
        print_int(count++);
        print_string("\n");
        
        // Simulate work
        for (volatile int i = 0; i < 10000000; i++);
        
        // Yield CPU
        process_yield();
    }
}

// Sample process: Fibonacci
void process_fibonacci(void) {
    int a = 0, b = 1;
    while (1) {
        print_string("[Fibonacci] ");
        print_int(a);
        print_string("\n");
        
        int temp = a + b;
        a = b;
        b = temp;
        
        // Simulate work
        for (volatile int i = 0; i < 10000000; i++);
        
        // Yield CPU
        process_yield();
    }
}

// Sample process: Prime finder
void process_prime(void) {
    int num = 2;
    while (1) {
        int is_prime = 1;
        for (int i = 2; i * i <= num; i++) {
            if (num % i == 0) {
                is_prime = 0;
                break;
            }
        }
        
        if (is_prime) {
            print_string("[Prime] ");
            print_int(num);
            print_string("\n");
        }
        
        num++;
        
        // Simulate work
        for (volatile int i = 0; i < 5000000; i++);
        
        // Yield CPU
        process_yield();
    }
}
