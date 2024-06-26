#include "system.h"
#include "driver/video.h"

unsigned char *exception_message[] = {
    "Division Error",
    "Debug",
    "Non-maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved"
};

void exception_handler(struct regs* r) {
    print_string("Exception: ", 0, WHITE, true);
    print_string(exception_message[r->int_no], -1, LIGHT_RED, true);
    print_string(".\nSystem Halted!", -1, WHITE, true);
    __asm__ volatile ("cli; hlt"); // completely hang the computer
}

extern void* isr_table[];

void isr_init() {
    for(unsigned char vector = 0; vector < 32; vector++)
        idt_set_descriptor(vector, isr_table[vector], 0x8e);
}
