
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "storage.h"
#include "slist.h"
#include "util.h"
#include "directory.h"
#include "inode.h"
#include "version.h"

slist* ls_tree_helper(slist* xs, slist* zs, const char* base) {
    for (; xs; xs = xs->next) {
        char* path = path_join(base, xs->data);
        zs = s_cons(path, zs);
        int inum = tree_lookup(path);
        assert(inum >= 0 && inum <= 256);
        inode* node = get_inode(inum);
        if (node->mode > 040000 && node->mode < 050000) {
            slist* ys = storage_list(path);
            zs = ls_tree_helper(ys, zs, path);
            s_free(ys);
        }
    }
    return zs;
}

slist*
image_ls_tree(const char* base)
{
    struct stat st;
    slist* zs = 0;
    slist* xs = storage_list(base);
    zs = ls_tree_helper(xs, zs, base);
    s_free(xs);
    return zs;
}

void
print_usage(const char* name)
{
    fprintf(stderr, "Usage: %s cmd ...\n", name);
    exit(1);
}

int
main(int argc, char* argv[])
{
    if (argc < 3) {
        print_usage(argv[0]);
    }

    const char* cmd = argv[1];
    const char* img = argv[2];

    if (streq(cmd, "new")) {
        assert(argc == 3);

        storage_init(img, 1);
        printf("Created disk image: %s\n", img);
        return 0;
    }

    if (access(img, R_OK) == -1) {
        fprintf(stderr, "No such image: %s\n", img);
        return 1;
    }

    storage_init(img, 0);

    if (streq(cmd, "ls")) {
        slist* xs = image_ls_tree("/");
        printf("List for %s:\n", img);
        for (slist* it = xs; it != 0; it = it->next) {
            printf("%s\n", it->data);
        }
        s_free(xs);
    } else if (streq(cmd, "versions")) {
        slist* xs = version_list();
        printf("Versions for %s:\n", img);
        for (slist* it = xs; it != 0; it = it->next) {
            printf("%s\n", it->data);
        }
        s_free(xs);
    } else if (argc > 3 && streq(cmd, "rollback")) {
        char* ptr;
        long ver = strtol(argv[3], &ptr, 10);
        revert(ver, img);
    } else {
        print_usage(argv[0]);
    }

    return 0;
}
