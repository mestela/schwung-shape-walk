#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/plugin_api_v1.h"

/* The pad palette uses the exact same recipe and seed as the MIDI FX. */
#include "../../dsp/shape_walk.c"

#define PAD_COUNT 16
#define VOICE_COUNT (PAD_COUNT * 2)

typedef struct {
    const host_api_v1_t *host;
    shape_walk_t *generator;
    chord_t saved[PAD_COUNT];
    chord_t held[VOICE_COUNT];
    int note_users[128];
    int channel;
    int seed;
} seq_t;

static const host_api_v1_t *seq_host;

static void send_note(seq_t *s, int on, int note) {
    if (!s->host || !s->host->midi_inject_to_move) return;
    uint8_t packet[4] = {
        (uint8_t)(0x20 | (on ? 0x09 : 0x08)),
        (uint8_t)((on ? 0x90 : 0x80) | s->channel),
        (uint8_t)note,
        (uint8_t)(on ? 100 : 0)
    };
    s->host->midi_inject_to_move(packet, 4);
}

static void release_pad(seq_t *s, int pad) {
    if (pad < 0 || pad >= VOICE_COUNT) return;
    chord_t *held = &s->held[pad];
    for (int i = 0; i < held->count; i++) {
        int note = held->notes[i];
        if (s->note_users[note] > 0 && --s->note_users[note] == 0)
            send_note(s, 0, note);
    }
    held->count = 0;
}

static void release_all(seq_t *s) {
    for (int i = 0; i < VOICE_COUNT; i++) release_pad(s, i);
}

static void press_pad(seq_t *s, int pad) {
    if (pad < 0 || pad >= VOICE_COUNT) return;
    release_pad(s, pad);
    const chord_t *source = pad < PAD_COUNT ? &s->generator->steps[pad] :
        &s->saved[pad - PAD_COUNT];
    s->held[pad] = *source;
    for (int i = 0; i < source->count; i++) {
        int note = source->notes[i];
        if (s->note_users[note]++ == 0) send_note(s, 1, note);
    }
}

static int chord_has(const chord_t *chord, int note) {
    for (int i = 0; i < chord->count; i++)
        if (chord->notes[i] == note) return 1;
    return 0;
}

static void update_held_pad(seq_t *s, int pad, const chord_t *next) {
    chord_t *old = &s->held[pad];
    if (!old->count) return;
    for (int i = 0; i < old->count; i++) {
        int note = old->notes[i];
        if (!chord_has(next, note) && s->note_users[note] > 0 &&
            --s->note_users[note] == 0)
            send_note(s, 0, note);
    }
    for (int i = 0; i < next->count; i++) {
        int note = next->notes[i];
        if (!chord_has(old, note) && s->note_users[note]++ == 0)
            send_note(s, 1, note);
    }
    *old = *next;
}

static int parse_index(const char *key, const char *prefix) {
    size_t length = strlen(prefix);
    if (strncmp(key, prefix, length)) return -1;
    char *end;
    long index = strtol(key + length, &end, 10);
    if (*end || end == key + length || index < 0 || index >= PAD_COUNT)
        return -1;
    return (int)index;
}

static void parse_chord(chord_t *chord, const char *value) {
    chord_t result = {{0}, 0};
    const char *p = value;
    while (p && *p && result.count < MAX_NOTES) {
        char *end;
        long note = strtol(p, &end, 10);
        if (end == p || note < 0 || note > 127) break;
        add_note(&result, (int)note);
        if (*end != ',') break;
        p = end + 1;
    }
    *chord = result;
}

static int format_chord(const chord_t *chord, char *buf, int size) {
    int used = 0;
    if (size < 1) return -1;
    buf[0] = '\0';
    for (int i = 0; i < chord->count; i++) {
        int written = snprintf(buf + used, (size_t)(size - used),
                               "%s%d", i ? "," : "", chord->notes[i]);
        if (written < 0 || written >= size - used) return -1;
        used += written;
    }
    return used;
}

static void *seq_create(const char *module_dir, const char *defaults) {
    (void)module_dir; (void)defaults;
    seq_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->host = seq_host;
    s->generator = create_instance(NULL, NULL);
    if (!s->generator) { free(s); return NULL; }
    set_param(s->generator, "length", "16");
    set_param(s->generator, "seed", "1234");
    s->seed = 1234;
    return s;
}

static void seq_destroy(void *instance) {
    seq_t *s = instance;
    if (!s) return;
    release_all(s);
    destroy_instance(s->generator);
    free(s);
}

static void seq_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    (void)instance; (void)msg; (void)len; (void)source;
}

static void seq_set_param(void *instance, const char *key, const char *value) {
    seq_t *s = instance;
    if (!s || !key || !value) return;
    int slot = parse_index(key, "slot_");
    if (slot >= 0) {
        parse_chord(&s->saved[slot], value);
        update_held_pad(s, PAD_COUNT + slot, &s->saved[slot]);
    } else if (!strcmp(key, "generator_state")) {
        release_all(s);
        set_param(s->generator, "state", value);
        set_param(s->generator, "length", "16");
        s->seed = s->generator->seed;
    } else if (!strcmp(key, "seed")) {
        release_all(s);
        s->seed = clamp(atoi(value), 0, 9999);
        char text[16];
        snprintf(text, sizeof(text), "%d", s->seed);
        set_param(s->generator, "seed", text);
    } else if (!strcmp(key, "channel")) {
        int next = clamp(atoi(value), 0, 3);
        if (next != s->channel) {
            release_all(s);
            s->channel = next;
        }
    } else if (!strcmp(key, "press_left")) {
        press_pad(s, atoi(value));
    } else if (!strcmp(key, "release_left")) {
        release_pad(s, atoi(value));
    } else if (!strcmp(key, "press_right")) {
        press_pad(s, PAD_COUNT + atoi(value));
    } else if (!strcmp(key, "release_right")) {
        release_pad(s, PAD_COUNT + atoi(value));
    } else if (!strcmp(key, "release_all")) {
        release_all(s);
    }
}

static int seq_get_param(void *instance, const char *key, char *buf, int size) {
    seq_t *s = instance;
    if (!s || !key || !buf || size < 1) return -1;
    int slot = parse_index(key, "slot_");
    if (slot >= 0) return format_chord(&s->saved[slot], buf, size);
    int index = parse_index(key, "candidate_");
    if (index >= 0) return format_chord(&s->generator->steps[index], buf, size);
    if (!strcmp(key, "module_id")) return snprintf(buf, size, "shape-walk-seq");
    if (!strcmp(key, "channel")) return snprintf(buf, size, "%d", s->channel);
    if (!strcmp(key, "seed")) return snprintf(buf, size, "%d", s->seed);
    return -1;
}

static int seq_get_error(void *instance, char *buf, int size) {
    (void)instance;
    if (buf && size > 0) buf[0] = '\0';
    return 0;
}

static void seq_render(void *instance, int16_t *out, int frames) {
    (void)instance;
    if (out && frames > 0) memset(out, 0, (size_t)frames * 2 * sizeof(int16_t));
}

static plugin_api_v2_t seq_api = {
    .api_version = MOVE_PLUGIN_API_VERSION_2,
    .create_instance = seq_create,
    .destroy_instance = seq_destroy,
    .on_midi = seq_on_midi,
    .set_param = seq_set_param,
    .get_param = seq_get_param,
    .get_error = seq_get_error,
    .render_block = seq_render
};

plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host) {
    seq_host = host;
    return &seq_api;
}
