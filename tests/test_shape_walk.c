#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/dsp/shape_walk.c"

static int mock_clock_status = MOVE_CLOCK_STATUS_STOPPED;
static int test_clock_status(void) { return mock_clock_status; }

static int send(shape_walk_t *s, uint8_t a, uint8_t b, uint8_t c,
                uint8_t out[][3], int lens[]) {
    uint8_t in[3] = {a, b, c};
    return process_midi(s, in, (a >= 0xf8 ? 1 : 3), out, lens, 16);
}

int main(void) {
    host_api_v1_t host = {0};
    host.get_clock_status = test_clock_status;
    assert(move_midi_fx_init(&host) != NULL);
    shape_walk_t *a = create_instance(NULL, NULL);
    shape_walk_t *b = create_instance(NULL, NULL);
    shape_walk_t *c = create_instance(NULL, NULL);
    assert(a && b && c);
    int major_notes[4] = {60, 65, 67, 74};
    int minor_notes[4] = {60, 65, 67, 74};
    int no_bass = -1;
    int open_roughness = chord_roughness(major_notes, 4);
    assert(tonalize(major_notes, &no_bass, 1, 0));
    assert(major_notes[1] == 64);
    assert(chord_roughness(major_notes, 4) <= open_roughness);
    assert(tonalize(minor_notes, &no_bass, 2, 0));
    int minor_third = 0;
    for (int i = 1; i < 4; i++) minor_third +=
        (minor_notes[i] - minor_notes[0] + 120) % 12 == 3;
    assert(minor_third);
    assert(chord_roughness(minor_notes, 4) <= open_roughness);
    assert(harsh_intervals(minor_notes, 4) == 0);
    char root_name[16];
    for (int i = 0; i < 12; i++) {
        set_param(c, "root", note_names[i]);
        assert(c->root == 60 + i);
        assert(get_param(c, "root", root_name, sizeof(root_name)) > 0);
        assert(!strcmp(root_name, note_names[i]));
    }
    set_param(c, "state", "{\"root\":54}");
    assert(c->root == 66);
    set_param(c, "state", "{\"root\":60}");
    assert(c->root == 60);
    assert(c->bass_offset == -12);
    set_param(c, "bass_mode", "follow");
    assert(c->steps[0].notes[0] == 48);
    set_param(c, "bass_offset", "-24");
    assert(c->steps[0].notes[0] == 36);
    set_param(c, "bass_offset", "-14");
    assert(c->steps[0].notes[0] == 46);
    set_param(c, "state", "{\"bass_mode\":0,\"colour\":0,\"wander\":0,\"split\":0,\"distance\":0}");
    assert(c->steps[0].notes[2] - c->steps[0].notes[1] < 7);
    set_param(c, "hand_gap", "7");
    for (int i = 0; i < c->length; i++) {
        assert(c->steps[i].count == 4);
        assert(c->steps[i].notes[2] - c->steps[i].notes[1] >= 7);
    }
    int original_roughness = 0, smoothed_roughness = 0;
    for (int seed = 0; seed < 100; seed++) {
        char settings[192];
        snprintf(settings, sizeof(settings),
            "{\"seed\":%d,\"left\":2,\"right\":2,\"bass_mode\":2,\"colour\":0,\"hand_gap\":7,\"smoothness\":0}", seed);
        set_param(c, "state", settings);
        for (int i = 0; i < c->length; i++) {
            assert(c->steps[i].count == 5);
            int notes[5];
            for (int j = 0; j < 5; j++) notes[j] = c->steps[i].notes[j];
            original_roughness += chord_roughness(notes, 5);
        }
        set_param(c, "smoothness", "100");
        for (int i = 0; i < c->length; i++) {
            assert(c->steps[i].count == 5);
            int notes[5];
            for (int j = 0; j < 5; j++) notes[j] = c->steps[i].notes[j];
            smoothed_roughness += chord_roughness(notes, 5);
        }
    }
    assert(smoothed_roughness < original_roughness / 4);
    set_param(c, "colour", "100");
    for (int i = 0; i < c->length; i++) assert(c->steps[i].count == 5);
    for (int i = 0; i < 8; i++) {
        assert(a->steps[i].count == b->steps[i].count);
        assert(!memcmp(a->steps[i].notes, b->steps[i].notes, a->steps[i].count));
    }

    set_param(a, "state", "{\"wander\":0,\"split\":0,\"colour\":0}");
    assert(a->steps[0].count == 4);
    const int positions[4] = {0, -5, -2, -7};
    for (int step = 0; step < 4; step++)
        for (int note = 0; note < 4; note++)
            assert(a->steps[step].notes[note] ==
                   a->steps[0].notes[note] + positions[step]);

    set_param(a, "state", "{\"seed\":42,\"wander\":80,\"split\":75,\"colour\":100}");
    set_param(b, "state", "{\"seed\":42,\"wander\":80,\"split\":75,\"colour\":100}");
    for (int i = 0; i < 8; i++) {
        assert(a->steps[i].count == b->steps[i].count);
        assert(!memcmp(a->steps[i].notes, b->steps[i].notes, a->steps[i].count));
        assert(a->steps[i].count <= 5);
        assert(a->steps[i].count == 5);
        for (int n = 1; n < a->steps[i].count; n++)
            assert(a->steps[i].notes[n] > a->steps[i].notes[n - 1]);
    }

    char state[1024];
    assert(get_param(a, "state", state, sizeof(state)) > 0);
    set_param(b, "seed", "1");
    set_param(b, "state", state);
    for (int i = 0; i < 8; i++)
        assert(!memcmp(&a->steps[i], &b->steps[i], sizeof(chord_t)));

    uint8_t out[16][3]; int lens[16];
    int count = send(a, 0x90, 48, 100, out, lens);
    assert(count == a->steps[0].count);
    for (int i = 0; i < count; i++) assert((out[i][0] & 0xf0) == 0x90);
    int previous = count;
    count = send(a, 0x90, 49, 100, out, lens);
    assert(count == previous + a->steps[1].count);
    for (int i = 0; i < previous; i++) assert((out[i][0] & 0xf0) == 0x80);
    assert(send(a, 0x80, 48, 0, out, lens) == 0);
    count = send(a, 0x80, 49, 0, out, lens);
    assert(count == a->steps[1].count);

    set_param(a, "mode", "clock");
    count = send(a, 0xfa, 0, 0, out, lens);
    assert(count == a->steps[0].count);
    for (int i = 0; i < 23; i++) assert(send(a, 0xf8, 0, 0, out, lens) == 0);
    count = send(a, 0xf8, 0, 0, out, lens);
    assert(count == a->steps[0].count + a->steps[1].count);
    count = send(a, 0xfc, 0, 0, out, lens);
    assert(count == a->steps[1].count);

    set_param(a, "mode", "pads");
    mock_clock_status = MOVE_CLOCK_STATUS_RUNNING;
    set_param(a, "mode", "clock");
    assert(a->running);
    count = tick(a, 128, 44100, out, lens, 16);
    assert(count == a->steps[0].count);
    chord_t playing = a->sounding;
    for (int i = 0; i < 10; i++) assert(send(a, 0xf8, 0, 0, out, lens) == 0);
    count = send(a, 0xf8, 0, 0, out, lens);
    assert(count == 0 && a->clock_count == 11);
    set_param(a, "seed", "43");
    set_param(a, "clock_rate", "1.5 beats");
    set_param(a, "sustain", "50");
    assert(a->running && a->clock_count == 11 && a->clock_step == 0);
    assert(a->active_period == 24 && a->active_gate == 24);
    assert(!memcmp(&a->sounding, &playing, sizeof(chord_t)));
    count = tick(a, 128, 44100, out, lens, 16);
    assert(count == 0);
    for (int i = 0; i < 12; i++) assert(send(a, 0xf8, 0, 0, out, lens) == 0);
    count = send(a, 0xf8, 0, 0, out, lens);
    assert(count == playing.count + a->steps[1].count);
    assert(a->active_period == 36 && a->active_gate == 18);
    for (int i = 0; i < 17; i++) assert(send(a, 0xf8, 0, 0, out, lens) == 0);
    count = send(a, 0xf8, 0, 0, out, lens);
    assert(count == a->steps[1].count);
    for (int i = 0; i < 17; i++) assert(send(a, 0xf8, 0, 0, out, lens) == 0);
    count = send(a, 0xf8, 0, 0, out, lens);
    assert(count == a->steps[2].count);
    mock_clock_status = MOVE_CLOCK_STATUS_STOPPED;
    count = send(a, 0xfc, 0, 0, out, lens);
    assert(count == a->steps[2].count);

    set_param(a, "state", "{\"mode\":1,\"clock_rate\":1,\"sustain\":50,\"colour\":0,\"bass_mode\":0}");
    assert(a->clock_rate == 1 && a->sustain == 50);
    char rate_name[16];
    assert(get_param(a, "clock_rate", rate_name, sizeof(rate_name)) > 0);
    assert(!strcmp(rate_name, "2 beats"));
    assert(send(a, 0xfa, 0, 0, out, lens) == 4);
    for (int i = 0; i < 23; i++) assert(send(a, 0xf8, 0, 0, out, lens) == 0);
    count = send(a, 0xf8, 0, 0, out, lens);
    assert(count == 4);
    for (int i = 0; i < count; i++) assert((out[i][0] & 0xf0) == 0x80);
    for (int i = 0; i < 23; i++) assert(send(a, 0xf8, 0, 0, out, lens) == 0);
    count = send(a, 0xf8, 0, 0, out, lens);
    assert(count == 4);
    for (int i = 0; i < count; i++) assert((out[i][0] & 0xf0) == 0x90);
    assert(send(a, 0xfc, 0, 0, out, lens) == 4);

    set_param(a, "clock_rate", "4 beats");
    set_param(a, "sustain", "25");
    assert(send(a, 0xfa, 0, 0, out, lens) == 4);
    for (int i = 0; i < 23; i++) assert(send(a, 0xf8, 0, 0, out, lens) == 0);
    assert(send(a, 0xf8, 0, 0, out, lens) == 4);
    for (int i = 0; i < 71; i++) assert(send(a, 0xf8, 0, 0, out, lens) == 0);
    assert(send(a, 0xf8, 0, 0, out, lens) == 4);
    assert(send(a, 0xfc, 0, 0, out, lens) == 4);

    const char *dotted_rates[] = {"1.5 beats", "3 beats", "6 beats"};
    const int dotted_periods[] = {36, 72, 144};
    for (int rate = 0; rate < 3; rate++) {
        set_param(a, "clock_rate", dotted_rates[rate]);
        set_param(a, "sustain", "50");
        assert(get_param(a, "clock_rate", rate_name, sizeof(rate_name)) > 0);
        assert(!strcmp(rate_name, dotted_rates[rate]));
        assert(send(a, 0xfa, 0, 0, out, lens) == 4);
        for (int pulse = 1; pulse <= dotted_periods[rate]; pulse++) {
            count = send(a, 0xf8, 0, 0, out, lens);
            if (pulse == dotted_periods[rate] / 2) {
                assert(count == 4);
                for (int i = 0; i < count; i++)
                    assert((out[i][0] & 0xf0) == 0x80);
            } else if (pulse == dotted_periods[rate]) {
                assert(count == 4);
                for (int i = 0; i < count; i++)
                    assert((out[i][0] & 0xf0) == 0x90);
            } else assert(count == 0);
        }
        assert(send(a, 0xfc, 0, 0, out, lens) == 4);
    }

    set_param(a, "mode", "pads");
    assert(send(a, 0x90, 48, 100, out, lens) == a->steps[0].count);
    set_param(a, "trigger", "60");
    count = tick(a, 128, 44100, out, lens, 16);
    assert(count == a->steps[0].count);
    for (int i = 0; i < count; i++) assert((out[i][0] & 0xf0) == 0x80);

    set_param(a, "state", "{\"seed\":42,\"left\":2,\"right\":2,\"wander\":0,\"split\":0,\"colour\":0}");
    set_param(b, "state", "{\"seed\":42,\"left\":2,\"right\":2,\"wander\":0,\"split\":0,\"colour\":0}");
    char value[32];
    assert(get_param(a, "left", value, sizeof(value)) > 0);
    assert(!strcmp(value, "either"));
    int left_fourth = 0, left_fifth = 0, right_fourth = 0, right_fifth = 0;
    for (int i = 0; i < a->length; i++) {
        assert(a->steps[i].count == 4);
        assert(!memcmp(&a->steps[i], &b->steps[i], sizeof(chord_t)));
        assert(a->chosen_left[i] == b->chosen_left[i]);
        assert(a->chosen_right[i] == b->chosen_right[i]);
        left_fourth += a->chosen_left[i] == 5;
        left_fifth += a->chosen_left[i] == 7;
        right_fourth += a->chosen_right[i] == 5;
        right_fifth += a->chosen_right[i] == 7;
    }
    assert(left_fourth && left_fifth && right_fourth && right_fifth);

    set_param(b, "bass_mode", "follow");
    uint8_t follow_bass[MAX_STEPS];
    for (int i = 0; i < a->length; i++) {
        assert(b->steps[i].count == a->steps[i].count + 1);
        assert(b->steps[i].notes[0] < a->steps[i].notes[0]);
        assert(!memcmp(&b->steps[i].notes[1], a->steps[i].notes,
                       a->steps[i].count));
        follow_bass[i] = b->steps[i].notes[0];
    }

    set_param(b, "bass_mode", "wander");
    set_param(b, "bass_motion", "100");
    int bass_changed = 0;
    for (int i = 0; i < a->length; i++) {
        assert(b->steps[i].count == a->steps[i].count + 1);
        assert(!memcmp(&b->steps[i].notes[1], a->steps[i].notes,
                       a->steps[i].count));
        bass_changed += b->steps[i].notes[0] != follow_bass[i];
    }
    assert(bass_changed);
    assert(get_param(b, "state", state, sizeof(state)) > 0);
    set_param(a, "state", state);
    for (int i = 0; i < a->length; i++)
        assert(!memcmp(&a->steps[i], &b->steps[i], sizeof(chord_t)));

    set_param(b, "colour", "100");
    assert(b->steps[0].count == 6);
    assert(send(b, 0x90, 48, 100, out, lens) == 6);
    assert(out[5][1] == b->steps[0].notes[0]);
    /* A four-voice, oldest-note-stealing synth retains the last four ons. */
    int bass_in_last_four = 0;
    for (int i = 2; i < 6; i++)
        bass_in_last_four += out[i][1] == b->steps[0].notes[0];
    assert(bass_in_last_four);
    assert(send(b, 0x90, 49, 100, out, lens) == 12);
    assert(out[11][1] == b->steps[1].notes[0]);
    assert(send(b, 0x80, 49, 0, out, lens) == 6);

    int major_changed = 0, minor_changed = 0;
    for (int seed = 0; seed < 100; seed++) {
        char settings[192];
        snprintf(settings, sizeof(settings),
            "{\"seed\":%d,\"root\":60,\"colour\":0,\"bass_mode\":0,\"smoothness\":80,\"hand_gap\":7,\"tonal_colour\":0,\"tonal_pull\":0}", seed);
        set_param(a, "state", settings);
        assert(get_param(a, "state", state, sizeof(state)) > 0);
        set_param(b, "state", state);
        set_param(b, "tonal_colour", "major");
        for (int i = 0; i < a->length; i++)
            assert(!memcmp(&a->steps[i], &b->steps[i], sizeof(chord_t)));
        set_param(b, "tonal_pull", "100");
        for (int i = 0; i < a->length; i++) {
            assert(a->steps[i].count == 4 && b->steps[i].count == 4);
            int open_notes[4], coloured_notes[4];
            for (int j = 0; j < 4; j++) {
                open_notes[j] = a->steps[i].notes[j];
                coloured_notes[j] = b->steps[i].notes[j];
            }
            assert(chord_roughness(coloured_notes, 4) <=
                   chord_roughness(open_notes, 4));
            major_changed += memcmp(&a->steps[i], &b->steps[i], sizeof(chord_t)) != 0;
        }
        set_param(b, "tonal_colour", "minor");
        for (int i = 0; i < a->length; i++) {
            int open_notes[4], coloured_notes[4];
            for (int j = 0; j < 4; j++) {
                open_notes[j] = a->steps[i].notes[j];
                coloured_notes[j] = b->steps[i].notes[j];
            }
            assert(chord_roughness(coloured_notes, 4) <=
                   chord_roughness(open_notes, 4));
            minor_changed += memcmp(&a->steps[i], &b->steps[i], sizeof(chord_t)) != 0;
        }
    }
    assert(major_changed > 100 && minor_changed > 100);
    const char *new_colours[] = {"2nd", "6th", "b6", "b7"};
    const int target_intervals[] = {2, 9, 8, 10};
    for (int mode = 0; mode < 4; mode++) {
        int changed = 0, signatures = 0;
        set_param(b, "tonal_colour", new_colours[mode]);
        assert(get_param(b, "tonal_colour", value, sizeof(value)) > 0);
        assert(!strcmp(value, new_colours[mode]));
        for (int seed = 0; seed < 100; seed++) {
            char settings[192];
            snprintf(settings, sizeof(settings),
                "{\"seed\":%d,\"root\":60,\"colour\":0,\"bass_mode\":0,\"smoothness\":80,\"hand_gap\":7,\"tonal_colour\":0,\"tonal_pull\":0}", seed);
            set_param(a, "state", settings);
            set_param(b, "state", settings);
            set_param(b, "tonal_colour", new_colours[mode]);
            set_param(b, "tonal_pull", "100");
            for (int i = 0; i < a->length; i++) {
                int open_notes[4], coloured_notes[4];
                for (int j = 0; j < 4; j++) {
                    open_notes[j] = a->steps[i].notes[j];
                    coloured_notes[j] = b->steps[i].notes[j];
                }
                assert(chord_roughness(coloured_notes, 4) <=
                       chord_roughness(open_notes, 4) +
                       ((mode == 0 || mode == 3) ? 20 : 0));
                assert(harsh_intervals(coloured_notes, 4) <=
                       harsh_intervals(open_notes, 4));
                if (memcmp(&a->steps[i], &b->steps[i], sizeof(chord_t))) {
                    changed++;
                    for (int j = 1; j < 4; j++)
                        signatures += (coloured_notes[j] - coloured_notes[0] + 120) % 12
                            == target_intervals[mode];
                }
            }
        }
    assert(changed > 400 && signatures > 400);
    }
    const char *extended_names[] = {"maj7", "min7", "maj9", "min9"};
    const int extended_degrees[][3] = {
        {4, 11, 9}, {3, 10, 5}, {4, 11, 2}, {3, 10, 2}
    };
    for (int mode = 0; mode < 4; mode++) {
        int matched = 0;
        for (int seed = 0; seed < 50; seed++) {
            char settings[192];
            snprintf(settings, sizeof(settings),
                "{\"seed\":%d,\"root\":60,\"colour\":0,\"bass_mode\":0,\"smoothness\":80,\"hand_gap\":7,\"tonal_colour\":0,\"tonal_pull\":0}", seed);
            set_param(a, "state", settings);
            set_param(b, "state", settings);
            set_param(b, "tonal_colour", extended_names[mode]);
            set_param(b, "tonal_pull", "100");
            assert(get_param(b, "tonal_colour", value, sizeof(value)) > 0);
            assert(!strcmp(value, extended_names[mode]));
            for (int step = 0; step < a->length; step++) {
                int root = a->steps[step].notes[0] % 12;
                int mask = 0;
                for (int note = 0; note < b->steps[step].count; note++)
                    mask |= 1 << ((b->steps[step].notes[note] - root + 120) % 12);
                int wanted = 1;
                for (int degree = 0; degree < 3; degree++)
                    wanted |= 1 << extended_degrees[mode][degree];
                matched += (mask & wanted) == wanted;
            }
        }
        assert(matched > 350);
    }
    set_param(a, "state", "{\"seed\":15,\"root\":60,\"colour\":0,\"bass_mode\":1,\"inversion\":0}");
    assert(get_param(a, "state", state, sizeof(state)) > 0);
    set_param(b, "state", state);
    set_param(b, "inversion", "1st");
    assert(b->inversion == 1 && b->steps[0].count == 5);
    assert(b->steps[0].notes[0] % 12 == b->steps[0].notes[1] % 12);
    assert(b->steps[0].notes[1] != a->steps[0].notes[1]);
    for (int degree = 0; degree < 12; degree++) {
        int before = 0, after = 0;
        for (int i = 1; i < 5; i++) {
            before += a->steps[0].notes[i] % 12 == degree;
            after += b->steps[0].notes[i] % 12 == degree;
        }
        assert(before == after);
    }
    assert(get_param(b, "state", state, sizeof(state)) > 0);
    set_param(c, "state", state);
    assert(c->inversion == 1);
    assert(!memcmp(&b->steps[0], &c->steps[0], sizeof(chord_t)));
    for (int seed = 0; seed < 20; seed++) {
        char settings[192];
        snprintf(settings, sizeof(settings),
            "{\"seed\":%d,\"root\":64,\"bass_mode\":2,\"colour\":0,\"smoothness\":80,\"hand_gap\":8,\"tonal_colour\":0,\"tonal_pull\":0}", seed);
        set_param(a, "state", settings);
        assert(get_param(a, "state", state, sizeof(state)) > 0);
        set_param(b, "state", state);
        set_param(b, "tonal_colour", "minor");
        set_param(b, "tonal_pull", "100");
        for (int i = 0; i < a->length; i++) {
            assert(a->steps[i].count == 5 && b->steps[i].count == 5);
            int open_notes[5], coloured_notes[5];
            for (int j = 0; j < 5; j++) {
                open_notes[j] = a->steps[i].notes[j];
                coloured_notes[j] = b->steps[i].notes[j];
            }
            assert(chord_roughness(coloured_notes, 5) <=
                   chord_roughness(open_notes, 5));
            assert(b->steps[i].notes[3] - b->steps[i].notes[2] >= 8);
        }
    }
    set_param(b, "colour", "100");
    for (int i = 0; i < b->length; i++) assert(b->steps[i].count == 5);
    assert(get_param(b, "state", state, sizeof(state)) > 0);
    set_param(c, "state", state);
    assert(c->tonal_colour == 2 && c->tonal_pull == 100);
    for (int i = 0; i < b->length; i++)
        assert(!memcmp(&b->steps[i], &c->steps[i], sizeof(chord_t)));

    destroy_instance(a); destroy_instance(b); destroy_instance(c);
    puts("Shape Walk tests passed");
    return 0;
}
