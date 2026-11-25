#ifndef TEST_SWSCALE_H
#define TEST_SWSCALE_H
#include <stdint.h>
typedef struct SwsContext { int unused; } SwsContext;
SwsContext *sws_getContext(int, int, int, int, int, int, int, void *, void *, void *);
void sws_freeContext(SwsContext *);
int sws_scale(SwsContext *, const uint8_t *const *, const int *, int, int, uint8_t *const *, const int *);
#endif
