#include "multiboot.h"

#include "system.h"
#include "process.h"
#include "syscall.h"
#include "mem.h"

#include "video.h"
#include "ata.h"
#include "kbd.h"
#include "timer.h"
#include "filesystem.h"
#include "locale.h"

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "debug.h"

#include "kshell.h"

#include "misc/elf.h"

uint32_t kernel_size;

int FS_ID = 0;
fs_t* fs = NULL;

virtual_addr_t video_addr = 0;
unsigned video_width = 0;
unsigned video_height = 0;
unsigned video_pitch = 0;
unsigned video_bpp = 0;

void mem_init(void* mmap_addr, uint32_t mmap_length) {
    // get memsize
    size_t memsize = 0;
    for(unsigned int i = 0; i < mmap_length; i += sizeof(multiboot_memory_map_t)) {
        multiboot_memory_map_t* mmmt = (multiboot_memory_map_t*)(mmap_addr + i);

        // ignore memory higher than 4GiB
        if((mmmt->addr >> 32) > 0 || (mmmt->len  >> 32) > 0) continue;
        // probaly bugs
        if(i > 0 && mmmt->addr == 0) continue;

        memsize += mmmt->len;
    }
    pmmngr_init(memsize);
    print_debug(LT_OK, "pmmngr initialised, detected %d MiB of memory\n", memsize/1024/1024);

    // init regions
    for(unsigned int i = 0; i < mmap_length; i += sizeof(multiboot_memory_map_t)) {
        multiboot_memory_map_t* mmmt = (multiboot_memory_map_t*)(mmap_addr + i);

        if((mmmt->addr >> 32) > 0 || (mmmt->len >> 32) > 0) continue;
        if(i > 0 && mmmt->addr == 0) continue;

        // if not usable or ACPI reclaimable then skip
        if(mmmt->type != MULTIBOOT_MEMORY_AVAILABLE && mmmt->type != MULTIBOOT_MEMORY_ACPI_RECLAIMABLE)
            continue;

        pmmngr_init_region(mmmt->addr, mmmt->len);
    }

    // deinit regions
    pmmngr_deinit_region(0, 4*1024*1024); // first 4MiB is preserved for kernel

    pmmngr_update_usage(); // always run this after init and deinit regions

    vmmngr_init();
}

void video_init(multiboot_info_t* mbd) {
    bool using_framebuffer = false;
    if(mbd->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) {
        video_addr = mbd->framebuffer_addr;
        video_width = mbd->framebuffer_width;
        video_height = mbd->framebuffer_height;
        video_pitch = mbd->framebuffer_pitch;
        video_bpp = mbd->framebuffer_bpp;
       
        switch(mbd->framebuffer_type) {
            case MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED:
                using_framebuffer = false;
                // very rare, not likely to happend
                // so let's just not support it hehe
                break;
            case MULTIBOOT_FRAMEBUFFER_TYPE_RGB:
                using_framebuffer = true;
                // TODO: do sth with framebuffer color_info
                break;
            case MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT:
                using_framebuffer = false;
                break;
        }
    }
    else {
        using_framebuffer = false;
        video_addr = VIDEO_TEXTMODE_ADDRESS;
        video_width = 80;
        video_height = 25;
        video_pitch = 160;
        video_bpp = 16;
    }

    // map video ptr
    for(unsigned i = 0; i < video_height * video_pitch; i += MMNGR_PAGE_SIZE)
        vmmngr_map(NULL, video_addr + i, VIDEO_START + i, PTE_WRITABLE);
    video_addr = VIDEO_START;
    if(using_framebuffer) {
        video_vesa_set_ptr(video_addr);
        video_vesa_init(
            video_width, video_height,
            video_pitch, video_bpp
        );
        print_debug(LT_OK, "VESA video initialised\n");
    }
    else {
        video_vga_set_ptr(video_addr);
        video_vga_init(video_width, video_height);
        print_debug(LT_OK, "VGA video initialised\n");
    }
}

void disk_init() {
    uint16_t* dump = kmalloc(256 * sizeof(uint16_t));
    if(!dump) {
        print_debug(LT_ER, "not enough memory to initialise disk\n");
        return;
    }

    ATA_PIO_ERR ata_err = ata_pio_init(dump);
    kfree(dump);
    if(ata_err) {
        print_debug(LT_WN, "failed to initialise ATA PIO mode. error code %d\n", ata_err);
        return;
    }

    print_debug(LT_OK, "ATA PIO mode initialised\n");

    bool mbr_err = mbr_load();
    if(mbr_err) {
        print_debug(LT_ER, "cannot load MBR\n");
        return;
    }
    print_debug(LT_OK, "MBR loaded\n");

    for(int i = 0; i < 4; i++) {
        partition_entry_t part = mbr_get_partition_entry(i);
        if(part.sector_count == 0) continue;

        FS_ERR err;
        switch(fs_detect(part)) {
            case FS_EMPTY:
                break;
            case FS_FAT32:
                err = fat32_init(part, FS_ID);
                if(err) {
                    print_debug(LT_ER, "failed to initialize FAT32 filesystem in partition %d. error code %d\n", i+1, err);
                    break;
                }
                print_debug(LT_OK, "initialised FAT32 filesystem in partition %d\n", i+1);
                fs = fs_get(FS_ID);
                break;
            case FS_EXT2:
                print_debug(LT_WN, "EXT2 filesystem in partition %d is not implemented, the partition will be ignored\n", i+1);
                break;
        }
    }

    if(fs->root_node.valid) {
        fs->root_node.name[0] = '/';
        fs->root_node.name[1] = '\0';
    }
}

void enter_usermode() {
    puts("entering usermode ...");

    // enter usermode
    uint32_t esp = 0;
    asm volatile("mov %%esp, %%eax" : "=a" (esp));
    tss_set_stack(esp);
    asm volatile(
        "cli;"
        "mov $0x23, %ax;"
        "mov %ax, %ds;"
        "mov %ax, %es;"
        "mov %ax, %fs;"
        "mov %ax, %gs;"

        "mov %esp, %eax;"
        "pushl $0x23;"
        "pushl %eax;"
        "pushf;"

        // enabler IF flag in EFLAGS (which will enable interrupts after `iret`)
        "pop %eax;"
        "or $0x200, %eax;"
        "push %eax;"

        "pushl $0x1b;"
        "push $1f;"
        "iret;"
        "1:"
    );

    // test syscall
    int ret;
    SYSCALL_0P(SYSCALL_TEST, ret);

    SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'h');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'e');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'l');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'l');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'o');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, ' ');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'u');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, 's');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'e');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'r');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, '!');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, '\n');

    SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'c');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'u');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'r');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'r');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'e');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'n');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, 't');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, ' ');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, 't');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'i');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'm');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'e');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, ':');
    SYSCALL_1P(SYSCALL_PUTCHAR, ret, ' ');

    SYSCALL_0P(SYSCALL_TIME, ret);
    char conv[64];
    itoa(ret, conv, 10);
    for(unsigned i = 0; i < 64 && conv[i] != '\0'; i++)
        SYSCALL_1P(SYSCALL_PUTCHAR, ret, conv[i]);

    // hlt instruction should be illegal
    asm("hlt");
}

void test_process() {
    int ret;
    // FIXME: calling syscall with params will cause a page fault
    // maybe it is sth to do with the "call *%6;" in syscall dispatcher

    // SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'h');
    // SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'e');
    // SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'l');
    // SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'l');
    // SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'o');
    // SYSCALL_1P(SYSCALL_PUTCHAR, ret, ' ');
    // SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'u');
    // SYSCALL_1P(SYSCALL_PUTCHAR, ret, 's');
    // SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'e');
    // SYSCALL_1P(SYSCALL_PUTCHAR, ret, 'r');
    // SYSCALL_1P(SYSCALL_PUTCHAR, ret, '!');
    // SYSCALL_1P(SYSCALL_PUTCHAR, ret, '\n');

    SYSCALL_0P(SYSCALL_TEST, ret);
    SYSCALL_0P(SYSCALL_PROCESS_TERMINATE, ret);
    while(true);
}

extern char kernel_start;
extern char kernel_end;
void kmain(multiboot_info_t* mbd) {
    kernel_size = &kernel_end - &kernel_start;

    // greeting msg to let us know we are in the kernel
    // note that this will print into preinit video buffer
    // and will not drawn to screen until video is initialised
    puts("hello");
    printf("this is "); puts("kernosene!");
    printf("build datetime: %s, %s\n", __TIME__, __DATE__);
    printf("kernel size: %d bytes\n", kernel_size);

    // since we have mapped 4MiB from 0x0 to 0xc0000000
    // any physical address under 4MiB can be converted to virtual address
    // by adding 0xc0000000 to it
    // i think that GRUB will not give any address that are larger than 4 MiB
    // except the framebuffer
    // note that the ELF section also included in the kernel (which lies in the first 4MiB)
    // so those physical address of ELF section headers need to offset to KERNEL_START too
    mbd = (void*)mbd + KERNEL_START;

    // disable interrupts to set up things
    asm volatile("cli");

    if(!(mbd->flags & MULTIBOOT_INFO_MEM_MAP)) kernel_panic(NULL);
    mem_init((void*)mbd->mmap_addr + KERNEL_START, mbd->mmap_length);

    video_init(mbd);

    bool kheap_err = kheap_init();
    if(kheap_err) {
        print_debug(LT_ER, "failed to initialise kernel heap. not enough memory\n");
        kernel_panic(NULL);
    }
    print_debug(LT_OK, "kernel heap initialised\n");

    if(mbd->flags & MULTIBOOT_INFO_BOOT_LOADER_NAME)
        print_debug(LT_IF, "using %s bootloader\n", mbd->boot_loader_name + KERNEL_START);

    
    // if(mbd->flags & MULTIBOOT_INFO_AOUT_SYMS) {
    //     aout_sym = mbd->u.aout_sym;
    // }

    // TODO: implement the a.out option, assumption is bad

    // we gave GRUB an ELF binary so GRUB will not give us the a.out symbol table option (commented above)
    // also only one of the two (a.out option or ELF option) must be existed
    // so we dont need to check the flag
    multiboot_elf_section_header_table_t* elf_sec = &(mbd->u.elf_sec);
    elf_section_header_t* shstrtab_sh = (elf_section_header_t*)
            (elf_sec->addr + KERNEL_START + elf_sec->shndx * elf_sec->size);
    char* shstrtab = (char*)(shstrtab_sh->addr + KERNEL_START);

    // find symtab and strtab
    for(unsigned i = 0; i < elf_sec->num; i++) {
        elf_section_header_t* sh = (elf_section_header_t*)
            (elf_sec->addr + KERNEL_START + i * elf_sec->size);
        char* sec_name = shstrtab + sh->name;

        if(strcmp(sec_name, ".symtab")) {
            print_debug(LT_IF, "found .symtab section\n");
            kernel_set_symtab_sh_ptr((uint32_t)sh);
        }
        else if(strcmp(sec_name, ".strtab")) {
            print_debug(LT_IF, "found .strtab section\n");
            kernel_set_strtab_ptr(sh->addr + KERNEL_START);
        }
    }

    gdt_init();
    print_debug(LT_OK, "GDT initialised\n");
    idt_init();
    print_debug(LT_OK, "IDT initialised\n");
    isr_init();
    print_debug(LT_OK, "ISR initialised\n");

    process_init();

    syscall_init();
    print_debug(LT_OK, "syscall initialised\n");

    if(!fs_mngr_init()) disk_init();
    else print_debug(LT_ER, "failed to initialise FS. not enough memory\n");

    locale_set_keyboard_layout(KBD_LAYOUT_US);
    print_debug(LT_IF, "set keyboard layout to US\n");

    kbd_init();
    print_debug(LT_OK, "keyboard initialised\n");

    timer_init();
    print_debug(LT_OK, "timer initialised\n");

    // start interrupts again after setting up everything
    asm volatile("sti");

    print_debug(LT_IF, "done initialising\n");

    if(!shell_init()) {
        // only set if fs is available
        if(fs) shell_set_root_node(fs->root_node);
    }
    else puts("not enough memory for kshell. quitting");

    page_directory_t* new_pd = vmmngr_alloc_page_directory();
    int pid = process_new(new_pd, 0, (uint32_t)test_process);
    printf("executing process %d\n", pid);
    process_exec();

    shell_start();

    puts("system hang");

    while(true);
}
