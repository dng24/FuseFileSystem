
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <alloca.h>
#include <string.h>
#include <libgen.h>
#include <bsd/string.h>
#include <stdint.h>

#include "storage.h"
#include "slist.h"
#include "util.h"
#include "pages.h"
#include "inode.h"
#include "directory.h"
#include "version.h"

void set_timestamp(inode* node, int which) {
    assert(which > 0 && which < 8);
    struct timespec t = {0, 0};
    clock_gettime(CLOCK_REALTIME, &t);
    if (which & 1) {
        node->ts[0].tv_sec = t.tv_sec;
        node->ts[0].tv_nsec = t.tv_nsec;
    }
    if (which & 2) {
        node->ts[1].tv_sec = t.tv_sec;
        node->ts[1].tv_nsec = t.tv_nsec;
    }
    if (which & 4) {
        node->ts[2].tv_sec = t.tv_sec;
        node->ts[2].tv_nsec = t.tv_nsec;
    }
}

void
storage_init(const char* path, int create)
{
    //printf("storage_init(%s, %d);\n", path, create);
    pages_init(path, create);
    if (create) {
        directory_init();
    }
    version_init(create);
}

int
storage_stat(const char* path, struct stat* st)
{
    printf("+ storage_stat(%s) with root %d\n", path, get_root_inum());
    int inum = tree_lookup(path);
    if (inum < 0) {
        return inum;
    }

    inode* node = get_inode(inum);
    memset(st, 0, sizeof(struct stat));
    st->st_ino   = inum;
    st->st_mode  = node->mode;
    st->st_nlink = node->refs;
    st->st_uid   = getuid();
    st->st_gid   = getgid();
    st->st_size  = node->size;
    st->st_blocks = bytes_to_pages(node->size);
    st->st_atim  = node->ts[0];
    st->st_mtim  = node->ts[1];
    st->st_ctim  = node->ts[2];
    return 0;
}

int
storage_read(const char* path, char* buf, size_t size, off_t offset)
{
    int inum = tree_lookup(path);
    if (inum < 0) {
        return inum;
    }
    inode* node = get_inode(inum);
    printf("+ storage_read(%s); inode %d\n", path, inum);
    print_inode(node);

    if (offset >= node->size) {
        return 0;
    }

    if (offset + size >= node->size) {
        size = node->size - offset;
    }

    int bytes_read = 0;
    int bytes_left = size;
    int bytes_to_read = 0;
    uint8_t* data;
    int page_num;
    short* indir_pgs = (short*) pages_get_page(node->iptr);
    while (bytes_left > 0) {
        page_num = offset / 4096;
        printf(" + reading from page: %d\n", page_num);
        assert(page_num >= 0);
        if (offset < 4096) {
            data = pages_get_page(node->ptrs[0]);
        } else if (offset < 8192) {
            data = pages_get_page(node->ptrs[1]);
        } else {
            data = pages_get_page(indir_pgs[page_num - 2]);
        }
        bytes_to_read = min((page_num + 1) * 4096 - offset, bytes_left);
        assert(bytes_to_read > 0);
        memcpy(buf + bytes_read, data + offset - page_num * 4096, bytes_to_read);
        bytes_read += bytes_to_read;
        offset += bytes_to_read;
        bytes_left -= bytes_to_read;
    }

    set_timestamp(node, 1);

    return bytes_read;
}

int write_data(inode* node, const char* buf, size_t size, off_t offset) {
    int bytes_written = 0;
    int bytes_left = size;
    int bytes_to_write = 0;
    uint8_t* data;
    int page_num;
    short* indir_pgs = (short*) pages_get_page(node->iptr);
    while (bytes_left > 0) {
        page_num = offset / 4096;
        printf("+ writing to relative page: %d\n", page_num);
        assert(page_num >= 0);
        if (offset < 4096) {
            printf("+ writing to actual page: %d\n", node->ptrs[0]);
            data = pages_get_page(node->ptrs[0]);
        } else if (offset < 8192) {
            printf("+ writing to actual page: %d\n", node->ptrs[1]);
            data = pages_get_page(node->ptrs[1]);
        } else {
            printf("+ writing to actual page: %d\n", indir_pgs[page_num - 2]);
            data = pages_get_page(indir_pgs[page_num - 2]);
        }
        //printf("bbbytes info %d %d %d %d\n", page_num, offset, bytes_left, bytes_written);
        bytes_to_write = min((page_num + 1) * 4096 - offset, bytes_left);
        //printf("bbbbbbyyytes %d\n", bytes_to_write);
        assert(bytes_to_write > 0);
        memcpy(data + offset - page_num * 4096, buf + bytes_written, bytes_to_write);
        bytes_written += bytes_to_write;
        offset += bytes_to_write;
        bytes_left -= bytes_to_write;
    }
    return bytes_written;
}

int
storage_write(const char* path, const char* buf, size_t size, off_t offset)
{
    printf("storage_write(%s, ..., %d, %d\n", path, size, offset);

    int rv = storage_truncate(path, size + offset);
    if (rv < 0) {
        return rv;
    }

    int old_inum = tree_lookup(path);
    if (old_inum < 0) {
        return old_inum;
    }

    size_t* bm = malloc(8 * sizeof(size_t));
    int copied_inum = copy_and_update(path, old_inum, bm, 0);
    if (copied_inum < 0) {
        return copied_inum;
    }

    inode* node = get_inode(copied_inum);

    printf("+ storage_write(%s); inode %d\n", path, copied_inum);
    print_inode(node);

    char* buf1 = (char*) malloc(offset);
    storage_read(path, buf1, offset, 0);
    write_data(node, buf1, offset, 0);
    int bytes_written = write_data(node, buf, size, offset);
    free(buf1);

    set_timestamp(node, 6);
    add_version(path, bm, "write");
    free(bm);
    return bytes_written;
}

int
storage_truncate(const char *path, off_t size)
{
    int inum = tree_lookup(path);
    if (inum < 0) {
        return inum;
    }

    size_t* bm = malloc(8 * sizeof(size_t));
    int copied_inum = copy_and_update(path, inum, bm, 0);
    if (copied_inum < 0) {
        return copied_inum;
    }

    inode* node = get_inode(copied_inum);
    print_inode(node);

    int num_orig_blocks = bytes_to_pages(node->size);
    int num_new_blocks = bytes_to_pages(size);
    int dif = num_new_blocks - num_orig_blocks;

    if (dif > 0) { //alloc new blocks
        int rv = grow_inode(node, dif);
        if (rv < 0) {
            return rv;
        }
    } else if (dif < 0) { //free blocks
        shrink_inode(node, -dif);
    }

    set_timestamp(node, 6);
    node->size = size;
    add_version(path, bm, "truncate");
    free(bm);
    return 0;
}

char* 
get_dir_name(const char* path) {
    assert(path[0] == '/');

    //ex path = "/dir1/dir2/dir3/"; path2 = "/dir1/dir2/file"
    char* path_cpy = alloca(strlen(path) + 1);
    strcpy(path_cpy, path);
    if (path_cpy[strlen(path_cpy) - 1] == '/') {
        path_cpy[strlen(path_cpy) - 1] = '\0'; //path_cpy = "/dir1/dir2/dir3"; path_cpy2 = "/dir1/dir2/file"
    }
    char* last_dir = strrchr(path_cpy, '/'); //last_dir = /dir3; last_dir2 = /file
    char* file_dir = malloc(strlen(path_cpy) + 1);
    int last_dir_idx = last_dir - path_cpy;
    if (last_dir_idx == 0) {
        file_dir[0] = '/';
        file_dir[1] = '\0';
    } else {
        strncpy(file_dir, path_cpy, last_dir_idx); //file_dir = "/dir1/dir2"; file_dir2 = /dir1/dir2
        file_dir[last_dir_idx] = '\0';
    }

    return file_dir;
}

char* get_file_name(const char* path) {
    assert(path[0] == '/');
    char* path_cpy = malloc(strlen(path) + 1);
    strcpy(path_cpy, path);
    if (path_cpy[strlen(path_cpy) - 1] == '/') {
        path_cpy[strlen(path_cpy) - 1] == '\0';
    }
    char* last_dir = strrchr(path_cpy, '/');
    int last_dir_idx = last_dir - path_cpy;
    return path_cpy + last_dir_idx + 1;
}

int
storage_mknod(const char* path, int mode)
{
    printf("storage_mknod(%s, %d)\n", path, mode);
    if (tree_lookup(path) != -ENOENT) {
        printf("mknod fail: already exist\n");
        return -EEXIST;
    }

    char* path_cpy = alloca(strlen(path) + 1);
    strcpy(path_cpy, path);
    if (path_cpy[strlen(path_cpy) - 1] == '/') {
        path_cpy[strlen(path_cpy) - 1] = '\0';
    }
    char* last_dir = strrchr(path_cpy, '/');
    int last_dir_idx = last_dir - path_cpy;
    int file_dir_inum = tree_lookup(get_dir_name(path));
    if (file_dir_inum < 0) {
        printf("storage_mknod(%s, %d) -> %d: R1\n", path, mode, file_dir_inum);
        return file_dir_inum;
    }

    size_t* bm = malloc(8 * sizeof(size_t));
    int copied_inum = copy_and_update(get_dir_name(path), file_dir_inum, bm, 1);
    if (copied_inum < 0) {
        printf("storage_mknod(%s, %d) -> %d: R2\n", path, mode, copied_inum);
        return copied_inum;
    }

    int inum = alloc_inode();
    if (inum < 0) {
        printf("storage_mknod(%s, %d) -> %d\n", path, mode, inum);
        return inum;
    }
    inode* node = get_inode(inum);
    node->mode = mode;
    node->size = 0;

    set_timestamp(node, 7);

    int rv = directory_put(get_inode(copied_inum), get_file_name(path), inum);
    if (rv < 0) {
        printf("storage_mknod(%s, %d) -> %d: R3\n", path, mode, rv);
        return rv;
    }

    printf("+ mknod create %s [%04o] - #%d\n", path, mode, copied_inum);

    add_version(path, bm, "mknod");
    free(bm);
    printf("storage_mknod(%s, %d) -> 0; orig dnum: %d; new dnum: %d: R4\n", path, mode, file_dir_inum, copied_inum);
    print_directory(path);
    return 0;
}

int
storage_unlink(const char* path)
{
    int inum = tree_lookup(path);
    if (inum < 0) {
        return inum;
    }

    size_t* bm = malloc(8 * sizeof(size_t));
    int copied_inum = copy_and_update(path, inum, bm, 0);
    if (copied_inum < 0) {
        return copied_inum;
    }

    int dnum = tree_lookup(get_dir_name(path));
    if (dnum < 0) {
        return dnum;
    }

    int rv = directory_delete(get_inode(dnum), copied_inum, get_file_name(path), 0);
    if (rv < 0) {
        return rv;
    }

    add_version(path, bm, "unlink");
    free(bm);
    return 0;
}

int
storage_link(const char* from, const char* to)
{
    int inum_from = tree_lookup(from);
    if (inum_from < 0) {
        return inum_from;
    }

    size_t* bm = malloc(8 * sizeof(size_t));
    int copied_inum_from = copy_and_update(from, inum_from, bm, 0);
    if (copied_inum_from < 0) {
        return copied_inum_from;
    }

    int dnum_to = tree_lookup(get_dir_name(to));
    if (dnum_to < 0) {
        return dnum_to;
    }
    inode* dnode_to = get_inode(dnum_to);
    char* name_to = get_file_name(to);

    inode* node = get_inode(copied_inum_from);
    set_timestamp(node, 4);
    
    int rv = directory_put(dnode_to, name_to, copied_inum_from);
    if (rv < 0) {
        return rv;
    }

    add_version(to, bm, "link");
    free(bm);
    return rv;
}

int
storage_rename(const char* from, const char* to)
{
    printf("storage_rename(%s, %s)\n", from, to);
    if (streq(from, to)) {
        return 0;
    }
    int inum = tree_lookup(from);
    if (inum < 0) {
        printf("rename fail\n");
        printf("storage_rename(%s, %s) -> %d: R1)\n", from, to, inum);
        return inum;
    }

    int dnum = tree_lookup(get_dir_name(to));
    if (dnum < 0) {
        printf("storage_rename(%s, %s) -> %d: R2)\n", from, to, dnum);
        return dnum;
    }
    
    size_t* bm = malloc(8 * sizeof(size_t));
    int copied_dnum = copy_and_update(get_dir_name(to), dnum, bm, 1);
    if (copied_dnum < 0) {
        printf("storage_rename(%s, %s) -> %d: R3)\n", from, to, copied_dnum);
        return copied_dnum;
    }
    
    int rv = directory_put(get_inode(copied_dnum), get_file_name(to), inum);
    if (rv < 0) {
        printf("storage_rename(%s, %s) -> %d: R4)\n", from, to, rv);
        return rv;
    }

    add_version(to, bm, "rename");

    int copied_inum = copy_and_update(from, inum, bm, 0);
    if (copied_inum < 0) {
        printf("storage_rename(%s, %s) -> %d: R6)\n", from, to, copied_inum);
        return copied_inum;
    }
    dnum = tree_lookup(get_dir_name(from));
    if (dnum < 0) {
        printf("storage_rename(%s, %s) -> %d: R5)\n", from, to, dnum);
        return dnum;
    }

    rv = directory_delete(get_inode(dnum), copied_inum, get_file_name(from), 1);
    if (rv < 0) {
        printf("storage_rename(%s, %s) -> %d: R7)\n", from, to, rv);
        return rv;
    }

    inode* node = get_inode(copied_inum);
    set_timestamp(node, 4);

    add_version(from, bm, "rename");
    free(bm);
    printf("storage_rename(%s, %s) -> 0: R8)\n", from, to);
    return 0;
}

int
storage_set_time(const char* path, const struct timespec ts[2])
{
    // Maybe we need space in a pnode for timestamps.
    int inum = tree_lookup(path);
    if (inum < 0) {
        return inum;
    }

    size_t* bm = malloc(8 * sizeof(size_t));
    int copied_inum = copy_and_update(path, inum, bm, 0);
    if (copied_inum < 0) {
        return copied_inum;
    }

    inode* node = get_inode(copied_inum);
    node->ts[0] = ts[0];
    node->ts[1] = ts[1];

    add_version(path, bm, "set time");
    free(bm);
    return 0;
}

int storage_chmod(const char* path, mode_t mode) {
    int inum = tree_lookup(path);
    if (inum < 0) {
        return inum;
    }

    size_t* bm = malloc(8 * sizeof(size_t));
    int copied_inum = copy_and_update(path, inum, bm, 0);
    if (copied_inum < 0) {
        return copied_inum;
    }

    inode* node = get_inode(copied_inum);
    node->mode &= ~07777;
    mode &= 07777;
    node->mode += mode;

    set_timestamp(node, 4);

    add_version(path, bm, "chmod");
    free(bm);
    return 0;
}

slist*
storage_list(const char* path)
{
    return directory_list(path);
}
