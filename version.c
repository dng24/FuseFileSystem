#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "pages.h"
#include "storage.h"
#include "inode.h"
#include "version.h"
#include "directory.h"
#include "bitmap.h"
#include "util.h"

#define MAX_VERSIONS 21

int current_version = 0;
int root = 0;

void version_init(int create) {
    version* ver_arr = pages_get_page(0) + 64;
    size_t* cur_bm = pages_get_page(0);
    if (create) {
        ver_arr[0].root_inum = 0;
        ver_arr[0].ver_num = 0;
        ver_arr[0].flag = true;
        for (int i = 0; i < 8; i++) {
            ver_arr[0].bitmaps[i] = cur_bm[i];
        }
        strcpy(ver_arr[0].operation, "init");
        strcpy(ver_arr[0].filepath, "/");
    } else {
        for (int i = 0; i < MAX_VERSIONS; i++) {
            if (ver_arr[i].ver_num > current_version) {
                current_version = ver_arr[i].ver_num;
                root = ver_arr[i].root_inum;
            }
        }
    }
}

int copy_inode(int old_inum, size_t* new_inode_bm) {
    if (old_inum < 0) {
        return old_inum;
    }
    inode* old_inode = get_inode(old_inum);
    int new_inum = cpy_inode(old_inode);
    if (new_inum < 0) {
        return new_inum;
    }

    bitmap_free(new_inode_bm, old_inum);
    bitmap_set_spec(new_inode_bm, new_inum);
    return new_inum;
}

int find_mod_page(inode* node, int child_inum) {
    int mod_page = -1;
    for (int i = 0; mod_page == -1 && i < 2; i++) {
        if (child_inum == -1 && !node->ptrs[i]) {
            mod_page = -2;
        } else {
            dirent* dirents = pages_get_page(node->ptrs[i]);
            for (int j = 0; mod_page == -1 && j < 64; j++) {
                if (child_inum >= 0 && dirents[j].inum == child_inum) {
                    mod_page = i;  
                } else if (child_inum == -1 && !dirents[j].name[0]) {
                    mod_page = i;
                }
            }
        }
    }
    if (mod_page == -1) {
        if (child_inum == -1 && !node->iptr) {
            mod_page = -2;
        } else {
            mod_page = 2;
        }
    }
    return mod_page;
}

int copy_page(inode* new_node, int mod_page, size_t* new_pages_bm) {
    int new_page = alloc_page();
    if (new_page < 0) {
        return new_page;
    }

    short old_dirents_page;
    if (mod_page == 0 || mod_page == 1) {
        old_dirents_page = new_node->ptrs[mod_page];
        memcpy(pages_get_page(new_page), pages_get_page(old_dirents_page), 4096);
        new_node->ptrs[mod_page] = new_page;
    } else if (mod_page == 2) {
        old_dirents_page = new_node->iptr;
        memcpy(pages_get_page(new_page), pages_get_page(old_dirents_page), 4096);
        new_node->iptr = new_page;
    } else if (mod_page >= 3) {
        short* iptrs = pages_get_page(new_node->iptr);
        old_dirents_page = iptrs[mod_page - 3];
        memcpy(pages_get_page(new_page), pages_get_page(old_dirents_page), 4096);
        iptrs[mod_page - 3] = new_page;
    }

    bitmap_free(new_pages_bm, old_dirents_page);
    bitmap_set_spec(new_pages_bm, new_page);
    return new_page;
}


// tree_lookup / different roots
// parent_path = path_to_mod
int copy_helper(const char* parent_path, int new_parent_inum, size_t* bm) {
    printf("copy_helper(%s, %d)\n", parent_path, new_parent_inum);

    //At this point:
    //- Parent inode has been copied and setup for user modification
    if (streq(parent_path, "/")) {
        root = new_parent_inum;
        printf("copy_helper(%s, %d) -> %d: R1\n", parent_path, new_parent_inum, root);
        return 0;
    }
        
    char* grandparent_path = get_dir_name(parent_path);
    int new_grandparent_inum = copy_inode(tree_lookup(grandparent_path), bm + 4);
    if (new_grandparent_inum < 0) {
        printf("copy_helper(%s, %d) -> %d: R2\n", parent_path, new_parent_inum, new_grandparent_inum);
        return new_grandparent_inum;
    }
    inode* new_grandparent_inode = get_inode(new_grandparent_inum);

    int old_parent_inum = tree_lookup(parent_path);
    if (old_parent_inum < 0) {
        printf("copy_helper(%s, %d) -> %d: R3\n", parent_path, new_parent_inum, old_parent_inum);
        return old_parent_inum;
    }

    int mod_page = find_mod_page(new_grandparent_inode, old_parent_inum);
    int new_pg = copy_page(new_grandparent_inode, mod_page, bm);
    if(new_pg < 0) {
        printf("copy_helper(%s, %d) -> %d: R4\n", parent_path, new_parent_inum, new_pg);
        return new_pg;
    }

    dirent* ents = pages_get_page(new_pg);
    for (int i = 0; i < 64; i++) {
        if (ents[i].inum == old_parent_inum) {
            ents[i].inum = new_parent_inum;
        }
    }

    int rv = copy_helper(grandparent_path, new_grandparent_inum, bm);
    printf("copy_helper(%s, %d) -> %d: R5\n", parent_path, new_parent_inum, rv);
    return rv;
}

int copy_and_update(const char* child_file_path, int old_child_inum, size_t* bm, int create) { 
    printf("copy_and_update(%s, %d, %d)\n", child_file_path, old_child_inum, create);
    
    size_t* start_bm;
    if (current_version == 0) {
        start_bm = pages_get_page(0);
    } else {
        version* ver_arr = pages_get_page(0) + 64;
        start_bm = ver_arr[current_version % MAX_VERSIONS].bitmaps;
    }
    for (int i = 0; i < 8; i++) {
        bm[i] = start_bm[i];
    }

    int new_child_inum = copy_inode(old_child_inum, bm + 4);
    if (new_child_inum < 0) {
        printf("copy_and_update(%s, %d, %d) -> %d: R1\n", child_file_path, old_child_inum, create, new_child_inum);
        return new_child_inum;
    }
    inode* new_child_inode = get_inode(new_child_inum);

    if (create == 1) {
        int mod_page = find_mod_page(new_child_inode, -1);
        if (mod_page != -2) {
            int new_pg = copy_page(new_child_inode, mod_page, bm);
            if (new_pg < 0) {
                printf("copy_and_update(%s, %d, %d) -> %d: R2\n", child_file_path, old_child_inum, create, new_pg);
                return new_pg;
            }
        }
    } else { // This means that the inode of child_inum should 100% guaranteed be in our fs
        if (streq(child_file_path, "/")) {
            root = new_child_inum;
            printf("copy_and_update(%s, %d, %d) -> %d: R3\n", child_file_path, old_child_inum, create, root);
            return root;
        }
        if (new_child_inode->mode < 040000 || new_child_inode->mode >= 050000) {
            for (int i = 0; i < 2; i++) {
                if (new_child_inode->ptrs[i]) {
                    int new_pg = copy_page(new_child_inode, i, bm);
                    if (new_pg < 0) {
                        printf("copy_and_update(%s, %d, %d) -> %d: R4\n", child_file_path, old_child_inum, create, new_pg);
                        return new_pg;
                    }
                }
            }

            if (new_child_inode->iptr) {
                short* iptrs = pages_get_page(new_child_inode->iptr);
                int new_pg = copy_page(new_child_inode, 2, bm);
                if (new_pg < 0) {
                    printf("copy_and_update(%s, %d, %d) -> %d: R5\n", child_file_path, old_child_inum, create, new_pg);
                    return new_pg;
                }

                for (int i = 0; iptrs[i] && i < 2048; i++) {
                    int new_pg = copy_page(get_inode(old_child_inum), 3 + i, bm);
                    if (new_pg < 0) {
                        printf("copy_and_update(%s, %d, %d) -> %d: R6\n", child_file_path, old_child_inum, create, new_pg);
                        return new_pg;
                    }
                }
            }
        }
    }
    int rv = copy_helper(child_file_path, new_child_inum, bm);
    if (rv < 0) {
        printf("copy_and_update(%s, %d, %d) -> %d: R7\n", child_file_path, old_child_inum, create, rv);
        return rv;
    }

    printf("copy_and_update(%s, %d, %d) -> %d: R8\n", child_file_path, old_child_inum, create, new_child_inum);
    return new_child_inum;
}

void coolio_garbage_collector() {
    version* ver_arr = pages_get_page(0) + 64;
    size_t* bitmaps = pages_get_page(0);
    size_t map = 0;
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < MAX_VERSIONS; j++) {
            if (ver_arr[j].flag) {
                map |= ver_arr[j].bitmaps[i];
            }
        }
        bitmaps[i] &= map;
        map = 0;
    }

    for (int i = 0; i < MAX_VERSIONS; i++) {
        if (!ver_arr[i].flag) {
            memset((void*) &ver_arr[i], 0, sizeof(version));
        }
    }
}

int add_version(const char* file_path, size_t* bm, const char* operation) {
    printf("add_version(%s, %s) Root: %d\n", file_path, operation, root);
    assert(get_root_inum() < 256);
    current_version++;
    int loc = current_version % MAX_VERSIONS;
    version* ver_arr = pages_get_page(0) + 64;
    ver_arr[loc].ver_num = current_version;
    ver_arr[loc].root_inum = root;
    ver_arr[loc].flag = true;
    for (int i = 0; i < 8; i++) {
        ver_arr[loc].bitmaps[i] = bm[i];
    }
    strcpy(ver_arr[loc].operation, operation);
    strcpy(ver_arr[loc].filepath, file_path);

    int next_page = alloc_page();
    if (next_page >= 0) {
        bitmap_free(pages_get_page(0), next_page);
    }
    int next_inode = alloc_inode();
    if (next_inode >= 0) {
        bitmap_free(pages_get_page(0) + 32, next_inode);
    }
    if (next_page > 220 || next_inode > 220) {
        coolio_garbage_collector();
    }
    printf("add_version(%s, %s) -> %d\n", file_path, operation, current_version);
    return current_version;
}

void revert(int version_num, const char* img) {
    version* ver_arr = pages_get_page(0) + 64;
    int oldest_version = -1;
    for (int i = 0; oldest_version == -1 && i < MAX_VERSIONS; i++) {
        if (ver_arr[i].operation[0]) {
            oldest_version = ver_arr[i].ver_num;
        }
    }
    for (int i = 0; i < MAX_VERSIONS; i++) {
        if (ver_arr[i].operation[0] && ver_arr[i].ver_num < oldest_version) {
            oldest_version = ver_arr[i].ver_num;
        }
    }
    if (version_num > current_version || version_num < oldest_version || version_num < 0) {
        printf("Cannot revert to version %d.\n", version_num);
    } else if (version_num != current_version) {
        printf("Rollback %s to version %d\n", img, version_num);
        int temp = current_version;
        current_version = version_num;
        for(int i = version_num + 1; i <= temp; i++) {
            ver_arr[i % MAX_VERSIONS].flag = false;
        }
        coolio_garbage_collector();
    } else {
        printf("Version %d is the current version.\n", version_num);
    }
}

int get_root_inum() {
    return root;
}

int get_current_version() {
    return current_version;
}

slist* version_list() {
    version* ver_arr = pages_get_page(0) + 64;
    slist* xs = 0;
    int ver = current_version % MAX_VERSIONS;
    for (int i = 0; i < MAX_VERSIONS && ver_arr[ver].operation[0]; i++) {
        char* ver_str = malloc(160);
        sprintf(ver_str, "%d %s %s", ver_arr[ver].ver_num, ver_arr[ver].operation, ver_arr[ver].filepath);
        xs = s_cons(ver_str, xs);
        free(ver_str);
        ver--;
        if (ver < 0) {
            ver = MAX_VERSIONS - 1;
        }
    }
    slist* ys = s_reverse(xs);
    free(xs);
    return ys;
}
