#include "host/midi_fx_api_v1.h"
#include "host/plugin_api_v1.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_INSTANCES 64
#define MAX_STEPS 16
#define MAX_NOTES 6

typedef struct {
    uint8_t notes[MAX_NOTES];
    uint8_t count;
} chord_t;

typedef struct {
    int in_use;
    int seed, root, length, left, right, distance, hand_gap;
    int path, wander, split, continuity, colour, tension, smoothness, mode, trigger;
    int clock_rate, sustain, tonal_colour, tonal_pull, inversion;
    int bass_mode, bass_offset, bass_motion;
    chord_t steps[MAX_STEPS];
    uint8_t chosen_left[MAX_STEPS], chosen_right[MAX_STEPS];
    chord_t sounding;
    uint8_t channel;
    int held_step;
    int running, clock_count, clock_step;
    int active_period, active_gate;
    int pending_silence, pending_start;
} shape_walk_t;

static shape_walk_t instances[MAX_INSTANCES];
static const host_api_v1_t *host_api;
static const char *note_names[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
static const char *tonal_names[] = {
    "open", "major", "minor", "2nd", "6th", "b6", "b7",
    "maj7", "min7", "maj9", "min9"
};
static const int tonal_intervals[] = {0, 4, 3, 2, 9, 8, 10};
static const char *inversion_names[] = {"root", "1st", "2nd", "3rd"};

static int clamp(int value, int lo, int hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

static int clock_period(int rate) {
    static const int periods[] = {24, 48, 96, 36, 72, 144};
    return periods[clamp(rate, 0, 5)];
}

static uint32_t random_u32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int chance(uint32_t *state, int percent) {
    return (int)(random_u32(state) % 100u) < percent;
}

static void add_note(chord_t *chord, int note) {
    if (note < 0 || note > 127 || chord->count >= MAX_NOTES) return;
    for (int i = 0; i < chord->count; i++) if (chord->notes[i] == note) return;
    int i = chord->count++;
    while (i > 0 && chord->notes[i - 1] > note) {
        chord->notes[i] = chord->notes[i - 1];
        i--;
    }
    chord->notes[i] = (uint8_t)note;
}

static int chromatic_distance(int a, int b) {
    int d = abs(a - b) % 12;
    return d > 6 ? 12 - d : d;
}

static int chord_roughness(const int *notes, int count) {
    int score = 0;
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            int interval = chromatic_distance(notes[i], notes[j]);
            if (interval == 1) score += 100;
            else if (interval == 6) score += 60;
            else if (interval == 2) score += 20;
        }
    }
    return score;
}

static int harsh_intervals(const int *notes, int count) {
    int count_harsh = 0;
    for (int i = 0; i < count; i++)
        for (int j = i + 1; j < count; j++) {
            int interval = chromatic_distance(notes[i], notes[j]);
            if (interval == 1 || interval == 6) count_harsh++;
        }
    return count_harsh;
}

/* Introduce a chosen interval relative to the moving left-hand root.
 * Search one-note changes first, then allow a second change only when the
 * target would otherwise make the chord rougher. */
static int tonalize(int pitches[4], int *bass_note, int tonal_colour,
                   int hand_gap) {
    int original[5] = {pitches[0], pitches[1], pitches[2], pitches[3],
                       *bass_note};
    int count = *bass_note >= 0 ? 5 : 4;
    int original_rough = chord_roughness(original, count);
    int original_harsh = harsh_intervals(original, count);
    /* A 2nd or b7 adds a whole-tone relationship to the anchor by design.
     * Permit that one mild interval, but never add semitone or tritone pairs. */
    int rough_budget = original_rough +
        ((tonal_colour == 3 || tonal_colour == 6) ? 20 : 0);
    int target_interval = tonal_intervals[clamp(tonal_colour, 0, 6)];
    int root_class = pitches[0] % 12;
    int aligned_bass = *bass_note;
    if (count == 5) {
        int nearest = 100;
        for (int shift = -6; shift <= 6; shift++) {
            int candidate = *bass_note + shift;
            if (candidate < 0 || candidate >= pitches[0] - 4 ||
                candidate % 12 != root_class) continue;
            if (abs(shift) < nearest) {
                nearest = abs(shift);
                aligned_bass = candidate;
            }
        }
    }

    for (int edits = 1; edits <= 2; edits++) {
        int best_score = 1000000;
        int best[4] = {0};
        int best_bass = *bass_note;
        for (int voice = 1; voice < 4; voice++) {
            for (int octave = -1; octave <= 2; octave++) {
                int target = pitches[0] + target_interval + 12 * octave;
                if (target < 0 || target > 127 ||
                    abs(target - original[voice]) > 5) continue;
                for (int second = -1; second < 4; second++) {
                    if (edits == 1 && second != -1) continue;
                    if (edits == 2 && (second < 1 || second == voice)) continue;
                    for (int delta = edits == 1 ? 0 : -5;
                         delta <= (edits == 1 ? 0 : 5); delta++) {
                        if (edits == 2 && delta == 0) continue;
                        int candidate[5];
                        memcpy(candidate, original, sizeof(candidate));
                        candidate[voice] = target;
                        if (second >= 0) candidate[second] += delta;
                        int valid = 1;
                        for (int i = 0; i < 4; i++) {
                            if (candidate[i] < 0 || candidate[i] > 127) valid = 0;
                            for (int j = i + 1; j < 4; j++)
                                if (candidate[i] == candidate[j]) valid = 0;
                        }
                        int left_top = candidate[0] > candidate[1] ?
                            candidate[0] : candidate[1];
                        int right_bottom = candidate[2] < candidate[3] ?
                            candidate[2] : candidate[3];
                        if (hand_gap > 0 && right_bottom - left_top < hand_gap)
                            valid = 0;
                        if (!valid) continue;
                        for (int bass_choice = 0; bass_choice < (count == 5 ? 2 : 1);
                             bass_choice++) {
                            candidate[4] = bass_choice ? aligned_bass : *bass_note;
                            int rough = chord_roughness(candidate, count);
                            if (rough > rough_budget ||
                                harsh_intervals(candidate, count) > original_harsh)
                                continue;
                            int movement = abs(target - original[voice]) +
                                (second >= 0 ? abs(delta) : 0) +
                                (count == 5 ? abs(candidate[4] - *bass_note) : 0);
                            int score = rough + movement * 15;
                            if (count == 5 && candidate[4] % 12 != root_class)
                                score += 100;
                            if (score < best_score) {
                                best_score = score;
                                memcpy(best, candidate, 4 * sizeof(int));
                                best_bass = candidate[4];
                            }
                        }
                    }
                }
            }
        }
        if (best_score < 1000000) {
            memcpy(pitches, best, 4 * sizeof(int));
            *bass_note = best_bass;
            return 1;
        }
    }
    return 0;
}

static int nearest_degree(int root, int degree, int original) {
    int best = -1, distance = 10;
    for (int octave = -2; octave <= 3; octave++) {
        int note = root + degree + 12 * octave;
        int move = abs(note - original);
        if (note >= 0 && note <= 127 && move < distance) {
            best = note;
            distance = move;
        }
    }
    return best;
}

/* Seventh and ninth colours claim two or three of the existing upper voices.
 * A seventh necessarily brings some tension; allow its signature clash while
 * rejecting further semitone/tritone pairs. The moving root stays geometric. */
static int tonalize_extended(int pitches[4], int *bass_note, int colour,
                             int hand_gap) {
    static const int degrees[4][3] = {
        {4, 11, 9}, {3, 10, 5}, {4, 11, 2}, {3, 10, 2}
    };
    int original[5] = {pitches[0], pitches[1], pitches[2], pitches[3], *bass_note};
    int count = *bass_note >= 0 ? 5 : 4;
    int original_rough = chord_roughness(original, count);
    int original_harsh = harsh_intervals(original, count);
    int mode = clamp(colour - 7, 0, 3);
    int targets = 3;
    int rough_budget = original_rough + (mode == 0 ? 120 :
                       mode == 1 ? 40 : 160);
    int harsh_budget = original_harsh + (mode == 1 ? 0 : 1);
    int best_score = 1000000, best[4] = {0}, best_bass = *bass_note;
    for (int v1 = 1; v1 < 4; v1++)
        for (int v2 = 1; v2 < 4; v2++) {
            if (v2 == v1) continue;
            for (int v3 = 1; v3 < 4; v3++) {
                if (v3 == v1 || v3 == v2) continue;
                int candidate[5];
                memcpy(candidate, original, sizeof(candidate));
                int voices[3] = {v1, v2, v3};
                int valid = 1, movement = 0;
                for (int t = 0; t < targets; t++) {
                    int voice = voices[t];
                    int note = nearest_degree(original[0], degrees[mode][t],
                                              original[voice]);
                    if (note < 0) { valid = 0; break; }
                    movement += abs(note - original[voice]);
                    candidate[voice] = note;
                }
                if (!valid) continue;
                for (int i = 0; i < 4; i++)
                    for (int j = i + 1; j < 4; j++)
                        if (candidate[i] == candidate[j]) valid = 0;
                int left_top = candidate[0] > candidate[1] ? candidate[0] : candidate[1];
                int right_bottom = candidate[2] < candidate[3] ? candidate[2] : candidate[3];
                if (hand_gap > 0 && right_bottom - left_top < hand_gap) valid = 0;
                if (!valid) continue;
                for (int bass_choice = 0; bass_choice < (count == 5 ? 2 : 1);
                     bass_choice++) {
                    candidate[4] = *bass_note;
                    if (bass_choice) {
                        int aligned = *bass_note;
                        int nearest = 100;
                        for (int shift = -6; shift <= 6; shift++) {
                            int note = *bass_note + shift;
                            if (note < 0 || note >= original[0] - 4 ||
                                note % 12 != original[0] % 12) continue;
                            if (abs(shift) < nearest) {
                                nearest = abs(shift);
                                aligned = note;
                            }
                        }
                        candidate[4] = aligned;
                    }
                    int rough = chord_roughness(candidate, count);
                    if (rough > rough_budget ||
                        harsh_intervals(candidate, count) > harsh_budget) continue;
                    int score = rough + movement * 12 +
                        (count == 5 ? abs(candidate[4] - *bass_note) * 12 : 0);
                    if (count == 5 && candidate[4] % 12 != original[0] % 12)
                        score += 100;
                    if (score < best_score) {
                        best_score = score;
                        memcpy(best, candidate, 4 * sizeof(int));
                        best_bass = candidate[4];
                    }
                }
            }
        }
    if (best_score == 1000000) return 0;
    memcpy(pitches, best, sizeof(best));
    *bass_note = best_bass;
    return 1;
}

static void invert_chord(int pitches[4], int *bass_note, int inversion) {
    if (inversion <= 0) return;
    int order[4] = {0, 1, 2, 3};
    for (int i = 0; i < 4; i++)
        for (int j = i + 1; j < 4; j++)
            if (pitches[order[j]] < pitches[order[i]]) {
                int swap = order[i]; order[i] = order[j]; order[j] = swap;
            }
    int highest = pitches[order[3]];
    for (int i = 0; i < inversion && i < 3; i++) {
        int note = pitches[order[i]];
        while (note <= highest && note <= 115) note += 12;
        if (note > 127 || note <= highest) continue;
        pitches[order[i]] = note;
        highest = note;
    }
    if (*bass_note >= 0) {
        int lowest = pitches[0];
        for (int i = 1; i < 4; i++)
            if (pitches[i] < lowest) lowest = pitches[i];
        int target = lowest - 12;
        while (target - 12 >= 0 &&
               abs(target - 12 - *bass_note) < abs(target - *bass_note))
            target -= 12;
        while (target + 12 < lowest - 4 &&
               abs(target + 12 - *bass_note) < abs(target - *bass_note))
            target += 12;
        *bass_note = target;
    }
}

/* All note positions are MIDI pitches. In Move's chromatic-fourths layout,
 * vertical motion is +/-5 semitones and horizontal motion is +/-1. */
static void generate(shape_walk_t *s) {
    static const int loop_path[4] = {-5, 3, -5, 7};
    static const int mirror_path[4] = {5, -3, 5, -7};
    static const int fifth_path[4] = {-5, -5, 7, 3};
    static const int alternatives[] = {-7, -5, -3, -2, -1, 1, 2, 3, 5, 7};
    static const int split_moves[] = {-5, -2, -1, 1, 2, 5};
    static const int bass_moves[] = {-5, -1, 1, 5};
    uint32_t rng = (uint32_t)s->seed ^ 0x9e3779b9u;
    if (!rng) rng = 1;
    /* Bass uses a separate stream so switching it on does not rewrite the
     * upper chord sequence or its colour-note choices. */
    uint32_t bass_rng = (uint32_t)s->seed ^ 0x6d2b79f5u;
    if (!bass_rng) bass_rng = 1;
    uint32_t smooth_rng = (uint32_t)s->seed ^ 0xb5297a4du;
    if (!smooth_rng) smooth_rng = 1;
    uint32_t tonal_rng = (uint32_t)s->seed ^ 0x3c6ef372u;
    if (!tonal_rng) tonal_rng = 1;
    int position = 0;
    int right_shift = 0;
    int bass_position = 0;
    int right_offset = 5 + (s->distance * 12) / 100;

    for (int step = 0; step < s->length; step++) {
        if (step > 0) {
            int motion = s->path == 1 ? mirror_path[(step - 1) % 4] :
                         s->path == 2 ? fifth_path[(step - 1) % 4] :
                         loop_path[(step - 1) % 4];
            if (s->path == 3 || chance(&rng, s->wander)) {
                int candidate = alternatives[random_u32(&rng) % 10u];
                /* Higher continuity keeps the random walk near its last chord. */
                if (chance(&rng, s->continuity)) {
                    for (int tries = 0; tries < 3; tries++) {
                        int next = alternatives[random_u32(&rng) % 10u];
                        if (abs(next) < abs(candidate)) candidate = next;
                    }
                }
                motion = candidate;
            }
            position += motion;
            if (position < -12) position += 12;
            if (position > 12) position -= 12;
            if (chance(&rng, s->split)) {
                int change = split_moves[random_u32(&rng) % 6u];
                if (chance(&rng, s->continuity) && abs(change) > 2)
                    change = change < 0 ? -1 : 1;
                right_shift = clamp(right_shift + change, -4, 8);
            }
        }

        /* The Either setting makes a fresh seeded choice for that hand on
         * each step. Fixed choices do not consume RNG, preserving old seeds. */
        int left_interval = s->left == 2 ? (chance(&rng, 50) ? 7 : 5) :
                            (s->left == 1 ? 7 : 5);
        int right_interval = s->right == 2 ? (chance(&rng, 50) ? 7 : 5) :
                             (s->right == 1 ? 7 : 5);
        s->chosen_left[step] = (uint8_t)left_interval;
        s->chosen_right[step] = (uint8_t)right_interval;

        int left_root = s->root + position;
        int right_root = left_root + right_offset + right_shift;
        /* Keep both hand shapes audible even when the walk brings their
         * anchors onto the same pitch. A horizontal pad nudge preserves the
         * fourth/fifth within the right hand. */
        int left_partner = left_root + left_interval;
        int right_partner = right_interval;
        if (s->hand_gap > 0 && right_root < left_partner + s->hand_gap)
            right_root = left_partner + s->hand_gap;
        for (int nudge = 0; nudge < 4; nudge++) {
            if (right_root != left_root && right_root != left_partner &&
                right_root + right_partner != left_root &&
                right_root + right_partner != left_partner) break;
            right_root++;
        }
        int bass_note = -1;
        if (s->bass_mode != 0) {
            if (step > 0 && s->bass_mode == 2 &&
                chance(&bass_rng, s->bass_motion)) {
                int move = bass_moves[random_u32(&bass_rng) % 4u];
                bass_position = clamp(bass_position + move, -12, 12);
            }
            bass_note = s->root + s->bass_offset +
                (s->bass_mode == 1 ? position : bass_position);
            while (bass_note >= left_root - 4) bass_note -= 12;
        }

        /* Search nearby hand and bass placements without changing either
         * fourth/fifth shape. A separate random stream preserves old seeds
         * exactly when Smoothness is zero. */
        if (s->smoothness > 0 && chance(&smooth_rng, s->smoothness)) {
            int original_right = right_root;
            int original_bass = bass_note;
            int best_score = 1000000;
            for (int shift = -7; shift <= 7; shift++) {
                int candidate_right = original_right + shift;
                int minimum = left_partner + (s->hand_gap > 0 ? s->hand_gap : 1);
                if (candidate_right < minimum || candidate_right + right_partner > 127)
                    continue;
                for (int bass_shift = s->bass_mode ? -5 : 0;
                     bass_shift <= (s->bass_mode ? 5 : 0); bass_shift++) {
                    int candidate_bass = original_bass + bass_shift;
                    if (s->bass_mode && (candidate_bass < 0 ||
                        candidate_bass >= left_root - 4)) continue;
                    int notes[5] = {left_root, left_partner, candidate_right,
                                    candidate_right + right_partner, candidate_bass};
                    int score = chord_roughness(notes, s->bass_mode ? 5 : 4)
                              + abs(shift) * 2 + abs(bass_shift) * 2;
                    if (score < best_score) {
                        best_score = score;
                        right_root = candidate_right;
                        bass_note = candidate_bass;
                    }
                }
            }
        }
        int pitches[4] = {left_root, left_partner,
                          right_root, right_root + right_partner};
        if (s->tonal_colour != 0 && s->tonal_pull > 0 &&
            chance(&tonal_rng, s->tonal_pull)) {
            if (s->tonal_colour <= 6)
                tonalize(pitches, &bass_note, s->tonal_colour, s->hand_gap);
            else
                tonalize_extended(pitches, &bass_note, s->tonal_colour, s->hand_gap);
        }
        invert_chord(pitches, &bass_note, s->inversion);
        chord_t chord = {{0}, 0};
        for (int i = 0; i < 4; i++) add_note(&chord, pitches[i]);

        int colour_chance = s->colour * (100 - s->smoothness) / 100;
        if (s->tonal_colour != 0)
            colour_chance = colour_chance * (100 - s->tonal_pull) / 100;
        if (chance(&rng, colour_chance)) {
            int best_note = -1;
            int best_score = -100000;
            for (int i = 0; i < 4; i++) {
                for (int direction = -1; direction <= 1; direction += 2) {
                    int candidate = pitches[i] + direction;
                    if (candidate < 0 || candidate > 127) continue;
                    int duplicate = 0;
                    for (int j = 0; j < chord.count; j++)
                        if (chord.notes[j] == candidate) duplicate = 1;
                    if (duplicate) continue;
                    int partner = pitches[i ^ 1];
                    int partner_class = chromatic_distance(candidate, partner);
                    /* A third against the partner is the gentler choice;
                     * a tritone is the more tense one. */
                    int score = partner_class == 4 ? 100 - s->tension :
                                partner_class == 6 ? s->tension : 50;
                    for (int j = 0; j < 4; j++) {
                        if (j == i) continue;
                        int interval = chromatic_distance(candidate, pitches[j]);
                        if (interval == 1) score -= (100 - s->tension) / 5;
                        if (interval == 6) score -= (100 - s->tension) / 8;
                    }
                    score += (int)(random_u32(&rng) % 21u) - 10;
                    if (score > best_score) { best_score = score; best_note = candidate; }
                }
            }
            if (best_note >= 0) add_note(&chord, best_note);
        }
        if (s->bass_mode != 0) add_note(&chord, bass_note);
        s->steps[step] = chord;
    }
}

static void *create_instance(const char *module_dir, const char *config_json) {
    (void)module_dir; (void)config_json;
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (instances[i].in_use) continue;
        shape_walk_t *s = &instances[i];
        memset(s, 0, sizeof(*s));
        s->in_use = 1;
        s->seed = 1234; s->root = 60; s->length = 8;
        s->left = 0; s->right = 1; s->distance = 20; s->hand_gap = 0;
        s->path = 0; s->wander = 25; s->split = 25;
        s->continuity = 65; s->colour = 25; s->tension = 25; s->smoothness = 0;
        s->mode = 0; s->trigger = 48; s->held_step = -1;
        s->clock_rate = 0; s->sustain = 100;
        s->active_period = 24; s->active_gate = 24;
        s->tonal_colour = 0; s->tonal_pull = 0; s->inversion = 0;
        s->bass_mode = 0; s->bass_offset = -12; s->bass_motion = 50;
        generate(s);
        return s;
    }
    return NULL;
}

static void destroy_instance(void *instance) {
    if (instance) ((shape_walk_t *)instance)->in_use = 0;
}

static int emit(uint8_t out[][3], int lens[], int count, int max,
                uint8_t status, uint8_t note, uint8_t velocity) {
    if (count >= max) return count;
    out[count][0] = status; out[count][1] = note; out[count][2] = velocity;
    lens[count] = 3;
    return count + 1;
}

static int silence(shape_walk_t *s, uint8_t out[][3], int lens[], int count, int max) {
    for (int i = 0; i < s->sounding.count; i++)
        count = emit(out, lens, count, max, 0x80 | s->channel,
                     s->sounding.notes[i], 0);
    s->sounding.count = 0;
    return count;
}

static int sound_step(shape_walk_t *s, int step, uint8_t velocity,
                      uint8_t out[][3], int lens[], int max) {
    s->pending_silence = 0;
    if (s->mode == 1) {
        s->active_period = clock_period(s->clock_rate);
        s->active_gate = (s->active_period * s->sustain + 99) / 100;
    }
    int count = silence(s, out, lens, 0, max);
    chord_t chord = s->steps[step % s->length];
    /* A transition needs at most twelve messages: six offs and six ons. */
    if (max - count < chord.count) return count;
    /* Four-voice instruments such as Braids steal the oldest note when a
     * fifth arrives. Put the bass on last so it remains in the audible set. */
    int first = s->bass_mode != 0 && chord.count > 0 ? 1 : 0;
    for (int i = first; i < chord.count; i++)
        count = emit(out, lens, count, max, 0x90 | s->channel,
                     chord.notes[i], velocity);
    if (first)
        count = emit(out, lens, count, max, 0x90 | s->channel,
                     chord.notes[0], velocity);
    s->sounding = chord;
    return count;
}

static int process_midi(void *instance, const uint8_t *in, int len,
                        uint8_t out[][3], int lens[], int max) {
    shape_walk_t *s = (shape_walk_t *)instance;
    if (!s || !in || len < 1 || max < 1) return 0;
    uint8_t status = in[0];
    uint8_t type = status & 0xf0;

    if (s->mode == 1) {
        if (status == 0xfa) {
            s->pending_start = 0;
            s->running = 1; s->clock_count = 0; s->clock_step = 0;
            return sound_step(s, 0, 100, out, lens, max);
        }
        if (status == 0xfb) { s->running = 1; return 0; }
        if (status == 0xfc) {
            s->pending_start = 0;
            s->running = 0;
            return silence(s, out, lens, 0, max);
        }
        if (status == 0xf8) {
            if (!s->running) return 0;
            s->clock_count++;
            if (s->clock_count >= s->active_period) {
                s->clock_count = 0;
                s->clock_step = (s->clock_step + 1) % s->length;
                return sound_step(s, s->clock_step, 100, out, lens, max);
            }
            if (s->active_gate < s->active_period &&
                s->clock_count >= s->active_gate &&
                s->sounding.count)
                return silence(s, out, lens, 0, max);
            return 0;
        }
    }

    if (s->mode == 0 && len >= 3 && (type == 0x80 || type == 0x90)) {
        int step = (int)in[1] - s->trigger;
        if (step >= 0 && step < s->length) {
            if (type == 0x90 && in[2] > 0) {
                s->channel = status & 0x0f;
                s->held_step = step;
                return sound_step(s, step, in[2], out, lens, max);
            }
            if (step == s->held_step) {
                s->held_step = -1;
                return silence(s, out, lens, 0, max);
            }
            return 0;
        }
    }

    out[0][0] = in[0]; out[0][1] = len > 1 ? in[1] : 0;
    out[0][2] = len > 2 ? in[2] : 0; lens[0] = len;
    return 1;
}

static int tick(void *instance, int frames, int sample_rate,
                uint8_t out[][3], int lens[], int max) {
    shape_walk_t *s = (shape_walk_t *)instance;
    (void)frames; (void)sample_rate;
    if (s && s->pending_start) {
        s->pending_start = 0;
        return sound_step(s, 0, 100, out, lens, max);
    }
    if (s && s->pending_silence) {
        s->pending_silence = 0;
        return silence(s, out, lens, 0, max);
    }
    return 0;
}

static int json_int(const char *json, const char *key, int *out) {
    char needle[48];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    *out = (int)strtol(p + 1, NULL, 10);
    return 1;
}

static void set_one(shape_walk_t *s, const char *key, int val) {
    if (!strcmp(key, "seed")) s->seed = clamp(val, 0, 9999);
    /* Old patches store an absolute MIDI root; the new control stores a
     * pitch-class index. Both resolve to the octave beginning at C4. */
    else if (!strcmp(key, "root")) s->root = 60 +
        (val >= 36 ? val % 12 : clamp(val, 0, 11));
    else if (!strcmp(key, "length")) s->length = val <= 4 ? 4 : (val <= 8 ? 8 : 16);
    else if (!strcmp(key, "left")) s->left = clamp(val, 0, 2);
    else if (!strcmp(key, "right")) s->right = clamp(val, 0, 2);
    else if (!strcmp(key, "distance")) s->distance = clamp(val, 0, 100);
    else if (!strcmp(key, "hand_gap")) s->hand_gap = clamp(val, 0, 12);
    else if (!strcmp(key, "path")) s->path = clamp(val, 0, 3);
    else if (!strcmp(key, "wander")) s->wander = clamp(val, 0, 100);
    else if (!strcmp(key, "split")) s->split = clamp(val, 0, 100);
    else if (!strcmp(key, "continuity")) s->continuity = clamp(val, 0, 100);
    else if (!strcmp(key, "colour")) s->colour = clamp(val, 0, 100);
    else if (!strcmp(key, "tension")) s->tension = clamp(val, 0, 100);
    else if (!strcmp(key, "smoothness")) s->smoothness = clamp(val, 0, 100);
    else if (!strcmp(key, "tonal_colour")) s->tonal_colour = clamp(val, 0, 10);
    else if (!strcmp(key, "tonal_pull")) s->tonal_pull = clamp(val, 0, 100);
    else if (!strcmp(key, "inversion")) s->inversion = clamp(val, 0, 3);
    else if (!strcmp(key, "mode")) s->mode = clamp(val, 0, 1);
    else if (!strcmp(key, "clock_rate")) s->clock_rate = clamp(val, 0, 5);
    else if (!strcmp(key, "sustain")) s->sustain = clamp(val, 1, 100);
    else if (!strcmp(key, "trigger")) s->trigger = clamp(val, 0, 112);
    else if (!strcmp(key, "bass_mode")) s->bass_mode = clamp(val, 0, 2);
    else if (!strcmp(key, "bass_offset")) s->bass_offset = clamp(val, -24, 0);
    else if (!strcmp(key, "bass_motion")) s->bass_motion = clamp(val, 0, 100);
}

static void set_param(void *instance, const char *key, const char *val) {
    shape_walk_t *s = (shape_walk_t *)instance;
    if (!s || !key || !val) return;
    int old_mode = s->mode;
    static const char *keys[] = {"seed", "root", "length", "left", "right",
        "distance", "hand_gap", "path", "wander", "split", "continuity", "colour",
        "tension", "smoothness", "tonal_colour", "tonal_pull", "inversion", "mode", "clock_rate", "sustain", "trigger", "bass_mode", "bass_offset",
        "bass_motion"};
    if (!strcmp(key, "state")) {
        for (int i = 0; i < 24; i++) {
            int value;
            if (json_int(val, keys[i], &value)) set_one(s, keys[i], value);
        }
    } else if (!strcmp(key, "root")) {
        int value = (int)strtol(val, NULL, 10);
        for (int i = 0; i < 12; i++)
            if (!strcmp(val, note_names[i])) { value = i; break; }
        set_one(s, key, value);
    } else if (!strcmp(key, "left") || !strcmp(key, "right") ||
               !strcmp(key, "mode") || !strcmp(key, "clock_rate") ||
               !strcmp(key, "tonal_colour") || !strcmp(key, "inversion") ||
               !strcmp(key, "path") ||
               !strcmp(key, "length") || !strcmp(key, "bass_mode")) {
        int value = (int)strtol(val, NULL, 10);
        if (!strcmp(key, "left") || !strcmp(key, "right"))
            value = !strcmp(val, "either") ? 2 :
                    (!strcmp(val, "5th") ? 1 : (!strcmp(val, "4th") ? 0 : value));
        if (!strcmp(key, "mode")) value = !strcmp(val, "clock") ? 1 : value;
        if (!strcmp(key, "tonal_colour")) {
            for (int i = 0; i < 11; i++)
                if (!strcmp(val, tonal_names[i])) { value = i; break; }
        }
        if (!strcmp(key, "inversion")) {
            for (int i = 0; i < 4; i++)
                if (!strcmp(val, inversion_names[i])) { value = i; break; }
        }
        if (!strcmp(key, "clock_rate")) {
            if (!strcmp(val, "1 beat")) value = 0;
            else if (!strcmp(val, "2 beats")) value = 1;
            else if (!strcmp(val, "4 beats")) value = 2;
            else if (!strcmp(val, "1.5 beats")) value = 3;
            else if (!strcmp(val, "3 beats")) value = 4;
            else if (!strcmp(val, "6 beats")) value = 5;
        }
        if (!strcmp(key, "bass_mode")) {
            if (!strcmp(val, "follow")) value = 1;
            else if (!strcmp(val, "wander")) value = 2;
        }
        if (!strcmp(key, "path")) {
            if (!strcmp(val, "mirror")) value = 1;
            else if (!strcmp(val, "fifths")) value = 2;
            else if (!strcmp(val, "free")) value = 3;
        }
        set_one(s, key, value);
    } else set_one(s, key, (int)strtol(val, NULL, 10));
    if (strcmp(key, "mode") && strcmp(key, "clock_rate") &&
        strcmp(key, "sustain") && strcmp(key, "trigger"))
        generate(s);
    if (old_mode == 1 && s->mode == 1) {
        /* Keep the audible chord, clock phase, and latched gate untouched.
         * New pitches and timing take effect at the next chord boundary. */
        s->clock_step %= s->length;
        return;
    }
    if (s->sounding.count) s->pending_silence = 1;
    s->held_step = -1;
    if (old_mode == 0 && s->mode == 0) return;
    s->running = 0;
    s->pending_start = 0;
    s->clock_count = 0;
    s->clock_step = 0;
    /* Entering Clock during playback is the one case that starts a new
     * sequence without MIDI Start. Ordinary knob changes never restart it. */
    if (s->mode == 1 && host_api && host_api->get_clock_status &&
        host_api->get_clock_status() == MOVE_CLOCK_STATUS_RUNNING) {
        s->running = 1;
        s->pending_start = 1;
    }
}

static int get_param(void *instance, const char *key, char *buf, int size) {
    shape_walk_t *s = (shape_walk_t *)instance;
    if (!s || !key || !buf || size < 1) return -1;
    if (!strcmp(key, "state"))
        return snprintf(buf, size, "{\"seed\":%d,\"root\":%d,\"length\":%d,\"left\":%d,\"right\":%d,\"distance\":%d,\"hand_gap\":%d,\"path\":%d,\"wander\":%d,\"split\":%d,\"continuity\":%d,\"colour\":%d,\"tension\":%d,\"smoothness\":%d,\"tonal_colour\":%d,\"tonal_pull\":%d,\"inversion\":%d,\"mode\":%d,\"clock_rate\":%d,\"sustain\":%d,\"trigger\":%d,\"bass_mode\":%d,\"bass_offset\":%d,\"bass_motion\":%d}",
            s->seed, s->root, s->length, s->left, s->right, s->distance,
            s->hand_gap,
            s->path, s->wander, s->split, s->continuity, s->colour,
            s->tension, s->smoothness, s->tonal_colour, s->tonal_pull,
            s->inversion,
            s->mode, s->clock_rate, s->sustain,
            s->trigger, s->bass_mode,
            s->bass_offset, s->bass_motion);
    if (!strcmp(key, "seed")) return snprintf(buf, size, "%d", s->seed);
    if (!strcmp(key, "root")) return snprintf(buf, size, "%s",
        note_names[s->root % 12]);
    if (!strcmp(key, "length")) return snprintf(buf, size, "%d", s->length);
    if (!strcmp(key, "left")) return snprintf(buf, size, "%s",
        s->left == 2 ? "either" : (s->left == 1 ? "5th" : "4th"));
    if (!strcmp(key, "right")) return snprintf(buf, size, "%s",
        s->right == 2 ? "either" : (s->right == 1 ? "5th" : "4th"));
    if (!strcmp(key, "distance")) return snprintf(buf, size, "%d", s->distance);
    if (!strcmp(key, "hand_gap")) return snprintf(buf, size, "%d", s->hand_gap);
    if (!strcmp(key, "path")) return snprintf(buf, size, "%s",
        (const char *[]) {"loop", "mirror", "fifths", "free"}[s->path]);
    if (!strcmp(key, "wander")) return snprintf(buf, size, "%d", s->wander);
    if (!strcmp(key, "split")) return snprintf(buf, size, "%d", s->split);
    if (!strcmp(key, "continuity")) return snprintf(buf, size, "%d", s->continuity);
    if (!strcmp(key, "colour")) return snprintf(buf, size, "%d", s->colour);
    if (!strcmp(key, "tension")) return snprintf(buf, size, "%d", s->tension);
    if (!strcmp(key, "smoothness")) return snprintf(buf, size, "%d", s->smoothness);
    if (!strcmp(key, "tonal_colour")) return snprintf(buf, size, "%s",
        tonal_names[s->tonal_colour]);
    if (!strcmp(key, "tonal_pull")) return snprintf(buf, size, "%d", s->tonal_pull);
    if (!strcmp(key, "inversion")) return snprintf(buf, size, "%s",
        inversion_names[s->inversion]);
    if (!strcmp(key, "mode")) return snprintf(buf, size, "%s", s->mode ? "clock" : "pads");
    if (!strcmp(key, "clock_rate")) return snprintf(buf, size, "%s",
        (const char *[]) {"1 beat", "2 beats", "4 beats",
                         "1.5 beats", "3 beats", "6 beats"}[s->clock_rate]);
    if (!strcmp(key, "sustain")) return snprintf(buf, size, "%d", s->sustain);
    if (!strcmp(key, "trigger")) return snprintf(buf, size, "%d", s->trigger);
    if (!strcmp(key, "bass_mode")) return snprintf(buf, size, "%s",
        (const char *[]) {"off", "follow", "wander"}[s->bass_mode]);
    if (!strcmp(key, "bass_offset")) return snprintf(buf, size, "%d", s->bass_offset);
    if (!strcmp(key, "bass_motion")) return snprintf(buf, size, "%d", s->bass_motion);
    return -1;
}

static midi_fx_api_v1_t api = {
    .api_version = MIDI_FX_API_VERSION,
    .create_instance = create_instance,
    .destroy_instance = destroy_instance,
    .process_midi = process_midi,
    .tick = tick,
    .set_param = set_param,
    .get_param = get_param
};

midi_fx_api_v1_t *move_midi_fx_init(const host_api_v1_t *host) {
    host_api = host;
    return &api;
}
