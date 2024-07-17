#pragma once

#include "stdbool.h"
#include "stdint.h"

#include "system.h"

// idk the difference between primary bus and secondary bus
// so i only use the primary bus here

#define PORT_ATA_PIO_DATA 0x1f0
#define PORT_ATA_PIO_ERROR 0x1f1
#define PORT_ATA_PIO_FEATURE 0x1f1
#define PORT_ATA_PIO_SECTOR_COUNT 0x1f2
#define PORT_ATA_PIO_LBA_LO 0x1f3
#define PORT_ATA_PIO_SECTOR_NUMBER 0x1f3
#define PORT_ATA_PIO_LBA_MI 0x1f4
#define PORT_ATA_PIO_CYLINDER_LO 0x1f4
#define PORT_ATA_PIO_LBA_HI 0x1f5
#define PORT_ATA_PIO_CYLINDER_HI 0x1f5
#define PORT_ATA_PIO_DRIVE 0x1f6
#define PORT_ATA_PIO_HEAD 0x1f6
#define PORT_ATA_PIO_STAT 0x1f7
#define PORT_ATA_PIO_COMM 0x1f7

#define PORT_ATA_PIO_ALT_STAT 0x3f6
#define PORT_ATA_PIO_DEV_CTRL 0x3f6
#define PORT_ATA_PIO_DRI_ADDR 0x3f7

#define ATA_PIO_CMD_READ_SECTORS 0x20
#define ATA_PIO_CMD_READ_SECTORS_EXT 0x24
#define ATA_PIO_CMD_WRITE_SECTORS 0x30
#define ATA_PIO_CMD_WRITE_SECTORS_EXT 0x34
#define ATA_PIO_CMD_CACHE_FLUSH 0xe7
#define ATA_PIO_CMD_IDENTIFY 0xec

// ata_pio.c
char* ata_pio_get_last_error();
bool LBA28_access(bool read_op, uint32_t lba, unsigned int sector_cnt, uint8_t* buff);
bool ata_pio_init(uint16_t* buff);
