/*
 * beatbank_plugin.c — Beat Bank: a library of fixed "standard" drum patterns.
 *
 * API: midi_fx_api_v1_t  (entry point: move_midi_fx_init)
 *
 * Bare-bones print tool. It plays nothing continuously. You browse the pattern
 * list (native Schwung menu); when you LAND on a pattern it "splats" one clean
 * bar into Move — i.e. it injects exactly one bar which Move records into the
 * armed track's clip. No loop, no preview, no note editing.
 *
 * Patterns load from external .beat files (see patterns.c). NO randomness.
 *
 * Selecting a pattern arms a short debounce; when it expires (you've settled on
 * one) it fires one bar. The bar itself steps on Move's MIDI clock (6 clocks
 * per 16th), so Move must be playing + recording to capture it.
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
#define GATE_CLOCKS     2u   /* note length in clocks (< one step) */
#define OUT_CHANNEL     0u   /* the chain/slot rewrites the channel on output */

#define OUTQ_SIZE       128u
#define PRINT_DEBOUNCE_TICKS 50   /* ~145 ms of "settled on a pattern" */

static char g_note_keys[BB_NUM_VOICES][16];

static const host_api_v1_t *g_host = NULL;

/* The pattern bank is read-only and shared across all instances. */
static BeatBank g_bank = { NULL, 0 };
static int      g_bank_loaded = 0;

typedef struct { uint8_t status, d1, d2; } OutEvent;

typedef struct {
    uint8_t active;
    uint8_t note;
    uint8_t clocks_left;
} PendingNoteOff;

typedef struct {
    int     pattern;                 /* selected index into the bank        */
    uint8_t note[BB_NUM_VOICES];     /* output note per voice (GM defaults)  */

    uint8_t cur_step;
    uint8_t clock_running;           /* Move transport running               */
    uint8_t midi_clocks_until_tick;

    int     print_remaining;         /* >0 while a one-shot bar is playing   */
    int     print_debounce;          /* >0: counting down to auto-splat      */
    uint8_t auto_print;              /* 1 = selecting a pattern auto-splats   */

    uint32_t preview_revision;

    PendingNoteOff pending[BB_NUM_VOICES];
    OutEvent outq[OUTQ_SIZE];
    unsigned outq_head, outq_tail;
} BeatBankInstance;

/* ── Emit queue ──────────────────────────────────────────────────────────── */

static void q_push(BeatBankInstance *bi, uint8_t status, uint8_t d1, uint8_t d2)
{
    unsigned next = (bi->outq_tail + 1u) % OUTQ_SIZE;
    if (next == bi->outq_head) return;
    bi->outq[bi->outq_tail].status = status;
    bi->outq[bi->outq_tail].d1 = d1;
    bi->outq[bi->outq_tail].d2 = d2;
    bi->outq_tail = next;
}
static void q_note_on(BeatBankInstance *bi, uint8_t note, uint8_t vel)  { q_push(bi, (uint8_t)(MIDI_NOTE_ON  | OUT_CHANNEL), note, vel); }
static void q_note_off(BeatBankInstance *bi, uint8_t note)             { q_push(bi, (uint8_t)(MIDI_NOTE_OFF | OUT_CHANNEL), note, 0); }

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

/* ── Sequencing ──────────────────────────────────────────────────────────── */

static void flush_all(BeatBankInstance *bi)
{
    for (int v = 0; v < BB_NUM_VOICES; v++) {
        if (bi->pending[v].active) {
            q_note_off(bi, bi->pending[v].note);
            bi->pending[v].active = 0;
            bi->pending[v].clocks_left = 0;
        }
    }
}

static void advance_pending_clocks(BeatBankInstance *bi)
{
    for (int v = 0; v < BB_NUM_VOICES; v++) {
        PendingNoteOff *p = &bi->pending[v];
        if (!p->active) continue;
        if (p->clocks_left > 0) p->clocks_left--;
        if (p->clocks_left == 0) { q_note_off(bi, p->note); p->active = 0; }
    }
}

static void fire_step(BeatBankInstance *bi)
{
    const BeatPattern *p = pattern_at(bi->pattern);
    uint8_t steps = pattern_steps(bi);
    uint8_t step = bi->cur_step;

    if (!p) return;
    if (step >= steps) step = 0;

    for (int v = 0; v < BB_NUM_VOICES; v++) {
        const char *row = p->rows[v];
        PendingNoteOff *pn = &bi->pending[v];
        uint8_t vel;
        if (pn->active) { q_note_off(bi, pn->note); pn->active = 0; }
        if (!row[0]) continue;
        if (step >= (uint8_t)strlen(row)) continue;
        vel = bb_char_velocity(row[step]);
        if (vel == 0) continue;
        q_note_on(bi, bi->note[v], vel);
        pn->active = 1; pn->note = bi->note[v]; pn->clocks_left = GATE_CLOCKS;
    }
    bi->cur_step = (uint8_t)((step + 1) % steps);
}

/* Start a one-shot bar of the current pattern. */
static void trigger_print(BeatBankInstance *bi)
{
    flush_all(bi);
    bi->cur_step = 0;
    bi->midi_clocks_until_tick = 1;   /* fire step 0 on the next clock */
    bi->print_remaining = pattern_steps(bi);
    bi->print_debounce = 0;
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
    for (int v = 0; v < BB_NUM_VOICES; v++) bi->note[v] = bb_default_notes[v];
    bi->cur_step = 0;
    bi->clock_running = 0;
    bi->midi_clocks_until_tick = CLOCKS_PER_STEP;
    bi->print_remaining = 0;
    bi->print_debounce = 0;
    bi->auto_print = 1;
    bi->preview_revision = 1;
    bi->outq_head = bi->outq_tail = 0;
    return bi;
}

static void bb_destroy_instance(void *instance) { free(instance); }

/* ── MIDI clock processing ───────────────────────────────────────────────── */

static int bb_process_midi(void *instance, const uint8_t *in_msg, int in_len,
                           uint8_t out_msgs[][3], int out_lens[], int max_out)
{
    BeatBankInstance *bi = (BeatBankInstance *)instance;
    (void)out_msgs; (void)out_lens; (void)max_out;
    if (!bi || in_len == 0) return 0;

    if (in_msg[0] == 0xFAu || in_msg[0] == 0xFBu) {       /* Start / Continue */
        bi->clock_running = 1;
    } else if (in_msg[0] == 0xF8u) {                      /* Clock tick */
        if (!bi->clock_running) return 0;
        advance_pending_clocks(bi);
        if (bi->midi_clocks_until_tick > 0) bi->midi_clocks_until_tick--;
        if (bi->midi_clocks_until_tick == 0) {
            if (bi->print_remaining > 0) { fire_step(bi); bi->print_remaining--; }
            bi->midi_clocks_until_tick = CLOCKS_PER_STEP;
        }
    } else if (in_msg[0] == 0xFCu) {                      /* Stop */
        bi->clock_running = 0;
        bi->print_remaining = 0;
        flush_all(bi);
    }
    return 0;   /* all output is emitted from tick() */
}

/* ── Tick: debounce auto-splat + drain queue ─────────────────────────────── */

static int bb_tick(void *instance, int frames, int sample_rate,
                   uint8_t out_msgs[][3], int out_lens[], int max_out)
{
    BeatBankInstance *bi = (BeatBankInstance *)instance;
    int count = 0;
    (void)frames; (void)sample_rate;
    if (!bi) return 0;

    if (g_host && g_host->get_clock_status) {
        int status = g_host->get_clock_status();
        if ((status == MOVE_CLOCK_STATUS_STOPPED ||
             status == MOVE_CLOCK_STATUS_UNAVAILABLE) && bi->clock_running) {
            bi->clock_running = 0;
            bi->print_remaining = 0;
            flush_all(bi);
        }
    }

    if (bi->print_debounce > 0) {
        bi->print_debounce--;
        if (bi->print_debounce == 0) trigger_print(bi);
    }

    while (bi->outq_head != bi->outq_tail && count < max_out) {
        OutEvent *e = &bi->outq[bi->outq_head];
        out_msgs[count][0] = e->status; out_msgs[count][1] = e->d1; out_msgs[count][2] = e->d2;
        out_lens[count] = 3;
        count++;
        bi->outq_head = (bi->outq_head + 1u) % OUTQ_SIZE;
    }
    return count;
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
            if (bi->auto_print) bi->print_debounce = PRINT_DEBOUNCE_TICKS; /* land -> splat */
        }
        return;
    }
    if (strcmp(key, "print") == 0)      { trigger_print(bi); return; }
    if (strcmp(key, "auto_print") == 0) { bi->auto_print = (uint8_t)(parse_int(val, 0, 1, 1) != 0); return; }

    for (int v = 0; v < BB_NUM_VOICES; v++)
        if (strcmp(key, g_note_keys[v]) == 0) { bi->note[v] = (uint8_t)parse_int(val, 0, 127, bb_default_notes[v]); return; }
}

static int bb_get_param(void *instance, const char *key, char *buf, int buf_len)
{
    BeatBankInstance *bi = (BeatBankInstance *)instance;
    const BeatPattern *p;
    if (!bi || !key || !buf || buf_len <= 0) return -1;
    p = pattern_at(bi->pattern);

    if (strcmp(key, "pattern") == 0)       return snprintf(buf, buf_len, "%d", bi->pattern);
    if (strcmp(key, "pattern_count") == 0) return snprintf(buf, buf_len, "%d", g_bank.count);
    if (strcmp(key, "pattern_name") == 0)  return snprintf(buf, buf_len, "%s", p ? p->name : "");
    if (strcmp(key, "pattern_label") == 0) return snprintf(buf, buf_len, "%s  %s", p ? p->name : "", p ? p->genre : "");
    if (strcmp(key, "pattern_genre") == 0) return snprintf(buf, buf_len, "%s", p ? p->genre : "");
    if (strcmp(key, "steps") == 0)         return snprintf(buf, buf_len, "%u", pattern_steps(bi));
    if (strcmp(key, "printing") == 0)      return snprintf(buf, buf_len, "%d", bi->print_remaining > 0 ? 1 : 0);
    if (strcmp(key, "preview_rev") == 0)   return snprintf(buf, buf_len, "%u", bi->preview_revision);

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
