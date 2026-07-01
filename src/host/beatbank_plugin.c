/*
 * beatbank_plugin.c — Beat Bank: a library of fixed "standard" drum patterns.
 *
 * API: midi_fx_api_v1_t  (entry point: move_midi_fx_init)
 *
 * A chainable MIDI FX drum sequencer, in the Schwung world only. It loops the
 * selected pattern on Move's clock and emits General MIDI drum notes into the
 * chain, which drives whatever sound generator sits in the slot (e.g. an SF2
 * drum kit). Same semantic as Super Arp: the module makes notes, the chain
 * feeds them to the synth. Record with Schwung's Quantized Sampler.
 *
 * NO randomness. Patterns load from external .beat files (see patterns.c).
 * It plays straight at Move's tempo — silent unless the transport is running.
 *
 *   0xFA reset to step 0 · 0xF8 advance (6 clocks / 16th), looping · 0xFC stop.
 */

#include "midi_fx_api_v1.h"
#include "plugin_api_v1.h"
#include "../dsp/patterns.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MIDI_NOTE_ON   0x90u
#define MIDI_NOTE_OFF  0x80u

#define CLOCKS_PER_STEP 6u   /* 24 PPQN / 4 sixteenths */
#define GATE_CLOCKS     3u   /* note length in clocks (< one step) */
#define OUT_CHANNEL     0u   /* the chain/slot rewrites the channel on output */

static char g_note_keys[BB_NUM_VOICES][16];
static const host_api_v1_t *g_host = NULL;

/* Note-map modes (voice order: kick snare ch oh clap rim tom ride crash cowbell conga perc).
 *   gm       — full General MIDI drum map; correct for SF2 / soundfont GM kits.
 *   drumrack — everything squeezed into 36..51, the Move/Ableton drum-rack pad
 *              range that every Schwung drum sampler (MrDrums, Forge, KrautDrums,
 *              Libpo32, ...) triggers on. cowbell/conga/perc move to free pads. */
static const uint8_t kGmNotes[BB_NUM_VOICES]       = {36,38,42,46,39,37,45,51,49,56,63,70};
static const uint8_t kDrumrackNotes[BB_NUM_VOICES] = {36,38,42,46,39,37,45,51,49,47,48,50};

/* The pattern bank is read-only and shared across all instances. */
static BeatBank g_bank = { NULL, 0 };
static int      g_bank_loaded = 0;

typedef struct {
    uint8_t active;
    uint8_t note;
    uint8_t clocks_left;
} PendingNoteOff;

typedef struct {
    int     pattern;                 /* selected index into the bank         */
    uint8_t note[BB_NUM_VOICES];     /* output note per voice                */
    uint8_t note_map;                /* 0 = gm, 1 = drumrack (36..51)         */
    uint8_t swing;                   /* 0..100, delays off-beat 16ths         */

    uint8_t cur_step;
    uint8_t clock_running;           /* Move transport running               */
    uint8_t midi_clocks_until_tick;

    uint32_t preview_revision;
    PendingNoteOff pending[BB_NUM_VOICES];
} BeatBankInstance;

/* ── Bank helpers ────────────────────────────────────────────────────────── */

static const BeatPattern *pattern_at(int idx)
{
    if (g_bank.count <= 0) return NULL;
    if (idx < 0) idx = 0;
    if (idx >= g_bank.count) idx = g_bank.count - 1;
    return &g_bank.patterns[idx];
}
static uint8_t pattern_steps(const BeatBankInstance *bi)
{
    const BeatPattern *p = pattern_at(bi->pattern);
    if (!p || p->steps < 1) return 16u;
    return p->steps > BB_MAX_STEPS ? BB_MAX_STEPS : p->steps;
}

/* Swing: 0..100 maps to 0..2 clocks of delay on the off-beat 16ths. Move's
 * clock is 6 clocks / 16th, so +1 ≈ 58% swing, +2 ≈ 66%. Off-beats (odd step
 * index) fire late, on-beats early, so each pair keeps its 12-clock span. */
static uint8_t swing_clocks(const BeatBankInstance *bi)
{
    return (uint8_t)((bi->swing * 2u + 50u) / 100u);
}
static uint8_t clocks_before_step(const BeatBankInstance *bi, uint8_t step)
{
    uint8_t sc = swing_clocks(bi);
    if (step & 1u) return (uint8_t)(CLOCKS_PER_STEP + sc);
    return (uint8_t)(CLOCKS_PER_STEP > sc ? CLOCKS_PER_STEP - sc : 1u);
}

/* ── MIDI emit (direct into the chain's out buffer) ──────────────────────── */

static int emit(uint8_t status, uint8_t note, uint8_t vel,
                uint8_t out_msgs[][3], int out_lens[], int max_out, int count)
{
    if (count >= max_out) return count;
    out_msgs[count][0] = status; out_msgs[count][1] = note; out_msgs[count][2] = vel;
    out_lens[count] = 3;
    return count + 1;
}

static int flush_all(BeatBankInstance *bi, uint8_t out_msgs[][3], int out_lens[], int max_out, int count)
{
    for (int v = 0; v < BB_NUM_VOICES; v++) {
        if (bi->pending[v].active) {
            count = emit((uint8_t)(MIDI_NOTE_OFF | OUT_CHANNEL), bi->pending[v].note, 0, out_msgs, out_lens, max_out, count);
            bi->pending[v].active = 0; bi->pending[v].clocks_left = 0;
            if (count >= max_out) break;
        }
    }
    return count;
}

static int advance_pending_clocks(BeatBankInstance *bi, uint8_t out_msgs[][3], int out_lens[], int max_out, int count)
{
    for (int v = 0; v < BB_NUM_VOICES; v++) {
        PendingNoteOff *p = &bi->pending[v];
        if (!p->active) continue;
        if (p->clocks_left > 0) p->clocks_left--;
        if (p->clocks_left == 0) {
            count = emit((uint8_t)(MIDI_NOTE_OFF | OUT_CHANNEL), p->note, 0, out_msgs, out_lens, max_out, count);
            p->active = 0;
            if (count >= max_out) break;
        }
    }
    return count;
}

static int fire_step(BeatBankInstance *bi, uint8_t out_msgs[][3], int out_lens[], int max_out, int count)
{
    const BeatPattern *p = pattern_at(bi->pattern);
    uint8_t steps = pattern_steps(bi);
    uint8_t step = bi->cur_step;

    if (!p) return count;
    if (step >= steps) step = 0;

    for (int v = 0; v < BB_NUM_VOICES; v++) {
        const char *row = p->rows[v];
        PendingNoteOff *pn = &bi->pending[v];
        uint8_t vel;
        if (pn->active) {
            count = emit((uint8_t)(MIDI_NOTE_OFF | OUT_CHANNEL), pn->note, 0, out_msgs, out_lens, max_out, count);
            pn->active = 0;
            if (count >= max_out) return count;
        }
        if (!row[0]) continue;
        if (step >= (uint8_t)strlen(row)) continue;
        vel = bb_char_velocity(row[step]);
        if (vel == 0) continue;
        count = emit((uint8_t)(MIDI_NOTE_ON | OUT_CHANNEL), bi->note[v], vel, out_msgs, out_lens, max_out, count);
        if (count >= max_out) return count;
        pn->active = 1; pn->note = bi->note[v]; pn->clocks_left = GATE_CLOCKS;
    }
    bi->cur_step = (uint8_t)((step + 1) % steps);
    return count;
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

static void *bb_create_instance(const char *module_dir, const char *config_json)
{
    BeatBankInstance *bi;
    (void)config_json;

    if (!g_bank_loaded) {
        bb_bank_init(&g_bank, module_dir);
        for (int v = 0; v < BB_NUM_VOICES; v++)
            snprintf(g_note_keys[v], sizeof(g_note_keys[v]), "%s_note", bb_voice_keys[v]);
        g_bank_loaded = 1;
    }

    bi = (BeatBankInstance *)calloc(1, sizeof(BeatBankInstance));
    if (!bi) return NULL;

    bi->pattern = 0;
    /* Default to the 36..51 drum-rack map: most Schwung drum synths use it;
     * only SF2 wants GM. Users can switch back in Globals -> Note Map. */
    bi->note_map = 1;
    for (int v = 0; v < BB_NUM_VOICES; v++) bi->note[v] = kDrumrackNotes[v];
    bi->cur_step = 0;
    bi->clock_running = 0;
    bi->midi_clocks_until_tick = CLOCKS_PER_STEP;
    bi->preview_revision = 1;
    return bi;
}

static void bb_destroy_instance(void *instance) { free(instance); }

/* ── MIDI clock processing (loops the pattern, drives the slot synth) ─────── */

static int bb_process_midi(void *instance, const uint8_t *in_msg, int in_len,
                           uint8_t out_msgs[][3], int out_lens[], int max_out)
{
    BeatBankInstance *bi = (BeatBankInstance *)instance;
    if (!bi || in_len == 0) return 0;

    if (in_msg[0] == 0xFAu) {                     /* Start */
        int count = flush_all(bi, out_msgs, out_lens, max_out, 0);
        bi->cur_step = 0;
        bi->midi_clocks_until_tick = clocks_before_step(bi, 0);
        bi->clock_running = 1;
        return count;
    }
    if (in_msg[0] == 0xFBu) {                      /* Continue */
        bi->clock_running = 1;
        return 0;
    }
    if (in_msg[0] == 0xF8u) {                      /* Clock tick */
        int count = 0;
        if (!bi->clock_running) return 0;
        count = advance_pending_clocks(bi, out_msgs, out_lens, max_out, count);
        if (count >= max_out) return count;
        if (bi->midi_clocks_until_tick > 0) bi->midi_clocks_until_tick--;
        if (bi->midi_clocks_until_tick == 0) {
            count = fire_step(bi, out_msgs, out_lens, max_out, count);   /* loops */
            bi->midi_clocks_until_tick = clocks_before_step(bi, bi->cur_step);
        }
        return count;
    }
    if (in_msg[0] == 0xFCu) {                      /* Stop */
        bi->clock_running = 0;
        return flush_all(bi, out_msgs, out_lens, max_out, 0);
    }
    return 0;
}

/* ── Tick: stop cleanly if the clock goes away without a 0xFC ────────────── */

static int bb_tick(void *instance, int frames, int sample_rate,
                   uint8_t out_msgs[][3], int out_lens[], int max_out)
{
    BeatBankInstance *bi = (BeatBankInstance *)instance;
    (void)frames; (void)sample_rate;
    if (!bi) return 0;

    if (g_host && g_host->get_clock_status) {
        int status = g_host->get_clock_status();
        if ((status == MOVE_CLOCK_STATUS_STOPPED ||
             status == MOVE_CLOCK_STATUS_UNAVAILABLE) && bi->clock_running) {
            bi->clock_running = 0;
            return flush_all(bi, out_msgs, out_lens, max_out, 0);
        }
    }
    return 0;
}

/* ── Parameter I/O ───────────────────────────────────────────────────────── */

static int parse_int(const char *s, int lo, int hi, int dflt)
{
    int v;
    if (!s) return dflt;
    v = atoi(s);
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Read the integer after "key": in a small JSON blob (our own state format). */
static int json_field_int(const char *json, const char *quoted_key, int dflt)
{
    const char *p = json ? strstr(json, quoted_key) : NULL;
    if (!p) return dflt;
    p = strchr(p + strlen(quoted_key), ':');
    return p ? atoi(p + 1) : dflt;
}

static void apply_note_map(BeatBankInstance *bi, int drumrack)
{
    bi->note_map = (uint8_t)(drumrack ? 1 : 0);
    for (int v = 0; v < BB_NUM_VOICES; v++)
        bi->note[v] = drumrack ? kDrumrackNotes[v] : kGmNotes[v];
}

static void bb_set_param(void *instance, const char *key, const char *val)
{
    BeatBankInstance *bi = (BeatBankInstance *)instance;
    if (!bi || !key || !val) return;

    if (strcmp(key, "pattern") == 0) {
        int hi = g_bank.count > 0 ? g_bank.count - 1 : 0;
        int idx = parse_int(val, 0, hi, 0);
        if (idx != bi->pattern) {
            bi->pattern = idx;
            if (bi->cur_step >= pattern_steps(bi)) bi->cur_step = 0;
            bi->preview_revision++;
        }
        return;
    }
    if (strcmp(key, "swing") == 0) { bi->swing = (uint8_t)parse_int(val, 0, 100, 0); return; }
    if (strcmp(key, "note_map") == 0) {
        int m;
        if (strcmp(val, "gm") == 0)            m = 0;
        else if (strcmp(val, "drumrack") == 0) m = 1;
        else                                   m = (atoi(val) != 0) ? 1 : 0;
        apply_note_map(bi, m);
        return;
    }
    if (strcmp(key, "state") == 0) {          /* chain autosave / patch restore */
        int hi = g_bank.count > 0 ? g_bank.count - 1 : 0;
        bi->pattern = clampi(json_field_int(val, "\"pattern\"", bi->pattern), 0, hi);
        if (bi->cur_step >= pattern_steps(bi)) bi->cur_step = 0;
        bi->preview_revision++;
        bi->swing = (uint8_t)clampi(json_field_int(val, "\"swing\"", bi->swing), 0, 100);
        /* Only override the map if the blob carries one; else keep the default. */
        if (strstr(val, "note_map")) apply_note_map(bi, strstr(val, "drumrack") != NULL);
        return;
    }
    for (int v = 0; v < BB_NUM_VOICES; v++)
        if (strcmp(key, g_note_keys[v]) == 0) { bi->note[v] = (uint8_t)parse_int(val, 0, 127, kGmNotes[v]); return; }
}

static int indexed_key(const char *key, const char *prefix)
{
    size_t pl = strlen(prefix);
    if (strncmp(key, prefix, pl) != 0 || key[pl] != '@') return -1;
    return atoi(key + pl + 1);
}

static int bb_get_param(void *instance, const char *key, char *buf, int buf_len)
{
    BeatBankInstance *bi = (BeatBankInstance *)instance;
    const BeatPattern *p;
    int gi;
    if (!bi || !key || !buf || buf_len <= 0) return -1;
    p = pattern_at(bi->pattern);

    if (strcmp(key, "pattern") == 0)       return snprintf(buf, buf_len, "%d", bi->pattern);
    if (strcmp(key, "pattern_count") == 0) return snprintf(buf, buf_len, "%d", g_bank.count);
    if (strcmp(key, "pattern_name") == 0)  return snprintf(buf, buf_len, "%s", p ? p->name : "");
    if (strcmp(key, "pattern_label") == 0) return snprintf(buf, buf_len, "%s  %s", p ? p->name : "", p ? p->genre : "");
    if (strcmp(key, "pattern_genre") == 0) return snprintf(buf, buf_len, "%s", p ? p->genre : "");
    if (strcmp(key, "steps") == 0)         return snprintf(buf, buf_len, "%u", pattern_steps(bi));
    if (strcmp(key, "play_step") == 0)     return snprintf(buf, buf_len, "%u", bi->cur_step);
    if (strcmp(key, "playing") == 0)       return snprintf(buf, buf_len, "%u", bi->clock_running);
    if (strcmp(key, "preview_rev") == 0)   return snprintf(buf, buf_len, "%u", bi->preview_revision);
    if (strcmp(key, "note_map") == 0)      return snprintf(buf, buf_len, "%s", bi->note_map ? "drumrack" : "gm");
    if (strcmp(key, "swing") == 0)         return snprintf(buf, buf_len, "%u", bi->swing);
    if (strcmp(key, "state") == 0)
        return snprintf(buf, buf_len, "{\"pattern\":%d,\"swing\":%u,\"note_map\":\"%s\"}",
                        bi->pattern, bi->swing, bi->note_map ? "drumrack" : "gm");

    if (strcmp(key, "genre_list") == 0) {
        /* Bank is grouped by genre, so same-genre patterns are contiguous.
         * Emit "GENRE:count|GENRE:count|..." for the UI's genre navigation. */
        int off = 0, i = 0;
        while (i < g_bank.count && off < buf_len - 1) {
            const char *gn = g_bank.patterns[i].genre;
            int c = 0;
            while (i + c < g_bank.count && strcmp(g_bank.patterns[i + c].genre, gn) == 0) c++;
            off += snprintf(buf + off, buf_len - off, "%s%s:%d", i > 0 ? "|" : "", gn, c);
            i += c;
        }
        return off;
    }

    gi = indexed_key(key, "name");
    if (gi >= 0) { const BeatPattern *q = pattern_at(gi); return snprintf(buf, buf_len, "%s", q ? q->name : ""); }
    gi = indexed_key(key, "genre");
    if (gi >= 0) { const BeatPattern *q = pattern_at(gi); return snprintf(buf, buf_len, "%s", q ? q->genre : ""); }

    if (strncmp(key, "row", 3) == 0) {
        int v = atoi(key + 3);
        if (v >= 0 && v < BB_NUM_VOICES && p) return snprintf(buf, buf_len, "%s", p->rows[v]);
        return snprintf(buf, buf_len, "%s", "");
    }
    for (int v = 0; v < BB_NUM_VOICES; v++)
        if (strcmp(key, g_note_keys[v]) == 0) return snprintf(buf, buf_len, "%u", bi->note[v]);

    if (strcmp(key, "sync_warn") == 0) {
        if (g_host && g_host->get_clock_status) {
            int status = g_host->get_clock_status();
            if (status == MOVE_CLOCK_STATUS_UNAVAILABLE) return snprintf(buf, buf_len, "Enable MIDI Clock Out");
            if (status == MOVE_CLOCK_STATUS_STOPPED)     return snprintf(buf, buf_len, "press Play");
        }
        return snprintf(buf, buf_len, "%s", "");
    }
    return -1;
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

static midi_fx_api_v1_t g_api = {
    .api_version      = MIDI_FX_API_VERSION,
    .create_instance  = bb_create_instance,
    .destroy_instance = bb_destroy_instance,
    .process_midi     = bb_process_midi,
    .tick             = bb_tick,
    .set_param        = bb_set_param,
    .get_param        = bb_get_param,
};

midi_fx_api_v1_t *move_midi_fx_init(const host_api_v1_t *host)
{
    g_host = host;
    return &g_api;
}
