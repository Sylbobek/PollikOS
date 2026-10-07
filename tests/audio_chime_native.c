#include <stdio.h>
#include <stdint.h>
#include "../kernel/audio_chime.h"

static void word(FILE *f, unsigned n, unsigned bytes) {
    for (unsigned i = 0; i < bytes; i++) fputc((int)((n >> (i * 8)) & 255u), f);
}

int main(int argc, char **argv) {
    if (argc != 3) return 1;
    for (int login = 0; login < 2; login++) {
        unsigned frames = audio_chime_frames(login), size = frames * 4u;
        FILE *f = fopen(argv[login + 1], "wb");
        if (!f) return 2;
        fwrite("RIFF", 1, 4, f); word(f, size + 36, 4);
        fwrite("WAVEfmt ", 1, 8, f); word(f, 16, 4);
        word(f, 1, 2); word(f, 2, 2); word(f, 48000, 4);
        word(f, 192000, 4); word(f, 4, 2); word(f, 16, 2);
        fwrite("data", 1, 4, f); word(f, size, 4);
        int peak = 0, crossings = 0, previous = 0;
        for (unsigned frame = 0; frame < frames; frame++) {
            int sample = audio_chime_sample(login, frame);
            int absolute = sample < 0 ? -sample : sample;
            if (absolute > peak) peak = absolute;
            if (previous < 0 && sample >= 0) crossings++;
            if (frame < 8 && absolute > 100) return 3;
            word(f, (unsigned)(uint16_t)sample, 2);
            word(f, (unsigned)(uint16_t)sample, 2);
            previous = sample;
        }
        if (fclose(f) || peak < 1000 || peak > 14000 || crossings < 30 ||
            audio_chime_sample(login, 0) || audio_chime_sample(login, frames - 1)) return 4;
        printf("PASS %s: frames=%u peak=%d crossings=%d smooth attack, silent tail, stereo PCM\n",
               login ? "login" : "startup", frames, peak, crossings);
    }
    return 0;
}
