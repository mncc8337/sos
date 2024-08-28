#include "system.h"
#include "string.h"

static tss_entry_t tss;

void tss_set_stack(uint32_t esp) {
    tss.esp0 = esp;
}

void tss_install(int gate, uint16_t kernel_ss, uint32_t kernel_esp) {
    gdt_set_gate(gate, (uint32_t)&tss, sizeof(tss_entry_t) - 1, 0xe9, 0x0);

    memset(&tss, 0, sizeof(tss_entry_t));

    tss.ss = kernel_ss;
    tss.esp0 = kernel_esp;
    tss.cs = 0x0b;
    tss.ss = 0x13;
    tss.es = 0x13;
    tss.ds = 0x13;
    tss.fs = 0x13;
    tss.gs = 0x13;
}

void tss_flush() {
    asm volatile("ltr %0" : : "r" (0x28));
}
