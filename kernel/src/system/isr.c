#include "system.h"
#include "kpanic.h"
#include "pic.h"
#include "stdio.h"
#include "string.h"
#include "video.h"

static void* routines[IDT_MAX_DESCRIPTORS];
// from isr.asm
extern void* isr_table[IDT_MAX_DESCRIPTORS];

static char* exception_message[] = {
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

static void page_fault_handler(regs_t* r) {
    // the faulting address is stored in the CR2 register
    uint32_t faulting_address;
    asm volatile("mov %%cr2, %0" : "=r" (faulting_address));

    bool present = !(r->err_code & 0x1);   // page not present
    bool rw = r->err_code & 0x2;           // write operation
    bool us = r->err_code & 0x4;           // processor was in user-mode
    bool reserved = r->err_code & 0x8;     // overwritten CPU-reserved bits of page entry
    bool id = r->err_code & 0x10;          // caused by an instruction fetch

    // output an error message
    printf("page fault at 0x%x\n", faulting_address);
    printf("flags: ");
    if(present) printf("present ");
    if(rw) printf("read-only ");
    if(us) printf("user-mode ");
    if(reserved) printf("reserved ");
    if(id) printf("instruction fetch ");
    putchar('\n');
}

static void* exception_handlers[32] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    page_fault_handler,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

// default exception handler
static void exception_handler(regs_t* r) {
    video_set_attr(video_rgb(VIDEO_WHITE), video_rgb(VIDEO_BLACK));
    printf("\nException: ");
    video_set_attr(video_rgb(VIDEO_LIGHT_RED), video_rgb(VIDEO_BLACK));
    puts(exception_message[r->int_no]);
    video_set_attr(video_rgb(VIDEO_WHITE), video_rgb(VIDEO_BLACK));
    printf("Error code: 0b%b\n", r->err_code);

    void (*handler)(regs_t*) = exception_handlers[r->int_no];
    if(!handler) handler(r);

    puts("System halted!");
    kpanic();
}

// default ISR. every interrupt will be "handled" by this function
void isr_handler(regs_t* reg) {
    void (*handler)(regs_t*) = routines[reg->int_no];
    if(handler) handler(reg);

    // if is an IRQ
    if(reg->int_no >= 32 && reg->int_no <= 47)
        pic_send_eoi(reg->int_no - 32);
}

void irq_install_handler(int irq, void (*handler)(regs_t* r)) {
    if(irq > 15) return;
    routines[irq+32] = handler;
}
void irq_uninstall_handler(int irq) {
    if(irq > 15) return;
    routines[irq+32] = 0;
}

void isr_new_interrupt(int isr, uint8_t flags, void (*handler)(regs_t* r)) {
    idt_set_descriptor(isr, isr_table[isr], flags);
    routines[isr] = handler;
}

void isr_init() {
    // set exception handler
    for(unsigned char vector = 0; vector < 32; vector++) {
        idt_set_descriptor(vector, isr_table[vector], 0x8e);
        routines[vector] = exception_handler;
    }

    // set IRQ handler
    // send commands to the PIC to make IRQ0 to 15 be remapped to IDT entry 32 to 47
    pic_remap(32, 40); // master holds 8 entries so slave is mapped to 40
    for(int vector = 32; vector < 48; vector++)
        idt_set_descriptor(vector, isr_table[vector], 0x8e);

    memset(routines + 32, 0, 255 - 32);
}

