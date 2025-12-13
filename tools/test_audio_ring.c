#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../src/audio.h"
#include "../src/state.h"

int main(void) {
    struct glwall_state state;
    memset(&state, 0, sizeof(state));
    state.audio_enabled = true;
    state.audio_source = GLWALL_AUDIO_SOURCE_FAKE;

    if (!init_audio(&state)) {
        fprintf(stderr, "init_audio failed\n");
        return 2;
    }

    const size_t N = 512;
    int16_t samples[N];
    for (size_t i = 0; i < N; ++i)
        samples[i] = (int16_t)(i & 0x7fff);

    audio_test_overwrite_ring(&state, samples, N);

    int16_t out[N];
    int got = audio_read_recent_samples(&state, out, N);
    if (got != (int)N) {
        fprintf(stderr, "Expected %zu samples, got %d\n", N, got);
        cleanup_audio(&state);
        return 1;
    }

    for (size_t i = 0; i < N; ++i) {
        if (out[i] != samples[i]) {
            fprintf(stderr, "Mismatch at %zu: expected %d got %d\n", i, samples[i], out[i]);
            cleanup_audio(&state);
            return 1;
        }
    }

    printf("Audio ring test: PASS\n");
    cleanup_audio(&state);
    return 0;
}
