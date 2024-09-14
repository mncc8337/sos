#pragma once

#include "stdint.h"
#include "mem.h"

#define MAX_PROCESSES 256

#define PROCESS_STATE_SLEEP  0
#define PROCESS_STATE_ACTIVE 1

typedef struct process {
    int pid;
    int priority;
    page_directory_t* page_directory;
    int state;
    bool is_user;
    struct thread* thread_list;
    struct process* next;
    struct process* prev;
} process_t;

typedef struct thread {
    struct process* parent;
    void* stack;
    uint32_t stack_size;
    uint32_t priority;
    int state;
    struct {
        uint32_t eax;
        uint32_t ebx;
        uint32_t ecx;
        uint32_t edx;
        uint32_t esi;
        uint32_t edi;
        uint32_t esp;
        uint32_t ebp;
        uint32_t eip;
        uint32_t eflags;
    } frame;
    struct thread* next;
} thread_t;

process_t* process_get_current();
process_t* process_new(uint32_t eip, bool is_user);
// int process_fork();
void process_switch(process_t* proc);
void process_terminate();
bool process_init();
