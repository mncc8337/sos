#include "mem.h"

// bitmap, the length is hardcoded because memory size will not exceed 3GiB
static uint32_t bitmap[24576]; // 3GiB * 1024*1024*1024 / 4096(block size) / 32 (size of uint32_t)
static size_t used_block;
static size_t total_block;

static void set_bit(uint32_t bit) {
    bitmap[bit/32] |= (1 << (bit % 32));
}
static void unset_bit(uint32_t bit) {
    bitmap[bit/32] &= ~(1 << (bit % 32));
}
// static bool test_bit(uint32_t bit) {
//     return (bitmap[bit/32] >> (bit % 32)) & 1;
// }

static int find_first_free_block() {
    for(unsigned i = 0; i < total_block/32; i++) {
        if(bitmap[i] == 0xffffffff) continue;
        for(int j = 0; j < 32;j++) {
            int bit = 1 << j;
            if(!(bitmap[i] & bit)) return i * 32 + j;
        }
    }
    return -1;
}
static int find_first_free(size_t frame_cnt) {
    if(frame_cnt == 0) return -1;
    if(frame_cnt == 1) return find_first_free_block();

    for(unsigned int i = 0; i < total_block/32; i++) {
        if(bitmap[i] == 0xfffffff) continue;
        for(int j = 0; j < 32;j++) {
            int bit = 1 << j;
            if(bitmap[i] & bit) continue;

            for(uint32_t k = 1; k < frame_cnt; k++) {
                if(bitmap[i] & (bit << k)) {
                    // we know that this segment has not enough space so we can skip
                    j += k;
                    break;
                }
                if(k == frame_cnt - 1) return i * 32 + j;
            }
        }
    }
    return -1;
}

// update used_block
// must be run after done initializing regions
void pmmngr_update_usage() {
    used_block = 0;
    for(unsigned i = 0; i < total_block/32; i++) {
        if(bitmap[i] == 0xffffffff) {
            used_block += 32;
            continue;
        }
        for(int j = 0; j < 32;j++) {
            int bit = 1 << j;
            if((bitmap[i] & bit)) used_block++;
        }
    }
    if(used_block > total_block) used_block = total_block;
}
size_t pmmngr_get_size() {
    return total_block * MMNGR_PAGE_SIZE;
}
size_t pmmngr_get_used_size() {
    return used_block * MMNGR_PAGE_SIZE;
}
size_t pmmngr_get_free_size() {
    return (total_block - used_block) * MMNGR_PAGE_SIZE;
}

void pmmngr_init_region(physical_addr_t base, size_t size) {
    int start = base / MMNGR_PAGE_SIZE;
    int block = size / MMNGR_PAGE_SIZE;

    for (; block > 0; block--)
        unset_bit(start++);

    set_bit(0); 
}

void pmmngr_deinit_region(physical_addr_t base, size_t size) {
    int start = base / MMNGR_PAGE_SIZE;
    int block = size / MMNGR_PAGE_SIZE;

    for (;block > 0; block--)
        set_bit(start++);
}

void* pmmngr_alloc_block() {
    if(total_block == used_block) return NULL;
    int frame = find_first_free_block();
    if(frame == -1) return NULL;

    set_bit(frame);

    physical_addr_t base = frame * MMNGR_PAGE_SIZE;
    used_block++;

    return (void*)base;
}
void* pmmngr_alloc_multi_block(size_t cnt) {
    if(used_block + cnt > total_block) return NULL;
    int frame = find_first_free(cnt);
    if(frame == -1) return NULL;

    for(uint32_t i = 0; i < cnt; i++)
        set_bit(frame + i);

    physical_addr_t addr = frame * MMNGR_PAGE_SIZE;
    used_block += cnt;

    return (void*)addr;
}
void pmmngr_free_block(void* base) {
    physical_addr_t addr = (physical_addr_t)base;
    int frame = addr / MMNGR_PAGE_SIZE;

    if(frame == 0) return;

    unset_bit(frame);
    used_block--;
}
void pmmngr_free_multi_block(void* base, size_t cnt) {
    physical_addr_t addr = (physical_addr_t)base;
    int frame = addr / MMNGR_PAGE_SIZE;

    if(frame == 0) return;

    for(uint32_t i = 0; i < cnt; i++)
        unset_bit(frame+i);
    used_block -= cnt;
}

void pmmngr_init(size_t size) {
    total_block = size / MMNGR_PAGE_SIZE;

    // assume that all memory are in use
    used_block = total_block;
    for(unsigned i = 0; i < total_block / 32; i++)
        bitmap[i] = 0xffffffff;
}
