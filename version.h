#ifndef VERSION_H
#define VERSION_H

#include <stdbool.h>

#include "slist.h"

typedef struct version {
    int root_inum;
    int ver_num;
    size_t bitmaps[8];
    char filepath[110];
    char operation[9];
    bool flag;
} version;

void version_init();
int copy_and_update(const char* child_file_path, int old_child_inum, size_t* bm, int create);
int add_version(const char* file_path, size_t* bm, const char* operation);
void revert(int version_num, const char* img);
int get_root_inum();
int get_current_version();
slist* version_list();

#endif
