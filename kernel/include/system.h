#pragma once

#include <stdint.h>
#include <stdbool.h>

#define GDT_MAX_DESCRIPTORS 6
#define IDT_MAX_DESCRIPTORS 256

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdtr_t;

typedef struct {
    uint16_t isr_low;
    uint16_t kernel_cs;
    uint8_t  reserved;
    uint8_t  attributes;
    uint16_t isr_high;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idtr_t;

typedef struct {
    unsigned int gs, fs, es, ds;
    unsigned int edi, esi, ebp, esp, ebx, edx, ecx, eax;
    unsigned int int_no, err_code;
    unsigned int eip, cs, eflags, useresp, ss; 
} __attribute__((packed)) regs_t;

typedef struct {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed)) tss_entry_t;

// port_io.c
uint8_t port_inb(uint16_t port);
void port_outb(uint16_t port, uint8_t data);
uint16_t port_inw(uint16_t port);
void port_outw(uint16_t port, uint16_t data);
void io_wait();

// gdt.c
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);
void gdt_init();

// idt.c
void idt_init();
void idt_set_descriptor(uint8_t vector, void* isr, uint8_t flags);

// isr.c
void irq_install_handler(int irq, void (*handler)(regs_t *r));
void irq_uninstall_handler(int irq);
void isr_new_interrupt(int isr, uint8_t flags, void (*handler)(regs_t* r));
void isr_init();

// tss.c
void tss_set_stack(uint32_t esp);
void tss_install(uint16_t kernel_ss, uint16_t kernel_esp);

// usermode.asm
void enter_usermode();
