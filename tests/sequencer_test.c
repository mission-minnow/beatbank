/*
 * sequencer_test.c — drive the Beat Bank plugin through MIDI clock and verify
 * it loops the pattern and places notes correctly. Loads a real .beat file via
 * the bank loader (module_dir).
 *
 * Beat Bank emits its notes directly from process_midi (on the clock), which
 * the chain feeds to the slot synth. Straight 16ths: a step every 6 clocks,
 * with step 0 firing on the Start message (0xFA) itself — the downbeat lands
 * with Move's transport, not a 16th behind it.
 */

#include "midi_fx_api_v1.h"
#include "plugin_api_v1.h"
#include "patterns.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

midi_fx_api_v1_t *move_midi_fx_init(const host_api_v1_t *host);

static host_api_v1_t g_host = { .api_version = 1, .sample_rate = 44100 };

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  !! FAIL: %s\n", msg); failures++; } \
    else         { printf("  ok: %s\n", msg); } \
} while (0)

static const char *MODROOT = "build/native/test_modroot";

static void write_test_patterns(void)
{
    mkdir("build", 0775); mkdir("build/native", 0775); mkdir(MODROOT, 0775);
    mkdir("build/native/test_modroot/patterns", 0775);
    FILE *fp = fopen("build/native/test_modroot/patterns/00-test.beat", "wb");
    if (!fp) { printf("  !! FAIL: cannot write test .beat\n"); failures++; return; }
    fputs(
        "name: All Hits\n" "genre: TEST\n" "steps: 16\n"
        "ch:    xxxxxxxxxxxxxxxx\n" "kick:  x...x...x...x...\n\n"
        "name: Backbeat\n" "genre: TEST\n" "steps: 16\n"
        "kick:  x...x...x...x...\n" "snare: ....A.......A...\n",
        fp);
    fclose(fp);
}

/* Send 0xFA (start), then nclocks of 0xF8; record the clock number of each
 * note-on matching match_note (<0 = any) from process_midi's output. Step 0
 * fires on the Start message itself (the downbeat), recorded as clock 0. */
static int run_bar(midi_fx_api_v1_t *api, void *inst, int match, int *clk, int maxc, int nclocks)
{
    uint8_t out[MIDI_FX_MAX_OUT_MSGS][3];
    int lens[MIDI_FX_MAX_OUT_MSGS];
    uint8_t b; int count = 0;
    b = 0xFA;
    int n0 = api->process_midi(inst, &b, 1, out, lens, MIDI_FX_MAX_OUT_MSGS);
    for (int i = 0; i < n0; i++)
        if ((out[i][0] & 0xF0) == 0x90 && out[i][2] > 0 && (match < 0 || out[i][1] == match)) {
            if (count < maxc) clk[count] = 0; count++; break;
        }
    for (int c = 1; c <= nclocks; c++) {
        b = 0xF8;
        int n = api->process_midi(inst, &b, 1, out, lens, MIDI_FX_MAX_OUT_MSGS);
        for (int i = 0; i < n; i++)
            if ((out[i][0] & 0xF0) == 0x90 && out[i][2] > 0 && (match < 0 || out[i][1] == match)) {
                if (count < maxc) clk[count] = c; count++; break;
            }
    }
    return count;
}

int main(void)
{
    write_test_patterns();
    midi_fx_api_v1_t *api = move_midi_fx_init(&g_host);
    int clk[64];
    uint8_t out[MIDI_FX_MAX_OUT_MSGS][3]; int lens[MIDI_FX_MAX_OUT_MSGS]; uint8_t b;

    void *inst = api->create_instance(MODROOT, NULL);
    char buf[32];
    int count = (api->get_param(inst, "pattern_count", buf, sizeof(buf)), atoi(buf));
    printf("Loaded %d patterns\n", count);
    CHECK(count >= 2, "loaded the two test patterns");

    api->set_param(inst, "pattern", "0");     /* All Hits: every step fires */

    /* Steps after the downbeat fire BB_RECORD_ALIGN_CLOCKS late (record-align).
     * Keep in sync with the plugin. Downbeat stays on the 0xFA (clock 0). */
    const int RA = 1;

    printf("\nStraight 16ths: downbeat on 0xFA, off-beats +%d (record-align)\n", RA);
    int ns = run_bar(api, inst, -1, clk, 64, 90 + RA);
    CHECK(ns == 16, "16 step-fires in one bar");
    CHECK(ns >= 3 && clk[0] == 0 && clk[1] == 6 + RA && clk[2] == 12 + RA,
          "step 0 on Start (0), off-beats +RA");
    CHECK(ns == 16 && clk[15] == 90 + RA, "step 15 at clock 90+RA");

    printf("\nKick lands on the four beats\n");
    int nk = run_bar(api, inst, 36, clk, 64, 90 + RA);
    CHECK(nk == 4 && clk[0] == 0 && clk[1] == 24 + RA && clk[2] == 48 + RA && clk[3] == 72 + RA,
          "kick at 0, 24+RA, 48+RA, 72+RA");

    printf("\nLoops continuously (two bars = 32 fires)\n");
    int n2 = run_bar(api, inst, -1, clk, 64, 186 + RA);
    CHECK(n2 == 32, "32 step-fires across two bars");
    CHECK(n2 == 32 && clk[16] == 96 + RA, "bar 2 downbeat at clock 96+RA");

    printf("\nSwing delays the off-beat 16ths\n");
    api->set_param(inst, "swing", "100");
    int sw = run_bar(api, inst, -1, clk, 64, 95 + RA);
    CHECK(sw == 16, "swing keeps 16 fires");
    CHECK(sw >= 4 && clk[0] == 0 && clk[1] == 8 + RA && clk[2] == 12 + RA && clk[3] == 20 + RA,
          "downbeat on grid, off-beats swung late (+RA)");
    CHECK(sw == 16 && clk[15] == 92 + RA, "last off-beat 16th at clock 92+RA");
    api->set_param(inst, "swing", "0");

    printf("\nPattern switch -> snare backbeat\n");
    api->set_param(inst, "pattern", "1");
    int nsn = run_bar(api, inst, 38, clk, 64, 90 + RA);
    CHECK(nsn == 2 && clk[0] == 24 + RA && clk[1] == 72 + RA, "snare at 24+RA, 72+RA (beats 2 & 4)");

    printf("\nStop flushes held notes\n");
    api->set_param(inst, "pattern", "0");     /* All Hits: step 0 always has notes */
    b = 0xFA; api->process_midi(inst, &b, 1, out, lens, MIDI_FX_MAX_OUT_MSGS); /* step 0 fires on Start, held */
    b = 0xF8; api->process_midi(inst, &b, 1, out, lens, MIDI_FX_MAX_OUT_MSGS); /* 1 clock < gate: still held */
    b = 0xFC;
    int fn = api->process_midi(inst, &b, 1, out, lens, MIDI_FX_MAX_OUT_MSGS);
    int offs = 0;
    for (int i = 0; i < fn; i++)
        if ((out[i][0] & 0xF0) == 0x80 || (out[i][2] == 0)) offs++;
    CHECK(offs >= 1, "stop emits note-off(s)");

    printf("\nNote map: drumrack (36-51) default vs gm (full range)\n");
    api->get_param(inst, "note_map", buf, sizeof(buf));
    CHECK(strcmp(buf, "drumrack") == 0, "defaults to drumrack");
    api->get_param(inst, "cowbell_note", buf, sizeof(buf)); int cb = atoi(buf);
    api->get_param(inst, "perc_note", buf, sizeof(buf));    int pc = atoi(buf);
    CHECK(cb == 47 && pc == 50, "drumrack: cowbell/perc in 36-51 (47,50)");
    api->set_param(inst, "note_map", "gm");
    api->get_param(inst, "cowbell_note", buf, sizeof(buf));
    CHECK(atoi(buf) == 56, "gm: cowbell = 56 (out of 36-51)");
    api->set_param(inst, "note_map", "drumrack");
    api->get_param(inst, "cowbell_note", buf, sizeof(buf));
    CHECK(atoi(buf) == 47, "switching back to drumrack restores 47");

    printf("\nState round-trips (pattern + swing + note_map + pad edits)\n");
    api->set_param(inst, "pattern", "1");
    api->set_param(inst, "swing", "40");
    api->set_param(inst, "note_map", "drumrack");
    api->set_param(inst, "kick_note", "41");   /* a pad remap done in Pattern view */
    char st[256]; api->get_param(inst, "state", st, sizeof(st));
    void *inst2 = api->create_instance(MODROOT, NULL);   /* fresh, defaults */
    api->set_param(inst2, "state", st);
    api->get_param(inst2, "pattern", buf, sizeof(buf));  int rp = atoi(buf);
    api->get_param(inst2, "swing", buf, sizeof(buf));    int rs = atoi(buf);
    api->get_param(inst2, "note_map", buf, sizeof(buf));
    CHECK(rp == 1 && rs == 40 && strcmp(buf, "drumrack") == 0,
          "restored state matches (pattern 1, swing 40, drumrack)");
    api->get_param(inst2, "kick_note", buf, sizeof(buf));
    CHECK(atoi(buf) == 41, "per-voice pad edit persists through state");
    api->destroy_instance(inst2);

    api->destroy_instance(inst);
    if (failures) { printf("\nFAIL: %d check(s) failed\n", failures); return 1; }
    printf("\nOK: sequencer behaves correctly\n");
    return 0;
}
