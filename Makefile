TARGET = i686-elf
CROSS_COMPILER_LOC = ./cross/bin/

CC = $(CROSS_COMPILER_LOC)$(TARGET)-gcc
# LD = $(CROSS_COMPILER_LOC)$(TARGET)-ld
GDB = gdb
ASM = nasm
QEMU = qemu-system-i386

C_SRC = kernel/*.c \
		kernel/utils/*.c \
		kernel/system/*.c \
		kernel/driver/*.c \
		kernel/mem/*.c \

C_SRC_WILDCARD = $(wildcard $(C_SRC))
A_SRC_WILDCARD = $(wildcard $(A_SRC))
OBJ = $(addsuffix .o, $(basename $(notdir $(C_SRC_WILDCARD))))

# number of sectors that the kernel will fit in
KERNEL_SECTOR_COUNT = 50
# memory address that second stage bootloader will be loaded to
SECOND_STAGE_ADDR = 0x7e00
# memory address that memmap entry count will be loaded to
MMAP_ENTRY_CNT_ADDR = 0x8000
# memory address that memmap will be loaded to
MMAP_ADDR = 0xB00
# memory address that the kernel will be loaded to
KERNEL_ADDR = 0x1000

DEFINES = -DKERNEL_SECTOR_COUNT=$(KERNEL_SECTOR_COUNT) \
		  -DSECOND_STAGE_ADDR=$(SECOND_STAGE_ADDR) \
		  -DMMAP_ENTRY_CNT_ADDR=$(MMAP_ENTRY_CNT_ADDR) \
		  -DMMAP_ADDR=$(MMAP_ADDR) \
		  -DKERNEL_ADDR=$(KERNEL_ADDR)

C_INCLUDES = -I./kernel/include

# CFLAGS = $(DEFINES) -ffreestanding -m32 -mtune=i386 -fno-pie -nostdlib -nostartfiles
# LDFLAGS = -T linker.ld -m elf_i386 -nostdlib --nmagic
CFLAGS = $(DEFINES) -ffreestanding -O2 -Wall -Wextra -std=gnu99
LDFLAGS = -T link.ld -nostdlib -lgcc
NASMFLAGS = $(DEFINES) -f elf32 -F dwarf

QEMUFLAGS = -m 128M -debugcon stdio -no-reboot

all: disk.img

stage1.bin: boot/stage1.asm
	$(ASM) -f bin -o $@ -I./boot/include $< $(DEFINES)
stage2.bin: boot/stage2.asm
	$(ASM) -f bin -o $@ -I./boot/include $< $(DEFINES)
bootloader.bin: stage1.bin stage2.bin
	cat $^ > $@

kernel_entry.o: kernel/kernel_entry.asm
	$(ASM) $(NASMFLAGS) -o $@ $<
%.o: kernel/%.c
	$(CC) $(CFLAGS) -o $@ $(C_INCLUDES) -c $<
%.o: kernel/driver/%.c
	$(CC) $(CFLAGS) -o $@ $(C_INCLUDES) -c $<
%.o: kernel/system/%.c
	$(CC) $(CFLAGS) -o $@ $(C_INCLUDES) -c $<
%.o: kernel/utils/%.c
	$(CC) $(CFLAGS) -o $@ $(C_INCLUDES) -c $<
%.o: kernel/mem/%.c
	$(CC) $(CFLAGS) -o $@ $(C_INCLUDES) -c $<

kernel.elf: kernel_entry.o $(OBJ)
	# im using GCC to link instead of LD because LD cannot find the libgcc
	$(CC) $(LDFLAGS) -o $@ -Ttext $(KERNEL_ADDR) $^
kernel.bin: kernel.elf
	$(CROSS_COMPILER_LOC)$(TARGET)-objcopy -O binary $< $@
	dd if=/dev/zero of=$@ bs=512 count=0 seek=$(KERNEL_SECTOR_COUNT)

disk.img: bootloader.bin kernel.bin
	cat $^ > $@

run: all
	$(QEMU) -drive file=disk.img,format=raw $(QEMUFLAGS)
debug: all
	$(QEMU) -drive file=disk.img,format=raw $(QEMUFLAGS) -s -S &
	$(GDB) kernel.elf \
        -ex 'target remote localhost:1234' \
        -ex 'layout src' \
        -ex 'layout reg' \
        -ex 'break main' \
        -ex 'continue'
clean:
	rm *.bin *.o *.elf *.img
