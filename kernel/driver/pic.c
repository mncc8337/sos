#include "driver/pic.h"

void pic_send_eoi(unsigned char irq) {
    if(irq >= 8)
        port_outb(PORT_PIC2_COM, PIC_EOI);
    port_outb(PORT_PIC1_COM, PIC_EOI);
}

void pic_remap(int offset1, int offset2) {
    unsigned char a1, a2;

    a1 = port_inb(PORT_PIC1_DAT);                        // save masks
    a2 = port_inb(PORT_PIC2_DAT);

    port_outb(PORT_PIC1_COM, ICW1_INIT | ICW1_ICW4);  // starts the initialization sequence (in cascade mode)
    io_wait();
    port_outb(PORT_PIC2_COM, ICW1_INIT | ICW1_ICW4);
    io_wait();
    port_outb(PORT_PIC1_DAT, offset1);                 // ICW2: Master PIC vector offset
    io_wait();
    port_outb(PORT_PIC2_DAT, offset2);                 // ICW2: Slave PIC vector offset
    io_wait();
    port_outb(PORT_PIC1_DAT, 4);                       // ICW3: tell Master PIC that there is a slave PIC at IRQ2 (0000 0100)
    io_wait();
    port_outb(PORT_PIC2_DAT, 2);                       // ICW3: tell Slave PIC its cascade identity (0000 0010)
    io_wait();

    port_outb(PORT_PIC1_DAT, ICW4_8086);               // ICW4: have the PICs use 8086 mode (and not 8080 mode)
    io_wait();
    port_outb(PORT_PIC2_DAT, ICW4_8086);
    io_wait();

    port_outb(PORT_PIC1_DAT, a1);   // restore saved masks.
    port_outb(PORT_PIC2_DAT, a2);
}

void pic_disable() {
    port_outb(PORT_PIC1_DAT, 0xff);
    port_outb(PORT_PIC2_DAT, 0xff);
}

void irq_set_mask(unsigned char irq_line) {
    unsigned short port;
    unsigned char value;

    if(irq_line < 8) {
        port = PORT_PIC1_DAT;
    }
    else {
        port = PORT_PIC2_DAT;
        irq_line -= 8;
    }
    value = port_inb(port) | (1 << irq_line);
    port_outb(port, value);        
}

void irq_clear_mask(unsigned char irq_line) {
    unsigned short port;
    unsigned char value;

    if(irq_line < 8) {
        port = PORT_PIC1_DAT;
    }
    else {
        port = PORT_PIC2_DAT;
        irq_line -= 8;
    }
    value = port_inb(port) & ~(1 << irq_line);
    port_outb(port, value);        
}
