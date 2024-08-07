TARGET = i686-elf
CROSS_COMPILER_LOC = ./cross/bin/

CC = $(CROSS_COMPILER_LOC)$(TARGET)-gcc
# LD = $(CROSS_COMPILER_LOC)$(TARGET)-ld
ASM = nasm

BIN = bin/

LIBC_SRC = libc/stdio/*.c \
		   libc/stdlib/*.c \
		   libc/string/*.c \
		   libc/time/*.c \

C_SRC = kernel/src/*.c \
		kernel/src/system/*.c \
		kernel/src/driver/*.c \
		kernel/src/driver/ata/*.c \
		kernel/src/filesystem/*.c \
		kernel/src/mem/*.c \

ASM_SRC = kernel/src/system/*.asm \
		  kernel/src/mem/*.asm \

_LIBC_OBJ = $(addsuffix .o, $(basename $(notdir $(wildcard $(LIBC_SRC)))))
_OBJ  = $(addsuffix .o, $(basename $(notdir $(wildcard $(C_SRC)))))
_OBJ += $(addsuffix .o, $(notdir $(wildcard $(ASM_SRC))))

LIBC_OBJ = $(addprefix $(BIN), $(_LIBC_OBJ))
OBJ = $(addprefix $(BIN), $(_OBJ))

DEFINES = -D__is_libk

C_INCLUDES = -I./kernel/include -I./libc/include

# CFLAGS = $(DEFINES) -ffreestanding -m32 -mtune=i386 -fno-pie -nostdlib -nostartfiles
# LDFLAGS = -T linker.ld -m elf_i386 -nostdlib --nmagic
CFLAGS = $(DEFINES) -ffreestanding -O2 -Wall -Wextra -std=gnu99 -g -MMD -MP
LDFLAGS = -T linker.ld -nostdlib -lgcc
NASMFLAGS = $(DEFINES) -f elf32 -F dwarf

.PHONY: all libc libk kernel disk copyfs run run-debug clean clean-all

all: $(BIN) $(BIN)kernel.bin disk copyfs

$(BIN):
	mkdir $(BIN)

$(BIN)%.o: libc/stdio/%.c
	$(CC) $(CFLAGS) -o $@ $(C_INCLUDES) -c $<
$(BIN)%.o: libc/stdlib/%.c
	$(CC) $(CFLAGS) -o $@ $(C_INCLUDES) -c $<
$(BIN)%.o: libc/string/%.c
	$(CC) $(CFLAGS) -o $@ $(C_INCLUDES) -c $<
$(BIN)%.o: libc/time/%.c
	$(CC) $(CFLAGS) -o $@ $(C_INCLUDES) -c $<
$(BIN)libk.a: $(LIBC_OBJ)
	$(CROSS_COMPILER_LOC)$(TARGET)-ar rcs $@ $^
	@echo done building libk
$(BIN)libc.a: $(LIBC_OBJ)
	@echo libc is not ready for build, yet

$(BIN)kernel_entry.o: kernel/src/kernel_entry.asm
	$(ASM) $(NASMFLAGS) -o $@ $<

$(BIN)%.o: kernel/src/%.c
	$(CC) $(CFLAGS) -o $@ $(C_INCLUDES) -c $<
$(BIN)%.o: kernel/src/system/%.c
	$(CC) $(CFLAGS) -o $@ $(C_INCLUDES) -c $<
$(BIN)%.o: kernel/src/driver/%.c
	$(CC) $(CFLAGS) -o $@ $(C_INCLUDES) -c $<
$(BIN)%.o: kernel/src/driver/ata/%.c
	$(CC) $(CFLAGS) -o $@ $(C_INCLUDES) -c $<
$(BIN)%.o: kernel/src/filesystem/%.c
	$(CC) $(CFLAGS) -o $@ $(C_INCLUDES) -c $<
$(BIN)%.o: kernel/src/mem/%.c
	$(CC) $(CFLAGS) -o $@ $(C_INCLUDES) -c $<

$(BIN)%.asm.o: kernel/src/system/%.asm
	$(ASM) $(NASMFLAGS) -o $@ $<
$(BIN)%.asm.o: kernel/src/mem/%.asm
	$(ASM) $(NASMFLAGS) -o $@ $<

$(BIN)kernel.bin: $(BIN)kernel_entry.o $(OBJ) $(BIN)libk.a
	@echo $(OBJ)
	# use GCC to link instead of LD because LD cannot find the libgcc
	$(CC) $(LDFLAGS) -o $@ $^ -L./bin -lk

libc: $(BIN)libc.a

libk: $(BIN)libk.a

kernel: $(BIN)kernel.bin

disk: kernel
	if [ ! -f disk.img ]; then ./script/gendiskimage.sh 65536; fi

copyfs: disk
	./script/cpyfile.sh grub.cfg ./mnt/boot/grub/ # update grub config
	./script/cpyfile.sh bin/kernel.bin ./mnt/boot/ # update kernel
	for file in fsfiles/*; do ./script/cpyfile.sh $$file ./mnt/; done

run:
	./script/run.sh

run-debug:
	./script/run.sh debug

clean:
	rm -r $(BIN)

clean-all: clean
	rm disk.img disk-backup.img

DEPS  = $(patsubst %.o,%.d,$(OBJ))
DEPS += $(patsubst %.o,%.d,$(LIBC_OBJ))
-include $(DEPS)
