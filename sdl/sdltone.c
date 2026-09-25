/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Rigby Foundation */
/* sdltone: SDL audio on sic. Asks for 44.1 kHz stereo (the device runs at
 * 48 kHz: SDL converts), plays a two-second A major chord from the audio
 * callback, reports what it got. */
#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>

static double phase[3];
static const double freq[3] = { 440.0, 554.37, 659.26 };
static int rate;

static void callback(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    Sint16 *out = (Sint16 *)stream;
    for (int i = 0; i < len / 4; i++) {
        double v = 0;
        for (int k = 0; k < 3; k++) { v += sin(phase[k]); phase[k] += 2 * M_PI * freq[k] / rate; }
        Sint16 s = (Sint16)(v * 3000);
        out[2 * i] = s; out[2 * i + 1] = s;
    }
}

int main(void)
{
    if (SDL_Init(SDL_INIT_AUDIO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_AudioSpec want = { 0 }, have;
    want.freq = 44100; want.format = AUDIO_S16LSB; want.channels = 2; want.samples = 1024; want.callback = callback;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) { fprintf(stderr, "SDL_OpenAudioDevice: %s\n", SDL_GetError()); return 1; }
    rate = have.freq;
    printf("sdltone: driver %s, %d Hz %d ch format %#x, %d samples/callback\n", SDL_GetCurrentAudioDriver(), have.freq, have.channels, have.format, have.samples);
    SDL_PauseAudioDevice(dev, 0);
    SDL_Delay(2000);
    SDL_CloseAudioDevice(dev);
    printf("sdltone: done\n");
    SDL_Quit();
    return 0;
}
