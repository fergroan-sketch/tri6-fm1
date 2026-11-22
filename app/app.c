/* SPDX-License-Identifier: MPL-2.0 */
/* TRI-6 FM-1: the app around the engine (tri6.c). The FM-1's controls as a Trigon-6 style panel, in pages:
 *
 *   HOME  the patch: PRESET +-1, ALGO +-10 (on the MOD MATRIX page: the slot); SELECT steps through every page;
 *         K1..K4 CUTOFF, RES, ENV AMT, FDBK/DRIVE
 *   EDIT  OSC 1 > OSC 2 > OSC 3 > OSC MORE (PW 2, SYNC, PW 3, KEY 3)
 *   SEL   FILTER (CUTOFF, RES, ENV AMT, KEY) > FILTER 2 (POLES, VEL, NOISE, FDBK/DRIVE)
 *   ENV   FILTER ENV > AMP ENV > AMP (ENV AMT, VEL)
 *   LFO   LFO (FREQ, SHAPE, AMOUNT, SYNC) > LFO DEST (PITCH, PW, FILTER, AMP)
 *   SEQ   POLY MOD (OSC 3, FIL ENV, PITCH, FILTER) > POLY MOD 2 (PW, FDBK) > MOD MATRIX (SLOT, SOURCE, DEST, AMOUNT)
 *   FX    FX A > FX B > FX MORE (FX on, syncs) > CHARACTER (DISTORT, VINTAGE, PAN, VOLUME)
 *   GLO   VOICE (UNISON, KEY MODE, GLIDE, RATE) > GLIDE / BEND > SETUP (clock, MIDI, cores)
 *   ARP   the arpeggiator off > on > hold, and its page (MODE, OCT, DIVIDE, BPM)
 *   PLAY  chord memory: the next chord played is kept, and UNISON becomes CHORD
 *   SAVE  the patch into its slot (lit when changed); OCT- / OCT+ the octave; REC held: Bluetooth off / on
 *
 * Part of the unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in
 * scope. The engine is its own unit (fm1.toml o2). */
#include <stddef.h>
#include "fastmath.h"
#include "tri6.h"

/* --------------------------------------------------------------- the patches --- */
#define NPATCH 128
#define BLK_N 32
#define NBLK (NPATCH / BLK_N)
#define BANK_MAGIC 0x54524232u           /* "TRB2": eight two-byte routes */
#define BANK_MAGIC_02 0x54524231u        /* "TRB1": 0.1 and 0.2, four three-byte routes (0.1's all zero) */
typedef struct {
    uint32_t magic;
    tri_patch_t p[BLK_N];
} block_t;
static PLAT_POOL block_t blocks[NBLK];   /* 14 KB: in the pool RAM */
static uint8_t blk_dirty[NBLK];
_Static_assert(sizeof(tri_patch_t) == 112, "a patch is 112 bytes");
_Static_assert(offsetof(tri_patch_t, mm) == 96, "the routes start at byte 96, where 0.2 kept its own");
_Static_assert(sizeof(block_t) <= PLAT_STORE_MAX, "a block fits a store object");

/* the factory patches: designed for this firmware (none copied from any instrument), from tools/gen_factory.py */
typedef struct {
    const char *name;
    uint8_t p[TP_N];
    int16_t pitch2, pitch3;
    uint8_t chord_n;
    int8_t chord[6];
    uint8_t mm[TRI_MM_SLOTS][3];         /* source, destination, amount (64 none) */
} factory_t;
static const factory_t FACTORY[] = {
#include "factory.h"
};
#define NFACTORY (int)(sizeof FACTORY / sizeof FACTORY[0])

static void mm_fix(tri_patch_t *d)       /* a patch from before the mod matrix: its spare bytes were zero */
{
    int s;
    for (s = 0; s < TRI_MM_SLOTS; s++) {
        tri_route_t *r = &d->mm[s];
        if (r->src >= MS_N || r->dst >= MD_N)
            r->src = r->dst = 0, r->amt = 64;
        if (r->src == 0 && r->dst == 0 && r->amt == 0)
            r->amt = 64;
    }
}

static void mm_from_02(tri_patch_t *d)   /* a patch saved by 0.2: four routes of three bytes, from byte 96 */
{
    const uint8_t *b = (const uint8_t *)d + offsetof(tri_patch_t, mm);
    uint8_t old[4][3];
    int s, j;
    for (s = 0; s < 4; s++)
        for (j = 0; j < 3; j++)
            old[s][j] = b[3 * s + j];
    for (s = 0; s < TRI_MM_SLOTS; s++) {
        tri_route_t *r = &d->mm[s];
        int ok = s < 4 && old[s][0] < MS_N && old[s][1] < MD_N && old[s][2] < 128u;
        r->src = ok ? old[s][0] : 0;
        r->dst = ok ? old[s][1] : 0;
        r->amt = ok ? old[s][2] : 64;
    }
    mm_fix(d);
}

static void patch_init(tri_patch_t *d, const factory_t *f)
{
    uint32_t i;
    uint8_t *b = (uint8_t *)d;
    for (i = 0; i < sizeof *d; i++)
        b[i] = 0;
    for (i = 0; i < TP_N; i++)
        d->p[i] = f->p[i];
    for (i = 0; i < TRI_NAME_LEN - 1u && f->name[i]; i++)
        d->name[i] = f->name[i];
    d->pitch2 = f->pitch2;
    d->pitch3 = f->pitch3;
    d->chord_n = f->chord_n;
    for (i = 0; i < 6; i++)
        d->chord[i] = f->chord[i];
    for (i = 0; i < TRI_MM_SLOTS; i++) {
        d->mm[i].src = f->mm[i][0] & 15u;
        d->mm[i].dst = f->mm[i][1] & 31u;
        d->mm[i].amt = f->mm[i][2] & 127u;
    }
    mm_fix(d);
}

/* a bank saved by an older version: its empty INIT slots get this version's factory patches */
static void banks_fill(void)
{
    tri_patch_t init;
    const uint8_t *ib = (const uint8_t *)&init;
    int b, i;
    patch_init(&init, &FACTORY[0]);
    mm_fix(&init);
    for (b = 0; b < NBLK; b++)
        for (i = 0; i < BLK_N; i++) {
            const uint8_t *pb = (const uint8_t *)&blocks[b].p[i];
            int g = b * BLK_N + i, same = 1;
            uint32_t k;
            for (k = 0; k < sizeof init && same; k++)
                same = pb[k] == ib[k];
            if (g > 0 && g < NFACTORY && same) {
                patch_init(&blocks[b].p[i], &FACTORY[g]);
                blk_dirty[b] = 1;
            }
        }
}

static void banks_load(void)
{
    int b;
    for (b = 0; b < NBLK; b++)
        if (plat_store_load(OBJ_BANK0 + (uint32_t)b, &blocks[b], sizeof blocks[b]) == (int)sizeof blocks[b]
            && blocks[b].magic == BANK_MAGIC_02) {   /* saved by 0.1 or 0.2: the same patches, routes converted */
            int i;
            for (i = 0; i < BLK_N; i++)
                mm_from_02(&blocks[b].p[i]);
            blocks[b].magic = BANK_MAGIC;
            blk_dirty[b] = 1;            /* written in the new form at the next SAVE */
        } else if (blocks[b].magic != BANK_MAGIC) {
            int i;
            for (i = 0; i < BLK_N; i++)
                patch_init(&blocks[b].p[i], &FACTORY[b * BLK_N + i < NFACTORY ? b * BLK_N + i : 0]);
            blocks[b].magic = BANK_MAGIC;
            continue;
        } else {
            int i;
            for (i = 0; i < BLK_N; i++)
                mm_fix(&blocks[b].p[i]);
        }
    banks_fill();
}

static tri_patch_t *slot(int i) { return &blocks[(i / BLK_N) % NBLK].p[i % BLK_N]; }

/* -------------------------------------------------------------- settings --- */
#define SET_MAGIC 0x54525331u            /* "TRS1" */
typedef struct {
    uint32_t magic;
    int8_t octave;
    uint8_t patch;
    uint8_t ble_off;
    uint8_t midi_ch;                     /* 0 omni, 1..16 */
    uint8_t local_off;
    uint8_t rsv[3];
    tri_perf_t perf;
} settings_t;
static settings_t set;
static uint8_t set_dirty;

static tri_patch_t edit;                 /* the patch being played and edited */
static uint8_t edit_dirty;

static void settings_defaults(void)
{
    uint32_t i;
    uint8_t *b = (uint8_t *)&set;
    for (i = 0; i < sizeof set; i++)
        b[i] = 0;
    set.magic = SET_MAGIC;
    set.perf.voices = 6;
    set.perf.clock_src = 2;
    set.perf.vel_curve = 1;
}

static void settings_load(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC
        || set.octave < -2 || set.octave > 2 || set.patch >= NPATCH || set.ble_off > 1u || set.midi_ch > 16u
        || set.perf.voices < 1 || set.perf.voices > TRI_MAXV || set.perf.clock_src > 2 || set.perf.vel_curve > 2)
        settings_defaults();
}

/* MIDI: the Trigon-6's controller numbers (its MIDI implementation) for the parameters it has a CC for; the
 * waveform switches come and go as bits of WAVE */
static const uint8_t PARAM_CC[TP_N] = {
    [TP_O1_OCT] = 67, [TP_O1_LEVEL] = 69, [TP_O1_PW] = 73, [TP_SYNC] = 68,
    [TP_O2_OCT] = 75, [TP_O2_LEVEL] = 77, [TP_O2_PW] = 81,
    [TP_O3_OCT] = 82, [TP_O3_LEVEL] = 84, [TP_O3_PW] = 88, [TP_O3_KEY] = 89,
    [TP_CUTOFF] = 102, [TP_RES] = 103, [TP_FKEY] = 104, [TP_FVEL] = 105, [TP_FDRV] = 106, [TP_2POLE] = 107,
    [TP_FENV_AMT] = 47, [TP_FA] = 50, [TP_FD] = 51, [TP_FS] = 52, [TP_FR] = 53,
    [TP_AENV_AMT] = 40, [TP_AVEL] = 41, [TP_AA] = 43, [TP_AD] = 44, [TP_AS] = 45, [TP_AR] = 46,
    [TP_DIST] = 9, [TP_VOLUME] = 12, [TP_GLIDE_RATE] = 5, [TP_GLIDE_ON] = 65,
    [TP_ARP_ON] = 58, [TP_ARP_MODE] = 59, [TP_ARP_OCT] = 60, [TP_ARP_DIV] = 62, [TP_BPM] = 3,
};
/* CC wave switches: osc, bit */
static const uint8_t WAVE_CC[][3] = {
    {70, 0, 2}, {71, 0, 1}, {72, 0, 4}, {78, 1, 2}, {79, 1, 1}, {80, 1, 4}, {85, 2, 2}, {86, 2, 1}, {87, 2, 4},
};
static const uint8_t WAVE_PARAM[3] = {TP_O1_WAVE, TP_O2_WAVE, TP_O3_WAVE};
static uint32_t midi_out_ch(void) { return set.midi_ch ? set.midi_ch - 1u : 0u; }
static uint8_t morph_now;                /* the MORPH knob (ALGO, or CC 16), 0..127 */
static void morph_set(int v, int send)
{
    morph_now = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
    tri_morph(morph_now);
    if (send)
        plat_midi_send(0x0Bu | (0xB0u | midi_out_ch()) << 8 | 16u << 16 | (uint32_t)morph_now << 24, TO_USB | TO_BLE);
}

/* a CC's 0..127 as a parameter's range, and back */
static int cc_to_param(int i, uint32_t v)
{
    int lo = TRI_MIN[i], hi = TRI_MAX[i];
    if (hi - lo == 1)
        return v >= 64u ? hi : lo;
    if (i == TP_BPM)
        return 30 + (int)((v * 220u + 63u) / 127u);
    if (hi == 127)
        return (int)v;
    return lo + (int)((v * (uint32_t)(hi - lo + 1)) / 128u);
}
static uint32_t param_to_cc(int i, int v)
{
    int lo = TRI_MIN[i], hi = TRI_MAX[i];
    if (hi == 127)
        return (uint32_t)v;
    if (i == TP_BPM)
        return (uint32_t)((v - 30) * 127 / 220);
    return (uint32_t)((v - lo) * 127 / (hi - lo));
}

static void patch_select(int i)
{
    set.patch = (uint8_t)((i % NPATCH + NPATCH) % NPATCH);
    edit = *slot(set.patch);
    edit_dirty = 0;
    set_dirty = 1;
    tri_set_patch(&edit);
}

int tri_ble_off(void) { return set.ble_off; }

void app_init(void)
{
    tri_init();
    settings_load();
    banks_load();
    if (!set.perf.one_core)
        plat_cpu2_set(1);                /* the second core renders half the voices; if it can't, one core does all */
    tri_set_perf(&set.perf);
    edit = *slot(set.patch);
    tri_set_patch(&edit);
}

/* ----------------------------------------------------------------- audio --- */
static volatile uint8_t ui_poke;
static volatile int pc_pending = -1;
static volatile int cc_bright;           /* CC 74: added to the cutoff */

static void param_in(int i, int v)       /* a parameter from MIDI */
{
    edit.p[i] = (uint8_t)v;
    tri_set_param(i, v);
    edit_dirty = 1;
    ui_poke = 1;
}

static void cc_in(uint32_t cc, uint32_t v)
{
    int i;
    for (i = 0; i < TP_N; i++)
        if (PARAM_CC[i] == cc) {
            param_in(i, cc_to_param(i, v));
            return;
        }
    for (i = 0; i < (int)(sizeof WAVE_CC / sizeof WAVE_CC[0]); i++)
        if (WAVE_CC[i][0] == cc) {
            int p = WAVE_PARAM[WAVE_CC[i][1]], m = edit.p[p], bit = WAVE_CC[i][2];
            m = v >= 64u ? m | bit : m & ~bit;
            if (m)                       /* a wave switched off, leaving none: kept (LEVEL silences an oscillator) */
                param_in(p, m);
            return;
        }
    switch (cc) {
    case 1: tri_mod((int)v); return;
    case 16: morph_set((int)v, 0); ui_poke = 1; return;   /* MORPH */
    case 64: tri_sustain(v >= 64u); return;
    case 74: cc_bright = (int)v; return;
    case 76: edit.pitch2 = tri_patch()->pitch2 = (int16_t)(((int)v - 64) * 700 / 63); edit_dirty = ui_poke = 1; return;
    case 83: edit.pitch3 = tri_patch()->pitch3 = (int16_t)(((int)v - 64) * 700 / 63); edit_dirty = ui_poke = 1; return;
    case 120: case 123: tri_all_off(); return;
    case 122: set.local_off = v < 64u; set_dirty = ui_poke = 1; return;
    default: return;
    }
}

static void midi_in(void)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, ch = st & 15u;
        uint32_t type = st & 0xF0u;
        if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu) {
            tri_clock((int)st);
            continue;
        }
        if (st >= 0xF0u)
            continue;
        if (set.midi_ch && ch + 1u != set.midi_ch)
            continue;
        if (type == 0x90u && d2)
            tri_note_on((int)d1, (int)d2);
        else if (type == 0x80u || type == 0x90u)
            tri_note_off((int)d1);
        else if (type == 0xE0u)
            tri_bend((int)(d1 | d2 << 7) - 8192);
        else if (type == 0xB0u)
            cc_in(d1, d2);
        else if (type == 0xC0u)
            pc_pending = (int)d1;
        else if (type == 0xD0u)          /* channel pressure: the mod matrix's PRESSURE */
            tri_pressure((int)d1);
        else if (type == 0xA0u)          /* poly pressure: taken as the channel's */
            tri_pressure((int)d2);
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float buf[2 * APP_BLOCK];
    static float gain_now;
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;
    uint32_t i;
    midi_in();
    if (n > APP_BLOCK)
        n = APP_BLOCK;
    {                                    /* CC 74, brightness: on top of the patch's cutoff */
        int c = edit.p[TP_CUTOFF] + (cc_bright > 0 ? cc_bright - 64 : 0);
        if (cc_bright > 0)
            tri_set_param(TP_CUTOFF, c < 0 ? 0 : c > 127 ? 127 : c);
    }
    tri_render(buf, n);
    for (i = 0; i < n; i++) {
        gain_now += (gain - gain_now) * 0.004f;
        lr[2 * i] = (int32_t)(buf[2 * i] * gain_now);
        lr[2 * i + 1] = (int32_t)(buf[2 * i + 1] * gain_now);
    }
}

void app_panic(void) { tri_panic(); }

/* ------------------------------------------------------------------ SysEx --- */
/* TRI-6 FM-1's own patch messages, under the non-commercial ID: F0 7D 54 36 cmd ... F7
 *   01 pp [224 nibbles]  a patch into slot pp       02 [224 nibbles]  a patch into the edit buffer
 *   03 pp                 ask for slot pp's patch (it answers with 01 pp ...) */
#define SX_LEN (5 + 1 + 2 * (int)sizeof(tri_patch_t) + 1)
static void send_patch(int pp)
{
    static uint8_t f[SX_LEN];
    const uint8_t *b = (const uint8_t *)slot(pp);
    int i;
    f[0] = 0xF0; f[1] = 0x7D; f[2] = 0x54; f[3] = 0x36; f[4] = 0x01; f[5] = (uint8_t)pp;
    for (i = 0; i < (int)sizeof(tri_patch_t); i++) {
        f[6 + 2 * i] = (uint8_t)(b[i] >> 4);
        f[7 + 2 * i] = (uint8_t)(b[i] & 15u);
    }
    f[SX_LEN - 1] = 0xF7;
    plat_sysex_send(f, SX_LEN);
}
static void nibbles_in(tri_patch_t *t, const uint8_t *p)
{
    uint8_t *b = (uint8_t *)t;
    int i;
    for (i = 0; i < (int)sizeof(tri_patch_t); i++)
        b[i] = (uint8_t)((p[2 * i] & 15u) << 4 | (p[2 * i + 1] & 15u));
    t->name[TRI_NAME_LEN - 1] = 0;
    mm_fix(t);
}
int app_sysex(const uint8_t *p, uint32_t n)
{
    if (n < 4 || p[0] != 0x7Du || p[1] != 0x54u || p[2] != 0x36u)
        return 0;
    if (p[3] == 0x01u && n >= 5u + 2u * sizeof(tri_patch_t)) {
        int pp = p[4] & 0x7F;
        nibbles_in(slot(pp), p + 5);
        blk_dirty[pp / BLK_N] = 1;
        if (pp == set.patch) {
            edit = *slot(pp);
            tri_set_patch(&edit);
        }
        ui_poke = 1;
        return 1;
    }
    if (p[3] == 0x02u && n >= 4u + 2u * sizeof(tri_patch_t)) {
        nibbles_in(&edit, p + 4);
        edit_dirty = 1;
        tri_set_patch(&edit);
        ui_poke = 1;
        return 1;
    }
    if (p[3] == 0x03u && n >= 5u) {
        send_patch(p[4] & 0x7F);
        return 1;
    }
    return 0;
}

/* -------------------------------------------------------------------- pages --- */
/* a knob: a patch parameter (0..TP_N-1), an extra of the patch (PX_*), a setting (PF_*), none -1 */
enum { PX_PITCH2 = 80, PX_PITCH3, PX_MSRC, PX_MDST, PX_MAMT, PX_MSLOT };   /* PX_MSLOT: the page's, not saved */
enum { PF_VOICES = 90, PF_VELCURVE, PF_CLOCK, PF_MIDICH, PF_LOCAL, PF_CORES };
enum { PG_HOME, PG_OSC1, PG_OSC2, PG_OSC3, PG_OSCX, PG_FILT, PG_FILT2, PG_FENV, PG_AENV, PG_AMP, PG_LFO, PG_LFOD,
       PG_PM, PG_PM2, PG_MM, PG_FXA, PG_FXB, PG_FXX, PG_CHAR, PG_VOICE, PG_GLIDE, PG_SETUP, PG_ARP, NPAGES };
typedef struct {
    const char *title;
    int16_t k[4];
} page_t;
static const page_t PAGES[NPAGES] = {
    [PG_HOME] = {"", {TP_CUTOFF, TP_RES, TP_FENV_AMT, TP_FDRV}},
    [PG_OSC1] = {"OSC 1", {TP_O1_OCT, TP_O1_WAVE, TP_O1_PW, TP_O1_LEVEL}},
    [PG_OSC2] = {"OSC 2", {TP_O2_OCT, PX_PITCH2, TP_O2_WAVE, TP_O2_LEVEL}},
    [PG_OSC3] = {"OSC 3", {TP_O3_OCT, PX_PITCH3, TP_O3_WAVE, TP_O3_LEVEL}},
    [PG_OSCX] = {"OSC MORE", {TP_O2_PW, TP_SYNC, TP_O3_PW, TP_O3_KEY}},
    [PG_FILT] = {"FILTER", {TP_CUTOFF, TP_RES, TP_FENV_AMT, TP_FKEY}},
    [PG_FILT2] = {"FILTER 2 / MIX", {TP_2POLE, TP_FVEL, TP_NOISE, TP_FDRV}},
    [PG_FENV] = {"FILTER ENV", {TP_FA, TP_FD, TP_FS, TP_FR}},
    [PG_AENV] = {"AMP ENV", {TP_AA, TP_AD, TP_AS, TP_AR}},
    [PG_AMP] = {"AMP", {TP_AENV_AMT, TP_AVEL, -1, -1}},
    [PG_LFO] = {"LFO", {TP_LFO_FREQ, TP_LFO_SHAPE, TP_LFO_AMT, TP_LFO_SYNC}},
    [PG_LFOD] = {"LFO DEST", {TP_LFO_PITCH, TP_LFO_PW, TP_LFO_FILT, TP_LFO_AMP}},
    [PG_PM] = {"POLY MOD", {TP_PM_OSC3, TP_PM_FENV, TP_PM_PITCH, TP_PM_FILT}},
    [PG_PM2] = {"POLY MOD 2", {TP_PM_PW, TP_PM_FDBK, -1, -1}},
    [PG_MM] = {"MOD MATRIX", {PX_MSLOT, PX_MSRC, PX_MDST, PX_MAMT}},
    [PG_FXA] = {"EFFECT A", {TP_FXA_TYPE, TP_FXA_MIX, TP_FXA_P1, TP_FXA_P2}},
    [PG_FXB] = {"EFFECT B", {TP_FXB_TYPE, TP_FXB_MIX, TP_FXB_P1, TP_FXB_P2}},
    [PG_FXX] = {"EFFECTS", {TP_FX_ON, TP_FXA_SYNC, TP_FXB_SYNC, -1}},
    [PG_CHAR] = {"CHARACTER", {TP_DIST, TP_VINTAGE, TP_PAN, TP_VOLUME}},
    [PG_VOICE] = {"VOICE", {TP_UNISON, TP_KEYMODE, TP_GLIDE_ON, TP_GLIDE_RATE}},
    [PG_GLIDE] = {"GLIDE / PLAY", {TP_GLIDE_MODE, TP_BEND, PF_VELCURVE, PF_VOICES}},
    [PG_SETUP] = {"SETUP", {PF_CLOCK, PF_MIDICH, PF_LOCAL, PF_CORES}},
    [PG_ARP] = {"ARPEGGIATOR", {TP_ARP_MODE, TP_ARP_OCT, TP_ARP_DIV, TP_BPM}},
};

static struct {
    int page;
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint32_t rec_t;
    uint32_t turned_at[4];
    uint32_t knob_t[4];
    uint8_t redraw;
    uint32_t morph_t;                    /* MORPH's last turn, for its speed */
    uint8_t oct_held, oct_used;          /* an OCT button down (its number, 0 none); PRESET turned while it was */
    uint8_t mslot;                       /* the mod matrix slot on show, 0..3 */
    uint8_t press;                       /* the keys' pressure, as last sent */
} ui;

static const char *const FX_NAME[FX_N] = {"OFF", "BBD", "DDL", "CHO", "PH1", "PH2", "PH3", "RING", "FL1", "FL2",
                                          "HALL", "ROOM", "PLATE", "SPRING"};

static int fx_type_of(int id)            /* the effect a P1 / P2 knob belongs to */
{
    return id == TP_FXA_P1 || id == TP_FXA_P2 ? edit.p[TP_FXA_TYPE] : edit.p[TP_FXB_TYPE];
}

static const char *knob_name(int id)
{
    static const char *const PF[] = {"VOICES", "VEL CURVE", "CLOCK", "MIDI CH", "LOCAL", "CORES"};
    if (id < 0)
        return "";
    if (id >= PF_VOICES)
        return PF[id - PF_VOICES];
    switch (id) {
    case PX_MSLOT: return "SLOT";
    case PX_MSRC: return "SOURCE";
    case PX_MDST: return "DEST";
    case PX_MAMT: return "AMOUNT";
    default: break;
    }
    if (id >= PX_PITCH2)
        return "PITCH";
    if (id == TP_FXA_P1 || id == TP_FXB_P1 || id == TP_FXA_P2 || id == TP_FXB_P2) {
        static const char *const P1[FX_N] = {"P1", "TIME", "TIME", "RATE", "RATE", "RATE", "RATE", "FREQ", "RATE",
                                             "RATE", "TIME", "TIME", "TIME", "DECAY"};
        static const char *const P2[FX_N] = {"P2", "FDBK", "FDBK", "DEPTH", "DEPTH", "DEPTH", "DEPTH", "TRACK",
                                             "DEPTH", "DEPTH", "EARLY", "EARLY", "EARLY", "TONE"};
        int t = fx_type_of(id);
        return (id == TP_FXA_P1 || id == TP_FXB_P1) ? P1[t % FX_N] : P2[t % FX_N];
    }
    if (id == TP_FXA_SYNC)
        return "A SYNC";
    if (id == TP_FXB_SYNC)
        return "B SYNC";
    return TRI_PNAME[id];
}

static uint8_t *pf_ptr(int id)
{
    switch (id) {
    case PF_VOICES: return &set.perf.voices;
    case PF_VELCURVE: return &set.perf.vel_curve;
    case PF_CLOCK: return &set.perf.clock_src;
    case PF_LOCAL: return &set.local_off;
    case PF_CORES: return &set.perf.one_core;
    default: return &set.midi_ch;
    }
}

static int knob_value(int id)
{
    if (id < 0)
        return 0;
    if (id >= PF_VOICES)
        return *pf_ptr(id);
    if (id == PX_PITCH2)
        return edit.pitch2;
    if (id == PX_PITCH3)
        return edit.pitch3;
    if (id == PX_MSLOT)
        return ui.mslot;
    if (id == PX_MSRC)
        return edit.mm[ui.mslot].src;
    if (id == PX_MDST)
        return edit.mm[ui.mslot].dst;
    if (id == PX_MAMT)
        return edit.mm[ui.mslot].amt;
    return edit.p[id];
}
static int knob_lo(int id)
{
    if (id == PX_PITCH2 || id == PX_PITCH3)
        return -700;
    if (id == PF_VOICES)
        return 1;
    return id >= 0 && id < TP_N ? TRI_MIN[id] : 0;
}
static int knob_hi(int id)
{
    if (id < 0)
        return 0;
    if (id < TP_N)
        return TRI_MAX[id];
    switch (id) {
    case PX_PITCH2: case PX_PITCH3: return 700;
    case PX_MSLOT: return TRI_MM_SLOTS - 1;
    case PX_MSRC: return MS_N - 1;
    case PX_MDST: return MD_N - 1;
    case PX_MAMT: return 127;
    case PF_VOICES: return TRI_MAXV;
    case PF_VELCURVE: case PF_CLOCK: return 2;
    case PF_LOCAL: case PF_CORES: return 1;
    case PF_MIDICH: return 16;
    default: return 127;
    }
}
static int knob_bipolar(int id)
{
    return id == TP_FDRV || id == TP_FENV_AMT || id == TP_PM_OSC3 || id == TP_PM_FENV || id == PX_PITCH2
        || id == PX_PITCH3 || id == PX_MAMT;
}
static int knob_is_switch(int id)        /* positions, not a knob's sweep */
{
    if (id == PX_MSRC || id == PX_MDST)
        return 1;
    return id >= 0 && id != TP_BPM && id != PX_PITCH2 && id != PX_PITCH3 && id != PX_MAMT
        && knob_hi(id) - knob_lo(id) <= 16;
}

static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}
static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}
static void signed_text(int32_t v, char *t)
{
    t[0] = 0;
    if (v > 0)
        text_cat(t, "+");
    int_text(v, t + (v > 0));
}
static void mask_text(int m, char *t)    /* oscillators 1 2 3 as a mask */
{
    int i;
    t[0] = 0;
    for (i = 0; i < 3; i++)
        if (m >> i & 1) {
            char d[2] = {(char)('1' + i), 0};
            text_cat(t, d);
        }
    if (!t[0])
        text_cat(t, "-");
}

static void value_text_of(int id, int v, char *t)
{
    static const char *const SHAPE[] = {"TRI", "SAW", "REV", "SQR", "RAND"};
    static const char *const LSYNC[] = {"OFF", "KEY", "CLOCK"};
    static const char *const FKEY[] = {"OFF", "HALF", "FULL"};
    static const char *const GMODE[] = {"FR", "FR A", "FT", "FT A"};
    static const char *const KMODE[] = {"LOW", "HIGH", "LAST", "LOW R", "HIGH R", "LAST R"};
    static const char *const AMODE[] = {"UP", "DOWN", "UP+DN", "RAND", "ASSIGN"};
    static const char *const DIV[] = {"1/2", "1/4", "1/8", "1/8 D", "1/8 S", "1/8 T", "1/16", "1/16 S", "1/16 T",
                                      "1/32"};
    static const char *const LDIV[] = {"4 BAR", "2 BAR", "1 BAR", "1/2 D", "1/2", "1/4 T", "1/4", "1/8 T", "1/8",
                                       "1/16T", "1/16", "1/32"};
    static const char *const DDIV[] = {"1", "2 D", "2", "4 T", "4 D", "4", "8 D", "8", "8 T", "16 D", "16"};
    static const char *const VC[] = {"SOFT", "MEDIUM", "HARD"};
    static const char *const CLK[] = {"INT", "MIDI", "AUTO"};
    t[0] = 0;
    if (id < 0)
        return;
    switch (id) {
    case TP_O1_OCT: case TP_O2_OCT: signed_text(v - 2, t); return;
    case TP_O3_OCT: if (v == 0) text_cat(t, "LO"); else signed_text(v - 3, t); return;
    case TP_O1_WAVE: case TP_O2_WAVE: case TP_O3_WAVE: {
        static const char *const W[4] = {"T", "S", "P", "R"};
        static const uint8_t BIT[4] = {1, 2, 4, 8};
        int i;
        for (i = 0; i < 4; i++)
            if (v & BIT[i]) {
                if (t[0])
                    text_cat(t, "+");
                text_cat(t, W[i]);
            }
        if (v == 1) text_cat(t, "RI");  /* a single wave by its name */
        else if (v == 2) text_cat(t, "AW");
        else if (v == 4) text_cat(t, "UL");
        else if (v == 8) text_cat(t, "EV");
        return;
    }
    case PX_PITCH2: case PX_PITCH3: {     /* semitones, two decimals */
        int a = v < 0 ? -v : v;
        char d[4];
        t[0] = v < 0 ? '-' : v > 0 ? '+' : ' ';
        t[1] = 0;
        int_text(a / 100, t + 1);
        text_cat(t, ".");
        d[0] = (char)('0' + a % 100 / 10);
        d[1] = (char)('0' + a % 10);
        d[2] = 0;
        text_cat(t, d);
        return;
    }
    case TP_FDRV:
        if (v == 64) text_cat(t, "0");
        else { text_cat(t, v < 64 ? "FB " : "DR "); int_text(v < 64 ? 64 - v : v - 64, t + 3); }
        return;
    case TP_FENV_AMT: case TP_PM_OSC3: case TP_PM_FENV: case PX_MAMT: signed_text(v - 64, t); return;
    case PX_MSLOT: int_text(v + 1, t); return;
    case PX_MSRC: text_cat(t, TRI_MS_NAME[v % MS_N]); return;
    case PX_MDST: text_cat(t, TRI_MD_NAME[v % MD_N]); return;
    case TP_2POLE: text_cat(t, v ? "2 POLE" : "4 POLE"); return;
    case TP_FKEY: text_cat(t, FKEY[v % 3]); return;
    case TP_SYNC: case TP_O3_KEY: case TP_FVEL: case TP_AVEL: case TP_LFO_FILT: case TP_LFO_AMP: case TP_PM_FDBK:
    case TP_PM_FILT: case TP_GLIDE_ON: case TP_FX_ON: case TP_FXA_SYNC: case TP_FXB_SYNC: case TP_ARP_HOLD:
    case TP_ARP_ON: case PF_CORES:
        if (id == PF_CORES) text_cat(t, v ? "1" : "2");
        else text_cat(t, v ? "ON" : "OFF");
        return;
    case TP_LFO_SHAPE: text_cat(t, SHAPE[v % 5]); return;
    case TP_LFO_SYNC: text_cat(t, LSYNC[v % 3]); return;
    case TP_LFO_FREQ:
        if (edit.p[TP_LFO_SYNC] == 2) {
            text_cat(t, LDIV[v * 12 / 128]);
        } else {                         /* Hz: 0.05 .. 100 */
            float hz = 0.05f * fm_exp2f((float)v * (11.0f / 127.0f));
            int c = (int)(hz * 100.0f + 0.5f);
            if (v == 127 && edit.p[TP_LFO_SHAPE] == 4) { text_cat(t, "NOISE"); return; }
            if (c >= 1000) { int_text(c / 100, t); }
            else { int_text(c / 100, t); text_cat(t, "."); { char d[3] = {(char)('0' + c % 100 / 10), (char)('0' + c % 10), 0}; text_cat(t, d); } }
            text_cat(t, "HZ");
        }
        return;
    case TP_LFO_PITCH: case TP_LFO_PW: case TP_PM_PITCH: case TP_PM_PW: mask_text(v, t); return;
    case TP_GLIDE_MODE: text_cat(t, GMODE[v & 3]); return;
    case TP_UNISON: if (v == 0) text_cat(t, "OFF"); else if (v == 7) text_cat(t, "CHORD"); else int_text(v, t); return;
    case TP_KEYMODE: text_cat(t, KMODE[v % 6]); return;
    case TP_FXA_TYPE: case TP_FXB_TYPE: text_cat(t, FX_NAME[v % FX_N]); return;
    case TP_FXA_P1: case TP_FXB_P1:
        if ((fx_type_of(id) == FX_BBD || fx_type_of(id) == FX_DDL)
            && edit.p[id == TP_FXA_P1 ? TP_FXA_SYNC : TP_FXB_SYNC]) {
            text_cat(t, DDIV[v * 11 / 128]);
            return;
        }
        int_text(v, t);
        return;
    case TP_FXA_P2: case TP_FXB_P2:
        if (fx_type_of(id) == FX_RING) { text_cat(t, v >= 64 ? "ON" : "OFF"); return; }
        int_text(v, t);
        return;
    case TP_ARP_MODE: text_cat(t, AMODE[v % 5]); return;
    case TP_ARP_OCT: int_text(v + 1, t); text_cat(t, " OCT"); return;
    case TP_ARP_DIV: text_cat(t, DIV[v % 10]); return;
    case PF_VELCURVE: text_cat(t, VC[v % 3]); return;
    case PF_CLOCK: text_cat(t, CLK[v % 3]); return;
    case PF_LOCAL: text_cat(t, v ? "OFF" : "ON"); return;
    case PF_MIDICH: if (v) int_text(v, t); else text_cat(t, "OMNI"); return;
    default: int_text(v, t); return;
    }
}

static void send_cc(uint32_t cc, uint32_t v)
{
    plat_midi_send(0x0Bu | (0xB0u | midi_out_ch()) << 8 | cc << 16 | (v & 0x7Fu) << 24, TO_USB | TO_BLE);
}

static void knob_set(int id, int v)
{
    v = clamp(v, knob_lo(id), knob_hi(id));
    if (v == knob_value(id))
        return;
    if (id >= PF_VOICES) {
        *pf_ptr(id) = (uint8_t)v;
        set_dirty = 1;
        if (id == PF_VOICES)
            tri_all_off();
        tri_set_perf(&set.perf);
        return;
    }
    if (id == PX_MSLOT) {
        ui.mslot = (uint8_t)v;
        return;
    }
    if (id == PX_MSRC || id == PX_MDST || id == PX_MAMT) {
        tri_route_t *r = &edit.mm[ui.mslot];
        if (id == PX_MSRC)
            r->src = (unsigned)v & 15u;
        else if (id == PX_MDST)
            r->dst = (unsigned)v & 31u;
        else
            r->amt = (unsigned)v & 127u;
        tri_set_route(ui.mslot, r->src, r->dst, r->amt);
        edit_dirty = 1;
        return;
    }
    if (id == PX_PITCH2 || id == PX_PITCH3) {
        if (id == PX_PITCH2)
            edit.pitch2 = tri_patch()->pitch2 = (int16_t)v;
        else
            edit.pitch3 = tri_patch()->pitch3 = (int16_t)v;
        edit_dirty = 1;
        send_cc(id == PX_PITCH2 ? 76u : 83u, (uint32_t)(64 + v * 63 / 700));
        return;
    }
    edit.p[id] = (uint8_t)v;
    edit_dirty = 1;
    tri_set_param(id, v);
    if (PARAM_CC[id])
        send_cc(PARAM_CC[id], param_to_cc(id, v));
}

/* ---------------------------------------------------------------------- UI --- */
void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void patch_label(int i, char *t)  /* "042" */
{
    t[0] = (char)('0' + i / 100);
    t[1] = (char)('0' + i / 10 % 10);
    t[2] = (char)('0' + i % 10);
    t[3] = 0;
}

static void speak(void)
{
    char t[64], l[8];
    if (ann_on() && ann_where()) {
        patch_label(set.patch, l);
        t[0] = 0;
        text_cat(t, APP_NAME ". ");
        text_cat(t, l);
        text_cat(t, " ");
        text_cat(t, edit.name);
        if (ui.page != PG_HOME) {
            text_cat(t, ". ");
            text_cat(t, PAGES[ui.page].title);
        }
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

static void page_cycle(const uint8_t *pages, int n)
{
    int i, next = pages[0];
    for (i = 0; i < n; i++)
        if (ui.page == pages[i])
            next = pages[(i + 1) % n];
    ui.page = next;
    ui.redraw = 1;
    if (ann_on())
        ann_say(PAGES[ui.page].title, 0, ANN_NOW);
}

static void octave_step(int d)
{
    int o = clamp(set.octave + d, -2, 2);
    char t[4];
    if (o == set.octave)
        return;
    tri_all_off();
    set.octave = (int8_t)o;
    set_dirty = 1;
    t[0] = o < 0 ? '-' : '+';
    t[1] = (char)('0' + (o < 0 ? -o : o));
    t[2] = 0;
    app_say("OCTAVE", t);
}

static void save(void)
{
    tri_patch_t *ep = tri_patch();
    int b = set.patch / BLK_N, rc = 0, i;
    edit.chord_n = ep->chord_n;
    for (i = 0; i < 6; i++)
        edit.chord[i] = ep->chord[i];
    edit.p[TP_UNISON] = ep->p[TP_UNISON];
    *slot(set.patch) = edit;
    blk_dirty[b] = 1;
    for (i = 0; i < NBLK && rc == 0; i++)
        if (blk_dirty[i]) {
            rc = plat_store_save(OBJ_BANK0 + (uint32_t)i, &blocks[i], sizeof blocks[i]);
            if (rc == 0)
                blk_dirty[i] = 0;
        }
    if (rc == 0 && set_dirty)
        rc = plat_store_save(OBJ_SETTINGS, &set, sizeof set);
    if (rc == 0) {
        edit_dirty = 0;
        set_dirty = 0;
        app_say("SAVED", edit.name);
    } else {
        app_say("SAVE", "FAILED");
    }
}

static int arp_state_of(void) { return edit.p[TP_ARP_ON] ? (edit.p[TP_ARP_HOLD] ? 2 : 1) : 0; }

static void arp_button(void)
{
    static const char *const W[] = {"OFF", "ON", "HOLD"};
    int s = (arp_state_of() + 1) % 3;
    knob_set(TP_ARP_ON, s != 0);
    knob_set(TP_ARP_HOLD, s == 2);
    ui.page = PG_ARP;
    app_say("ARPEGGIATOR", W[s]);
}

static void play_button(void)
{
    tri_chord_learn();
    app_say("CHORD", "PLAY IT");
}

static void input(void)
{
    static const uint8_t EDIT_P[] = {PG_OSC1, PG_OSC2, PG_OSC3, PG_OSCX}, SEL_P[] = {PG_FILT, PG_FILT2},
                         ENV_P[] = {PG_FENV, PG_AENV, PG_AMP}, LFO_P[] = {PG_LFO, PG_LFOD}, SEQ_P[] = {PG_PM, PG_PM2, PG_MM},
                         FX_P[] = {PG_FXA, PG_FXB, PG_FXX, PG_CHAR}, GLO_P[] = {PG_VOICE, PG_GLIDE, PG_SETUP},
                         HOME_P[] = {PG_HOME};
    uint32_t keys, btn = plat_buttons(), k, now = plat_ms();
    int32_t e;
    touch_poll();
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k, v, ch = midi_out_ch();
        int32_t note = 53 + (int32_t)k + 12 * set.octave;
        if ((keys & bit) && !(ui.keys & bit)) {
            v = touch_vel(k);
            if (!set.local_off)
                tri_note_on((int)note, (int)v);
            plat_midi_send(0x09u | (0x90u | ch) << 8 | (uint32_t)note << 16 | v << 24, TO_USB | TO_BLE);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            if (!set.local_off)
                tri_note_off((int)note);
            plat_midi_send(0x08u | (0x80u | ch) << 8 | (uint32_t)note << 16, TO_USB | TO_BLE);
        }
    }
    {                                    /* the keys' pressure: the hardest held key, to the mod matrix and MIDI out */
        uint32_t pmax = 0;
        for (k = 0; k < NKEYS; k++)
            if ((keys >> k & 1u) && touch_down(k)) {
                uint32_t pr = touch_pressure(k);
                if (pr > pmax)
                    pmax = pr;
            }
        if (pmax != ui.press) {
            ui.press = (uint8_t)pmax;
            if (!set.local_off)
                tri_pressure((int)pmax);
            plat_midi_send(0x0Du | (0xD0u | midi_out_ch()) << 8 | pmax << 16, TO_USB | TO_BLE);
        }
    }
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        switch (k) {
        case B_OCTDN: case B_OCTUP:      /* the octave on release, unless PRESET turned while it was held */
            ui.oct_held = (uint8_t)k;
            ui.oct_used = 0;
            break;
        case B_HOME: page_cycle(HOME_P, 1); break;
        case B_EDIT: page_cycle(EDIT_P, 4); break;
        case B_SEL: page_cycle(SEL_P, 2); break;
        case B_ENV: page_cycle(ENV_P, 3); break;
        case B_LFO: page_cycle(LFO_P, 2); break;
        case B_SEQ: page_cycle(SEQ_P, 3); break;
        case B_FX: page_cycle(FX_P, 4); break;
        case B_GLO: page_cycle(GLO_P, 3); break;
        case B_ARP: arp_button(); break;
        case B_PLAY: play_button(); break;
        case B_SAVE: save(); break;
        case B_REC: if (FM1B_BLE) ui.rec_t = now; break;
        default: break;
        }
    }
    if (FM1B_BLE && ui.rec_t) {
        if (btn >> B_REC & 1u) {
            if (now - ui.rec_t >= 1000u) {
                ui.rec_t = 0;
                set.ble_off = (uint8_t)!set.ble_off;
                set_dirty = 1;
                plat_ble_enable(!set.ble_off);
                app_say("BLUETOOTH", set.ble_off ? "OFF" : "ON");
            }
        } else {
            ui.rec_t = 0;
        }
    }
    for (k = B_OCTDN; ui.oct_held && k <= B_OCTUP; k += B_OCTUP - B_OCTDN)   /* an OCT button let go */
        if (ui.oct_held == k && !(btn >> k & 1u)) {
            if (!ui.oct_used)
                octave_step(k == B_OCTUP ? 1 : -1);
            ui.oct_held = 0;
        }
    if ((e = plat_enc(EN_SELECT)) != 0) {
        if (ui.page == PG_MM) {          /* SELECT on the matrix page: the slot */
            ui.mslot = (uint8_t)clamp(ui.mslot + e, 0, TRI_MM_SLOTS - 1);
            ui.turned_at[0] = now;
        } else {                         /* elsewhere: through every page, in the panel's order */
            ui.page = (int)(((ui.page + e) % NPAGES + NPAGES) % NPAGES);
            if (ann_on())
                ann_say(ui.page == PG_HOME ? "HOME" : PAGES[ui.page].title, 0, ANN_NOW);
        }
        ui.redraw = 1;
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {  /* ALGO: MORPH, the patch's macro (a mod matrix source) */
        uint32_t dt = now - ui.morph_t;
        ui.morph_t = now;
        char t[16];
        int s, routed = 0;
        morph_set(morph_now + e * (dt < 40u ? 4 : dt < 100u ? 2 : 1), 1);
        for (s = 0; s < TRI_MM_SLOTS; s++)
            routed |= edit.mm[s].src == MS_MORPH && edit.mm[s].dst != MD_OFF && edit.mm[s].amt != 64;
        int_text(morph_now, t);
        if (!routed)
            text_cat(t, " NO ROUTE");
        app_say("MORPH", t);
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {   /* PRESET: the patches; with OCT held, ten at a time */
        if (ui.oct_held) {
            e *= 10;
            ui.oct_used = 1;
        }
        patch_select(set.patch + e);
        plat_midi_send(0x0Cu | (0xC0u | midi_out_ch()) << 8 | (uint32_t)set.patch << 16, TO_USB | TO_BLE);
    }
    for (k = 0; k < 4u; k++) {
        int id = PAGES[ui.page].k[k], step, span;
        uint32_t dt;
        if ((e = plat_enc(EN_K1 + (int)k)) == 0 || id < 0)
            continue;
        dt = now - ui.knob_t[k];
        span = knob_hi(id) - knob_lo(id);
        if (knob_is_switch(id))
            step = 1;
        else if (span > 200)             /* the pitches, in cents: a cent, then a semitone at speed */
            step = dt < 30u ? 50 : dt < 70u ? 10 : dt < 150u ? 2 : 1;
        else
            step = dt < 40u ? 4 : dt < 100u ? 2 : 1;
        ui.knob_t[k] = now;
        knob_set(id, knob_value(id) + e * step);
        ui.turned_at[k] = now;
        ui.redraw = 1;
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    {
        static const uint8_t PAGE_BTN[NPAGES] = {
            [PG_HOME] = B_HOME, [PG_OSC1] = B_EDIT, [PG_OSC2] = B_EDIT, [PG_OSC3] = B_EDIT, [PG_OSCX] = B_EDIT,
            [PG_FILT] = B_SEL, [PG_FILT2] = B_SEL, [PG_FENV] = B_ENV, [PG_AENV] = B_ENV, [PG_AMP] = B_ENV,
            [PG_LFO] = B_LFO, [PG_LFOD] = B_LFO, [PG_PM] = B_SEQ, [PG_PM2] = B_SEQ, [PG_MM] = B_SEQ, [PG_FXA] = B_FX, [PG_FXB] = B_FX,
            [PG_FXX] = B_FX, [PG_CHAR] = B_FX, [PG_VOICE] = B_GLO, [PG_GLIDE] = B_GLO, [PG_SETUP] = B_GLO,
            [PG_ARP] = B_ARP};
        static uint32_t arp_blink;
        uint32_t lit = btn | 1u << PAGE_BTN[ui.page];
        int arp = arp_state_of();
        if (tri_arp_step_lit())
            arp_blink = now;
        if (arp == 1 || (arp == 2 && now - arp_blink < 60u))
            lit |= 1u << B_ARP;
        if (tri_learning() && (now / 250u) & 1u)
            lit |= 1u << B_PLAY;
        if (edit_dirty || set_dirty)
            lit |= 1u << B_SAVE;
        plat_glow(1);
        plat_leds(lit, keys);
    }
}

void app_input_only(void) { input(); }

/* ---------------------------------------------------------------- drawing --- */
/* a dark panel, cream knobs with an amber arc, each section's label in its own colour */
#define T_PANEL RGB(22, 22, 24)
#define T_KNOB RGB(46, 46, 50)
#define T_TRACK RGB(70, 70, 76)
#define T_CREAM RGB(236, 226, 200)
#define T_AMBER RGB(226, 160, 64)
#define T_RED RGB(214, 76, 60)
#define T_BLUE RGB(96, 160, 220)
#define T_GREEN RGB(120, 190, 120)

static uint16_t section_colour(int id)
{
    if (id >= PF_VOICES)
        return T_CREAM;
    if (id >= PX_MSRC && id <= PX_MSLOT)
        return T_GREEN;
    if (id >= PX_PITCH2 || id <= TP_FDRV)
        return T_AMBER;
    if (id <= TP_FVEL)
        return T_RED;
    if (id <= TP_AVEL)
        return T_BLUE;
    if (id <= TP_PM_FILT)
        return T_GREEN;
    return T_CREAM;
}

/* a knob: a dial with its arc from 7 to 5 o'clock (from 12 for a centred value), a pointer, the value below */
static void draw_dial(int32_t cx, int32_t cy, int32_t rad, float f, int bip, uint16_t col)
{
    int i;
    float a0 = 0.75f * FM_PI, sweep = 1.5f * FM_PI, at = a0 + sweep * f, from = bip ? 1.5f * FM_PI : a0;
    for (i = 0; i <= 54; i++) {          /* the track, and the arc over it */
        float a = a0 + sweep * (float)i / 54.0f, c = fm_cosf(a), s = fm_sinf(a);
        int on = bip ? (a >= fm_minf(from, at) && a <= fm_maxf(from, at)) : a <= at;
        int r;
        for (r = rad - 1; r <= rad + 1; r++)
            cv_pset(cx + (int32_t)(c * (float)r), cy + (int32_t)(s * (float)r), on ? col : T_TRACK);
    }
    for (i = 0; i < rad - 4; i++) {      /* the knob, filled */
        int32_t h = (int32_t)fm_sqrtf((float)((rad - 4) * (rad - 4) - i * i));
        cv_rect(cx - h, cy - i, 2 * h + 1, 1, T_KNOB);
        cv_rect(cx - h, cy + i, 2 * h + 1, 1, T_KNOB);
    }
    cv_line(cx + (int32_t)(fm_cosf(at) * 4.0f), cy + (int32_t)(fm_sinf(at) * 4.0f),
            cx + (int32_t)(fm_cosf(at) * (float)(rad - 5)), cy + (int32_t)(fm_sinf(at) * (float)(rad - 5)), T_CREAM);
}

static void draw_knob(int32_t x, int32_t w, int id, int lit)
{
    char t[16];
    int hi = knob_hi(id), lo = knob_lo(id), v = knob_value(id), n = hi - lo + 1;
    uint16_t col = section_colour(id);
    if (id < 0)
        return;
    cv_text(x + (w - text_w(&FONT_XS, knob_name(id))) / 2, 0, &FONT_XS, knob_name(id), lit ? C_WHITE : C_GRAY);
    if (knob_is_switch(id) && n <= 4) {  /* a switch: its positions, top to bottom */
        int i, hgt = 22, gap = 26;
        for (i = 0; i < n; i++) {
            int32_t y = 20 + i * gap;
            value_text_of(id, lo + i, t);
            cv_rect(x + 3, y, w - 6, hgt, v == lo + i ? col : T_KNOB);
            cv_text(x + (w - text_w(&FONT_XS, t)) / 2, y + (hgt - 13) / 2, &FONT_XS, t,
                    v == lo + i ? C_BLACK : C_GRAY);
        }
        return;
    }
    if (knob_is_switch(id)) {            /* a selector: the value, and a row of dots for where it is */
        int i;
        value_text_of(id, v, t);
        cv_rect(x + 3, 44, w - 6, 40, T_KNOB);
        cv_text(x + (w - text_w(&FONT_XS, t)) / 2, 57, &FONT_XS, t, col);
        for (i = 0; i < n; i++) {        /* (a long list: 2 px apart, to fit the column) */
            int32_t sp = n * 4 <= w - 4 ? 4 : 2, dx = x + w / 2 - (n * sp) / 2 + i * sp;
            cv_rect(dx, 94, sp == 4 ? 2 : 1, i == v - lo ? 6 : 2, i == v - lo ? col : C_DIM);
        }
        return;
    }
    draw_dial(x + w / 2, 70, 24, (float)(v - lo) / (float)(hi - lo), knob_bipolar(id), col);
    value_text_of(id, v, t);
    cv_text(x + (w - text_w(&FONT_XS, t)) / 2, 112, &FONT_XS, t, lit ? C_WHITE : C_HI);
}

static void draw(void)
{
    char t[24], l[8];
    uint32_t i, now = plat_ms();
    const page_t *pg = &PAGES[ui.page];
    cv_begin(240, 40, T_PANEL);
    cv_rect(0, 38, 240, 2, T_AMBER);
    patch_label(set.patch, l);
    if (now < ui.msg_until) {
        int32_t x = cv_text(8, 8, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 8, &FONT_B, ui.msg[1], T_AMBER);
    } else {
        int32_t x = cv_text(8, 4, &FONT_B, l, T_AMBER);
        x = cv_text(x + 8, 4, &FONT_B, edit.name, C_WHITE);
        if (edit_dirty)
            cv_text(x + 2, 4, &FONT_B, "*", T_AMBER);
        cv_text(8, 24, &FONT_XS, ui.page == PG_HOME ? "HOME" : pg->title, C_GRAY);
        if (ui.page == PG_SETUP) {       /* the load, for the voice count */
            t[0] = 0;
            text_cat(t, "CPU ");
            int_text((int32_t)plat_cpu_pct(), t + 4);
            text_cat(t, tri_cores() == 2 ? "% 2C" : "% 1C");
            cv_text(232 - text_w(&FONT_XS, t), 24, &FONT_XS, t, T_GREEN);
        } else if (ui.page == PG_MM) {   /* the routes in use */
            int s, n = 0;
            for (s = 0; s < TRI_MM_SLOTS; s++)
                n += edit.mm[s].src && edit.mm[s].dst && edit.mm[s].amt != 64;
            int_text(n, t);
            text_cat(t, n == 1 ? " ROUTE" : " ROUTES");
            cv_text(232 - text_w(&FONT_XS, t), 24, &FONT_XS, t, T_GREEN);
        } else {
            int u = tri_patch()->p[TP_UNISON];
            t[0] = 0;
            if (u == 0)
                text_cat(t, "POLY");
            else {
                value_text_of(TP_UNISON, u, t);
                if (u != 7) {
                    t[0] = 0;
                    text_cat(t, "UNI ");
                    int_text(u, t + 4);
                }
            }
            cv_text(232 - text_w(&FONT_XS, t), 24, &FONT_XS, t, T_CREAM);
        }
        if (FM1B_BLE)
            cv_text(208, 6, &FONT_XS, "BLE", plat_ble_connected() ? T_BLUE : C_DIM);
    }
    cv_blit(0, 0);
    cv_begin(240, 150, T_PANEL);
    for (i = 0; i < 4u; i++)
        draw_knob(4 + (int32_t)i * 58, 56, pg->k[i], ui.turned_at[i] && now - ui.turned_at[i] < 600u);
    cv_blit(0, 44);
    cv_begin(240, 44, T_PANEL);
    cv_rect(0, 0, 240, 1, C_LINE);
    for (i = 0; i < (uint32_t)set.perf.voices && i < TRI_MAXV; i++) {
        float lv = tri_voice_level((int)i);
        int32_t h = (int32_t)(lv * 18.0f), x = 8 + (int32_t)i * 15;
        cv_rect(x, 8, 12, 20, T_KNOB);
        if (tri_voice_note((int)i) >= 0)
            cv_rect(x, 8 + 20 - h - 1, 12, h + 1, T_AMBER);
    }
    t[0] = 0;
    text_cat(t, "OCT ");
    {
        char o[4] = {set.octave < 0 ? '-' : '+', (char)('0' + (set.octave < 0 ? -set.octave : set.octave)), 0};
        text_cat(t, o);
    }
    cv_text(104, 20, &FONT_XS, t, C_GRAY);
    {
        int arp = arp_state_of();
        if (arp)
            cv_text(104, 4, &FONT_XS, arp == 2 ? "HOLD" : "ARP", T_AMBER);
        if (tri_clock_running())
            cv_text(140, 4, &FONT_XS, "CLK", T_BLUE);
    }
    if (edit.p[TP_FX_ON]) {
        t[0] = 0;
        text_cat(t, FX_NAME[edit.p[TP_FXA_TYPE] % FX_N]);
        text_cat(t, " ");
        text_cat(t, FX_NAME[edit.p[TP_FXB_TYPE] % FX_N]);
    } else {
        t[0] = 0;
        text_cat(t, "FX OFF");
    }
    cv_text(232 - text_w(&FONT_XS, t), 20, &FONT_XS, t, edit.p[TP_FX_ON] ? T_BLUE : C_DIM);
    cv_blit(0, 196);
}

/* ----------------------------------------------------------------- splash --- */
/* the boot screen, retro: a triangle (a trigon) striped like a 70s sunset, the name over it in a drop shadow, a rule
 * with the tagline, and a little keyboard between wooden cheeks. Drawn in two bands of 120 rows (the canvas holds
 * 240 x 144); every shape is drawn in screen rows, the band clipping it */
#define SP_BG0 RGB(34, 24, 20)
#define SP_BG1 RGB(12, 10, 10)
static uint16_t sp_mix(uint16_t a, uint16_t b, int t, int n)   /* a to b, t of n */
{
    int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31, br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
    return (uint16_t)(((ar + (br - ar) * t / n) << 11) | ((ag + (bg - ag) * t / n) << 5) | (ab + (bb - ab) * t / n));
}

static void splash_band(int32_t y0, int32_t h)
{
    static const uint16_t STRIPE[] = {RGB(244, 232, 200), RGB(240, 196, 96), RGB(232, 150, 52), RGB(222, 104, 44),
                                      RGB(196, 64, 44), RGB(140, 40, 48)};
    const int32_t ax = 120, ay = 14, by = 134, hw = 70;   /* the triangle: apex, base row, half its base */
    int32_t y, x, i;
    cv_begin(240, (uint32_t)h, SP_BG1);
    cv_oy = -y0;                         /* screen rows: the canvas is rows y0 .. y0+h-1 */
    cv_y0 = 0;
    cv_y1 = h;
    for (y = y0; y < y0 + h; y++)        /* the ground: warm at the top, near black at the foot */
        cv_rect(0, y, 240, 1, sp_mix(SP_BG0, SP_BG1, y, 240));
    /* a faint sun-ray fan behind the triangle */
    for (i = -5; i <= 5; i++)
        cv_line(ax, ay + 40, ax + i * 40, 0, RGB(52, 36, 28));
    /* the triangle: stripes that get thicker toward its base, a dark gap between each */
    for (y = ay; y <= by; y++) {
        int32_t half = (y - ay) * hw / (by - ay), t = (y - ay) * 100 / (by - ay), s, edge;
        int32_t band = t < 22 ? 0 : t < 40 ? 1 : t < 56 ? 2 : t < 71 ? 3 : t < 86 ? 4 : 5;
        static const int8_t START[] = {0, 22, 40, 56, 71, 86};
        s = t - START[band];
        edge = (s < 2 && band > 0) ? 1 : 0;   /* the gap above each stripe */
        cv_rect(ax - half, y, 2 * half + 1, 1, edge ? SP_BG1 : STRIPE[band]);
    }
    /* its outline, cream */
    cv_line(ax, ay - 2, ax - hw - 3, by + 2, RGB(244, 232, 200));
    cv_line(ax, ay - 2, ax + hw + 3, by + 2, RGB(244, 232, 200));
    cv_line(ax - hw - 3, by + 2, ax + hw + 3, by + 2, RGB(244, 232, 200));
    /* six small triangles in a row under it: the six voices */
    for (i = 0; i < 6; i++) {
        int32_t cx = 120 - 50 + i * 20, r;
        for (r = 0; r < 7; r++)
            cv_rect(cx - r * 6 / 7, 140 + r, 2 * (r * 6 / 7) + 1, 1, STRIPE[i]);
    }
    /* the name, a red shadow under cream */
    {
        const char *n = "TRI-6";
        int32_t w = text_w(&FONT_L, n), tx = 120 - w / 2;
        cv_text(tx + 2, 151, &FONT_L, n, RGB(196, 64, 44));
        cv_text(tx, 149, &FONT_L, n, RGB(244, 232, 200));
    }
    /* the tagline between two rules */
    {
        const char *s = "FM-1  POLYSYNTH";
        int32_t w = text_w(&FONT_XS, s), tx = 120 - w / 2;
        cv_rect(18, 191, tx - 26, 1, RGB(240, 196, 96));
        cv_rect(tx + w + 8, 191, 222 - (tx + w + 8), 1, RGB(240, 196, 96));
        cv_text(tx, 184, &FONT_XS, s, RGB(240, 196, 96));
    }
    /* the keyboard: wooden cheeks, a black panel strip, two octaves of keys */
    {
        const int32_t ky = 204, kh = 34, kx = 16, nk = 14, kw = 15;   /* 14 white keys of 15: 210 px */
        static const uint8_t BLACK[7] = {1, 1, 0, 1, 1, 1, 0};        /* a black key after C D, F G A */
        for (y = 198; y < 240; y++)      /* the cheeks: walnut, with grain */
            for (x = 0; x < 240; x++) {
                if (x >= kx - 2 && x < kx + nk * kw + 2)
                    continue;
                {
                    int32_t g = ((x * 7 + y * 3 + ((y * y) >> 3)) % 23) < 3;
                    cv_pset(x, y, g ? RGB(92, 52, 28) : RGB(128, 76, 40));
                }
            }
        cv_rect(kx - 2, 198, nk * kw + 4, 6, RGB(20, 18, 18));   /* the strip above the keys */
        cv_rect(kx - 2, 200, nk * kw + 4, 1, RGB(196, 64, 44));
        for (i = 0; i < nk; i++) {       /* the white keys */
            cv_rect(kx + i * kw, ky, kw - 1, kh, RGB(236, 230, 214));
            cv_rect(kx + i * kw, ky + kh - 3, kw - 1, 3, RGB(196, 188, 170));
        }
        for (i = 0; i < nk - 1; i++)     /* the black keys */
            if (BLACK[i % 7])
                cv_rect(kx + i * kw + kw - 4, ky, 8, 20, RGB(24, 22, 22));
    }
    cv_oy = 0;
    cv_blit(0, (uint32_t)y0);
}

void app_splash(void)
{
    splash_band(0, 120);
    splash_band(120, 120);
}

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, T_PANEL);
    ui.page = PG_HOME;
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last_foot, last_msg;
    static int was_learning;
    uint32_t now = plat_ms();
    int pc = pc_pending;
    if (pc >= 0) {
        pc_pending = -1;
        patch_select(pc);
        ui.redraw = 1;
    }
    if (ui_poke) {
        ui_poke = 0;
        ui.redraw = 1;
    }
    if (was_learning && !tri_learning()) {   /* a chord learnt: UNISON is now CHORD */
        tri_patch_t *ep = tri_patch();
        int i;
        edit.chord_n = ep->chord_n;
        for (i = 0; i < 6; i++)
            edit.chord[i] = ep->chord[i];
        edit.p[TP_UNISON] = ep->p[TP_UNISON];
        edit_dirty = 1;
        {
            char t[16];
            int_text(edit.chord_n, t);
            text_cat(t, edit.chord_n == 1 ? " NOTE" : " NOTES");
            app_say("CHORD KEPT", t);
        }
    }
    was_learning = tri_learning();
    input();
    speak();
    if (now - last_foot >= 50u) {
        last_foot = now;
        ui.redraw = 1;
    }
    if (ui.msg_until && now >= ui.msg_until && last_msg != ui.msg_until) {
        last_msg = ui.msg_until;
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "octave"))
        *got = set.octave;
    else if (!strcmp(what, "patch"))
        *got = set.patch;
    else if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strcmp(what, "dirty"))
        *got = edit_dirty;
    else if (!strcmp(what, "arp"))
        *got = arp_state_of();
    else if (!strcmp(what, "unison"))
        *got = tri_patch()->p[TP_UNISON];
    else if (!strcmp(what, "chord_n"))
        *got = tri_patch()->chord_n;
    else if (!strcmp(what, "pitch2"))
        *got = edit.pitch2;
    else if (!strncmp(what, "mm", 2) && what[2] >= '0' && what[2] < '0' + TRI_MM_SLOTS) {   /* mm0src, mm0dst, mm0amt */
        const tri_route_t *r = &tri_patch()->mm[what[2] - '0'];
        *got = !strcmp(what + 3, "src") ? r->src : !strcmp(what + 3, "dst") ? r->dst : r->amt;
    }
    else if (!strcmp(what, "morph"))
        *got = morph_now;
    else if (!strcmp(what, "conv02")) {  /* 0.2's three-byte routes into 0.3's: 1 when right */
        tri_patch_t t;
        uint8_t *b = (uint8_t *)&t + offsetof(tri_patch_t, mm);
        static const uint8_t OLD[12] = {MS_KEY, MD_CUTOFF, 104, MS_VEL, MD_AMP, 20, 0, 0, 0, 0, 0, 0};
        int i, ok;
        memset(&t, 0, sizeof t);
        for (i = 0; i < 12; i++)
            b[i] = OLD[i];
        mm_from_02(&t);
        ok = t.mm[0].src == MS_KEY && t.mm[0].dst == MD_CUTOFF && t.mm[0].amt == 104 && t.mm[1].src == MS_VEL
          && t.mm[1].dst == MD_AMP && t.mm[1].amt == 20;
        for (i = 2; i < TRI_MM_SLOTS; i++)
            ok = ok && t.mm[i].src == 0 && t.mm[i].dst == 0 && t.mm[i].amt == 64;
        *got = ok;
    }
    else if (!strncmp(what, "param", 5))
        *got = edit.p[atoi(what + 5) % TP_N];
    else if (!strncmp(what, "voice", 5))
        *got = tri_voice_note(atoi(what + 5));
    else
        return 0;
    return 1;
}
#endif
