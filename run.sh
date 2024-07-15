#!/bin/sh

QEMUFLAGS="-m 128M \
          -debugcon stdio \
          -no-reboot \
          -accel tcg,thread=single \
          -usb"

if [ "$1" == "debug" ]; then
    qemu-system-i386 -hda disk.img $QEMUFLAGS -s -S &
    gdb kernel.elf \
        -ex 'target remote localhost:1234' \
        -ex 'layout src' \
        -ex 'layout reg' \
        -ex 'break main' \
        -ex 'continue'
else
    qemu-system-i386 -hda disk.img $QEMUFLAGS
fi