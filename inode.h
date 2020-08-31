#ifndef INODE_H
#define INODE_H

#include <time.h>

#include "pages.h"

typedef struct inode {
    int size; // bytes
    int mode; // permission & type; zero for unused
    short refs; // reference count
    short ptrs[2]; // direct pointers to block #
    short iptr; // single indirect pointer to block #
    struct timespec ts[3]; // 0: st_atim; 1: st_mtim; 2: st_ctim
} inode;

inode* get_inode(int inum);
int alloc_inode();
void free_inode(int inum);
void print_inode(inode* node);
int cpy_inode(inode* node);
int grow_inode(inode* node, int num_blocks);
void shrink_inode(inode* node, int num_blocks);

#endif
