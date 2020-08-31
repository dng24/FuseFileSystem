
#include <assert.h>
#include <stdint.h>

#include "pages.h"
#include "inode.h"
#include "util.h"
#include "bitmap.h"
#include "version.h"

const int INODE_COUNT = 256;

inode*
get_inode(int inum)
{
    printf("get_inode(%d)\n", inum);
    assert(inum >= 0 && inum < 256);
    uint8_t* base = (uint8_t*) pages_get_page(1);
    inode* nodes = (inode*)(base);
    return &(nodes[inum]);
}

int
alloc_inode()
{
    int node_num = bitmap_set(get_inode_bitmap(), INODE_COUNT);
    //printf("IIINODE %d\n", node_num);
    if (node_num < 0) {
        return node_num;
    }
    inode* node = get_inode(node_num);
    memset(node, 0, sizeof(inode));
    node->mode = 0100644;
    printf("+ alloc_inode() -> %d\n", node_num);
    return node_num;
}

void
free_inode(int inum)
{
    printf("+ free_inode(%d)\n", inum);
    version* ver_arr = pages_get_page(0) + 64;
    size_t* bm = ver_arr[(get_current_version() + 1) % 21].bitmaps;
    bitmap_free(bm + 4, inum);
}

void
print_inode(inode* node)
{
    if (node) {
        printf("node{mode: %04o, size: %d, refs: %d, ptr0: %d, ptr1: %d, iptr: %d,\n", node->mode, node->size, node->refs, node->ptrs[0], node->ptrs[1], node->iptr);
        printf("     acc: %ld.%ld, mod:%ld.%ld, chg: %ld.%ld}\n", node->ts[0].tv_sec, node->ts[0].tv_nsec, node->ts[1].tv_sec, node->ts[1].tv_nsec, node->ts[2].tv_sec, node->ts[2].tv_nsec);
    }
    else {
        printf("node{null}\n");
    }
}

int cpy_inode(inode* node) {
    //puts("COPY INODE");
    //print_inode(node);
    int tmp = alloc_inode();
    if (tmp < 0) {
        //printf("cpy_inode(...) -> %d\n", tmp);
        //print_inode(node);
        return tmp;
    }
    inode* cpy = get_inode(tmp);
    cpy->size = node->size;
    cpy->mode = node->mode;
    cpy->refs = node->refs;
    cpy->ptrs[0] = node->ptrs[0];
    cpy->ptrs[1] = node->ptrs[1];
    cpy->iptr = node->iptr;
    cpy->ts[0] = node->ts[0];
    cpy->ts[1] = node->ts[1];
    cpy->ts[2] = node->ts[2];
    //printf("cpy_inode(...) -> 0\n");
    //print_inode(cpy);
    return tmp;
}

int grow_inode(inode* node, int num_blocks) {
    //printf("GROW by blocks: %d\n", num_blocks);
    int i = 0;
    for (; i < num_blocks; i++) {
        int page = alloc_page();
        if (page < 0) {
            return page;
        }
    
        if (node->ptrs[0] == 0) {
            node->ptrs[0] = page;
        } else if (node->ptrs[1] == 0) {
            node->ptrs[1] = page;
        } else {
            if (node->iptr == 0) {
                int ptr_pg = alloc_page();
                if (ptr_pg < 0) {
                    return ptr_pg;
                }
                node->iptr = ptr_pg;
                memset(pages_get_page(node->iptr), 0, 4096);
            }

            short* pg_ptr = pages_get_page(node->iptr);
            int j = 0;
            while (pg_ptr[j] != 0) {
                j++;
            }
            pg_ptr[j] = page;
        }
    }
    return 0;
}

void shrink_inode(inode* node, int num_blocks) {
    //printf("SHRINK by blocks: %d\n", num_blocks);
    int i = 0;
    while (i < num_blocks) {
        if (node->ptrs[1] == 0) {
            free_page(node->ptrs[0]);
            node->ptrs[0] = 0;
            i++;
        } else if (node->iptr == 0) {
            free_page(node->ptrs[1]);
            node->ptrs[1] = 0;
            i++;
        } else {
            short* pg_ptr = (short*) pages_get_page(node->iptr);
            int j = 0;
            while (pg_ptr[j] != 0) {
                j++;
            }
            int k = j - 1;
            for (; k >= 0 && i < num_blocks; k--) {
                free_page(pg_ptr[k]);
                pg_ptr[k] = 0;
                i++;
            }
            if (k < 0) {
                free_page(node->iptr);
                node->iptr = 0;
            }
        }
    }
}
