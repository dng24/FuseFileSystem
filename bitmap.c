#include <stdio.h>
//#include <stdlib.h>

#include <assert.h>
#include <errno.h>

#include "bitmap.h"

int largest_bit(size_t num) {
    assert(num > 0);
    int ans = 0;
    while (num != 1) {
        num >>= 1;
        ans++;
    }
    return ans;
}

void bitmap_free(void* bm, int ii) {
    assert(ii >= 0);
    size_t* bitmap = (size_t*) bm;
    int sizet_id = ii / (sizeof(size_t) * 8);
    size_t bit = ii % (sizeof(size_t) * 8);
    bitmap[sizet_id] &= ~((size_t) 1 << bit);
}

int bitmap_set(void* bm, int size) {
    assert(size > 0);
    size_t* bitmap = (size_t*) bm;
    int num_sizet = size / (sizeof(size_t) * 8);
    if (size % (sizeof(size_t) * 8) != 0) {
        num_sizet++;
    }
    int sizet_id = 0;

    for (int i = 0; ~bitmap[i] == 0; i++) {
        sizet_id++;
        if (sizet_id >= num_sizet) {
            return -ENOSPC;
        }
    }

    size_t bit_bin = (bitmap[sizet_id] + 1) & ~bitmap[sizet_id];
    int bit_dec = largest_bit(bit_bin);
    int bit_num = (sizeof(size_t) * 8) * sizet_id + bit_dec;
    if (bit_num >= size) {
        return -ENOSPC;
    }

    bitmap[sizet_id] += bit_bin;
    
    assert(bit_num < size);
    return bit_num;
}

void bitmap_set_spec(void* bm, int ii) {
    int sizet_id = ii / (sizeof(size_t) * 8);
    size_t bit_num = ii % (sizeof(size_t) * 8);
    size_t bit_bin = (size_t) 1 << bit_num;
    ((size_t*) bm)[sizet_id] |= bit_bin;
}

/*int main(int argc, char* argv[]) {
    void* bm = malloc(256);
    for (int i = 0; i < atoi(argv[1]); i++) {
        //printf("%d ", i);
        bitmap_set(bm, 256);
    }
    int b = bitmap_set(bm, 256);
    printf("%d\n", b);
    bitmap_free(bm, atoi(argv[2]));
    bitmap_free(bm, atoi(argv[2]));
    b = bitmap_set(bm, 256);
    printf("%d\n", b);
    

    for (int i = 2; i < atoi(argv[1]) + 2; i++) {
        bitmap_set_spec(bm, atoi(argv[i]));
    }
    for (int i = 0; i < 4; i++) {
        printf("0x%016lX ", ((size_t*) bm)[i]);
    }
    puts("");
    return 0;
}
*/
