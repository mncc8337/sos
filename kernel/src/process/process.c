#include "process.h"
#include "system.h"
#include "mem.h"

#define DEFAULT_EFLAGS 0x202
#define DEFAULT_STACK_SIZE 16 * 1024

static unsigned process_count = 0;

process_t* process_new(uint32_t eip, int priority, bool is_user) {
    process_t* proc = (process_t*)kmalloc(sizeof(process_t));
    if(!proc) return NULL;

    proc->pid = ++process_count;
    proc->alive_ticks = 1;
    proc->priority = priority;
    if(process_count == 1) {
        // then we are creating the main kernel process
        proc->page_directory = vmmngr_get_directory();
    }
    else {
        proc->page_directory = vmmngr_alloc_page_directory();
        if(!proc->page_directory) {
            kfree(proc);
            return NULL;
        }
    }
    proc->state = PROCESS_STATE_SLEEP;
    proc->thread_count = 1;
    // FIXME: thread should be allocated in proccess heap
    proc->thread_list = (thread_t*)kmalloc(sizeof(thread_t));
    if(!proc->thread_list) {
        vmmngr_free_page_directory(proc->page_directory);
        kfree(proc);
        return NULL;
    }
    proc->next = NULL;
    proc->prev = NULL;

    thread_t* thread = proc->thread_list;
    // thread->priority;
    thread->state = PROCESS_STATE_ACTIVE;
    thread->next = NULL;

    regs_t* regs = &thread->regs;
    if(is_user) {
        // switch page directory to create user heap
        page_directory_t* saved_pd = vmmngr_get_directory();
        vmmngr_switch_page_directory(proc->page_directory);

        heap_t* heap = heap_new(UHEAP_START, UHEAP_INITIAL_SIZE, UHEAP_MAX_SIZE, 0b00);

        regs->eip = eip;
        regs->cs = 0x1b; // user code selector
        regs->ds = 0x23; // user data selector
        regs->es = regs->ds;
        regs->fs = regs->ds;
        regs->gs = regs->ds;
        regs->ss = regs->ds;
        regs->eflags = DEFAULT_EFLAGS;
        regs->useresp = (uint32_t)heap_alloc(heap, DEFAULT_STACK_SIZE, false) + DEFAULT_STACK_SIZE;

        vmmngr_switch_page_directory(saved_pd);
    }
    else {
        regs->eip = eip;
        regs->cs = 0x08; // kernel code selector
        regs->ds = 0x10; // kernel data selector
        regs->es = regs->ds;
        regs->fs = regs->ds;
        regs->gs = regs->ds;
        regs->ss = regs->ds;
        regs->eflags = DEFAULT_EFLAGS;
        regs->useresp = (uint32_t)kmalloc(DEFAULT_STACK_SIZE) + DEFAULT_STACK_SIZE;
    }

    return proc;
}

void process_delete(process_t* proc) {
    vmmngr_free_page_directory(proc->page_directory);

    // free all threads
    thread_t* thread = proc->thread_list;
    while(thread) {
        void* t = thread;
        thread = thread->next;
        kfree(t);
    }

    kfree(proc);
}
