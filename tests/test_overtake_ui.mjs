import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import { renderPageMovy } from '../../schwung/src/shared/param_pages/render_page_movy.mjs';
import { drawEnumList } from '../../schwung/src/shared/param_pages/enum_list.mjs';
import { listKnobInit, listKnobStep } from '../../schwung/src/shared/param_pages/list_knob.mjs';
import { buildMetaIndex, NOTE_NAMES } from '../../schwung/src/shared/param_pages/param_meta.mjs';

const source = fs.readFileSync('src/overtake/ui.js', 'utf8')
    .replace(/import \{[\s\S]*?\} from '[^']+';\n/g, '');
const params = new Map();
let candidate = '48,60,64,67';
let saved = null;
let printed = [];
let filled = 0;
const context = {
    MovePads: Array.from({ length: 32 }, (_, i) => i + 68),
    MoveSteps: Array.from({ length: 16 }, (_, i) => i + 16),
    Blue: 33, DarkGrey: 119, White: 120, BrightGreen: 8,
    decodeDelta(value) {
        return value <= 63 ? value : value >= 65 ? value - 128 : 0;
    },
    renderPageMovy, drawEnumList, listKnobInit, listKnobStep,
    buildMetaIndex, NOTE_NAMES,
    host_module_set_param_blocking(key, value) { params.set(key, value); return true; },
    host_module_get_param(key) {
        if (key.startsWith('candidate_')) return candidate;
        return params.get(key) ?? '';
    },
    shadow_get_selected_slot() { return 3; },
    shadow_get_param(slot, key) {
        if (slot === 3 && key === 'midi_fx1_module') return 'shape-walk';
        if (slot === 3 && key === 'midi_fx1:state')
            return JSON.stringify({ seed: 3500, root: 64, length: 8 });
        return '';
    },
    host_read_file() { return saved; },
    host_write_file(_path, content) { saved = content; return true; },
    setLED() {}, clear_screen() { printed = []; filled = 0; },
    print(_x, _y, text) { printed.push(text); }, announce() {},
    fill_rect() { filled++; }, text_width(text) { return String(text).length * 6; },
    console
};
vm.createContext(context);
vm.runInContext(source, context);
function turn(cc, detents = 6, value = 1) {
    for (let i = 0; i < detents; i++)
        context.onMidiMessageInternal([0xb0, cc, value]);
}
context.init();
context.tick();
assert(filled > 40, 'the shared Schwung knob renderer drew the main page');
assert.equal(params.get('channel'), '3');
context.onMidiMessageInternal([0xb0, 43, 127]);
assert.equal(params.get('channel'), '0');
context.onMidiMessageInternal([0xb0, 40, 127]);
assert.equal(params.get('channel'), '3');
assert.equal(JSON.parse(params.get('generator_state')).seed, 3500);

/* Bottom-left pad 68 maps to candidate 0; adjacent right pad 72 maps to slot 0. */
context.onMidiMessageInternal([0x90, 68, 127]);
assert.equal(params.get('press_left'), '0');
context.onMidiMessageInternal([0x80, 68, 0]);
assert.equal(params.get('release_left'), '0');
context.onMidiMessageInternal([0x90, 72, 127]);
assert.equal(params.get('slot_0'), candidate);
assert.equal(params.get('press_right'), '0');
context.onMidiMessageInternal([0x80, 72, 0]);
assert.equal(params.get('release_right'), '0');
assert.deepEqual(JSON.parse(saved).slots[0], [48, 60, 64, 67]);
context.tick();
assert(filled > 40, 'saved pad opens the shared chord editor');

/* Knob 1 edits the first pitch; knob 8 moves the entire saved chord. */
context.onMidiMessageInternal([0xb0, 71, 1]);
assert.equal(params.get('slot_0'), '49,60,64,67');
context.onMidiMessageInternal([0xb0, 78, 1]);
assert.equal(params.get('slot_0'), '50,61,65,68');
assert.equal(JSON.parse(saved).slotTranspose[0], 1);
for (let i = 0; i < 20; i++) context.onMidiMessageInternal([0xb0, 78, 1]);
assert.equal(JSON.parse(saved).slotTranspose[0], 12);
assert.equal(params.get('slot_0'), '61,72,76,79');

/* Copy a saved chord to another pad, then edit that pad independently. */
context.onMidiMessageInternal([0xb0, 60, 127]);
context.onMidiMessageInternal([0x90, 73, 127]);
context.onMidiMessageInternal([0xb0, 60, 0]);
assert.equal(params.get('slot_1'), '61,72,76,79');
assert.equal(JSON.parse(saved).slotTranspose[1], 0);
context.onMidiMessageInternal([0xb0, 78, 1]);
assert.equal(params.get('slot_1'), '62,73,77,80');
assert.equal(params.get('slot_0'), '61,72,76,79');
context.onMidiMessageInternal([0x80, 73, 0]);

/* Returning to a blue pad restores the generator controls immediately. */
context.onMidiMessageInternal([0x90, 68, 127]);
context.onMidiMessageInternal([0x80, 68, 0]);
turn(74, 5);
assert.equal(JSON.parse(params.get('generator_state')).left, 0,
    'short enum turns do not skip a choice');
turn(74, 1);
assert.equal(JSON.parse(params.get('generator_state')).left, 1);
assert.equal(params.get('slot_1'), '62,73,77,80');
turn(74, 6, 127);
assert.equal(JSON.parse(params.get('generator_state')).left, 0);
context.onMidiMessageInternal([0x90, 73, 127]);
context.onMidiMessageInternal([0x80, 73, 0]);

/* Jog click returns from the chord editor to the main parameter bank. */
context.onMidiMessageInternal([0xb0, 3, 127]);

context.onMidiMessageInternal([0xb0, 55, 127]);
assert.equal(params.get('seed'), '3501');
assert.equal(params.get('slot_0'), '61,72,76,79');

/* The primary screen's knob 4 changes the left dyad. */
turn(74);
assert.equal(JSON.parse(params.get('generator_state')).left, 1);
assert.equal(JSON.parse(saved).recipe.left, 1);
assert.equal(params.get('slot_0'), '61,72,76,79');

/* Jog click opens the status display; turning a knob returns to parameters. */
context.onMidiMessageInternal([0xb0, 3, 127]);
context.tick();
assert.match(printed[0], /Shape Pads/);
context.onMidiMessageInternal([0xb0, 74, 1]);
context.tick();
assert(printed.some(String), 'a knob turn returned to the parameter display');

/* The next two parameter banks reach smoothness and bass offset. */
context.onMidiMessageInternal([0xb0, 14, 1]);
context.onMidiMessageInternal([0xb0, 76, 1]);
assert.equal(JSON.parse(params.get('generator_state')).smoothness, 1);
turn(77, 36);
assert.equal(JSON.parse(params.get('generator_state')).tonal_colour, 6);
assert.equal(JSON.parse(saved).recipe.tonal_colour, 6);
context.tick();
assert(printed.some(line => String(line) === 'b7'), 'enum list appears while turning');
turn(77, 24);
assert.equal(JSON.parse(params.get('generator_state')).tonal_colour, 10);
context.onMidiMessageInternal([0xb0, 14, 1]);
context.onMidiMessageInternal([0xb0, 72, 1]);
assert.equal(JSON.parse(params.get('generator_state')).bass_offset, -11);
turn(74);
assert.equal(JSON.parse(params.get('generator_state')).inversion, 1);
assert.equal(JSON.parse(saved).recipe.inversion, 1);

context.onMidiMessageInternal([0xb0, 119, 127]);
context.onMidiMessageInternal([0x90, 72, 127]);
assert.equal(params.get('slot_0'), '');
assert.deepEqual(JSON.parse(saved).slots[0], []);
context.onMidiMessageInternal([0xb0, 119, 0]);

/* A previous 16-step file becomes the saved pad bank on first load. */
saved = JSON.stringify({ version: 1, seed: 1234, channel: 2,
    steps: [[48, 60, 64, 67]] });
context.init();
assert.equal(params.get('slot_0'), candidate);
assert.equal(params.get('channel'), '2');

/* Five-note chords put the bass on knob 5 and keep edits on reload. */
candidate = '36,60,65,67,72';
saved = JSON.stringify({ version: 3, recipe: { bass_mode: 1 },
    slots: Array.from({ length: 16 }, () => []) });
context.init();
context.onMidiMessageInternal([0x90, 68, 127]);
context.onMidiMessageInternal([0x80, 68, 0]);
context.onMidiMessageInternal([0x90, 72, 127]);
assert.equal(params.get('slot_0'), '60,65,67,72,36');
assert.deepEqual(JSON.parse(saved).slots[0], [60, 65, 67, 72, 36]);
assert.equal(JSON.parse(saved).slotBass[0], true);
context.onMidiMessageInternal([0xb0, 75, 1]);
assert.deepEqual(JSON.parse(saved).slots[0], [60, 65, 67, 72, 37]);
context.onMidiMessageInternal([0x80, 72, 0]);
context.init();
assert.equal(params.get('slot_0'), '60,65,67,72,37');
console.log('Shape Walk Pads UI tests passed');
