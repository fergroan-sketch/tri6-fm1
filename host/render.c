/* SPDX-License-Identifier: MPL-2.0 */
/* a quick listening test of the engine, without the app: render OUT.wav "NAME=v,NAME=v" ["notes"] [seconds]
 * parameter names as TRI_PNAME with spaces taken out and a section prefix: see the table below */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tri6.h"

int plat_cpu2_on(void) { return 0; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }
int plat_cpu2_wait(void) { return 0; }

static const char *const KEY[TP_N] = {
    "o1oct", "o1wave", "o1pw", "o1lvl", "o2oct", "o2wave", "o2pw", "o2lvl", "sync", "o3oct", "o3wave", "o3pw", "o3lvl",
    "o3key", "noise", "fdrv", "cut", "res", "fenv", "pole2", "fkey", "fvel", "fa", "fd", "fs", "fr", "aa", "ad", "as",
    "ar", "aenv", "avel", "lfreq", "lshape", "lamt", "lsync", "lpitch", "lpw", "lfilt", "lamp", "pm3", "pmf", "pmpitch",
    "pmpw", "pmfb", "pmfilt", "dist", "vint", "pan", "vol", "glide", "grate", "gmode", "bend", "uni", "keymode", "fx",
    "fxa", "amix", "ap1", "ap2", "async", "fxb", "bmix", "bp1", "bp2", "bsync", "arp", "amode", "aoct", "adiv", "bpm",
    "ahold"};

static void wav(FILE *f, const float *x, uint32_t frames)
{
    uint32_t i, bytes = frames * 4u;
    uint32_t h[11] = {0x46464952u, 36u + bytes, 0x45564157u, 0x20746d66u, 16u, 0x00020001u, 44100u, 44100u * 4u,
                      0x00100004u, 0x61746164u, bytes};
    fwrite(h, 4, 11, f);
    for (i = 0; i < frames * 2u; i++) {
        float v = x[i] * 32767.0f;
        short s = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        fwrite(&s, 2, 1, f);
    }
}

int main(int argc, char **argv)
{
    static tri_patch_t p;
    tri_perf_t perf = {6, 0, 1, 1};
    static float buf[44100 * 2 * 40];
    uint32_t pos = 0, i;
    char *spec, *tok;
    float peak = 0, secs = argc > 4 ? (float)atof(argv[4]) : 2.0f;
    clock_t c0;
    int k;
    /* a plain patch: osc 1 saw */
    p.p[TP_O1_OCT] = 2; p.p[TP_O2_OCT] = 2; p.p[TP_O3_OCT] = 3;
    p.p[TP_O1_WAVE] = 2; p.p[TP_O2_WAVE] = 2; p.p[TP_O3_WAVE] = 2;
    p.p[TP_O1_PW] = p.p[TP_O2_PW] = p.p[TP_O3_PW] = 64;
    p.p[TP_O1_LEVEL] = 100; p.p[TP_O3_KEY] = 1; p.p[TP_FDRV] = 64; p.p[TP_CUTOFF] = 90; p.p[TP_FENV_AMT] = 64;
    p.p[TP_FKEY] = 1; p.p[TP_AS] = 127; p.p[TP_AR] = 40; p.p[TP_FS] = 127; p.p[TP_FD] = 60; p.p[TP_AD] = 60;
    p.p[TP_FR] = 40; p.p[TP_AENV_AMT] = 127; p.p[TP_LFO_FREQ] = 64; p.p[TP_PM_OSC3] = 64; p.p[TP_PM_FENV] = 64;
    p.p[TP_VOLUME] = 100; p.p[TP_BEND] = 2; p.p[TP_FXA_P1] = 64; p.p[TP_FXA_P2] = 64; p.p[TP_FXB_P1] = 64;
    p.p[TP_FXB_P2] = 64; p.p[TP_FXA_MIX] = 64; p.p[TP_FXB_MIX] = 40; p.p[TP_BPM] = 120; p.p[TP_ARP_DIV] = 6;
    if (argc < 3)
        return 1;
    spec = strdup(argv[2]);
    for (tok = strtok(spec, ","); tok; tok = strtok(0, ",")) {
        char *eq = strchr(tok, '=');
        if (!eq)
            continue;
        *eq = 0;
        for (k = 0; k < TP_N; k++)
            if (!strcmp(tok, KEY[k]))
                p.p[k] = (uint8_t)atoi(eq + 1);
        if (!strcmp(tok, "p2"))
            p.pitch2 = (int16_t)atoi(eq + 1);
        if (!strcmp(tok, "p3"))
            p.pitch3 = (int16_t)atoi(eq + 1);
        if (!strcmp(tok, "voices"))
            perf.voices = (uint8_t)atoi(eq + 1);
        if (!strcmp(tok, "cores"))
            perf.one_core = atoi(eq + 1) != 2;
    }
    tri_init();
    tri_set_perf(&perf);
    tri_set_patch(&p);
    c0 = clock();
    {
        const char *score = argc > 3 ? argv[3] : "48 55 60 64 67 72";
        int notes[16], nn = 0;
        char *s = strdup(score);
        for (tok = strtok(s, " "); tok && nn < 16; tok = strtok(0, " "))
            notes[nn++] = atoi(tok);
        for (i = 0; i < (uint32_t)nn; i++)
            tri_note_on(notes[i], 100);
        for (i = 0; i < secs * 44100 / 256; i++) {
            tri_render(buf + pos * 2, 256);
            pos += 256;
        }
        for (i = 0; i < (uint32_t)nn; i++)
            tri_note_off(notes[i]);
        for (i = 0; i < 44100 * 2.0 / 256; i++) {
            tri_render(buf + pos * 2, 256);
            pos += 256;
        }
    }
    fprintf(stderr, "cpu %.2f%% of real time (host)\n", 100.0 * (clock() - c0) / CLOCKS_PER_SEC / (pos / 44100.0));
    for (i = 0; i < pos * 2; i++)
        if (buf[i] > peak || -buf[i] > peak)
            peak = buf[i] > 0 ? buf[i] : -buf[i];
    fprintf(stderr, "peak %.3f\n", peak);
    {
        FILE *f = fopen(argv[1], "wb");
        wav(f, buf, pos);
        fclose(f);
    }
    return 0;
}
