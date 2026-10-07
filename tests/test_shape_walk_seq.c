#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/overtake/dsp/shape_walk_seq.c"

static uint8_t injected[256][4];
static int injected_count;

static int capture_packet(const uint8_t *msg, int len) {
    assert(len == 4 && injected_count < 256);
    memcpy(injected[injected_count++], msg, 4);
    return 4;
}

int main(void) {
    host_api_v1_t host = {0};
    host.midi_inject_to_move = capture_packet;
    assert(move_plugin_init_v2(&host) != NULL);
    seq_t *s = seq_create(NULL, NULL);
    assert(s && s->generator);

    char first[64], saved[64];
    assert(seq_get_param(s, "candidate_0", first, sizeof(first)) > 0);
    seq_set_param(s, "slot_0", first);
    assert(seq_get_param(s, "slot_0", saved, sizeof(saved)) > 0);
    assert(!strcmp(saved, first));
    int voices = s->saved[0].count;
    assert(voices >= 4);

    seq_set_param(s, "channel", "3");
    seq_set_param(s, "press_left", "0");
    assert(injected_count == voices);
    for (int i = 0; i < injected_count; i++) {
        assert(injected[i][0] == 0x29);
        assert(injected[i][1] == 0x93);
    }
    /* Playing the same saved chord simultaneously must not cut shared notes. */
    seq_set_param(s, "press_right", "0");
    assert(injected_count == voices);
    seq_set_param(s, "release_left", "0");
    assert(injected_count == voices);
    seq_set_param(s, "release_right", "0");
    assert(injected_count == voices * 2);
    for (int i = voices; i < injected_count; i++) {
        assert(injected[i][0] == 0x28);
        assert(injected[i][1] == 0x83);
    }

    seq_set_param(s, "seed", "2000");
    assert(seq_get_param(s, "slot_0", saved, sizeof(saved)) > 0);
    assert(!strcmp(saved, first));
    injected_count = 0;
    seq_set_param(s, "press_right", "0");
    assert(injected_count == voices);
    seq_set_param(s, "release_all", "1");
    assert(injected_count == voices * 2);

    /* Editing a held pad changes only the notes that differ. */
    seq_set_param(s, "slot_1", "48,60,64,67");
    injected_count = 0;
    seq_set_param(s, "press_right", "1");
    assert(injected_count == 4);
    seq_set_param(s, "slot_1", "48,60,65,67");
    assert(injected_count == 6);
    assert(injected[4][0] == 0x28 && injected[4][2] == 64);
    assert(injected[5][0] == 0x29 && injected[5][2] == 65);
    seq_set_param(s, "release_right", "1");
    assert(injected_count == 10);

    /* Transport clocks never trigger the saved pads. */
    injected_count = 0;
    uint8_t clock = 0xf8;
    for (int i = 0; i < 24; i++) seq_on_midi(s, &clock, 1, MOVE_MIDI_SOURCE_EXTERNAL);
    assert(injected_count == 0);

    seq_set_param(s, "slot_0", "");
    assert(s->saved[0].count == 0);
    seq_destroy(s);
    puts("Shape Walk Pads DSP tests passed");
    return 0;
}
