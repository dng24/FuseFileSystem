#ifndef BITMAP_H
#define BITMAP_H

void bitmap_free(void* bm, int ii);
int bitmap_set(void* bm, int size); //size = # bits total
void bitmap_set_spec(void* bm, int ii);

#endif

