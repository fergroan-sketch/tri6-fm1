/* SPDX-License-Identifier: MPL-2.0 */
/* TRI-6 FM-1: a six-voice polysynth in the style of the Sequential Trigon-6, from its published user's guide and MIDI
 * implementation (three oscillators a voice with triangle, saw and pulse at once, a reverse saw on the third, hard sync,
 * noise, feedback and drive into a resonant ladder low-pass switchable between two and four poles, a filter and an
 * amp envelope, a global LFO with five shapes, poly mod from oscillator 3 and the filter envelope, vintage, pan
 * spread, stereo distortion, two effects in series, unison with chord memory, glide and an arpeggiator).
 * Unofficial: not affiliated with or endorsed by Sequential. No code or data from Sequential's firmware.
 * Pure C, single-precision float, no allocation: runs on the FM-1 and on a computer alike. */
#pragma once
#include <stdint.h>

#define TRI_SR 44100.0f
#define TRI_MAXV 6
#define TRI_NAME_LEN 12

/* a patch's parameters, each a byte; the ranges are TRI_MAX[] */
enum {
    TP_O1_OCT, TP_O1_WAVE, TP_O1_PW, TP_O1_LEVEL,              /* OCT 0..4 = -2..+2; WAVE a mask: 1 tri 2 saw 4 pulse */
    TP_O2_OCT, TP_O2_WAVE, TP_O2_PW, TP_O2_LEVEL, TP_SYNC,     /* SYNC: osc 2 to osc 1 */
    TP_O3_OCT, TP_O3_WAVE, TP_O3_PW, TP_O3_LEVEL, TP_O3_KEY,   /* OCT 0 LO, 1..5 = -2..+2; WAVE adds 8 reverse saw */
    TP_NOISE, TP_FDRV,                                         /* FDRV: 0..63 feedback, 64 none, 65..127 drive */
    TP_CUTOFF, TP_RES, TP_FENV_AMT, TP_2POLE, TP_FKEY, TP_FVEL, /* FENV_AMT 64 = none; FKEY 0 off 1 half 2 full */
    TP_FA, TP_FD, TP_FS, TP_FR,
    TP_AA, TP_AD, TP_AS, TP_AR, TP_AENV_AMT, TP_AVEL,
    TP_LFO_FREQ, TP_LFO_SHAPE, TP_LFO_AMT, TP_LFO_SYNC,        /* SHAPE tri saw rev-saw square random; SYNC off key clock */
    TP_LFO_PITCH, TP_LFO_PW, TP_LFO_FILT, TP_LFO_AMP,          /* PITCH, PW: masks of oscs 1 2 3 (1 2 4) */
    TP_PM_OSC3, TP_PM_FENV,                                    /* poly mod amounts, 64 = none */
    TP_PM_PITCH, TP_PM_PW, TP_PM_FDBK, TP_PM_FILT,             /* PITCH mask of oscs 1 2 3, PW of oscs 1 2 */
    TP_DIST, TP_VINTAGE, TP_PAN, TP_VOLUME,
    TP_GLIDE_ON, TP_GLIDE_RATE, TP_GLIDE_MODE, TP_BEND,        /* GLIDE_MODE: FR FRA FT FTA; BEND semitones 0..12 */
    TP_UNISON, TP_KEYMODE,                                     /* UNISON: 0 off, 1..6 voices, 7 the chord; KEYMODE LO HI LAST LOr HIr LAr */
    TP_FX_ON, TP_FXA_TYPE, TP_FXA_MIX, TP_FXA_P1, TP_FXA_P2, TP_FXA_SYNC,
    TP_FXB_TYPE, TP_FXB_MIX, TP_FXB_P1, TP_FXB_P2, TP_FXB_SYNC,
    TP_ARP_ON, TP_ARP_MODE, TP_ARP_OCT, TP_ARP_DIV, TP_BPM, TP_ARP_HOLD,   /* MODE up down up+down random assign; BPM 30..250 */
    TP_N
};

/* the effects (Effect A takes 0..9, Effect B all) */
enum { FX_OFF, FX_BBD, FX_DDL, FX_CHO, FX_PH1, FX_PH2, FX_PH3, FX_RING, FX_FL1, FX_FL2,
       FX_HALL, FX_ROOM, FX_PLATE, FX_SPRING, FX_N };

/* the mod matrix: TRI_MM_SLOTS routes, each a source, a destination and a bipolar amount (64 = none), two bytes a
 * route in what were the patch's 16 spare bytes. (0.2 kept four routes of three bytes there: app.c converts a bank
 * saved by 0.2.) New sources and destinations go at the ends, so saved routes keep their meaning. */
#define TRI_MM_SLOTS 8
enum { MS_OFF, MS_LFO, MS_FENV, MS_AENV, MS_VEL, MS_WHEEL, MS_PRESS, MS_KEY, MS_RAND, MS_BEND,
       MS_OSC3,                                                /* OSC 3's wave: with OSC 3 at LO, a second LFO */
       MS_MORPH,                                               /* the MORPH knob (ALGO), 0..1: a macro each patch routes */
       MS_N };
enum { MD_OFF, MD_PITCH, MD_PITCH1, MD_PITCH2, MD_PITCH3, MD_PW1, MD_PW2, MD_PW3, MD_LEVEL1, MD_LEVEL2, MD_LEVEL3,
       MD_NOISE, MD_CUTOFF, MD_RES, MD_FDRV, MD_AMP, MD_PAN, MD_LFO_RATE, MD_LFO_AMT, MD_FXA_MIX, MD_FXB_MIX,
       MD_FENV_AMT, MD_ATTACK, MD_DECAY, MD_FXA_P1, MD_FXB_P1,   /* ATTACK both envelopes' attack, DECAY decay and release */
       MD_N };
typedef struct {                         /* 2 bytes: source 0..15, destination 0..31, amount 0..127 */
    uint16_t src : 4, dst : 5, amt : 7;
} tri_route_t;
_Static_assert(MS_N <= 16 && MD_N <= 32, "a route's fields hold every source and destination");
extern const char *const TRI_MS_NAME[MS_N];
extern const char *const TRI_MD_NAME[MD_N];

/* a patch: 112 bytes */
typedef struct {
    int16_t pitch2, pitch3;              /* oscs 2 and 3 fine pitch, cents, -700..700 */
    uint8_t p[TP_N];
    char name[TRI_NAME_LEN];
    uint8_t chord_n;                     /* unison chord memory: notes, 0 none */
    int8_t chord[6];                     /* their offsets from the lowest, in semitones */
    tri_route_t mm[TRI_MM_SLOTS];        /* the mod matrix (from byte 96) */
} tri_patch_t;

/* the settings that are not the patch's */
typedef struct {
    uint8_t voices;                      /* 1 .. TRI_MAXV */
    uint8_t clock_src;                   /* 0 internal, 1 MIDI clock, 2 auto (internal until MIDI clock comes) */
    uint8_t one_core;                    /* 1: everything on the first core */
    uint8_t vel_curve;                   /* 0 soft, 1 medium, 2 hard */
} tri_perf_t;

extern const uint8_t TRI_MAX[TP_N];      /* each parameter's top value */
extern const uint8_t TRI_MIN[TP_N];      /* and its bottom (mostly 0) */
extern const char *const TRI_PNAME[TP_N];

void tri_init(void);
void tri_set_patch(const tri_patch_t *p);
void tri_set_param(int i, int v);
int tri_param(int i);
tri_patch_t *tri_patch(void);            /* the engine's own copy (pitches, chord) */
void tri_set_perf(const tri_perf_t *f);

/* events: from the main loop or MIDI; queued and applied at the start of the next render */
void tri_note_on(int note, int vel);
void tri_note_off(int note);
void tri_all_off(void);
void tri_panic(void);
void tri_bend(int v);                    /* -8192 .. 8191 */
void tri_mod(int v);                     /* mod wheel 0..127: adds to the LFO amount; a mod matrix source */
void tri_pressure(int v);                /* pressure 0..127 (channel or the keys' own): a mod matrix source */
void tri_set_route(int slot, int src, int dst, int amt);
void tri_morph(int v);                   /* the MORPH knob 0..127: a mod matrix source */
void tri_sustain(int on);
void tri_chord_learn(void);              /* the next chord played (all keys up) is kept; UNISON becomes the chord */
void tri_clock(int rt);                  /* MIDI real time: 0xF8, 0xFA, 0xFB, 0xFC */

void tri_render(float *out, uint32_t n); /* n frames, L R L R ..., about +-1 */

/* for the screen */
float tri_voice_level(int v);
int tri_voice_note(int v);
int tri_learning(void);
int tri_arp_step_lit(void);
int tri_clock_running(void);
int tri_cores(void);                     /* 2 when the second core renders half the voices */
float tri_lfo_value(void);
