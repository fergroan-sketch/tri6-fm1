/* SPDX-License-Identifier: MPL-2.0 */
/* TRI-6 FM-1's engine (tri6.h).
 *
 * Written from the Trigon-6 user's guide and MIDI implementation: what each control does and its range. No code,
 * tables or data from Sequential's firmware or any other synth's.
 *
 * Per voice:  OSC 1 + OSC 2 (synced to 1) + OSC 3 + noise + feedback  -> drive -> ladder low-pass (2 or 4 pole)
 *             -> VCA -> pan
 * then:       sum -> distortion (stereo) -> effect A -> effect B -> program volume -> out
 *
 * Poly mod takes OSC 3 (at audio rate) and the filter envelope to the oscillators' pitch, OSCs 1 and 2's pulse width,
 * the feedback and the cutoff. The mod matrix (eight routes a patch) adds control-rate sources (LFO, the envelopes,
 * velocity, the mod wheel, pressure, the key, a random value a note, bend, OSC 3) to most of the voice, its envelopes'
 * times, and to the LFO and the effects' mix and P1. Control-rate parts (envelopes, LFO, pitch, the arpeggiator) run every CR samples;
 * cutoff and gain are ramped across each block. With two cores the second renders voices 4 to 6. */
#include "tri6.h"
#include "fastmath.h"
#include "plat.h"

#define CR 32                           /* control-rate block: 0.73 ms */
#define INV_SR (1.0f / TRI_SR)
#define CR_DT ((float)CR * INV_SR)

const uint8_t TRI_MAX[TP_N] = {
    [TP_O1_OCT] = 4, [TP_O1_WAVE] = 7, [TP_O1_PW] = 127, [TP_O1_LEVEL] = 127,
    [TP_O2_OCT] = 4, [TP_O2_WAVE] = 7, [TP_O2_PW] = 127, [TP_O2_LEVEL] = 127, [TP_SYNC] = 1,
    [TP_O3_OCT] = 5, [TP_O3_WAVE] = 15, [TP_O3_PW] = 127, [TP_O3_LEVEL] = 127, [TP_O3_KEY] = 1,
    [TP_NOISE] = 127, [TP_FDRV] = 127,
    [TP_CUTOFF] = 127, [TP_RES] = 127, [TP_FENV_AMT] = 127, [TP_2POLE] = 1, [TP_FKEY] = 2, [TP_FVEL] = 1,
    [TP_FA] = 127, [TP_FD] = 127, [TP_FS] = 127, [TP_FR] = 127,
    [TP_AA] = 127, [TP_AD] = 127, [TP_AS] = 127, [TP_AR] = 127, [TP_AENV_AMT] = 127, [TP_AVEL] = 1,
    [TP_LFO_FREQ] = 127, [TP_LFO_SHAPE] = 4, [TP_LFO_AMT] = 127, [TP_LFO_SYNC] = 2,
    [TP_LFO_PITCH] = 7, [TP_LFO_PW] = 7, [TP_LFO_FILT] = 1, [TP_LFO_AMP] = 1,
    [TP_PM_OSC3] = 127, [TP_PM_FENV] = 127, [TP_PM_PITCH] = 7, [TP_PM_PW] = 3, [TP_PM_FDBK] = 1, [TP_PM_FILT] = 1,
    [TP_DIST] = 127, [TP_VINTAGE] = 127, [TP_PAN] = 127, [TP_VOLUME] = 127,
    [TP_GLIDE_ON] = 1, [TP_GLIDE_RATE] = 127, [TP_GLIDE_MODE] = 3, [TP_BEND] = 12,
    [TP_UNISON] = 7, [TP_KEYMODE] = 5,
    [TP_FX_ON] = 1, [TP_FXA_TYPE] = FX_FL2, [TP_FXA_MIX] = 127, [TP_FXA_P1] = 127, [TP_FXA_P2] = 127, [TP_FXA_SYNC] = 1,
    [TP_FXB_TYPE] = FX_N - 1, [TP_FXB_MIX] = 127, [TP_FXB_P1] = 127, [TP_FXB_P2] = 127, [TP_FXB_SYNC] = 1,
    [TP_ARP_ON] = 1, [TP_ARP_MODE] = 4, [TP_ARP_OCT] = 2, [TP_ARP_DIV] = 9, [TP_BPM] = 250, [TP_ARP_HOLD] = 1,
};
const uint8_t TRI_MIN[TP_N] = {[TP_O1_WAVE] = 1, [TP_O2_WAVE] = 1, [TP_O3_WAVE] = 1, [TP_BPM] = 30};
const char *const TRI_MS_NAME[MS_N] = {"OFF", "LFO", "FIL ENV", "AMP ENV", "VELO", "WHEEL", "PRESSURE", "KEY",
                                      "RANDOM", "BEND", "OSC 3", "MORPH"};
const char *const TRI_MD_NAME[MD_N] = {"OFF", "PITCH", "PITCH 1", "PITCH 2", "PITCH 3", "PW 1", "PW 2", "PW 3",
                                      "LEVEL 1", "LEVEL 2", "LEVEL 3", "NOISE", "CUTOFF", "RES", "FB/DRV", "AMP",
                                      "PAN", "LFO RATE", "LFO AMT", "FXA MIX", "FXB MIX", "ENV AMT", "ATTACK",
                                      "DECAY", "FXA P1", "FXB P1"};
const char *const TRI_PNAME[TP_N] = {
    "OCT", "WAVE", "PW", "LEVEL",
    "OCT", "WAVE", "PW 2", "LEVEL", "SYNC 2",
    "OCT", "WAVE", "PW 3", "LEVEL", "KEY 3",
    "NOISE", "FDBK/DRV",
    "CUTOFF", "RES", "ENV AMT", "POLES", "KEY", "VEL",
    "ATTACK", "DECAY", "SUSTAIN", "RELEASE",
    "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "ENV AMT", "VEL",
    "FREQ", "SHAPE", "AMOUNT", "SYNC",
    "PITCH", "PW", "FILTER", "AMP",
    "OSC 3", "FIL ENV", "PITCH", "PW", "FDBK", "FILTER",
    "DISTORT", "VINTAGE", "PAN", "VOLUME",
    "GLIDE", "RATE", "MODE", "BEND",
    "UNISON", "KEY MODE",
    "FX", "TYPE", "MIX", "P1", "P2", "SYNC",
    "TYPE", "MIX", "P1", "P2", "SYNC",
    "ARP", "MODE", "OCT", "DIVIDE", "BPM", "HOLD",
};

/* ------------------------------------------------------------------ state --- */
typedef struct {
    int stage;                           /* 0 idle, 1 attack, 2 decay/sustain, 3 release */
    float level;
} env_t;

typedef struct {
    int note, key;                       /* the pitch it plays, the key that started it; -1 free */
    uint8_t gate, held;
    uint32_t age;
    float vel;                           /* 0..1, after the curve */
    float mrand;                         /* the mod matrix's RANDOM: -1..1, new each note */
    float pitch, pitch_target, glide_step;   /* semitones; glide: semitones a control block (0: at once) */
    float uni;                           /* unison spread, semitones */
    float drift, drift_to;
    uint32_t drift_t;
    float vr[6];                         /* vintage: this voice's own offsets, -1..1 (3 pitches, cutoff, env, pw) */
    env_t ef, ea;
    float gate_lvl;
    /* the audio: set by the control block, read by the render */
    float ph[3], inc[3], inv[3], pw[3], lv[3], hw[3];
    int m[3];
    int sync;
    float noise, drive, fbk, k, comp;
    int two_pole;
    float g0, g1, a0, a1;                /* filter coefficient and gain, ramped */
    float gl, gr;                        /* pan */
    float pm_f[3], pm_pw[2], pm_fb, pm_cut;  /* poly mod from OSC 3, per sample */
    int pm_on;
    float s[4], fb_y, fb_lp;
    uint32_t rng;
} voice_t;

static PLAT_POOL voice_t V[TRI_MAXV];
static tri_patch_t P;
static tri_perf_t F = {6, 2, 0, 1};
static uint32_t clock_age;
static int next_voice;
static float bend, modw, press, morph;
static float mm_lfo_rate, mm_lfo_amt, mm_fx_mix[2], mm_fx_p1[2];   /* the mod matrix on what all voices share */
static int sustain;
static int low_note = 48;                /* the lowest note sounding: the ring modulator's tracking */

/* the LFO */
static float lfo_phase, lfo_val, lfo_uni, lfo_sh;
static uint32_t lfo_rng = 0x1234567u;

/* the clock: internal from BPM or MIDI's; 24 ticks a beat either way */
static uint32_t sample_t, tick_last, tick_count;
static float tick_len;                   /* samples a MIDI tick, smoothed */
static int clock_seen;
static float int_acc;                    /* the internal clock's tick fraction */

#define EVQ 128
static volatile uint32_t evq[EVQ];
static volatile uint32_t ev_w, ev_r;
enum { EV_ON = 1, EV_OFF, EV_ALLOFF, EV_PANIC, EV_BEND, EV_MOD, EV_SUS, EV_LEARN, EV_RT, EV_PRESS, EV_MORPH };

static void ev_push(uint32_t type, uint32_t a, uint32_t b)
{
    uint32_t w = ev_w;
    if (w - ev_r >= EVQ)
        return;
    evq[w % EVQ] = type | (a & 0xFFFFu) << 8 | (b & 0xFFu) << 24;
    ev_w = w + 1u;
}

/* --------------------------------------------------------------- curves --- */
static float knob(int i) { return (float)P.p[i] * (1.0f / 127.0f); }
static float bip(int i) { return ((float)P.p[i] - 64.0f) * (1.0f / 63.0f); }   /* 64 = 0, -1.016..1 */
static float sq_sign(float x) { return x < 0.0f ? -x * x : x * x; }
static float env_time(int v, float lo, float top)
{
    return lo * fm_exp2f((float)v * (1.0f / 127.0f) * fm_log2f(top / lo));
}
static float coef(float t)               /* a control block's step of an exponential that is 99% there in t */
{
    float k = CR_DT * 4.6f / t;
    return k >= 1.0f ? 1.0f : 1.0f - fm_expf(-k);
}
static float bpm_now(void);

static inline float rnd(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return (float)(int32_t)*s * (1.0f / 2147483648.0f);
}

static inline float step_after(float h, float t) { return -0.5f * h * (1.0f - t) * (1.0f - t); }
static inline float step_before(float h, float t) { return 0.5f * h * t * t; }

static inline float sat(float x)
{
    float x2;
    if (x > 3.0f)
        return 1.0f;
    if (x < -3.0f)
        return -1.0f;
    x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* ------------------------------------------------------------ voice modes --- */
static int nvoices(void) { return F.voices < 1 ? 1 : F.voices > TRI_MAXV ? TRI_MAXV : F.voices; }
static int uni_mode(void) { return P.p[TP_UNISON]; }   /* 0 poly, 1..6 stacked, 7 the chord */
static float vel_curve(int vel)
{
    float x = (float)vel * (1.0f / 127.0f);
    return F.vel_curve == 0 ? fm_sqrtf(x) : F.vel_curve == 2 ? x * x : x;
}

static int any_held(void)
{
    int i;
    for (i = 0; i < TRI_MAXV; i++)
        if (V[i].held)
            return 1;
    return 0;
}

static void glide_to(voice_t *v, float note, int legato)
{
    int mode = P.p[TP_GLIDE_MODE];
    float r = (float)P.p[TP_GLIDE_RATE];
    v->pitch_target = note;
    if (!P.p[TP_GLIDE_ON] || r == 0.0f || ((mode == 1 || mode == 3) && !legato)) {
        v->pitch = note;
        v->glide_step = 0.0f;
        return;
    }
    if (mode < 2)                        /* fixed rate: 2000 .. 0.1 semitones a second */
        v->glide_step = 2000.0f * fm_exp2f(-r * (14.0f / 127.0f)) * CR_DT;
    else {                               /* fixed time: 2 ms .. 4 s whatever the interval */
        float t = 0.002f * fm_exp2f(r * (11.0f / 127.0f)), d = fm_fabsf(note - v->pitch);
        v->glide_step = d * CR_DT / t;
    }
}

static void voice_start(voice_t *v, int key, int note, float vel, int retrigger, int legato)
{
    v->key = key;
    v->note = note;
    v->held = 1;
    v->age = ++clock_age;
    v->vel = vel;
    glide_to(v, (float)note, legato);
    if (retrigger || v->ea.stage == 0 || v->ea.stage == 3) {
        v->mrand = rnd(&v->rng);
        v->gate = 1;
        v->ea.stage = 1;
        v->ef.stage = 1;
    }
}

static void voice_release(voice_t *v)
{
    v->held = 0;
    if (!sustain) {
        v->gate = 0;
        if (v->ea.stage)
            v->ea.stage = 3;
        if (v->ef.stage)
            v->ef.stage = 3;
    }
}

static int voice_pick(int n)             /* round robin: a free voice from the last one given, else the oldest */
{
    int k, best = -1;
    uint32_t oldest = 0xFFFFFFFFu;
    for (k = 0; k < n; k++) {
        int j = (next_voice + k) % n;
        if (V[j].ea.stage == 0 && V[j].a0 == 0.0f) {
            best = j;
            break;
        }
    }
    if (best < 0)
        for (k = 0; k < n; k++)
            if (!V[k].held && V[k].age < oldest) {
                oldest = V[k].age;
                best = k;
            }
    if (best < 0)
        for (k = 0; k < n; k++)
            if (V[k].age < oldest) {
                oldest = V[k].age;
                best = k;
            }
    next_voice = (best + 1) % n;
    return best;
}

/* unison and the chord: the keys held, in the order pressed; the key mode picks which one sounds */
static int8_t keys_down[16];
static uint8_t keys_vel[16];
static int nkeys, mono_key = -1;

static void keys_add(int note, int vel)
{
    int i;
    for (i = 0; i < nkeys; i++)
        if (keys_down[i] == note)
            return;
    if (nkeys < 16) {
        keys_vel[nkeys] = (uint8_t)vel;
        keys_down[nkeys++] = (int8_t)note;
    }
}
static void keys_remove(int note)
{
    int i, j;
    for (i = 0; i < nkeys; i++)
        if (keys_down[i] == note) {
            for (j = i; j < nkeys - 1; j++) {
                keys_down[j] = keys_down[j + 1];
                keys_vel[j] = keys_vel[j + 1];
            }
            nkeys--;
            return;
        }
}
static int keys_pick(void)               /* index into keys_down by the key mode, -1 none */
{
    int i, b = 0, km = P.p[TP_KEYMODE] % 3;
    if (nkeys == 0)
        return -1;
    if (km == 2)
        return nkeys - 1;
    for (i = 1; i < nkeys; i++)
        if (km == 0 ? keys_down[i] < keys_down[b] : keys_down[i] > keys_down[b])
            b = i;
    return b;
}

static void mono_play(int idx, int retrig, int legato)   /* the stack (or the chord) on keys_down[idx] */
{
    int i, n = nvoices(), u = uni_mode(), note = keys_down[idx];
    float fv = vel_curve(keys_vel[idx]);
    mono_key = note;
    if (u == 7 && P.chord_n) {
        int k;
        for (k = 0; k < n; k++)
            if (k < P.chord_n)
                voice_start(&V[k], note, note + P.chord[k], fv, retrig, legato);
            else if (V[k].held)
                voice_release(&V[k]);
        return;
    }
    if (u == 7)
        u = 1;                           /* no chord learnt yet: one voice */
    for (i = 0; i < n; i++)
        if (i < u)
            voice_start(&V[i], note, note, fv, retrig, legato);
        else if (V[i].held)
            voice_release(&V[i]);
}

/* chord memory */
static int learning;
static int8_t learn_notes[6];
static int learn_n, learn_down;

static void learn_key(int note, int on)
{
    int i;
    if (on) {
        learn_down++;
        if (learn_n < 6) {
            for (i = 0; i < learn_n; i++)
                if (learn_notes[i] == note)
                    return;
            learn_notes[learn_n++] = (int8_t)note;
        }
        return;
    }
    if (--learn_down > 0 || learn_n == 0)
        return;
    {
        int lo = 127, a, b;
        for (i = 0; i < learn_n; i++)
            if (learn_notes[i] < lo)
                lo = learn_notes[i];
        for (i = 0; i < learn_n; i++)
            learn_notes[i] = (int8_t)(learn_notes[i] - lo);
        for (a = 0; a < learn_n; a++)
            for (b = a + 1; b < learn_n; b++)
                if (learn_notes[b] < learn_notes[a]) {
                    int8_t t = learn_notes[a];
                    learn_notes[a] = learn_notes[b];
                    learn_notes[b] = t;
                }
        for (i = 0; i < 6; i++)
            P.chord[i] = (int8_t)(i < learn_n ? learn_notes[i] : 0);
        P.chord_n = (uint8_t)learn_n;
        P.p[TP_UNISON] = 7;
        learning = 0;
        learn_down = 0;
    }
}

static void lfo_key_reset(void);

static void play_on(int note, int vel)
{
    int n = nvoices(), legato = any_held() || nkeys > 0;
    if (!legato)
        lfo_key_reset();
    if (learning) {
        learn_key(note, 1);
        voice_start(&V[voice_pick(n)], note, note, vel_curve(vel), 1, 0);   /* heard while learnt */
        return;
    }
    if (uni_mode()) {
        int had = nkeys, idx;
        keys_add(note, vel);
        idx = keys_pick();
        if (keys_down[idx] != mono_key || !had)
            mono_play(idx, !had || P.p[TP_KEYMODE] >= 3, had);
        return;
    }
    voice_start(&V[voice_pick(n)], note, note, vel_curve(vel), 1, legato);
}

static void play_off(int note)
{
    int i;
    if (learning) {
        learn_key(note, 0);
        for (i = 0; i < TRI_MAXV; i++)
            if (V[i].key == note && V[i].held)
                voice_release(&V[i]);
        return;
    }
    if (uni_mode()) {
        int idx;
        keys_remove(note);
        idx = keys_pick();
        if (idx < 0) {
            mono_key = -1;
            for (i = 0; i < TRI_MAXV; i++)
                if (V[i].held)
                    voice_release(&V[i]);
        } else if (keys_down[idx] != mono_key) {
            mono_play(idx, 0, 1);        /* back to a key still held: the pitch only */
        }
        return;
    }
    for (i = 0; i < TRI_MAXV; i++)
        if (V[i].key == note && V[i].held)
            voice_release(&V[i]);
}

/* ----------------------------------------------------------- arpeggiator --- */
static int arp_on;                       /* 0 off, 1 on, 2 hold: the patch's ARP and HOLD, as last seen */
static int8_t arp_keys[16];
static uint8_t arp_nk, arp_down, arp_vel = 100;
static int arp_pos, arp_note = -1, arp_restart;
static uint32_t arp_ticks, arp_swing;    /* ticks since the last step; the swing's long/short turn */
static float arp_gate_left;
static volatile int arp_lit;

static void arp_key(int note, int vel, int on)
{
    int i, j;
    if (on) {
        if (arp_down == 0) {
            if (arp_on == 2)
                arp_nk = 0;              /* hold: a new chord replaces the one held */
            arp_restart = 1;
        }
        arp_down++;
        arp_vel = (uint8_t)vel;
        for (i = 0; i < arp_nk; i++)
            if (arp_keys[i] == note)
                return;
        if (arp_nk < 16)
            arp_keys[arp_nk++] = (int8_t)note;
        return;
    }
    if (arp_down)
        arp_down--;
    if (arp_on == 2)
        return;
    for (i = 0; i < arp_nk; i++)
        if (arp_keys[i] == note) {
            for (j = i; j < arp_nk - 1; j++)
                arp_keys[j] = arp_keys[j + 1];
            arp_nk--;
            return;
        }
}

static int arp_len(void)
{
    int n = arp_nk * (P.p[TP_ARP_OCT] + 1);
    if (P.p[TP_ARP_MODE] == 2 && n > 2)
        return 2 * n - 2;
    return n;
}
static int arp_note_at(int k)
{
    int8_t s[16];
    int i, j, n = arp_nk, total = n * (P.p[TP_ARP_OCT] + 1), idx, note, mode = P.p[TP_ARP_MODE];
    for (i = 0; i < n; i++)
        s[i] = arp_keys[i];
    if (mode != 4)                       /* by pitch; ASSIGN keeps the order played */
        for (i = 0; i < n; i++)
            for (j = i + 1; j < n; j++)
                if (s[j] < s[i]) {
                    int8_t t = s[i];
                    s[i] = s[j];
                    s[j] = t;
                }
    if (mode == 1)
        idx = total - 1 - k;
    else if (mode == 2 && total > 2)
        idx = k < total ? k : 2 * total - 2 - k;
    else
        idx = k;
    note = s[idx % n] + 12 * (idx / n);
    while (note > 127)
        note -= 12;
    return note;
}

static void arp_release_note(void)
{
    if (arp_note >= 0) {
        play_off(arp_note);
        arp_note = -1;
    }
}

/* DIVIDE: half, quarter, 8th, dotted 8th, swung 8th, 8th triplet, 16th, swung 16th, 16th triplet, 32nd */
static uint32_t arp_step_ticks(void)
{
    static const uint8_t T[10] = {48, 24, 12, 18, 12, 8, 6, 6, 4, 3};
    int d = P.p[TP_ARP_DIV] % 10;
    if (d == 4)
        return arp_swing ? 8u : 16u;
    if (d == 7)
        return arp_swing ? 4u : 8u;
    return T[d];
}

static void arp_step(void)
{
    int len = arp_len();
    float tl = tick_len > 0.0f ? tick_len : TRI_SR * 60.0f / (bpm_now() * 24.0f);
    arp_release_note();
    if (arp_nk == 0 || len == 0)
        return;
    if (arp_restart) {
        arp_pos = 0;
        arp_restart = 0;
    } else if (P.p[TP_ARP_MODE] == 3) {
        arp_pos = (int)((0.5f + 0.5f * rnd(&lfo_rng)) * (float)len) % len;
    } else {
        arp_pos = (arp_pos + 1) % len;
    }
    arp_note = arp_note_at(arp_pos);
    play_on(arp_note, arp_vel);
    arp_gate_left = (float)arp_step_ticks() * tl * 0.6f;   /* samples */
    arp_lit = 1;
}

static void arp_tick(void)               /* every clock tick */
{
    if (!arp_on)
        return;
    if (arp_nk == 0) {
        arp_ticks = 1000;                /* the first step comes at the first tick */
        arp_swing = 0;
        return;
    }
    if (++arp_ticks >= arp_step_ticks() || arp_restart) {
        arp_ticks = 0;
        arp_swing ^= 1u;
        arp_step();
    }
}

static void do_all_off(void);
static void arp_state(void)              /* the patch's ARP / HOLD changed */
{
    int want = P.p[TP_ARP_ON] ? (P.p[TP_ARP_HOLD] ? 2 : 1) : 0;
    if (want == arp_on)
        return;
    if (!want || !arp_on)
        do_all_off();
    else if (want == 1 && arp_down == 0) {
        arp_release_note();
        arp_nk = 0;
    }
    arp_on = want;
    arp_restart = 1;
    arp_ticks = 1000;
}

/* ------------------------------------------------------------------ input --- */
static void key_on(int note, int vel)
{
    if (arp_on)
        arp_key(note, vel, 1);
    else
        play_on(note, vel);
}
static void key_off(int note)
{
    if (arp_on)
        arp_key(note, 0, 0);
    else
        play_off(note);
}

static void do_sustain(int on)
{
    int i;
    sustain = on;
    if (!on)
        for (i = 0; i < TRI_MAXV; i++)
            if (!V[i].held && V[i].gate) {
                V[i].gate = 0;
                if (V[i].ea.stage)
                    V[i].ea.stage = 3;
                if (V[i].ef.stage)
                    V[i].ef.stage = 3;
            }
}

static void do_all_off(void)
{
    int i;
    nkeys = 0;
    mono_key = -1;
    arp_nk = arp_down = 0;
    arp_note = -1;
    learn_down = 0;
    for (i = 0; i < TRI_MAXV; i++) {
        V[i].held = 0;
        V[i].gate = 0;
        if (V[i].ea.stage)
            V[i].ea.stage = 3;
        if (V[i].ef.stage)
            V[i].ef.stage = 3;
    }
}

static void do_panic(void)
{
    int i;
    do_all_off();
    for (i = 0; i < TRI_MAXV; i++) {
        voice_t *v = &V[i];
        v->note = v->key = -1;
        v->ea.stage = v->ef.stage = 0;
        v->ea.level = v->ef.level = v->gate_lvl = v->a0 = v->a1 = 0.0f;
        v->s[0] = v->s[1] = v->s[2] = v->s[3] = v->fb_y = v->fb_lp = 0.0f;
    }
}

static int use_midi_clock(void) { return F.clock_src == 1 || (F.clock_src == 2 && clock_seen); }
static float bpm_now(void)
{
    if (use_midi_clock() && tick_len > 0.0f)
        return TRI_SR * 60.0f / (24.0f * tick_len);
    return (float)(P.p[TP_BPM] < 30 ? 30 : P.p[TP_BPM]);
}

static void tick(void)
{
    tick_count++;
    arp_tick();
}

static void midi_rt(int rt)
{
    if (rt == 0xF8) {
        uint32_t d = sample_t - tick_last;
        if (clock_seen && d > 0u && d < (uint32_t)(TRI_SR * 0.25f))
            tick_len = tick_len > 0.0f ? tick_len + ((float)d - tick_len) * 0.15f : (float)d;
        tick_last = sample_t;
        clock_seen = 1;
        if (use_midi_clock())
            tick();
    } else if (rt == 0xFA) {
        tick_count = 0;
        arp_ticks = 1000;
        arp_restart = 1;
    } else if (rt == 0xFC) {
        if (arp_on && use_midi_clock())
            arp_release_note();
    }
}

static void events(void)
{
    while (ev_r != ev_w) {
        uint32_t e = evq[ev_r % EVQ], t = e & 0xFFu, a = (e >> 8) & 0xFFFFu, b = e >> 24;
        ev_r++;
        switch (t) {
        case EV_ON: key_on((int)a, (int)b); break;
        case EV_OFF: key_off((int)a); break;
        case EV_ALLOFF: do_all_off(); break;
        case EV_PANIC: do_panic(); break;
        case EV_BEND: bend = ((float)a - 8192.0f) * (1.0f / 8192.0f); break;
        case EV_MOD: modw = (float)b * (1.0f / 127.0f); break;
        case EV_PRESS: press = (float)b * (1.0f / 127.0f); break;
        case EV_MORPH: morph = (float)b * (1.0f / 127.0f); break;
        case EV_SUS: do_sustain((int)b); break;
        case EV_LEARN:
            learning = 1;
            learn_n = learn_down = 0;
            break;
        case EV_RT: midi_rt((int)b); break;
        default: break;
        }
    }
}

/* ----------------------------------------------------------- control rate --- */
/* attack: an RC charge aimed past the top, as an analog envelope's; decay and release exponential */
static void env_run(env_t *e, float att, float decay, float sus, float release)
{
    if (e->stage == 1) {
        e->level += (1.3f - e->level) * att;
        if (e->level >= 1.0f) {
            e->level = 1.0f;
            e->stage = 2;
        }
    } else if (e->stage == 2) {
        e->level += (sus - e->level) * decay;
    } else if (e->stage == 3) {
        e->level -= e->level * release;
        if (e->level < 0.0002f)
            e->level = 0.0f;
    }
}
static float att_coef(float t)           /* reaching 1 of 1.3 takes 1.466 time constants */
{
    float k = CR_DT * 1.466f / t;
    return k >= 1.0f ? 1.0f : 1.0f - fm_expf(-k);
}

/* LFO FREQ in sync: 4 bars .. 1/32, in ticks */
static const uint16_t LFO_SYNC_TICKS[12] = {384, 192, 96, 72, 48, 32, 24, 16, 12, 8, 6, 3};

static void lfo_key_reset(void)
{
    if (P.p[TP_LFO_SYNC] == 1)
        lfo_phase = 0.0f;
}

static void lfo_control(void)
{
    int shape = P.p[TP_LFO_SHAPE];
    float ph_was = lfo_phase;
    if (P.p[TP_LFO_SYNC] == 2) {         /* to the clock: the phase from the ticks */
        uint32_t n = LFO_SYNC_TICKS[P.p[TP_LFO_FREQ] * 12 / 128];
        float tl = use_midi_clock() && tick_len > 0.0f ? tick_len : TRI_SR * 60.0f / (bpm_now() * 24.0f);
        float frac = use_midi_clock() ? (float)(sample_t - tick_last) / tl : int_acc;
        lfo_phase = ((float)(tick_count % n) + (frac < 1.0f ? frac : 1.0f)) / (float)n;
    } else {                             /* free: 0.05 Hz .. 100 Hz */
        lfo_phase += 0.05f * fm_exp2f(fm_clampf(knob(TP_LFO_FREQ) * 11.0f + mm_lfo_rate, -3.0f, 12.0f)) * CR_DT;
        if (lfo_phase >= 1.0f)
            lfo_phase -= fm_floorf(lfo_phase);
    }
    switch (shape) {
    case 0: lfo_val = lfo_phase < 0.5f ? 4.0f * lfo_phase - 1.0f : 3.0f - 4.0f * lfo_phase; lfo_uni = 0.5f + 0.5f * lfo_val; break;
    case 1: lfo_uni = lfo_phase; lfo_val = lfo_uni; break;
    case 2: lfo_uni = 1.0f - lfo_phase; lfo_val = lfo_uni; break;
    case 3: lfo_uni = lfo_phase < 0.5f ? 1.0f : 0.0f; lfo_val = lfo_uni; break;
    default:
        if ((P.p[TP_LFO_FREQ] == 127 && P.p[TP_LFO_SYNC] != 2) || lfo_phase < ph_was)
            lfo_sh = rnd(&lfo_rng);      /* a new step each cycle; at the top of FREQ, noise */
        lfo_val = lfo_sh;
        lfo_uni = 0.5f + 0.5f * lfo_sh;
        break;
    }
}

/* ------------------------------------------------------------ mod matrix --- */
static inline float wave(float ph, float pw, int m);
static float mm_src(int s, const voice_t *v)   /* a source's value now: unipolar 0..1 or bipolar -1..1 */
{
    switch (s) {
    case MS_LFO: return lfo_val;
    case MS_FENV: return v ? v->ef.level : 0.0f;
    case MS_AENV: return v ? v->ea.level : 0.0f;
    case MS_VEL: return v ? v->vel : 0.0f;
    case MS_WHEEL: return modw;
    case MS_PRESS: return press;
    case MS_KEY: return v ? fm_clampf((v->pitch - 60.0f) * (1.0f / 60.0f), -1.0f, 1.0f) : 0.0f;
    case MS_RAND: return v ? v->mrand : 0.0f;
    case MS_BEND: return bend;
    case MS_MORPH: return morph;
    case MS_OSC3: return v ? fm_clampf(wave(v->ph[2], v->pw[2], v->m[2]), -1.0f, 1.0f) : 0.0f;
    default: return 0.0f;
    }
}
static float mm_amt(const tri_route_t *r)       /* -1..1; the pitches on a square law, for fine vibrato */
{
    float a = ((float)r->amt - 64.0f) * (1.0f / 63.0f);
    if (a > 1.0f)
        a = 1.0f;
    return r->dst >= MD_PITCH && r->dst <= MD_PITCH3 ? sq_sign(a) : a;
}
/* the routes into d[MD_N] for voice v (NULL: what all voices share, from the newest voice) */
static int mm_sum(const voice_t *v, float *d)
{
    int s, any = 0;
    for (s = 0; s < MD_N; s++)
        d[s] = 0.0f;
    for (s = 0; s < TRI_MM_SLOTS; s++) {
        const tri_route_t *r = &P.mm[s];
        if (r->src == MS_OFF || r->dst == MD_OFF || r->src >= MS_N || r->dst >= MD_N || r->amt == 64)
            continue;
        d[r->dst] += mm_src(r->src, v) * mm_amt(r);
        any = 1;
    }
    return any;
}

static void mm_global(void)
{
    float d[MD_N];
    const voice_t *nv = 0;
    uint32_t best = 0;
    int i;
    for (i = 0; i < TRI_MAXV; i++)
        if (V[i].ea.stage && V[i].age >= best) {
            best = V[i].age;
            nv = &V[i];
        }
    mm_sum(nv, d);
    mm_lfo_rate = d[MD_LFO_RATE] * 4.0f;     /* +-4 octaves */
    mm_lfo_amt = d[MD_LFO_AMT];
    mm_fx_mix[0] = d[MD_FXA_MIX];
    mm_fx_mix[1] = d[MD_FXB_MIX];
    mm_fx_p1[0] = d[MD_FXA_P1];
    mm_fx_p1[1] = d[MD_FXB_P1];
}

static void control(void)
{
    static const float UNI[6] = {0.0f, 1.0f, -1.0f, 2.0f, -2.0f, 3.0f};
    static const float PAN[6] = {-1.0f, 1.0f, -0.6f, 0.6f, -0.25f, 0.25f};
    int i, n = nvoices(), u = uni_mode();
    float vint = knob(TP_VINTAGE);
    float fa_t = env_time(P.p[TP_FA], 0.0008f, 10.0f), aa_t = env_time(P.p[TP_AA], 0.0008f, 10.0f);
    float fa = att_coef(fa_t), fd = env_time(P.p[TP_FD], 0.002f, 15.0f);
    float fr = env_time(P.p[TP_FR], 0.002f, 15.0f), fs = knob(TP_FS);
    float aa = att_coef(aa_t), ad = env_time(P.p[TP_AD], 0.002f, 15.0f);
    float ar = env_time(P.p[TP_AR], 0.002f, 15.0f), as = knob(TP_AS);
    float lfo_d, lfo_pitch, lfo_pw, lfo_cut, lfo_amp, bend_semi, cut0, fenv, res, k, comp, fdrv, drive, fbk;
    float pm3 = bip(TP_PM_OSC3), pmf = bip(TP_PM_FENV), aamt = knob(TP_AENV_AMT), spread = knob(TP_PAN);
    float pw_base[3] = {knob(TP_O1_PW) * 0.99f, knob(TP_O2_PW) * 0.99f, knob(TP_O3_PW) * 0.99f};
    float lv[3] = {knob(TP_O1_LEVEL), knob(TP_O2_LEVEL), knob(TP_O3_LEVEL)};
    int mask[3] = {P.p[TP_O1_WAVE], P.p[TP_O2_WAVE], P.p[TP_O3_WAVE]};
    float oct[3] = {12.0f * (float)(P.p[TP_O1_OCT] - 2), 12.0f * (float)(P.p[TP_O2_OCT] - 2) + (float)P.pitch2 * 0.01f,
                    P.p[TP_O3_OCT] == 0 ? -96.0f + (float)P.pitch3 * 0.01f
                                        : 12.0f * (float)(P.p[TP_O3_OCT] - 3) + (float)P.pitch3 * 0.01f};
    int lfo_pm = P.p[TP_LFO_PITCH], lfo_pwm = P.p[TP_LFO_PW], pm_pitch = P.p[TP_PM_PITCH], pm_pw = P.p[TP_PM_PW];
    float uni_step = 0.03f + vint * 0.12f;
    mm_global();
    lfo_control();
    if (!use_midi_clock()) {             /* the internal clock */
        int_acc += CR_DT * bpm_now() * (24.0f / 60.0f);
        while (int_acc >= 1.0f) {
            int_acc -= 1.0f;
            tick();
        }
    }
    if (arp_on && arp_note >= 0) {
        arp_gate_left -= (float)CR;
        if (arp_gate_left <= 0.0f)
            arp_release_note();
    }
    lfo_d = fm_clampf(knob(TP_LFO_AMT) + modw + mm_lfo_amt, 0.0f, 1.0f);
    lfo_pitch = lfo_val * lfo_d * lfo_d * 12.0f;     /* up to an octave */
    lfo_pw = lfo_val * lfo_d * 0.45f;
    lfo_cut = P.p[TP_LFO_FILT] ? lfo_val * lfo_d * 5.0f : 0.0f;   /* octaves */
    lfo_amp = P.p[TP_LFO_AMP] ? lfo_d : 0.0f;
    bend_semi = bend * (float)P.p[TP_BEND];
    cut0 = knob(TP_CUTOFF) * 10.4f;      /* octaves above 15 Hz: 15 Hz .. 20 kHz */
    fenv = bip(TP_FENV_AMT) * 9.0f;
    res = knob(TP_RES);
    k = 4.25f * res * (0.82f + 0.18f * res);
    comp = 1.0f + 0.3f * k;
    fdrv = bip(TP_FDRV);
    drive = fdrv > 0.0f ? 1.0f + 7.0f * fdrv * fdrv : 1.0f;
    fbk = fdrv < 0.0f ? -fdrv * 2.4f : 0.0f;
    low_note = 127;
    for (i = 0; i < TRI_MAXV; i++) {
        voice_t *v = &V[i];
        float ef, ea, cut_oct, fc, g, amp, base, pitch, et = 1.0f + 0.15f * vint * v->vr[4];
        float md[MD_N], fav = fa, aav = aa, dm = 1.0f;
        int o, mm;
        if (i >= n && v->ea.stage) {
            v->held = v->gate = 0;
            v->ea.stage = 3;
            if (v->ef.stage)
                v->ef.stage = 3;
        }
        v->g0 = v->g1;
        v->a0 = v->a1;
        if (v->ea.stage == 0 && v->a0 == 0.0f)
            continue;                    /* silent: nothing to compute */
        mm = mm_sum(v, md);              /* the matrix, from the sources as the last block left them */
        if (md[MD_ATTACK] != 0.0f) {     /* +-4 octaves of time: 16 times shorter or longer */
            float x = fm_exp2f(fm_clampf(md[MD_ATTACK], -1.0f, 1.0f) * 4.0f);
            fav = att_coef(fa_t * x);
            aav = att_coef(aa_t * x);
        }
        if (md[MD_DECAY] != 0.0f)
            dm = fm_exp2f(fm_clampf(md[MD_DECAY], -1.0f, 1.0f) * 4.0f);
        env_run(&v->ef, fav, coef(fd * et * dm), fs, coef(fr * et * dm));
        env_run(&v->ea, aav, coef(ad * et * dm), as, coef(ar * et * dm));
        v->gate_lvl += ((v->gate ? 1.0f : 0.0f) - v->gate_lvl) * 0.3f;
        ef = v->ef.level;
        ea = v->ea.level;
        if (v->ea.stage == 3 && ea == 0.0f && (aamt > 0.99f || v->gate_lvl < 0.0005f)) {
            v->ea.stage = v->ef.stage = 0;
            v->ef.level = 0.0f;
            v->note = v->key = -1;
        }
        if (v->note >= 0 && v->note < low_note)
            low_note = v->note;
        /* vintage: a slow wander, wider with VINTAGE */
        if (vint > 0.0f) {
            if (v->drift_t == 0) {
                v->drift_to = vint * 0.09f * rnd(&v->rng);
                v->drift_t = (uint32_t)((1.5f + 2.5f * (0.5f + 0.5f * rnd(&v->rng))) / CR_DT);
            } else {
                v->drift_t--;
            }
            v->drift += (v->drift_to - v->drift) * 0.001f;
        } else {
            v->drift = 0.0f;
        }
        /* glide */
        if (v->glide_step <= 0.0f)
            v->pitch = v->pitch_target;
        else if (v->pitch < v->pitch_target)
            v->pitch = fm_minf(v->pitch + v->glide_step, v->pitch_target);
        else
            v->pitch = fm_maxf(v->pitch - v->glide_step, v->pitch_target);
        v->uni = u >= 1 && u <= 6 ? uni_step * UNI[i] : 0.0f;
        pitch = v->pitch + bend_semi + v->drift + v->uni + md[MD_PITCH] * 24.0f;
        for (o = 0; o < 3; o++) {
            float semis = (o == 2 && !P.p[TP_O3_KEY] ? 60.0f + bend_semi : pitch) + oct[o]
                        + vint * 0.15f * v->vr[o];
            float pw = pw_base[o];
            int m = mask[o];
            if (lfo_pm >> o & 1)
                semis += lfo_pitch;
            if (pm_pitch >> o & 1)
                semis += sq_sign(pmf) * 48.0f * ef;
            semis += md[MD_PITCH1 + o] * 24.0f;
            v->inc[o] = fm_minf(440.0f * fm_exp2f((semis - 69.0f) * (1.0f / 12.0f)) * INV_SR, 0.45f);
            v->inv[o] = 1.0f / v->inc[o];
            if (lfo_pwm >> o & 1)
                pw += lfo_pw;
            if (o < 2 && (pm_pw >> o & 1))
                pw += pmf * 0.5f * ef;
            pw += vint * 0.02f * v->vr[5] + md[MD_PW1 + o] * 0.5f;
            v->pw[o] = fm_clampf(pw, 0.0f, 0.98f);
            if (v->pw[o] < 0.005f)
                m &= ~4;                 /* a pulse of zero width: silent */
            v->m[o] = m;
            v->hw[o] = ((m & 2) ? -2.0f : 0.0f) + ((m & 8) ? 2.0f : 0.0f) + ((m & 4) ? 2.0f : 0.0f);
            v->lv[o] = (mm ? fm_clampf(lv[o] + md[MD_LEVEL1 + o], 0.0f, 1.0f) : lv[o]) * 0.7f;
            v->pm_f[o] = (pm_pitch >> o & 1) ? sq_sign(pm3) * 4.0f : 0.0f;   /* octaves per unit of OSC 3 */
        }
        v->pm_pw[0] = (pm_pw & 1) ? pm3 * 0.5f : 0.0f;
        v->pm_pw[1] = (pm_pw & 2) ? pm3 * 0.5f : 0.0f;
        v->pm_fb = P.p[TP_PM_FDBK] ? pm3 * 2.0f : 0.0f;
        v->pm_cut = P.p[TP_PM_FILT] ? sq_sign(pm3) * 6.0f : 0.0f;
        v->pm_on = v->pm_f[0] != 0.0f || v->pm_f[1] != 0.0f || v->pm_f[2] != 0.0f || v->pm_pw[0] != 0.0f
                || v->pm_pw[1] != 0.0f || v->pm_fb != 0.0f || v->pm_cut != 0.0f;
        v->sync = P.p[TP_SYNC];
        v->noise = fm_clampf(knob(TP_NOISE) + md[MD_NOISE], 0.0f, 1.0f) * 0.8f;
        if (md[MD_RES] != 0.0f || md[MD_FDRV] != 0.0f) {   /* the matrix on resonance or feedback / drive */
            float rv = fm_clampf(res + md[MD_RES], 0.0f, 1.0f), fv = fm_clampf(fdrv + md[MD_FDRV], -1.0f, 1.0f);
            float kv = 4.25f * rv * (0.82f + 0.18f * rv), cv = 1.0f + 0.3f * kv;
            v->drive = (fv > 0.0f ? 1.0f + 7.0f * fv * fv : 1.0f) * cv;
            v->fbk = (fv < 0.0f ? -fv * 2.4f : 0.0f) + (P.p[TP_PM_FDBK] ? pmf * 2.0f * ef : 0.0f);
            v->k = kv;
            v->comp = cv;
        } else {
            v->drive = drive * comp;
            v->fbk = fbk + (P.p[TP_PM_FDBK] ? pmf * 2.0f * ef : 0.0f);
            v->k = k;
            v->comp = comp;
        }
        v->two_pole = P.p[TP_2POLE];
        /* the cutoff, in octaves above 15 Hz */
        cut_oct = cut0 + (fenv + 9.0f * md[MD_FENV_AMT]) * ef * (P.p[TP_FVEL] ? 0.25f + 0.75f * v->vel : 1.0f) + lfo_cut
                + (P.p[TP_PM_FILT] ? sq_sign(pmf) * 6.0f * ef : 0.0f)
                + 0.5f * (float)P.p[TP_FKEY] * (v->pitch - 60.0f) * (1.0f / 12.0f) + vint * 0.25f * v->vr[3]
                + md[MD_CUTOFF] * 8.0f;
        fc = 15.0f * fm_exp2f(fm_clampf(cut_oct, -1.0f, 10.6f));
        if (fc > 0.43f * TRI_SR)
            fc = 0.43f * TRI_SR;
        g = fm_tanf(FM_PI * fc * INV_SR);
        v->g1 = g;
        if (v->ea.stage == 1 && v->a0 == 0.0f)
            v->g0 = g;
        /* the VCA: the envelope by ENV AMT, the rest of the way a gate (0: an organ's gate) */
        base = v->gate_lvl + (ea - v->gate_lvl) * aamt;
        if (aamt > 0.99f)
            base = ea;
        if (P.p[TP_AVEL])
            base *= 0.15f + 0.85f * v->vel;
        amp = base * (1.0f - lfo_amp + lfo_amp * lfo_uni);
        if (md[MD_AMP] != 0.0f)
            amp *= fm_clampf(1.0f + md[MD_AMP], 0.0f, 2.0f);
        if (v->ea.stage == 0)
            amp = 0.0f;
        v->a1 = amp;
        {
            float p = fm_clampf(PAN[i] * spread * (u >= 1 && u <= 6 ? 1.0f : 0.8f) + md[MD_PAN], -1.0f, 1.0f);
            v->gl = 1.0f - (p > 0.0f ? p * 0.8f : 0.0f);
            v->gr = 1.0f - (p < 0.0f ? -p * 0.8f : 0.0f);
        }
    }
    if (low_note == 127)
        low_note = 48;
}

/* ------------------------------------------------------------- the voice --- */
static inline float wave(float ph, float pw, int m)
{
    float x = 0.0f;
    if (m & 1)
        x += 1.0f - 4.0f * fm_fabsf(ph - 0.5f);
    if (m & 2)
        x += 2.0f * ph - 1.0f;
    if (m & 8)
        x += 1.0f - 2.0f * ph;
    if (m & 4)
        x += (ph < pw ? 1.0f : -1.0f) + 1.0f - 2.0f * pw;
    return x;
}

/* one oscillator's sample, band-limited at its wrap and its pulse edge (polyBLEP) */
static inline __attribute__((always_inline)) float osc(float *php, float inc, float inv, float pw, int m, float hw)
{
    float ph = *php + inc, c = 0.0f;
    if (ph >= 1.0f) {
        ph -= 1.0f;
        if (ph >= 1.0f)
            ph -= fm_floorf(ph);
        c = step_after(hw, fm_minf(ph * inv, 1.0f));
    } else if (ph + inc >= 1.0f) {
        c = step_before(hw, fm_minf((ph + inc - 1.0f) * inv, 1.0f));
    }
    if (m & 4) {
        if (ph >= pw && ph - inc < pw)
            c += step_after(-2.0f, fm_minf((ph - pw) * inv, 1.0f));
        else if (ph < pw && ph + inc >= pw)
            c += step_before(-2.0f, fm_minf((ph + inc - pw) * inv, 1.0f));
    }
    *php = ph;
    return wave(ph, pw, m) + c;
}

static float ladder_G(float g) { return g / (1.0f + g); }

static void voice_render(voice_t *v, float *outl, float *outr, int ns)
{
    int i, pm = v->pm_on, sync = v->sync, two = v->two_pole, m1 = v->m[0], m2 = v->m[1], m3 = v->m[2];
    float k = v->k, drive = v->drive, fbk = v->fbk, noise = v->noise + 1e-4f;
    float lv1 = v->lv[0], lv2 = v->lv[1], lv3 = v->lv[2], gl = v->gl, gr = v->gr;
    float inc1b = v->inc[0], inc2b = v->inc[1], inc3 = v->inc[2], inv1b = v->inv[0], inv2b = v->inv[1], inv3 = v->inv[2];
    float pw1b = v->pw[0], pw2b = v->pw[1], pw3 = v->pw[2], hw1 = v->hw[0], hw2 = v->hw[1], hw3 = v->hw[2];
    float pmf1 = v->pm_f[0] * 0.5f, pmf2 = v->pm_f[1] * 0.5f, pmf3 = v->pm_f[2] * 0.01f;
    float pmpw1 = v->pm_pw[0] * 0.5f, pmpw2 = v->pm_pw[1] * 0.5f, pmfb = v->pm_fb * 0.5f, pmcut = v->pm_cut * 0.5f;
    float ph1 = v->ph[0], ph2 = v->ph[1], ph3 = v->ph[2];
    float s0 = v->s[0], s1 = v->s[1], s2 = v->s[2], s3 = v->s[3], fb_y = v->fb_y, fb_lp = v->fb_lp;
    uint32_t rng = v->rng;
    /* the ladder's coefficient and its feedback divisor at the block's ends, ramped between */
    float Ga = ladder_G(v->g0), Gb = ladder_G(v->g1), Da = 1.0f / (1.0f + k * Ga * Ga * Ga * Ga);
    float Db = 1.0f / (1.0f + k * Gb * Gb * Gb * Gb), dG = (Gb - Ga) * (1.0f / (float)CR), dD = (Db - Da) * (1.0f / (float)CR);
    float G = Ga, D = Da, g = v->g0, dg = (v->g1 - v->g0) * (1.0f / (float)CR);
    float A = v->a0, da = (v->a1 - v->a0) * (1.0f / (float)CR);
    for (i = 0; i < ns; i++) {
        float o1, o2, o3, x, u, G2, G3, G4, sig, y4, y, y2, inc1 = inc1b, inc2 = inc2b, inv1 = inv1b, inv2 = inv2b;
        float pw1 = pw1b, pw2 = pw2b, fb = fbk, r;
        G += dG;
        D += dD;
        A += da;
        /* OSC 3 first: it is the poly mod source */
        o3 = osc(&ph3, inc3, inv3, pw3, m3, hw3);
        if (pm) {
            if (pmf1 != 0.0f) {
                inc1 = fm_minf(inc1 * fm_exp2f(pmf1 * o3), 0.45f);
                inv1 = 1.0f / inc1;
            }
            if (pmf2 != 0.0f) {
                inc2 = fm_minf(inc2 * fm_exp2f(pmf2 * o3), 0.45f);
                inv2 = 1.0f / inc2;
            }
            if (pmf3 != 0.0f) {          /* on itself: a slow, chaotic pull of its own pitch */
                inc3 = fm_clampf(inc3 * (1.0f + pmf3 * o3), 1e-6f, 0.45f);
                inv3 = 1.0f / inc3;
            }
            pw1 = fm_clampf(pw1 + pmpw1 * o3, 0.0f, 0.98f);
            pw2 = fm_clampf(pw2 + pmpw2 * o3, 0.0f, 0.98f);
            fb += pmfb * o3;
            if (pmcut != 0.0f) {         /* the cutoff at audio rate */
                float gg;
                g += dg;
                gg = fm_minf(g * fm_exp2f(pmcut * o3), 6.0f);
                G = gg / (1.0f + gg);
                G4 = G * G;
                G4 *= G4;
                D = 1.0f / (1.0f + k * G4);
            }
        }
        {
            int wrapped = ph1 + inc1 >= 1.0f;
            o1 = osc(&ph1, inc1, inv1, pw1, m1, hw1);
            if (sync && wrapped) {       /* OSC 1 wrapped d samples ago: OSC 2 starts its cycle again there */
                float d = fm_minf(ph1 * inv1, 1.0f), pa = ph2 + inc2 * (1.0f - d), h;
                if (pa >= 1.0f)
                    pa -= 1.0f;
                ph2 = inc2 * d;
                h = wave(0.0f, pw2, m2) - wave(pa, pw2, m2);
                o2 = wave(ph2, pw2, m2) + step_after(h, d);
            } else {
                o2 = osc(&ph2, inc2, inv2, pw2, m2, hw2);
            }
        }
        r = rnd(&rng);
        x = lv1 * o1 + lv2 * o2 + lv3 * o3 + noise * r;
        fb_lp += (fb_y - fb_lp) * 0.012f;    /* the feedback path's DC block, about 85 Hz */
        x = (x + fb * (fb_y - fb_lp)) * drive;
        /* the ladder: four one-pole stages, zero-delay feedback, the input stage saturating */
        G2 = G * G;
        G3 = G2 * G;
        G4 = G2 * G2;
        sig = (1.0f - G) * (G3 * s0 + G2 * s1 + G * s2 + s3);
        y4 = (G4 * x + sig) * D;
        u = sat(x - k * y4);
        y = G * (u - s0) + s0; s0 = 2.0f * y - s0; u = y;
        y = G * (u - s1) + s1; s1 = 2.0f * y - s1; u = y; y2 = y;
        y = G * (u - s2) + s2; s2 = 2.0f * y - s2; u = y;
        y = G * (u - s3) + s3; s3 = 2.0f * y - s3;
        if (two)
            y = y2 * 0.8f;
        fb_y = y;
        y *= A;
        outl[i] += y * gl;
        outr[i] += y * gr;
    }
    v->ph[0] = ph1;
    v->ph[1] = ph2;
    v->ph[2] = ph3;
    v->inc[2] = inc3;
    v->rng = rng;
    v->s[0] = fm_flush(s0);
    v->s[1] = fm_flush(s1);
    v->s[2] = fm_flush(s2);
    v->s[3] = fm_flush(s3);
    v->fb_y = fm_flush(fb_y);
    v->fb_lp = fm_flush(fb_lp);
}

/* ------------------------------------------------------------ distortion --- */
static float dist_lp[2];
static void dist_render(float *l, float *r, int ns)
{
    int i, c;
    float d = knob(TP_DIST), drive, make, lpc;
    if (d <= 0.0f)
        return;
    drive = 1.0f + 24.0f * d * d;
    make = 1.0f / (1.0f + 1.6f * d);
    lpc = 1.0f - fm_expf(-FM_TWO_PI * (12000.0f - 6000.0f * d) * INV_SR);
    for (c = 0; c < 2; c++) {
        float *x = c ? r : l, lp = dist_lp[c];
        for (i = 0; i < ns; i++) {
            float a = x[i] * drive + 0.15f * d;   /* a little bias: even harmonics, as a real stage has */
            float y = sat(a) - sat(0.15f * d);
            lp += (y - lp) * lpc;
            x[i] = x[i] + (lp * make - x[i]) * fm_minf(1.0f, d * 4.0f);
        }
        dist_lp[c] = lp;
    }
}

/* --------------------------------------------------------------- effects --- */
#define FX_LEN 32768u                    /* each effect's line: 0.74 s at 44.1 kHz, 16 bits */
#define FX_MASK (FX_LEN - 1u)
typedef struct {
    int type;
    int16_t *buf;
    uint32_t w;
    float lfo, lfo2;
    float d_now;                         /* delay time now, samples (smoothed: the BBD's pitch bend) */
    float lp[4];
    float ap[2][8], apf[2];              /* phaser stages and feedback, L R */
    float car;                           /* ring: the carrier's phase */
    /* reverb */
    float rlp[4], dif_lp;
    uint32_t rlen[4], roff[4], pre_off, pre_len;
    uint32_t aoff[6], alen[6];
    int nap;
    float mix_now;
} fx_t;
static PLAT_POOL int16_t fx_mem[2][FX_LEN];
static fx_t FXS[2];

static inline int16_t to16(float x)
{
    x *= 8192.0f;                        /* +-4 before it clips */
    return (int16_t)(x > 32767.0f ? 32767.0f : x < -32768.0f ? -32768.0f : x);
}
static inline float from16(int16_t s) { return (float)s * (1.0f / 8192.0f); }
static inline float rd(fx_t *f, float dly)   /* a delayed sample, interpolated */
{
    uint32_t di = (uint32_t)dly;
    float fr = dly - (float)di, a = from16(f->buf[(f->w - di) & FX_MASK]), b = from16(f->buf[(f->w - di - 1u) & FX_MASK]);
    return a + (b - a) * fr;
}

static void fx_reset(fx_t *f, int type)
{
    static const uint16_t LEN[4][4] = {
        {2473, 2767, 3217, 3557},        /* hall */
        {709, 863, 1013, 1151},          /* room */
        {1123, 1327, 1559, 1783},        /* plate */
        {1931, 2297, 2621, 2903},        /* spring */
    };
    static const uint16_t AP[6] = {142, 107, 379, 277, 53, 31};
    uint32_t i, off;
    for (i = 0; i < FX_LEN; i++)
        f->buf[i] = 0;
    f->type = type;
    f->w = 0;
    f->d_now = 0.0f;
    for (i = 0; i < 4; i++)
        f->lp[i] = f->rlp[i] = 0.0f;
    for (i = 0; i < 8; i++)
        f->ap[0][i] = f->ap[1][i] = 0.0f;
    f->apf[0] = f->apf[1] = 0.0f;
    f->dif_lp = 0.0f;
    if (type >= FX_HALL) {               /* the reverb's lines, laid out in the effect's memory */
        int r = type - FX_HALL;
        off = 0;
        f->pre_off = off;
        f->pre_len = 2048;
        off += 2048;
        f->nap = r == 2 ? 6 : r == 3 ? 6 : 4;
        for (i = 0; i < 6; i++) {
            f->aoff[i] = off;
            f->alen[i] = r == 3 ? (uint32_t)(AP[i] / 4u + 7u * i + 5u) : AP[i];
            off += 512;
        }
        for (i = 0; i < 4; i++) {
            f->roff[i] = off;
            f->rlen[i] = LEN[r][i];
            off += 4096;
        }
    }
}

static void fx_run(fx_t *f, float *l, float *r, int ns, int type, float mix, int p1, int p2, int sync)
{
    int i;
    float a = (float)p1 * (1.0f / 127.0f), b = (float)p2 * (1.0f / 127.0f);
    if (type != f->type)
        fx_reset(f, type);
    if (type == FX_OFF) {
        f->mix_now = 0.0f;
        return;
    }
    switch (type) {
    case FX_BBD:
    case FX_DDL: {
        float target, fbk = b * 1.0f, lpc, wob = 0.0f;
        if (sync) {                      /* 1, 2d, 2, 4t, 4d, 4, 8d, 8, 8t, 16d, 16 */
            static const float BEATS[11] = {4.0f, 3.0f, 2.0f, 0.6667f, 1.5f, 1.0f, 0.75f, 0.5f, 0.3333f, 0.375f, 0.25f};
            float s = BEATS[p1 * 11 / 128] * 60.0f / bpm_now();
            while (s > 0.74f)
                s *= 0.5f;
            target = s * TRI_SR;
        } else {
            target = 0.005f * fm_exp2f(a * 7.2f) * TRI_SR;   /* 5 ms .. 0.74 s */
        }
        if (target > (float)FX_LEN - 4.0f)
            target = (float)FX_LEN - 4.0f;
        if (f->d_now <= 0.0f)
            f->d_now = target;
        lpc = type == FX_BBD ? 1.0f - fm_expf(-FM_TWO_PI * (2200.0f + 2500.0f * (1.0f - a)) * INV_SR) : 1.0f;
        if (fbk > 0.98f)
            fbk = 0.98f;
        for (i = 0; i < ns; i++) {
            float in = 0.5f * (l[i] + r[i]), wl, wr, x;
            f->d_now += (target - f->d_now) * (type == FX_BBD ? 0.0002f : 0.002f);
            if (type == FX_BBD) {
                f->lfo += 0.35f * INV_SR;
                if (f->lfo >= 1.0f)
                    f->lfo -= 1.0f;
                wob = (f->lfo < 0.5f ? 4.0f * f->lfo - 1.0f : 3.0f - 4.0f * f->lfo) * 6.0f;
            }
            wl = rd(f, f->d_now + wob);
            wr = rd(f, f->d_now * 0.985f + wob);
            if (type == FX_BBD) {
                f->lp[0] += (wl - f->lp[0]) * lpc;
                f->lp[1] += (f->lp[0] - f->lp[1]) * lpc;
                f->lp[2] += (wr - f->lp[2]) * lpc;
                f->lp[3] += (f->lp[2] - f->lp[3]) * lpc;
                wl = f->lp[1];
                wr = f->lp[3];
                x = sat(in + fbk * wl);
            } else {
                x = in + fbk * wl;
            }
            f->buf[f->w & FX_MASK] = to16(x);
            f->w++;
            l[i] += (wl - l[i]) * mix;
            r[i] += (wr - r[i]) * mix;
        }
        return;
    }
    case FX_CHO:
    case FX_FL1:
    case FX_FL2: {
        int fl = type != FX_CHO;
        float rate = 0.05f * fm_exp2f(a * 7.0f), centre = fl ? 0.0003f : 0.007f, depth = fl ? 0.006f * b : 0.0035f * b;
        float fbk = type == FX_FL1 ? 0.82f : type == FX_FL2 ? 0.45f : 0.0f;
        for (i = 0; i < ns; i++) {
            float in = 0.5f * (l[i] + r[i]), t1, t2, wl, wr;
            f->lfo += rate * INV_SR;
            if (f->lfo >= 1.0f)
                f->lfo -= 1.0f;
            t1 = f->lfo < 0.5f ? 4.0f * f->lfo - 1.0f : 3.0f - 4.0f * f->lfo;
            {
                float q = f->lfo + 0.25f;
                if (q >= 1.0f)
                    q -= 1.0f;
                t2 = q < 0.5f ? 4.0f * q - 1.0f : 3.0f - 4.0f * q;
            }
            if (fl) {
                wl = rd(f, (centre + depth * (0.5f + 0.5f * t1)) * TRI_SR + 1.0f);
                wr = rd(f, (centre + depth * (0.5f + 0.5f * t2)) * TRI_SR + 1.0f);
            } else {
                wl = rd(f, (centre + depth * t1) * TRI_SR);
                wr = rd(f, (centre - depth * t1) * TRI_SR);
            }
            f->buf[f->w & FX_MASK] = to16(in + fbk * 0.5f * (wl + wr));
            f->w++;
            if (fl) {                    /* a flanger's wet is the comb: dry plus the swept line */
                wl = 0.5f * (l[i] + wl);
                wr = 0.5f * (r[i] + wr);
            }
            l[i] += (wl - l[i]) * mix;
            r[i] += (wr - r[i]) * mix;
        }
        return;
    }
    case FX_PH1:
    case FX_PH2:
    case FX_PH3: {
        int st = type == FX_PH3 ? 4 : 6, c, j;
        float rate = 0.03f * fm_exp2f(a * 8.0f), fbk = type == FX_PH1 ? 0.72f : type == FX_PH2 ? 0.3f : 0.5f;
        float co[2];
        f->lfo += rate * CR_DT;
        if (f->lfo >= 1.0f)
            f->lfo -= 1.0f;
        for (c = 0; c < 2; c++) {        /* the sweep, once a block: the second channel a quarter cycle on in PH3 */
            float q = f->lfo + (type == FX_PH3 && c ? 0.25f : 0.0f), t, fc, tn;
            if (q >= 1.0f)
                q -= 1.0f;
            t = q < 0.5f ? 2.0f * q : 2.0f - 2.0f * q;
            fc = 160.0f * fm_exp2f((0.4f + 4.6f * b) * t);
            tn = fm_tanf(FM_PI * fm_minf(fc, 16000.0f) * INV_SR);
            co[c] = (tn - 1.0f) / (tn + 1.0f);
        }
        for (i = 0; i < ns; i++)
            for (c = 0; c < 2; c++) {
                float *x = c ? &r[i] : &l[i], y = *x + fbk * f->apf[c];
                for (j = 0; j < st; j++) {   /* first-order all-passes */
                    float z = co[c] * y + f->ap[c][j];
                    f->ap[c][j] = y - co[c] * z;
                    y = z;
                }
                f->apf[c] = fm_flush(y);
                *x += (0.5f * (*x + y) - *x) * fm_minf(mix * 1.6f, 1.0f);   /* the notches: dry and all-passed */
            }
        for (c = 0; c < 2; c++)
            for (j = 0; j < st; j++)
                f->ap[c][j] = fm_flush(f->ap[c][j]);
        return;
    }
    case FX_RING: {
        float hz = p2 >= 64 ? 440.0f * fm_exp2f(((float)low_note - 69.0f) * (1.0f / 12.0f)) * fm_exp2f((a - 0.5f) * 4.0f)
                            : 10.0f * fm_exp2f(a * 9.0f);
        float inc = hz * INV_SR;
        for (i = 0; i < ns; i++) {
            float c;
            f->car += inc;
            if (f->car >= 1.0f)
                f->car -= 1.0f;
            c = fm_sin_turns(f->car);
            l[i] += (l[i] * c - l[i]) * mix;
            r[i] += (r[i] * c - r[i]) * mix;
        }
        return;
    }
    default: {                           /* the reverbs: four lines, a Hadamard mix, damped; diffused input */
        int rv = type - FX_HALL, j;
        float rt = rv == 3 ? 0.4f + 4.0f * a * a : 0.3f * fm_exp2f(a * 5.3f);
        float damp = rv == 0 ? 5500.0f : rv == 1 ? 7000.0f : rv == 2 ? 10000.0f : 2500.0f + 6000.0f * b;
        float dc = 1.0f - fm_expf(-FM_TWO_PI * damp * INV_SR), gfb[4], er = rv == 3 ? 0.0f : b;
        float apg = rv == 3 ? 0.65f : rv == 2 ? 0.7f : 0.6f, pre = rv == 0 ? 900.0f : rv == 1 ? 300.0f : 100.0f;
        for (j = 0; j < 4; j++)
            gfb[j] = fm_exp2f(-3.0f * 3.3219f * (float)f->rlen[j] / (rt * TRI_SR));   /* 60 dB in rt */
        for (i = 0; i < ns; i++) {
            float in = 0.5f * (l[i] + r[i]), x, y[4], h[4], wl, wr;
            uint32_t w = f->w;
            /* pre-delay, and the early reflections taken from it */
            f->buf[(f->pre_off + (w & 2047u))] = to16(in);
            x = from16(f->buf[f->pre_off + ((w - (uint32_t)pre) & 2047u)]);
            wl = er * 0.6f * (from16(f->buf[f->pre_off + ((w - 397u) & 2047u)]) + 0.7f * from16(f->buf[f->pre_off + ((w - 1171u) & 2047u)]));
            wr = er * 0.6f * (from16(f->buf[f->pre_off + ((w - 613u) & 2047u)]) + 0.7f * from16(f->buf[f->pre_off + ((w - 1489u) & 2047u)]));
            for (j = 0; j < f->nap; j++) {   /* the diffusers */
                int16_t *d = f->buf + f->aoff[j];
                uint32_t p = (w) & 511u, q = (w - f->alen[j]) & 511u;
                float z = from16(d[q]), v = x + apg * z;
                d[p] = to16(v);
                x = z - apg * v;
            }
            for (j = 0; j < 4; j++) {
                uint32_t len = f->rlen[j];
                y[j] = from16(f->buf[f->roff[j] + ((w - len) & 4095u)]);
                f->rlp[j] += (y[j] - f->rlp[j]) * dc;
                y[j] = f->rlp[j] * gfb[j];
            }
            h[0] = 0.5f * (y[0] + y[1] + y[2] + y[3]);
            h[1] = 0.5f * (y[0] - y[1] + y[2] - y[3]);
            h[2] = 0.5f * (y[0] + y[1] - y[2] - y[3]);
            h[3] = 0.5f * (y[0] - y[1] - y[2] + y[3]);
            for (j = 0; j < 4; j++)
                f->buf[f->roff[j] + (w & 4095u)] = to16(h[j] + (j & 1 ? -x : x) * 0.5f);
            f->w = w + 1u;
            wl += y[0] + 0.6f * y[2];
            wr += y[1] + 0.6f * y[3];
            l[i] += (wl - l[i]) * mix;
            r[i] += (wr - r[i]) * mix;
        }
        for (j = 0; j < 4; j++)
            f->rlp[j] = fm_flush(f->rlp[j]);
        return;
    }
    }
}

/* --------------------------------------------------------------- render --- */
static float bufl[2][CR], bufr[2][CR];
static int sub_ns, cores = 1;

static void voices_part(int from, int to, int b)
{
    int i;
    for (i = 0; i < sub_ns; i++)
        bufl[b][i] = bufr[b][i] = 0.0f;
    for (i = from; i < to; i++)
        if (V[i].ea.stage || V[i].a0 > 0.0f)
            voice_render(&V[i], bufl[b], bufr[b], sub_ns);
}
static void job2(uint32_t arg)           /* on the second core: voices 4..6 */
{
    (void)arg;
    voices_part(TRI_MAXV / 2, TRI_MAXV, 1);
}

static int fx_p1(int i, float m)         /* an effect's P1, moved by the mod matrix */
{
    int v = P.p[i] + (int)(m * 127.0f);
    return v < 0 ? 0 : v > 127 ? 127 : v;
}

void tri_render(float *out, uint32_t n)
{
    static float dc_l, dc_r, dcx_l, dcx_r;
    events();
    arp_state();
    if (clock_seen && sample_t - tick_last > (uint32_t)TRI_SR)
        clock_seen = 0;
    while (n) {
        int i, ns = n < CR ? (int)n : CR, two;
        float vol = P.p[TP_VOLUME] ? fm_exp2f(((float)P.p[TP_VOLUME] - 100.0f) * (0.45f / 6.02f)) : 0.0f;   /* 100: 0 dB, 0.45 dB a step */
        float fxon = P.p[TP_FX_ON];
        control();
        sub_ns = ns;
        two = !F.one_core && plat_cpu2_on() && plat_cpu2_run(job2, 0);
        voices_part(0, TRI_MAXV / 2, 0);
        if (!two || plat_cpu2_wait() != 0)
            job2(0);
        cores = two ? 2 : 1;
        for (i = 0; i < ns; i++) {       /* the sum, its DC taken out */
            float l = (bufl[0][i] + bufl[1][i]) * 0.6f, r = (bufr[0][i] + bufr[1][i]) * 0.6f;
            dc_l = l - dcx_l + 0.9995f * dc_l;
            dcx_l = l;
            dc_r = r - dcx_r + 0.9995f * dc_r;
            dcx_r = r;
            bufl[0][i] = dc_l;
            bufr[0][i] = dc_r;
        }
        dist_render(bufl[0], bufr[0], ns);
        {
            int ta = fxon ? P.p[TP_FXA_TYPE] : FX_OFF, tb = fxon ? P.p[TP_FXB_TYPE] : FX_OFF;
            if (ta >= FX_HALL)
                ta = FX_OFF;
            fx_run(&FXS[0], bufl[0], bufr[0], ns, ta, fm_clampf(knob(TP_FXA_MIX) + mm_fx_mix[0], 0.0f, 1.0f),
                   fx_p1(TP_FXA_P1, mm_fx_p1[0]), P.p[TP_FXA_P2], P.p[TP_FXA_SYNC]);
            fx_run(&FXS[1], bufl[0], bufr[0], ns, tb, fm_clampf(knob(TP_FXB_MIX) + mm_fx_mix[1], 0.0f, 1.0f),
                   fx_p1(TP_FXB_P1, mm_fx_p1[1]), P.p[TP_FXB_P2], P.p[TP_FXB_SYNC]);
        }
        for (i = 0; i < ns; i++) {
            float l = bufl[0][i] * vol, r = bufr[0][i] * vol;
            if (fm_fabsf(l) > 0.7f) {
                float a = fm_fabsf(l) - 0.7f, y = 0.7f + 0.3f * fm_tanhf(a * (1.0f / 0.3f));
                l = l > 0.0f ? y : -y;
            }
            if (fm_fabsf(r) > 0.7f) {
                float a = fm_fabsf(r) - 0.7f, y = 0.7f + 0.3f * fm_tanhf(a * (1.0f / 0.3f));
                r = r > 0.0f ? y : -y;
            }
            out[2 * i] = l;
            out[2 * i + 1] = r;
        }
        out += 2 * ns;
        n -= (uint32_t)ns;
        sample_t += (uint32_t)ns;
    }
}

/* ------------------------------------------------------------------- API --- */
void tri_init(void)
{
    int i, j;
    uint32_t seed = 0xC0FFEE11u;
    for (i = 0; i < TRI_MAXV; i++) {
        V[i].note = V[i].key = -1;
        V[i].rng = 0x9E3779B9u * (uint32_t)(i + 1);
        for (j = 0; j < 3; j++)
            V[i].ph[j] = 0.5f + 0.5f * rnd(&seed);   /* free-running: never in step */
        for (j = 0; j < 6; j++)
            V[i].vr[j] = rnd(&seed);
        V[i].drift_t = (uint32_t)i * 300u;
        V[i].pitch = V[i].pitch_target = 60.0f;
    }
    FXS[0].buf = fx_mem[0];
    FXS[1].buf = fx_mem[1];
    FXS[0].type = FXS[1].type = -1;
}

static void patch_clamp(void)
{
    int i;
    for (i = 0; i < TP_N; i++) {
        if (P.p[i] > TRI_MAX[i])
            P.p[i] = TRI_MAX[i];
        if (P.p[i] < TRI_MIN[i])
            P.p[i] = TRI_MIN[i];
    }
    if (P.pitch2 < -700 || P.pitch2 > 700)
        P.pitch2 = 0;
    if (P.pitch3 < -700 || P.pitch3 > 700)
        P.pitch3 = 0;
    if (P.chord_n > 6)
        P.chord_n = 6;
    for (i = 0; i < TRI_MM_SLOTS; i++) {
        if (P.mm[i].src >= MS_N || P.mm[i].dst >= MD_N)
            P.mm[i].src = P.mm[i].dst = 0;
        if (P.mm[i].src == 0 && P.mm[i].dst == 0 && P.mm[i].amt == 0)
            P.mm[i].amt = 64;            /* a patch from before the matrix: its spare bytes were zero */
    }
}

void tri_set_patch(const tri_patch_t *p)
{
    int uni_was = P.p[TP_UNISON] != 0;
    P = *p;
    patch_clamp();
    if ((P.p[TP_UNISON] != 0) != uni_was)
        ev_push(EV_ALLOFF, 0, 0);
}
void tri_set_param(int i, int v)
{
    if (i >= 0 && i < TP_N) {
        int was = P.p[i];
        P.p[i] = (uint8_t)(v < TRI_MIN[i] ? TRI_MIN[i] : v > TRI_MAX[i] ? TRI_MAX[i] : v);
        if (i == TP_UNISON && (was != 0) != (P.p[i] != 0))
            ev_push(EV_ALLOFF, 0, 0);
    }
}
int tri_param(int i) { return i >= 0 && i < TP_N ? P.p[i] : 0; }
tri_patch_t *tri_patch(void) { return &P; }
void tri_set_perf(const tri_perf_t *f) { F = *f; }

void tri_note_on(int note, int vel)
{
    if (vel <= 0)
        tri_note_off(note);
    else
        ev_push(EV_ON, (uint32_t)note, (uint32_t)vel);
}
void tri_note_off(int note) { ev_push(EV_OFF, (uint32_t)note, 0); }
void tri_all_off(void) { ev_push(EV_ALLOFF, 0, 0); }
void tri_panic(void) { ev_push(EV_PANIC, 0, 0); }
void tri_bend(int v) { ev_push(EV_BEND, (uint32_t)(v + 8192), 0); }
void tri_mod(int v) { ev_push(EV_MOD, 0, (uint32_t)v); }
void tri_pressure(int v) { ev_push(EV_PRESS, 0, (uint32_t)v); }
void tri_morph(int v) { ev_push(EV_MORPH, 0, (uint32_t)v); }
int tri_uses(int src)
{
    int s;
    for (s = 0; s < TRI_MM_SLOTS; s++)
        if (P.mm[s].src == src && P.mm[s].dst != MD_OFF && P.mm[s].amt != 64)
            return 1;
    return 0;
}
void tri_set_route(int slot, int src, int dst, int amt)
{
    if (slot < 0 || slot >= TRI_MM_SLOTS)
        return;
    P.mm[slot].src = (uint16_t)(src < 0 ? 0 : src >= MS_N ? MS_N - 1 : src) & 15u;
    P.mm[slot].dst = (uint16_t)(dst < 0 ? 0 : dst >= MD_N ? MD_N - 1 : dst) & 31u;
    P.mm[slot].amt = (uint16_t)(amt < 0 ? 0 : amt > 127 ? 127 : amt) & 127u;
}
void tri_sustain(int on) { ev_push(EV_SUS, 0, (uint32_t)(on != 0)); }
void tri_chord_learn(void) { ev_push(EV_LEARN, 0, 0); }
void tri_clock(int rt) { ev_push(EV_RT, 0, (uint32_t)rt); }

float tri_voice_level(int v) { return v >= 0 && v < TRI_MAXV && V[v].ea.stage ? V[v].ea.level : 0.0f; }
int tri_voice_note(int v) { return v >= 0 && v < TRI_MAXV && V[v].ea.stage ? V[v].note : -1; }
int tri_learning(void) { return learning; }
int tri_arp_step_lit(void)
{
    int l = arp_lit;
    arp_lit = 0;
    return l;
}
int tri_clock_running(void) { return clock_seen; }
int tri_cores(void) { return cores; }
float tri_lfo_value(void) { return lfo_val; }
