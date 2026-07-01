/*
 * sequencer_test.c — drive the Beat Bank plugin through MIDI clock and verify
 * it loops the pattern and places notes correctly. Loads a real .beat file via
 * the bank loader (module_dir).
 *
 * Beat Bank emits its notes directly from process_midi (on the clock), which
 * the chain feeds to the slot synth. Straight 16ths: a step every 6 clocks,
 * step 0 firing 6 clocks after Start (0xFA).
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
 * note-on matching match_note (<0 = any) from process_midi's output. */
static int run_bar(midi_fx_api_v1_t *api, void *inst, int match, int *clk, int maxc, int nclocks)
{
    uint8_t out[MIDI_FX_MAX_OUT_MSGS][3];
    int lens[MIDI_FX_MAX_OUT_MSGS];
    uint8_t b; int count = 0;
    b = 0xFA; api->process_midi(inst, &b, 1, out, lens, MIDI_FX_MAX_OUT_MSGS);
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

    printf("\nStraight 16ths: a step every 6 clocks\n");
    int ns = run_bar(api, inst, -1, clk, 64, 96);
    CHECK(ns == 16, "16 step-fires in one bar");
    CHECK(ns >= 3 && clk[0] == 6 && clk[1] == 12 && clk[2] == 18, "fires at clocks 6,12,18...");
    CHECK(ns == 16 && clk[15] == 96, "bar ends at clock 96");

    printf("\nKick lands on the four beats\n");
    int nk = run_bar(api, inst, 36, clk, 64, 96);
    CHECK(nk == 4 && clk[0] == 6 && clk[1] == 30 && clk[2] == 54 && clk[3] == 78,
          "kick at clocks 6,30,54,78");

    printf("\nLoops continuously (two bars = 32 fires)\n");
    int n2 = run_bar(api, inst, -1, clk, 64, 192);
    CHECK(n2 == 32, "32 step-fires across two bars");

    printf("\nSwing delays the off-beat 16ths\n");
    api->set_param(inst, "swing", "100");
    int sw = run_bar(api, inst, -1, clk, 64, 96);
    CHECK(sw == 16, "swing keeps 16 fires");
    CHECK(sw >= 4 && clk[0] == 4 && clk[1] == 12 && clk[2] == 16 && clk[3] == 24,
          "swung off-beats late (clocks 4,12,16,24)");
    CHECK(sw == 16 && clk[15] == 96, "swung bar still ends at clock 96");
    api->set_param(inst, "swing", "0");

    printf("\nPattern switch -> snare backbeat\n");
    api->set_param(inst, "pattern", "1");
    int nsn = run_bar(api, inst, 38, clk, 64, 96);
    CHECK(nsn == 2 && clk[0] == 30 && clk[1] == 78, "snare at clocks 30,78 (beats 2 & 4)");

    printf("\nStop flushes held notes\n");
    b = 0xFA; api->process_midi(inst, &b, 1, out, lens, MIDI_FX_MAX_OUT_MSGS);
    for (int c = 1; c <= 6; c++) { b = 0xF8; api->process_midi(inst, &b, 1, out, lens, MIDI_FX_MAX_OUT_MSGS); } /* step 0 fires */
    b = 0xFC;
    int fn = api->process_midi(inst, &b, 1, out, lens, MIDI_FX_MAX_OUT_MSGS);
    int offs = 0;
    for (int i = 0; i < fn; i++)
        if ((out[i][0] & 0xF0) == 0x80 || (out[i][2] == 0)) offs++;
    CHECK(offs >= 1, "stop emits note-off(s)");

    printf("\nNote map: gm (full range) vs drumrack (36-51)\n");
    api->get_param(inst, "note_map", buf, sizeof(buf));
    CHECK(strcmp(buf, "gm") == 0, "defaults to gm");
    api->get_param(inst, "cowbell_note", buf, sizeof(buf));
    CHECK(atoi(buf) == 56, "gm: cowbell = 56 (out of 36-51)");
    api->set_param(inst, "note_map", "drumrack");
    api->get_param(inst, "cowbell_note", buf, sizeof(buf)); int cb = atoi(buf);
    api->get_param(inst, "perc_note", buf, sizeof(buf));    int pc = atoi(buf);
    CHECK(cb == 47 && pc == 50, "drumrack: cowbell/perc moved into 36-51 (47,50)");
    api->set_param(inst, "note_map", "gm");
    api->get_param(inst, "cowbell_note", buf, sizeof(buf));
    CHECK(atoi(buf) == 56, "switching back to gm restores 56");

    printf("\nState round-trips (pattern + swing + note_map)\n");
    api->set_param(inst, "pattern", "1");
    api->set_param(inst, "swing", "40");
    api->set_param(inst, "note_map", "drumrack");
    char st[128]; api->get_param(inst, "state", st, sizeof(st));
    void *inst2 = api->create_instance(MODROOT, NULL);   /* fresh, defaults */
    api->set_param(inst2, "state", st);
    api->get_param(inst2, "pattern", buf, sizeof(buf));  int rp = atoi(buf);
    api->get_param(inst2, "swing", buf, sizeof(buf));    int rs = atoi(buf);
    api->get_param(inst2, "note_map", buf, sizeof(buf));
    CHECK(rp == 1 && rs == 40 && strcmp(buf, "drumrack") == 0,
          "restored state matches (pattern 1, swing 40, drumrack)");
    api->destroy_instance(inst2);

    api->destroy_instance(inst);
    if (failures) { printf("\nFAIL: %d check(s) failed\n", failures); return 1; }
    printf("\nOK: sequencer behaves correctly\n");
    return 0;
}
