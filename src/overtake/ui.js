import {
    MovePads, MoveSteps, Blue, DarkGrey, White, BrightGreen
} from '/data/UserData/schwung/shared/constants.mjs';
import { setLED, decodeDelta } from '/data/UserData/schwung/shared/input_filter.mjs';
import { announce } from '/data/UserData/schwung/shared/screen_reader.mjs';
import { renderPageMovy } from '/data/UserData/schwung/shared/param_pages/render_page_movy.mjs';
import { drawEnumList } from '/data/UserData/schwung/shared/param_pages/enum_list.mjs';
import { listKnobInit, listKnobStep } from '/data/UserData/schwung/shared/param_pages/list_knob.mjs';
import { buildMetaIndex, NOTE_NAMES } from '/data/UserData/schwung/shared/param_pages/param_meta.mjs';

const SAVE_PATH = '/data/UserData/schwung/shape-walk-seq.json';
const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const PAD_COUNT = 16;
const DEFAULT_RECIPE = {
    seed: 1234, root: 60, length: 8, left: 0, right: 1,
    distance: 20, hand_gap: 0, path: 0, wander: 25, split: 25,
    continuity: 65, colour: 25, tension: 25, smoothness: 0,
    tonal_colour: 0, tonal_pull: 0, inversion: 0, bass_mode: 0, bass_offset: -12,
    bass_motion: 50
};
const PARAM_PAGES = [
    [
        { key: 'seed', label: 'Seed', min: 0, max: 9999 },
        { key: 'root', label: 'Root', min: 0, max: 11, names: NAMES },
        { key: 'length', label: 'Chords', values: [4, 8, 16] },
        { key: 'left', label: 'Left', names: ['4th', '5th', 'either'] },
        { key: 'right', label: 'Right', names: ['4th', '5th', 'either'] },
        { key: 'distance', label: 'Dist', min: 0, max: 100 },
        { key: 'hand_gap', label: 'Gap', min: 0, max: 12 },
        { key: 'path', label: 'Path', names: ['loop', 'mirror', 'fifths', 'free'] }
    ],
    [
        { key: 'wander', label: 'Wander', min: 0, max: 100 },
        { key: 'split', label: 'Split', min: 0, max: 100 },
        { key: 'continuity', label: 'Cont', min: 0, max: 100 },
        { key: 'colour', label: 'Colour', min: 0, max: 100 },
        { key: 'tension', label: 'Tension', min: 0, max: 100 },
        { key: 'smoothness', label: 'Smooth', min: 0, max: 100 },
        { key: 'tonal_colour', label: 'Tonality', names: ['open', 'major', 'minor', '2nd', '6th', 'b6', 'b7', 'maj7', 'min7', 'maj9', 'min9'] },
        { key: 'tonal_pull', label: 'Pull', min: 0, max: 100 }
    ],
    [
        { key: 'bass_mode', label: 'Bass', names: ['off', 'follow', 'wander'] },
        { key: 'bass_offset', label: 'Offset', min: -24, max: 0 },
        { key: 'bass_motion', label: 'Motion', min: 0, max: 100 },
        { key: 'inversion', label: 'Invert', names: ['root', '1st', '2nd', '3rd'] }
    ]
];
const PAGE_NAMES = ['Main', 'Motion', 'Bass'];
const RENDER_PAGES = PARAM_PAGES.map((params, i) => ({
    kind: 'knobs', name: PAGE_NAMES[i],
    keys: Array.from({ length: 8 }, (_, knob) => params[knob]?.key || null)
}));
const PARAM_META = buildMetaIndex({ hierarchy: { levels: {
    root: { params: PARAM_PAGES.flat().map(param => ({
        key: param.key, name: param.label, label: param.label,
        type: (param.names || param.values) ? 'enum' : 'int',
        min: param.min, max: param.max,
        options: param.names || (param.values && param.values.map(String)),
        short_options: param.names || (param.values && param.values.map(String))
    })) }
} } });

let recipe = null;
let seed = 1234;
let channel = 0;
let candidateCount = 16;
let slots = Array.from({ length: PAD_COUNT }, () => []);
let slotBass = Array(PAD_COUNT).fill(false);
let slotTranspose = Array(PAD_COUNT).fill(0);
let selectedCandidate = -1;
let lastPlayedNotes = [];
let lastPlayedBass = false;
let selectedSlot = -1;
let editSlot = -1;
let clipboard = null;
let copyHeld = false;
let copyArmed = false;
let pastedWhileHeld = false;
let heldPads = new Set();
let shiftHeld = false;
let deleteHeld = false;
let page = 0;
let statusOpen = false;
let focusedParam = null;
let focusKnob = -1;
let focusUntil = 0;
let touchedKnobs = new Set();
let enumPeek = null;
const enumKnobs = new Map();
let message = 'Left: explore  Right: save';
let redraw = true;
let ledQueue = [];

function dspSet(key, value) {
    if (typeof host_module_set_param_blocking === 'function')
        return host_module_set_param_blocking(key, String(value), 500);
    if (typeof host_module_set_param === 'function')
        return host_module_set_param(key, String(value));
}

function dspGet(key) {
    if (typeof host_module_get_param !== 'function') return null;
    return host_module_get_param(key);
}

function clamp(value, low, high) {
    return Math.max(low, Math.min(high, value));
}

function parseNotes(text) {
    if (!text) return [];
    return String(text).split(',').map(Number).filter(n =>
        Number.isInteger(n) && n >= 0 && n <= 127).slice(0, 6);
}

function describe(notes) {
    if (!notes || !notes.length) return 'empty';
    return notes.map(n => NAMES[n % 12]).join(' ');
}

function paletteSize(state) {
    const length = Number(state && state.length);
    return length === 4 || length === 8 || length === 16 ? length : 16;
}

function paramValue(param) {
    const raw = Number(recipe[param.key]);
    if (param.values) return String(raw);
    if (param.names) return param.names[param.key === 'root' ?
        ((raw % 12) + 12) % 12 : raw] || '?';
    return String(raw);
}

function displayValues() {
    const values = {};
    for (const param of PARAM_PAGES[page]) {
        const raw = Number(recipe[param.key]);
        values[param.key] = param.values ? param.values.indexOf(raw) :
            param.key === 'root' ? ((raw % 12) + 12) % 12 : raw;
    }
    return values;
}

function editPage() {
    const chord = slots[editSlot] || [];
    return { kind: 'knobs', name: `Pad ${editSlot + 1}`,
        keys: Array.from({ length: 8 }, (_, knob) =>
            knob < 6 && knob < chord.length ? `note_${knob}` :
            knob === 7 ? 'transpose' : null) };
}

function editMeta() {
    const chord = slots[editSlot] || [];
    const params = chord.slice(0, 6).map((_, i) => ({
        key: `note_${i}`,
        name: slotBass[editSlot] && i === 4 ? 'Bass' : `Note ${i + 1}`,
        type: 'enum', options: NOTE_NAMES, short_options: NOTE_NAMES
    }));
    params.push({ key: 'transpose', name: 'Transpose', type: 'int',
        min: -12, max: 12, default: 0 });
    return buildMetaIndex({ hierarchy: { levels: { root: { params } } } });
}

function editValues() {
    const values = { transpose: slotTranspose[editSlot] || 0 };
    for (let i = 0; i < slots[editSlot].length && i < 6; i++)
        values[`note_${i}`] = slots[editSlot][i];
    return values;
}

function writeEditedChord(index, chord, notice) {
    slots[index] = chord;
    dspSet(`slot_${index}`, chord.join(','));
    message = notice;
    announce(message);
    redraw = true;
    save();
}

function editSavedChord(knob, delta) {
    if (editSlot < 0 || !delta || knob === 6 || knob > 7) return;
    const chord = slots[editSlot];
    if (!chord.length) return;
    const amount = shiftHeld ? 12 : Math.abs(delta);
    const direction = delta > 0 ? 1 : -1;
    if (knob === 7) {
        const current = slotTranspose[editSlot];
        const wanted = clamp(current + direction * amount, -12, 12);
        let change = wanted - current;
        const low = Math.min(...chord), high = Math.max(...chord);
        change = clamp(change, -low, 127 - high);
        if (!change) return;
        slotTranspose[editSlot] = current + change;
        writeEditedChord(editSlot, chord.map(note => note + change),
            `Pad ${editSlot + 1} transpose ${slotTranspose[editSlot]}`);
    } else if (knob < chord.length) {
        const next = clamp(chord[knob] + direction * amount, 0, 127);
        if (next === chord[knob] || chord.some((note, i) => i !== knob && note === next)) return;
        const changed = chord.slice();
        changed[knob] = next;
        writeEditedChord(editSlot, changed,
            `Pad ${editSlot + 1} note ${knob + 1} ${NOTE_NAMES[next]}`);
    }
    focusKnob = knob;
    focusUntil = Date.now() + 1500;
    statusOpen = false;
    queueLeds();
}

function editParameter(knob, delta) {
    if (!delta) return;
    enumPeek = null;
    if (editSlot >= 0) {
        editSavedChord(knob, delta);
        return;
    }
    if (statusOpen) { statusOpen = false; redraw = true; }
    const param = PARAM_PAGES[page][knob];
    if (!param) return;
    const current = Number(recipe[param.key]);
    const direction = delta > 0 ? 1 : -1;
    let next;
    if (param.values) {
        const index = param.values.indexOf(current);
        next = param.values[clamp(index + direction, 0, param.values.length - 1)];
    } else if (param.names) {
        const state = enumKnobs.get(param.key) || listKnobInit();
        enumKnobs.set(param.key, state);
        const steps = shiftHeld ? delta : listKnobStep(state, delta, Date.now(), param.names.length);
        const value = param.key === 'root' ? ((current % 12) + 12) % 12 : current;
        next = clamp(value + steps, 0, param.names.length - 1);
        enumPeek = { title: param.label, options: param.names, index: next,
            until: Date.now() + 1500 };
        redraw = true;
    } else {
        const amount = shiftHeld ? 10 : Math.abs(delta);
        next = clamp(current + direction * amount, param.min, param.max);
    }
    if (next === current || (param.key === 'root' && next === current % 12)) return;
    recipe[param.key] = next;
    if (param.key === 'seed') {
        seed = next;
        dspSet('seed', seed);
    } else {
        dspSet('generator_state', JSON.stringify(recipe));
    }
    if (param.key === 'length') {
        candidateCount = next;
        queueLeds();
    }
    focusedParam = param;
    focusKnob = knob;
    focusUntil = Date.now() + 1500;
    statusOpen = false;
    queueLeds();
    message = `${param.label} ${paramValue(param)}`;
    announce(message);
    redraw = true;
    save();
}

/* The hardware numbers pads left to right within each row, bottom row first. */
function gridPad(index) {
    const column = index % 8;
    const row = Math.floor(index / 8);
    return { side: column < 4 ? 'left' : 'right',
        index: row * 4 + (column % 4) };
}

function liveRecipe() {
    if (typeof shadow_get_param !== 'function') return null;
    let selected = 0;
    try { selected = Number(shadow_get_selected_slot()) || 0; } catch (_e) {}
    const tracks = [...new Set([selected, 0, 1, 2, 3])];
    for (const track of tracks) {
        for (let fx = 1; fx <= 8; fx++) {
            try {
                if (shadow_get_param(track, `midi_fx${fx}_module`) !== 'shape-walk')
                    continue;
                const parsed = JSON.parse(shadow_get_param(track, `midi_fx${fx}:state`));
                if (parsed && typeof parsed === 'object') return parsed;
            } catch (_e) {}
        }
    }
    return null;
}

function save() {
    if (typeof host_write_file !== 'function') return;
    const content = JSON.stringify({ version: 3, recipe, seed, channel,
        slots, slotBass, slotTranspose });
    if (!host_write_file(SAVE_PATH, content)) {
        message = 'Could not save chords';
        redraw = true;
    }
}

function queueLeds() {
    ledQueue = [];
    for (let i = 0; i < MovePads.length; i++) {
        const pad = gridPad(i);
        const active = pad.side === 'left' ? pad.index < candidateCount :
            slots[pad.index].length > 0;
        const color = heldPads.has(i) ? White : !active ? DarkGrey :
            pad.side === 'left' ? Blue : BrightGreen;
        ledQueue.push([MovePads[i], color]);
    }
    for (let i = 0; i < MoveSteps.length; i++)
        ledQueue.push([MoveSteps[i], (statusOpen ? i === 3 : i === page) ? White :
            i < 4 ? Blue : DarkGrey]);
}

function draw() {
    clear_screen();
    if (!statusOpen && editSlot < 0 && enumPeek && Date.now() < enumPeek.until) {
        drawEnumList({ fillRect: fill_rect, print, textWidth: text_width }, {
            title: enumPeek.title, headerRight: 'TURNING',
            options: enumPeek.options, index: enumPeek.index,
            markIndex: enumPeek.index, footer: [['TURN', 'SET']]
        });
        redraw = false;
        return;
    }
    if (!statusOpen && editSlot >= 0) {
        const touched = [...touchedKnobs];
        if (!touched.length && focusKnob >= 0 && Date.now() < focusUntil)
            touched.push(focusKnob);
        renderPageMovy({ fillRect: fill_rect, print, textWidth: text_width }, {
            title: 'Shape Walk', page: editPage(), pageIndex: 0, pageCount: 1,
            metaIndex: editMeta(), values: editValues(),
            touched: touched.length ? touched[touched.length - 1] : -1,
            touchedSlots: touched,
            footer: [['COPY', 'PAD'], ['CLICK', 'BACK']], nowMs: Date.now()
        });
        redraw = false;
        return;
    }
    if (!statusOpen) {
        const touched = [...touchedKnobs];
        if (!touched.length && focusKnob >= 0 && Date.now() < focusUntil)
            touched.push(focusKnob);
        renderPageMovy({ fillRect: fill_rect, print, textWidth: text_width }, {
            title: 'Shape Walk',
            page: RENDER_PAGES[page],
            pageIndex: page,
            pageCount: PARAM_PAGES.length,
            metaIndex: PARAM_META,
            values: displayValues(),
            touched: touched.length ? touched[touched.length - 1] : -1,
            touchedSlots: touched,
            footer: [['CLICK', 'STATUS']],
            nowMs: Date.now()
        });
        redraw = false;
        return;
    }
    print(0, 0, `Shape Pads  S${seed}`, 2);
    print(0, 17, `Track ${channel + 1}  L:${candidateCount} R:${slots.filter(s => s.length).length}`, 1);
    if (selectedSlot >= 0)
        print(0, 33, `Saved ${selectedSlot + 1}: ${describe(slots[selectedSlot])}`.slice(0, 24), 1);
    else if (selectedCandidate >= 0)
        print(0, 33, `Chord ${selectedCandidate + 1}: ${describe(parseNotes(dspGet(`candidate_${selectedCandidate}`)))}`.slice(0, 24), 1);
    else
        print(0, 33, 'Left: chords  Right: saved', 1);
    print(0, 50, message.slice(0, 25), 1);
    redraw = false;
}

function load() {
    let stored = null;
    if (typeof host_read_file === 'function') {
        try { stored = JSON.parse(host_read_file(SAVE_PATH)); } catch (_e) {}
    }
    recipe = { ...DEFAULT_RECIPE,
        ...(stored && stored.recipe ? stored.recipe : liveRecipe() || {}) };
    seed = clamp(Number(recipe.seed) || 0, 0, 9999);
    candidateCount = paletteSize(recipe);
    if (stored) {
        seed = clamp(Number(stored.seed ?? seed), 0, 9999);
        channel = clamp(Number(stored.channel) || 0, 0, 3);
        /* Existing sixteenth-note patterns become the right-hand pad bank. */
        const previous = Array.isArray(stored.slots) ? stored.slots : stored.steps;
        if (Array.isArray(previous))
            slots = Array.from({ length: PAD_COUNT }, (_, i) =>
                Array.isArray(previous[i]) ? previous[i].filter(n =>
                    Number.isInteger(n) && n >= 0 && n <= 127).slice(0, 6) : []);
        if (Array.isArray(stored.slotBass))
            slotBass = Array.from({ length: PAD_COUNT }, (_, i) =>
                !!stored.slotBass[i] && slots[i].length >= 5);
        else if (recipe.bass_mode)
            for (let i = 0; i < PAD_COUNT; i++) {
                slotBass[i] = slots[i].length >= 5;
                if (slotBass[i]) slots[i] = arrangeVoices(slots[i], true);
            }
        if (Array.isArray(stored.slotTranspose))
            slotTranspose = Array.from({ length: PAD_COUNT }, (_, i) =>
                clamp(Number(stored.slotTranspose[i]) || 0, -12, 12));
    } else if (typeof shadow_get_selected_slot === 'function') {
        try { channel = clamp(Number(shadow_get_selected_slot()) || 0, 0, 3); }
        catch (_e) {}
    }
    recipe.seed = seed;
    dspSet('generator_state', JSON.stringify(recipe));
    dspSet('seed', seed);
    dspSet('channel', channel);
    for (let i = 0; i < PAD_COUNT; i++) dspSet(`slot_${i}`, slots[i].join(','));
}

function selectSeed(next) {
    seed = (next + 10000) % 10000;
    dspSet('seed', seed);
    recipe.seed = seed;
    message = `Seed ${seed}; saved kept`;
    announce(message);
    redraw = true;
    save();
}

function chooseTrack(next) {
    channel = clamp(next, 0, 3);
    dspSet('channel', channel);
    message = `Track ${channel + 1}`;
    announce(message);
    redraw = true;
    save();
}

function arrangeVoices(notes, hasBass) {
    const ordered = notes.slice().sort((a, b) => a - b);
    if (!hasBass || ordered.length < 5) return ordered;
    const bass = ordered.shift();
    return ordered.slice(0, 4).concat(bass, ordered.slice(4));
}

function setSlot(index, notes, hasBass = false) {
    slots[index] = arrangeVoices(notes, hasBass);
    slotBass[index] = !!hasBass && notes.length >= 5;
    slotTranspose[index] = 0;
    dspSet(`slot_${index}`, slots[index].join(','));
    selectedSlot = index;
    message = notes.length ? `Saved chord to pad ${index + 1}` :
        `Cleared pad ${index + 1}`;
    announce(message);
    redraw = true;
    queueLeds();
    save();
}

function pasteSlot(index) {
    if (!clipboard) return;
    slots[index] = clipboard.notes.slice();
    slotBass[index] = clipboard.hasBass;
    slotTranspose[index] = 0;
    dspSet(`slot_${index}`, slots[index].join(','));
    selectedSlot = editSlot = index;
    message = `Pasted chord to pad ${index + 1}`;
    announce(message);
    redraw = true;
    queueLeds();
    save();
}

function startCopy(index) {
    if (index < 0 || !slots[index].length) return false;
    clipboard = { notes: slots[index].slice(), hasBass: slotBass[index] };
    copyArmed = true;
    selectedSlot = editSlot = index;
    message = `Copied pad ${index + 1}; tap destination`;
    announce(message);
    redraw = true;
    return true;
}

globalThis.init = function() {
    load();
    queueLeds();
    redraw = true;
    announce('Shape Walk Pads. Play left chords, tap an empty right pad to save the last chord.');
};

globalThis.onResume = function() {
    queueLeds();
    redraw = true;
};

globalThis.onSuspend = function() {
    dspSet('release_all', 1);
    heldPads.clear();
};

globalThis.tick = function() {
    if (enumPeek && Date.now() >= enumPeek.until) {
        enumPeek = null;
        redraw = true;
    }
    if (!statusOpen && !touchedKnobs.size && focusKnob >= 0 &&
        Date.now() >= focusUntil) {
        focusKnob = -1;
        focusedParam = null;
        redraw = true;
    }
    for (let i = 0; i < 8 && ledQueue.length; i++) {
        const [note, color] = ledQueue.shift();
        setLED(note, color, true);
    }
    if (redraw) draw();
};

globalThis.onMidiMessageInternal = function(data) {
    if (!data || data.length < 3) return;
    const status = data[0] & 0xf0;
    const d1 = data[1] | 0;
    const d2 = data[2] | 0;
    const noteOn = status === 0x90 && d2 > 0;
    const noteOff = status === 0x80 || (status === 0x90 && d2 === 0);
    if (status === 0xb0) {
        if (d1 === 49) { shiftHeld = d2 > 0; return; }
        if (d1 === 119) { deleteHeld = d2 > 0; return; }
        if (d1 === 60) {
            copyHeld = d2 > 0;
            if (copyHeld) {
                pastedWhileHeld = false;
                if (editSlot >= 0) startCopy(editSlot);
                else {
                    clipboard = null;
                    copyArmed = true;
                    message = 'Tap a saved pad to copy';
                    announce(message);
                    redraw = true;
                }
            } else if (pastedWhileHeld) copyArmed = false;
            return;
        }
        if (d1 === 3 && d2 > 0) {
            enumPeek = null;
            if (editSlot >= 0 && !statusOpen) editSlot = -1;
            else statusOpen = !statusOpen;
            focusedParam = null;
            queueLeds();
            redraw = true;
            announce(statusOpen ? 'Chord status' : `Parameter page ${page + 1}`);
            return;
        }
        if (d1 === 14) {
            const delta = decodeDelta(d2);
            if (delta && !statusOpen && editSlot < 0) {
                const next = clamp(page + (delta > 0 ? 1 : -1), 0, PARAM_PAGES.length - 1);
                if (next !== page) {
                    enumPeek = null;
                    page = next;
                    focusedParam = null;
                    focusKnob = -1;
                    queueLeds();
                    redraw = true;
                    announce(`${PAGE_NAMES[page]} parameters`);
                }
            }
            return;
        }
        if (d1 >= 71 && d1 <= 78) {
            editParameter(d1 - 71, decodeDelta(d2));
            return;
        }
        if (!d2) return;
        if (d1 >= 40 && d1 <= 43) chooseTrack(43 - d1);
        else if (d1 === 55 && shiftHeld) {
            const latest = liveRecipe();
            if (latest) {
                recipe = { ...DEFAULT_RECIPE, ...latest };
                seed = clamp(Number(recipe.seed) || 0, 0, 9999);
                candidateCount = paletteSize(recipe);
                dspSet('generator_state', JSON.stringify(recipe));
                message = 'Imported Shape Walk';
                announce(message);
                redraw = true;
                queueLeds();
                save();
            }
        } else if (d1 === 55) selectSeed(seed + 1);
        else if (d1 === 54) selectSeed(seed - 1);
        return;
    }
    if (noteOn && d1 >= 16 && d1 <= 19) {
        enumPeek = null;
        if (d1 === 19) statusOpen = !statusOpen;
        else { page = d1 - 16; statusOpen = false; editSlot = -1; }
        focusedParam = null;
        message = statusOpen ? 'Chord status' : `Parameter page ${page + 1}`;
        announce(message);
        queueLeds();
        redraw = true;
        return;
    }
    if ((status === 0x90 || status === 0x80) && d1 >= 0 && d1 <= 7) {
        if (noteOn) touchedKnobs.add(d1);
        else if (noteOff) touchedKnobs.delete(d1);
        redraw = true;
        return;
    }
    if ((status !== 0x90 && status !== 0x80) || d1 < 68 || d1 > 99) return;
    const hardwareIndex = d1 - 68;
    const pad = gridPad(hardwareIndex);
    if (pad.side === 'left') {
        if (noteOn && pad.index < candidateCount) {
            enumPeek = null;
            selectedCandidate = pad.index;
            lastPlayedNotes = parseNotes(dspGet(`candidate_${pad.index}`));
            lastPlayedBass = !!recipe.bass_mode && lastPlayedNotes.length >= 5;
            selectedSlot = -1;
            editSlot = -1;
            statusOpen = false;
            focusKnob = -1;
            focusedParam = null;
            heldPads.add(hardwareIndex);
            dspSet('press_left', pad.index);
            message = 'Tap empty right pad to save';
            redraw = true;
            queueLeds();
        } else if (noteOff) {
            heldPads.delete(hardwareIndex);
            dspSet('release_left', pad.index);
            queueLeds();
        }
    } else if (noteOn) {
        enumPeek = null;
        selectedSlot = pad.index;
        if (deleteHeld) {
            setSlot(pad.index, []);
            if (editSlot === pad.index) editSlot = -1;
            return;
        }
        if (copyArmed) {
            if (!clipboard) {
                if (startCopy(pad.index)) return;
            } else if (pad.index !== editSlot) {
                pasteSlot(pad.index);
                pastedWhileHeld = true;
                if (!copyHeld) copyArmed = false;
                return;
            } else return;
        }
        if (!slots[pad.index].length && lastPlayedNotes.length)
            setSlot(pad.index, lastPlayedNotes, lastPlayedBass);
        if (slots[pad.index].length) {
            editSlot = pad.index;
            statusOpen = false;
            heldPads.add(hardwareIndex);
            dspSet('press_right', pad.index);
            message = `Saved ${pad.index + 1}: ${describe(slots[pad.index])}`;
        } else {
            message = 'Play a left chord first';
        }
        redraw = true;
        queueLeds();
    } else if (noteOff) {
        heldPads.delete(hardwareIndex);
        dspSet('release_right', pad.index);
        queueLeds();
    }
};

globalThis.onMidiMessageExternal = function(_data) {};

globalThis.onUnload = function() {
    dspSet('release_all', 1);
    save();
};
