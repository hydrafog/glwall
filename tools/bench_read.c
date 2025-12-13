#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include "../src/utils.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <path> <iterations>\n", argv[0]);
        return 2;
    }
    const char *path = argv[1];
    int iters = atoi(argv[2]);
    struct timespec a,b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (int i = 0; i < iters; ++i) {
        char *s = read_file(path);
        if (!s) {
            fprintf(stderr, "read_file failed on iteration %d\n", i);
            return 1;
        }
        free(s);
    }
    clock_gettime(CLOCK_MONOTONIC, &b);
    double dt = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec)/1e9;
    printf("Read %d files in %.6f s (%.2f ops/s)\n", iters, dt, iters/dt);
    return 0;
}
