#include "filesystem.h"
#include "ata.h"
#include "mem.h"

// TODO: implement vfs to remove this

static fs_t* FS;

static bool is_field_fs_type(uint8_t* buff, int cnt) {
    for(int i = 0; i < cnt; i++) {
        if(buff[i] != 0x20 // space
                && (buff[i] < 0x41 || buff[i] > 0x5a) // uppercase
                && (buff[i] < 0x61 || buff[i] > 0x7a) // lowercase
                && (buff[i] < 0x30 || buff[i] > 0x39)) // number
            return false;
    }

    return true;
}

static bool fat32_check(uint8_t* sect) {
    fat32_bootrecord_t* fat32_bootrec = (fat32_bootrecord_t*)sect;

    // check BPB 7.0 signature
    if(fat32_bootrec->ebpb.signature != 0x28 && fat32_bootrec->ebpb.signature != 0x29)
        return false;

    // check the filesystem type field
    if(!is_field_fs_type(fat32_bootrec->ebpb.system_identifier, 8))
        return false;

    return true;
}

// TODO: implement EXT* check
// static bool ext2_check(uint8_t* sect) {
//     return false;
// }

bool fs_mngr_init() {
    FS = (fs_t*)kmalloc(sizeof(fs_t) * MAX_DISK);

    return FS == 0;
}

fs_type_t fs_detect(partition_entry_t part) {
    uint8_t sect[512];
    ata_pio_LBA28_access(true, part.LBA_start, 1, sect);

    if(fat32_check(sect)) return FS_FAT32;
    // if(ext2_check(sect)) return FS_EXT2;
    // if(ext3_check(sect)) return FS_EXT3;
    // if(ext4_check(sect)) return FS_EXT4;

    return FS_EMPTY;
}

fs_t* fs_get(int id) {
    return FS + id;
}
