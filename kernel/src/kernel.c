#include "multiboot.h"

#include "system.h"
#include "syscall.h"
#include "kpanic.h"
#include "mem.h"

#include "video.h"
#include "ata.h"
#include "kbd.h"
#include "timer.h"
#include "filesystem.h"

#include "stdio.h"
#include "debug.h"

#include "kshell.h"

uint32_t kernel_size;

char freebuff[512];

int FS_ID = 0;
fs_t* fs;

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
    pmmngr_deinit_region(0, 2*1024*1024 + kernel_size); // kernel start at 2MiB

    pmmngr_update_usage(); // always run this after init and deinit regions

    // vmmngr init
    MEM_ERR err = vmmngr_init();
    if(err != ERR_MEM_SUCCESS) kpanic();
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
        vmmngr_map_page(video_addr + i, VMBASE_VIDEO + i, PTE_WRITABLE);
    video_addr = VMBASE_VIDEO;
    if(using_framebuffer) {
        video_framebuffer_set_ptr(video_addr);
        video_framebuffer_init(
            video_width, video_height,
            video_pitch, video_bpp
        );
        print_debug(LT_OK, "VESA video initialised\n");
    }
    else {
        video_textmode_set_ptr(video_addr);
        video_textmode_init(video_width, video_height);
        print_debug(LT_OK, "VGA video initialised\n");
    }
}

void disk_init() {
    if(!ata_pio_init((uint16_t*)freebuff)) {
        print_debug(LT_WN, "failed to initialise ATA PIO mode\n");
        return;
    }

    print_debug(LT_OK, "ATA PIO mode initialised\n");

    bool mbr_ok = mbr_load();
    if(!mbr_ok) {
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
                if(err != ERR_FS_SUCCESS) {
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

extern void* kernel_start;
extern void* kernel_end;
void kmain(multiboot_info_t* mbd) {
    // greeting msg to let us know we are in the kernel
    // note that this will print into preinit video buffer
    // and will not drawn to screen until video is initialised
    puts("hello");
    printf("this is "); puts("kernosene!");
    printf("build datetime: %s, %s\n", __TIME__, __DATE__);

    // since we have mapped 4MiB from 0x0 to 0xc0000000
    // any physical address under 4MiB can be converted to virtual address
    // by adding 0xc0000000 to it
    // i think that GRUB will not give any address that are larger than 4 MiB
    // except the framebuffer
    mbd = (void*)mbd + VMBASE_KERNEL;

    kernel_size = &kernel_end - &kernel_start;

    // disable interrupts to set up things
    asm volatile("cli");

    if(!(mbd->flags & MULTIBOOT_INFO_MEM_MAP)) kpanic();
    mem_init((void*)mbd->mmap_addr + VMBASE_KERNEL, mbd->mmap_length);

    video_init(mbd);

    if(mbd->flags & MULTIBOOT_INFO_BOOT_LOADER_NAME)
        print_debug(LT_IF, "using %s bootloader\n", mbd->boot_loader_name + VMBASE_KERNEL);

    gdt_init();
    print_debug(LT_OK, "GDT initialised\n");
    uint32_t esp = 0;
    asm volatile("mov %%esp, %%eax" : "=a" (esp));
    tss_install(0x10, esp);
    print_debug(LT_OK, "TSS installed\n");
    idt_init();
    print_debug(LT_OK, "IDT initialised\n");
    isr_init();
    print_debug(LT_OK, "ISR initialised\n");

    disk_init();

    syscall_init();
    print_debug(LT_OK, "syscall initialised\n");

    kbd_init();

    timer_init();

    // start interrupts again after setting up everything
    asm volatile("sti");

    print_debug(LT_IF, "done initialising\n");

    shell_set_root_node(fs->root_node);
    shell_start();

    // // i cannot get this to work :(
    // // enter_usermode();

    // // test "syscall"
    // // the privilege is still ring 0, though
    // asm volatile("xor %eax, %eax; int $0x80"); // SYS_SYSCALL_TEST
    // asm volatile("xor %eax, %eax; inc %eax; int $0x80"); // SYS_PUTCHAR

    while(true);
}
