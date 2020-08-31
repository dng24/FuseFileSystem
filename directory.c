
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
#include <stdbool.h>

#include "directory.h"
#include "pages.h"
#include "slist.h"
#include "util.h"
#include "inode.h"
#include "version.h"

void
directory_init()
{
    inode* rn = get_inode(0);

    if (rn->mode == 0) {
        rn->size = 0;
        rn->mode = 040755;
        rn->refs = 1;
        
        struct timespec t = {0, 0};
        clock_gettime(CLOCK_REALTIME, &t);
        rn->ts[0].tv_sec = t.tv_sec;
        rn->ts[0].tv_nsec = t.tv_nsec;
        rn->ts[1].tv_sec = t.tv_sec;
        rn->ts[1].tv_nsec = t.tv_nsec;
        rn->ts[2].tv_sec = t.tv_sec;
        rn->ts[2].tv_nsec = t.tv_nsec;
    }
}

int
directory_lookup(inode* node, const char* name)
{
    printf("directory_lookup(..., %s)\n", name);
    print_inode(node);

    dirent* dirents;
    for (int i = 0; i < 2; i++) {
        if (node->ptrs[i] != 0) {
            dirents = (dirent*) pages_get_page(node->ptrs[i]);
            for (int j = 0; j < 64; j++) {
                if (streq(dirents[j].name, name)) {
                    printf("directory_lookup(..., %s) -> %d\n", name, dirents[j].inum);
                    return dirents[j].inum;
                }
            }
        }
    }
    if (node->iptr != 0) {
        dirents = (dirent*) pages_get_page(node->iptr);
        for (int i = 0; i < 64; i++) {
            if (streq(dirents[i].name, name)) {
                printf("directory_lookup(..., %s) -> %d\n", name, dirents[i].inum);
                return dirents[i].inum;
            }
        }
    }
               
    printf("directory_lookup(..., %s) -> %d\n", name, -ENOENT);
    return -ENOENT;
}

int
tree_lookup(const char* path) //get inode # of path
{
    printf("tree_lookup(%s) with root %d\n", path, get_root_inum());
    assert(path[0] == '/');

    if (streq(path, "/")) {
        printf("tree_lookup(%s) -> %d\n", path, get_root_inum());
        return get_root_inum();
    }

    int num_files = 0;
    int valid_path_len = strlen(path) - 1; //don't count '/' at the end
    for (int i = 0; i < valid_path_len; i++) {
        if (path[i] == '/') {
            num_files++;
        }
    }

    inode* node = get_inode(get_root_inum());
    char* path_cpy = malloc(strlen(path) + 1);
    strcpy(path_cpy, path);
    int file_inum = -ENOENT;
    //printf("treelookup #files = %d; path_cpy = %s\n", num_files, path_cpy);
    for (int i = 0; i < num_files; i++) {
        //printf("%d %d LOOP INFFFFFOOOOO: %d %d %d %d %d '%s'\n", i, num_files, file_inum, node->size, node->refs, node->ptrs[0], node->ptrs[1], ((dirent*)pages_get_page(node->ptrs[0]))[0].name);
        char* name = alloca(strlen(path_cpy) + 1);
        strcpy(name, path_cpy);
        file_inum = directory_lookup(node, strtok(name, "/"));
        if (file_inum < 0) {
            printf("tree_lookup(%s) -> %d\n", path, file_inum);
            return file_inum;
        }
        if (i < num_files - 1) {
            node = get_inode(file_inum);
            assert(strchr(path_cpy + 1, '/') != NULL);
            strcpy(path_cpy, strchr(path_cpy + 1, '/'));
        }
    }
    free(path_cpy);
    printf("tree_lookup(%s) -> %d\n", path, file_inum);
    return file_inum;
}

int
directory_put(inode* dnode, const char* name, int inum)
{
    assert(dnode->mode >= 040000 && dnode->mode < 050000);
    if (strlen(name) > 59) {
        return -ENAMETOOLONG;
    }
    printf("directory_put(..., %s, %d)\n", name, inum);
    //printf("DDDNNNOOOOODDDDDEeeeEEEE info begin: %d %d %d %d\n", dnode->size, dnode->refs, dnode->ptrs[0], dnode->ptrs[1]);

    int num_ent = dnode->size / sizeof(dirent);

    dirent ent;
    dirent* dirents;
    int done = 0;
    for (int i = 0; !done && i < 2; i++) {
        if (dnode->ptrs[i] == 0) {
            short new_pg = (short) alloc_page();
            if (new_pg < 0) {
                return new_pg;
            }
            assert(new_pg > 4);
            dnode->ptrs[i] = new_pg;
        }
        dirents = (dirent*) pages_get_page(dnode->ptrs[i]);
        for (int j = 0; !done && j < 64; j++) {
            if (dirents[j].name[0] == 0) {
                done = 1;
                strcpy(dirents[j].name, name);
                dirents[j].inum = inum;
            }
        }
    }
    if (!done && dnode->iptr == 0) {
        short new_pg = (short) alloc_page();
        if (new_pg < 0) {
            return new_pg;
        }
        assert(new_pg > 4);
        dnode->iptr = new_pg;
    }
    dirents = (dirent*) pages_get_page(dnode->iptr);
    for (int i = 0; !done && i < 64; i++) {
        if (dirents[i].name[0] == 0) {
            done = 1;
            strcpy(dirents[i].name, name);
            dirents[i].inum = inum;
        }
    }
    
    dnode->size += sizeof(dirent);
    //printf("DDDNNNOOOOODDDDDEeeeEEEE info end: %d %d %d %d\n", dnode->size, dnode->refs, dnode->ptrs[0], dnode->ptrs[1]);
    printf("+ dirent = '%s'\n", name);

    inode* node = get_inode(inum);
    node->refs++;
    printf("+ directory_put(..., %s, %d) -> 0\n", name, inum);
    print_inode(node);
 
    return 0;
}

int
directory_delete(inode* dnode, int inum, const char* name, bool move)
{
    //dnode = directory node, inum = file inode num, name = filename
    printf("+ directory_delete(%d, %s, %d)\n", inum, name, move);

    if (inum < 0) {
        return inum;
    }

    inode* node = get_inode(inum);
    print_inode(node);
    node->refs--;
    dirent* dirents;
    int idx;
    int done = 0;
    for (int i = 0; !done && i < 2; i++) {
        if (dnode->ptrs[i] != 0) {
            dirents = pages_get_page(dnode->ptrs[i]);
            int count = 0;
            for (int j = 0; j < 64; j++) {
                if (dirents[j].name[0] != 0) {
                    //printf("DDDDelete %d '%s' '%s'\n", j, dirents[j].name, name);
                    if (streq(dirents[j].name, name)) {
                        done = 1;
                        idx = j;
                    }
                    count++;
                }
            }
            if (done && count == 1) {
                free_page(dnode->ptrs[i]);
                dnode->ptrs[i] = 0;
            }
            if (done) {
                dnode->size -= sizeof(dirent);
                memset(dirents[idx].name, 0, sizeof(dirent));
            }
        }
    }
    
    if (!done && dnode->iptr != 0) {
        dirents = pages_get_page(dnode->iptr);
        int count = 0;
        for (int i = 0; i < 64; i++) {
            if (dirents[i].name[0] != 0) {
                if (streq(dirents[i].name, name)) {
                    done = 1;
                    idx = i;
                }
                count++;
            }
        }
        if (done && count == 1) {
            free_page(dnode->iptr);
            dnode->iptr = 0;
        }
        if (done) {
            dnode->size -= sizeof(dirent);
            memset(dirents[idx].name, 0, sizeof(dirent));
        }
    }
 
    if (done) {
        if (!move && node->refs <= 0) {
            int num_blocks = bytes_to_pages(node->size);
            shrink_inode(node, num_blocks);
            free_inode(inum);
        }
        return 0;
    } else {
        return -ENOENT;
    }
}

slist*
directory_list(const char* path)
{
    printf("+ directory_list(%s)\n", path);
    slist* ys = 0;

    int inum = tree_lookup(path);
    assert(inum >= 0);
    inode* node = get_inode(inum);
    dirent* dirents;
    for (int i = 0; i < 2; i++) {
        if (node->ptrs[i] != 0) {
            dirents = (dirent*) pages_get_page(node->ptrs[i]);
            //printf("PPPPPPPAAGE %d\n", node->ptrs[i]);
            for (int j = 0; j < 64; j++) {
                if (dirents[j].name[0] != 0) {
                    printf(" - %d %d: %s [%d]\n", i, j, dirents[j].name, dirents[j].inum);
                    ys = s_cons(dirents[j].name, ys);
                }
            }
        }
    }
    if (node->iptr != 0) {
        dirents = (dirent*) pages_get_page(node->iptr);
        for (int i = 0; i < 64; i++) {
            if (dirents[i].name[0] != 0) {
                printf(" - 2 %d: %s [%d]\n", i, dirents[i].name, dirents[i].inum);
                ys = s_cons(dirents[i].name, ys);
            }
        }
    }

    return ys;
}

void
print_directory(const char* path)
{
    printf("Contents:\n");
    int inum = tree_lookup(path);
    if (inum < 0) {
        printf("CANT LIST CONTENTS INUM OF %s IS %d\n", path, inum);
    } else {
        inode* node = get_inode(inum);
        if (node->mode < 040000 || node->mode > 050000) {
            printf("CANNOT PRINT, %s NOT A DIRECTORY\n", path);
        } else {
            slist* items = directory_list(path);
            for (slist* xs = items; xs != 0; xs = xs->next) {
                printf("- %s\n", xs->data);
            }
            printf("(end of contents)\n");
            s_free(items);
        }
    }
}
