#ifndef DIRECTORY_H
#define DIRECTORY_H

#include <stdbool.h>

#include <bsd/string.h>

#include "slist.h"
#include "pages.h"
#include "inode.h"

typedef struct dirent {
    char name[60];
    int inum;
} dirent;

void directory_init();
int directory_lookup(inode* node, const char* name);
int tree_lookup(const char* path);
int directory_put(inode* dnode, const char* name, int inum);
int directory_delete(inode* dnode, int inum, const char* name, bool move);
slist* directory_list(const char* path);
void print_directory(const char* path);

#endif

