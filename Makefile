C_SRC = $(wildcard kernel/*.c kernel/driver/*.c kernel/system/*.c)
C_OBJ = $(addsuffix .o, $(basename $(notdir $(C_SRC))))

# number of sectors that the kernel will fit in
KERNEL_PADDING = 15
# memory address that the kernel will be loaded to
KERNEL_OFFSET = 0x1000
# memory address that second stage bootloader will be loaded to
SECOND_STAGE_OFFSET= 0x7e00

DEFINES = -DKERNEL_PADDING=$(KERNEL_PADDING) \
		  -DKERNEL_OFFSET=$(KERNEL_OFFSET) \
		  -DSECOND_STAGE_OFFSET=$(SECOND_STAGE_OFFSET)

C_FLAGS = $(DEFINES) -ffreestanding -m32 -mtune=i386 -fno-pie -nostdlib -nostartfiles
LD_FLAGS = -T linker.ld -m elf_i386
NASM_FLAGS = $(DEFINES) -f elf

all: disk.img

stage1.bin: boot/stage1.asm
	nasm -f bin -o $@ -I./boot/include $< $(DEFINES)
stage2.bin: boot/stage2.asm
	nasm -f bin -o $@ -I./boot/include $< $(DEFINES)
bootsect.bin: stage1.bin stage2.bin
	cat $^ > $@

kernel_entry.o: kernel/kernel_entry.asm
	nasm $(NASM_FLAGS) -o $@ $<
%.o: kernel/%.c
	gcc $(C_FLAGS) -o $@ -I./kernel/include -c $<
%.o: kernel/driver/%.c
	gcc $(C_FLAGS) -o $@ -I./kernel/include -c $<
%.o: kernel/system/%.c
	gcc $(C_FLAGS) -o $@ -I./kernel/include -c $<

kernel.bin: kernel_entry.o $(C_OBJ)
	ld $(LD_FLAGS) -o kernel.bin -Ttext $(KERNEL_OFFSET) $^ --oformat binary
	dd if=/dev/zero of=$@ bs=512 count=0 seek=$(KERNEL_PADDING)

disk.img: bootsect.bin kernel.bin
	cat $^ > $@

run: all
	qemu-system-i386 disk.img
debug: all
	qemu-system-i386 disk.img -s -S
clean:
	rm *.bin *.o *.img
