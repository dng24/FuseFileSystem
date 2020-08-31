
#define _GNU_SOURCE
#include <string.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>

#include "pages.h"
#include "util.h"
#include "bitmap.h"
#include "version.h"

const int PAGE_COUNT = 256;
const int NUFS_SIZE  = 4096 * 256; // 1MB

static int   pages_fd   = -1;
static void* pages_base =  0;

void
pages_init(const char* path, int create)
{
    if (create) {
        pages_fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0644);
        assert(pages_fd != -1);

        int rv = ftruncate(pages_fd, NUFS_SIZE);
        assert(rv == 0);
    }
    else {
        pages_fd = open(path, O_RDWR);
        assert(pages_fd != -1);
    }

    pages_base = mmap(0, NUFS_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, pages_fd, 0);
    assert(pages_base != MAP_FAILED);

    if (create) {
        memset(pages_base, 0, 4096); //init bitmaps

        bitmap_set(get_pages_bitmap(), PAGE_COUNT); //page 0 used for bitmaps & versions
        bitmap_set(get_pages_bitmap(), PAGE_COUNT); //page 1 used for inodes 0-63
        bitmap_set(get_pages_bitmap(), PAGE_COUNT); //page 2 used for inodes 64-127
        bitmap_set(get_pages_bitmap(), PAGE_COUNT); //page 3 used for inodes 128-191
        bitmap_set(get_pages_bitmap(), PAGE_COUNT); //page 4 used for inodes 192-255
        bitmap_set(get_inode_bitmap(), 256); //inode 0 is root dir
    }
}

void
pages_free()
{
    int rv = munmap(pages_base, NUFS_SIZE);
    assert(rv == 0);
}

void*
pages_get_page(int pnum)
{
    assert(pnum > -1 && pnum < PAGE_COUNT);
    return pages_base + 4096 * pnum;
}

void* get_pages_bitmap() {
    return pages_get_page(0);
}

void* get_inode_bitmap() {
    return pages_get_page(0) + 32;
}

int alloc_page() {
    int page = bitmap_set(get_pages_bitmap(), PAGE_COUNT);
    if (page < 0) {
        return page;
    }
    memset(pages_get_page(page), 0, 4096);
    //printf("PAGGGGGGGGGGGGGGGGGGGGGGEEEEEEEEEEEEEEEEE %d\n", page);
    printf("+ alloc_page() -> %d\n", page); 
    return page;
}

void free_page(int pnum) {
    printf("+ free_page(%d)\n", pnum);
    assert(pnum > 4); //first 5 pages used for bitmaps and inodes
    version* ver_arr = pages_get_page(0) + 64;
    size_t* bm = ver_arr[(get_current_version() + 1) % 21].bitmaps;
    bitmap_free(bm, pnum);
}
