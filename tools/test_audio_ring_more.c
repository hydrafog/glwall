#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>

#include "../src/audio.h"
#include "../src/state.h"

struct thread_args {
    struct glwall_state *state;
    const int16_t *samples;
    size_t count;
    int loops;
};

static void *writer_thread(void *arg) {
    struct thread_args *a = arg;
    for (int i = 0; i < a->loops; ++i) {
        audio_test_overwrite_ring(a->state, a->samples, a->count);
    }
    return NULL;
}

int main(void) {
    struct glwall_state state;
    memset(&state, 0, sizeof(state));
    state.audio_enabled = true;
    state.audio_source = GLWALL_AUDIO_SOURCE_FAKE;

    if (!init_audio(&state)) {
        fprintf(stderr, "init_audio failed\n");
        return 2;
    }

    // We expect audio ring length to be GLWALL_FFT_SIZE * 8; using 512 * 8 = 4096 locally
    size_t ring_len = 512 * 8;
    printf("Assumed ring length: %zu\n", ring_len);

    // Test wrap-around: write ring_len + 100 samples and ensure last 100 are read
    size_t write_count = ring_len + 100;
    int16_t *samples = malloc(sizeof(int16_t) * write_count);
    if (!samples) {
        fprintf(stderr, "OOM test samples\n");
        cleanup_audio(&state);
        return 4;
    }
    for (size_t i = 0; i < write_count; ++i)
        samples[i] = (int16_t)((i + 1000) & 0x7fff);

    audio_test_overwrite_ring(&state, samples, write_count);

    // Now read the last 100 samples
    const size_t N = 100;
    int16_t out[N];
    int got = audio_read_recent_samples(&state, out, N);
    if (got != (int)N) {
        fprintf(stderr, "Expected %zu samples, got %d\n", N, got);
        cleanup_audio(&state);
        return 1;
    }
    for (size_t i = 0; i < N; ++i) {
        int16_t expected = samples[write_count - N + i];
        if (out[i] != expected) {
            fprintf(stderr, "Wrap mismatch at %zu: expected %d got %d\n", i, expected, out[i]);
            cleanup_audio(&state);
            return 1;
        }
    }
    printf("Wrap-around test passed\n");
    // Reset audio state to ensure a clean ring before partial-read test
    cleanup_audio(&state);
    memset(&state, 0, sizeof(state));
    state.audio_enabled = true;
    state.audio_source = GLWALL_AUDIO_SOURCE_FAKE;
    if (!init_audio(&state)) {
        fprintf(stderr, "Re-init failed\n");
        return 2;
    }

    // Test partial reads: write 50 samples and request 512
    size_t small = 50;
    int16_t small_samples[small];
    for (size_t i = 0; i < small; ++i)
        small_samples[i] = (int16_t)(200 + i);

    audio_test_overwrite_ring(&state, small_samples, small);

    int16_t out_big[512];
    int got2 = audio_read_recent_samples(&state, out_big, 512);
    if (got2 != (int)small) {
        fprintf(stderr, "Expected %zu available samples, got %d\n", small, got2);
        cleanup_audio(&state);
        return 1;
    }
    // The front of the buffer should be zeros
    for (size_t i = 0; i < (size_t)(512 - small); ++i) {
        if (out_big[i] != 0) {
            fprintf(stderr, "Expected 0 at pad index %zu got %d\n", i, out_big[i]);
            cleanup_audio(&state);
            return 1;
        }
    }
    for (size_t i = 0; i < small; ++i) {
        if (out_big[512 - small + i] != small_samples[i]) {
            fprintf(stderr, "Partial read mismatch at %zu: expected %d got %d\n", i, small_samples[i], out_big[512 - small + i]);
            cleanup_audio(&state);
            return 1;
        }
    }
    printf("Partial read test passed\n");

    // Threaded writer test
    pthread_t thr;
    struct thread_args args;
    args.state = &state;
    args.samples = samples;
    args.count = 256;
    args.loops = 10;

    if (pthread_create(&thr, NULL, writer_thread, &args) != 0) {
        fprintf(stderr, "Failed to create writer thread\n");
        free(samples);
        cleanup_audio(&state);
        return 1;
    }

    // Read a few times while thread writes
    for (int i = 0; i < 20; ++i) {
        int16_t temp[128];
        int r = audio_read_recent_samples(&state, temp, 128);
        if (r < 0) {
            fprintf(stderr, "Error reading during threaded test\n");
            pthread_join(thr, NULL);
            free(samples);
            cleanup_audio(&state);
            return 1;
        }
    }

    pthread_join(thr, NULL);
    printf("Threaded write/read test passed\n");

    free(samples);
    cleanup_audio(&state);
    printf("All audio ring tests: PASS\n");
    return 0;
}
