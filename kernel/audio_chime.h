#ifndef POLLIK_AUDIO_CHIME_H
#define POLLIK_AUDIO_CHIME_H
/* Integer, 48 kHz bell synthesis shared by the driver and PCM verification. */
static inline unsigned audio_chime_frames(int login) { return login ? 14400u : 22080u; }

static inline int audio_chime_wave(unsigned phase) {
    int x = (short)(phase & 65535u);
    int absolute = x < 0 ? -x : x;
    return x * (32768 - absolute) / 16384;
}

static inline short audio_chime_sample(int login, unsigned frame) {
    static const unsigned startup[] = {392, 494, 587};
    static const unsigned welcome[] = {587, 784};
    unsigned voices = login ? 2u : 3u, duration = login ? 10080u : 16000u;
    int sample = 0;
    for (unsigned voice = 0; voice < voices; voice++) {
        unsigned delay = voice * 2880u;
        if (frame < delay || frame - delay >= duration) continue;
        unsigned age = frame - delay;
        unsigned gain = (duration - age) * 256u / duration;
        gain = gain * gain / 256u;
        if (age < 576u) gain = gain * age / 576u;
        unsigned hz = login ? welcome[voice] : startup[voice];
        unsigned phase = age * (hz * 65536u / 48000u);
        int wave = audio_chime_wave(phase) + audio_chime_wave(phase * 2u) / 8;
        sample += wave * (int)gain / 1280;
    }
    return (short)sample;
}
#endif
