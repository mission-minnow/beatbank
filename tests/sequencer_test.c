/*
 * sequencer_test.c — drive the Beat Bank plugin and verify note placement.
 * Loads a real .beat file via the bank loader (module_dir).
 *
 * Beat Bank is a print tool: it emits only a one-shot bar, triggered either
 * explicitly (set_param "print") or by settling on a pattern (debounced
 * auto-splat). Note events are emitted from tick(), so we pump tick() after
 * each clock (as the chain does).
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

/* Assumes clock is running. Runs nclocks of 0xF8 (+ tick each), recording the
 * clock number of each note-on matching match_note (<0 = any). */
static int run_clocks(midi_fx_api_v1_t *api, void *inst, int match, int *clk, int maxc, int nclocks)
{
    uint8_t out[MIDI_FX_MAX_OUT_MSGS][3];
    int lens[MIDI_FX_MAX_OUT_MSGS];
    uint8_t b; int count = 0;
    for (int c = 1; c <= nclocks; c++) {
        b = 0xF8; api->process_midi(inst, &b, 1, out, lens, MIDI_FX_MAX_OUT_MSGS);
        int n = api->tick(inst, 128, 44100, out, lens, MIDI_FX_MAX_OUT_MSGS);
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

    /* Isolate explicit print for the timing checks. */
    api->set_param(inst, "auto_print", "0");
    b = 0xFA; api->process_midi(inst, &b, 1, out, lens, MIDI_FX_MAX_OUT_MSGS);
    api->tick(inst, 128, 44100, out, lens, MIDI_FX_MAX_OUT_MSGS);

    printf("\nExplicit print = exactly one bar\n");
    api->set_param(inst, "pattern", "0");     /* All Hits */
    api->set_param(inst, "print", "1");
    int n = run_clocks(api, inst, -1, clk, 64, 120);
    CHECK(n == 16, "16 step-fires (one bar)");
    CHECK(n >= 3 && clk[0] == 1 && clk[1] == 7 && clk[2] == 13, "steps at clocks 1,7,13...");
    CHECK(n == 16 && clk[15] == 91, "last step at clock 91");

    printf("\nKick lands on the four beats\n");
    api->set_param(inst, "print", "1");
    int nk = run_clocks(api, inst, 36, clk, 64, 120);
    CHECK(nk == 4 && clk[0] == 1 && clk[1] == 25 && clk[2] == 49 && clk[3] == 73,
          "kick at clocks 1,25,49,73 (beats 1-4)");

    printf("\nPattern switch -> snare backbeat\n");
    api->set_param(inst, "pattern", "1");     /* Backbeat (auto_print off = no splat) */
    api->set_param(inst, "print", "1");
    int ns = run_clocks(api, inst, 38, clk, 64, 120);
    CHECK(ns == 2 && clk[0] == 25 && clk[1] == 73, "snare at clocks 25,73 (beats 2 & 4)");

    printf("\nStop flushes held notes\n");
    api->set_param(inst, "print", "1");
    run_clocks(api, inst, -1, clk, 64, 2);    /* fire step 0 (a note is held) */
    b = 0xFC; api->process_midi(inst, &b, 1, out, lens, MIDI_FX_MAX_OUT_MSGS);
    int fn = api->tick(inst, 128, 44100, out, lens, MIDI_FX_MAX_OUT_MSGS);
    int offs = 0;
    for (int i = 0; i < fn; i++)
        if ((out[i][0] & 0xF0) == 0x80 || (out[i][2] == 0)) offs++;
    CHECK(offs >= 1, "stop emits note-off(s)");

    printf("\nSelecting a pattern auto-splats one bar\n");
    api->set_param(inst, "auto_print", "1");
    b = 0xFA; api->process_midi(inst, &b, 1, out, lens, MIDI_FX_MAX_OUT_MSGS);
    api->tick(inst, 128, 44100, out, lens, MIDI_FX_MAX_OUT_MSGS);
    api->set_param(inst, "pattern", "0");     /* change 1 -> 0 arms the debounce */
    int nd = run_clocks(api, inst, -1, clk, 64, 160);  /* debounce (~50) then one bar */
    CHECK(nd == 16, "auto-splat fires exactly one bar after settling");

    api->destroy_instance(inst);
    if (failures) { printf("\nFAIL: %d check(s) failed\n", failures); return 1; }
    printf("\nOK: sequencer behaves correctly\n");
    return 0;
}
