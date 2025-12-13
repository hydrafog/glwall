#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <complex.h>
#include <math.h>

static void fft_local(float complex *data, int n) {
    if (n <= 1)
        return;
    int j = 0;
    for (int i = 1; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            float complex tmp = data[i];
            data[i] = data[j];
            data[j] = tmp;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * 3.14159265358979323846 / len;
        float complex wlen = cos(ang) + I * sin(ang);
        for (int i = 0; i < n; i += len) {
            float complex w = 1.0 + 0.0 * I;
            for (int k = 0; k < len / 2; ++k) {
                float complex u = data[i + k];
                float complex v = data[i + k + len / 2] * w;
                data[i + k] = u + v;
                data[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

int main(int argc, char **argv) {
    int iters = 1000;
    if (argc >= 2) iters = atoi(argv[1]);

    float complex data[512];
    for (int i = 0; i < 512; ++i)
        data[i] = (float)i;

    struct timespec a,b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (int i = 0; i < iters; ++i) {
        fft_local(data, 512);
    }
    clock_gettime(CLOCK_MONOTONIC, &b);
    double dt = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec)/1e9;
    printf("Performed %d FFTs in %.6f s (%.2f ops/s)\n", iters, dt, iters/dt);
    return 0;
}
