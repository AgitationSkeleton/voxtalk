// voxtalk web front end.
import { createEngine } from './engine.js';
import * as voice from './voice.js';
import * as audio from './audio.js';

const $ = (id) => document.getElementById(id);
const status = (s) => { $('status').textContent = s; };

const VOICE_SLIDERS = [
    { id: 'pitch', label: 'Pitch', min: 0, max: 100, fmt: (v) => v },
    { id: 'expression', label: 'Expression', min: 0, max: 100, fmt: (v) => v },
    { id: 'rate', label: 'Speaking rate', min: 0, max: 100, fmt: (v) => v },
    { id: 'headSize', label: 'Head size', min: 0, max: 100, fmt: (v) => v },
];
const EFFECT_SLIDERS = [
    { id: 'speed', label: 'Playback speed', min: 600, max: 1400, fmt: (v) => (v / 10).toFixed(1) + ' %' },
    { id: 'echo', label: 'Echo amount', min: 0, max: 200, fmt: (v) => v + ' %' },
    { id: 'delay', label: 'Echo delay', min: 20, max: 500, fmt: (v) => v + ' ms' },
    { id: 'lowcut', label: 'Low cut', min: 0, max: 600, fmt: (v) => (v == 0 ? 'off' : v + ' Hz') },
];

// Lines as written in the games' sound/sentences.txt
const SENTENCES = [
    ['Half-Life', 'doop (e95) doctor johnson, please call observation tank one(e100)'],
    ['Half-Life', 'doop (e95 p103) doctor freeman, (e95) to(e70) anomalous materials test lab immediately(e100)'],
    ['Half-Life', 'bloop (e95) black mesa topside temperature is ninety three degrees(e100)'],
    ['Half-Life', 'bloop (e95 p98) attention. experimental propulsion lab test fire in(s0 e100 t0) ninety minutes(e100)'],
    ['Half-Life', 'dadeda (e95) cryogenic safety crew, report status at eleven hundred please(e100)'],
    ['Half-Life', 'deeoo (e95) emergency cleanup team to(e70) biological waste processing plant(e100)'],
    ['Half-Life', 'bizwarn bizwarn bizwarn warning, high energy detonation detected in materials lab'],
    ['Half-Life', 'buzwarn buzwarn (p102) warning, unauthorized biological forms detected in sector c'],
    ['Half-Life', 'buzwarn buzwarn warning, sector c elevator failure. do not use.'],
    ['Team Fortress', 'buzwarn Your team has the(s15 e80) enemy flag'],
    ['Team Fortress', 'buzwarn The enemy has(s5) your flag'],
    ['Team Fortress', 'buzwarn you(e80) have(e90) the(s15 e80) enemy flag'],
    ['Team Fortress', 'deeoo Your(e80) flag(e80) has(s10) returned'],
    ['Team Fortress', 'dadeda The enemy flag(e80) has(s5) returned'],
    ['Team Fortress', 'endgame Your(e90) team(e80) captured(e90) the(s15 e80) flag'],
    ['Team Fortress', 'doop blue team captured red base'],
    ['Team Fortress', '(e90) command point one secured'],
];

let engine = null;
let busy = false;

// ------------------------------------------------------------ settings UI
function addSliders(container, defs) {
    for (const d of defs) {
        const lab = document.createElement('label');
        lab.htmlFor = 'sl-' + d.id;
        lab.textContent = d.label;
        const inp = document.createElement('input');
        inp.type = 'range'; inp.id = 'sl-' + d.id; inp.min = d.min; inp.max = d.max; inp.step = 1;
        const out = document.createElement('output');
        out.htmlFor = inp.id;
        inp.addEventListener('input', () => { out.textContent = d.fmt(+inp.value); saveSettings(); });
        d.el = inp; d.out = out;
        container.append(lab, inp, out);
    }
}
const allSliders = () => [...VOICE_SLIDERS, ...EFFECT_SLIDERS];
const setSlider = (id, v) => { const d = allSliders().find((s) => s.id === id); d.el.value = v; d.out.textContent = d.fmt(+d.el.value); };
const getSlider = (id) => +allSliders().find((s) => s.id === id).el.value;

function loadDefaults() {
    const v = voice.voxDefaults();
    setSlider('pitch', v.engine.pitch);
    setSlider('expression', v.engine.expression);
    setSlider('rate', v.engine.rate);
    setSlider('headSize', v.engine.headSize);
    setSlider('speed', Math.round(v.speed * 1000));
    setSlider('echo', Math.round(v.echoAmount * 100));
    setSlider('delay', Math.round(v.echoDelayMs));
    setSlider('lowcut', Math.round(v.lowCutHz));
    $('style').value = v.style;
    $('match').checked = v.gameTiming;
}

function currentSettings() {
    const v = voice.voxDefaults();
    v.engine.pitch = getSlider('pitch');
    v.engine.expression = getSlider('expression');
    v.engine.rate = getSlider('rate');
    v.engine.headSize = getSlider('headSize');
    const sp = getSlider('speed');
    if (sp !== Math.round(v.speed * 1000)) v.speed = sp / 1000;  // keep the exact 11025/12000 at the default
    v.echoAmount = getSlider('echo') / 100;
    const d = getSlider('delay');
    if (d !== Math.round(v.echoDelayMs)) v.echoDelayMs = d;
    v.lowCutHz = getSlider('lowcut');
    v.style = $('style').value;
    v.gameTiming = v.gamePitch = $('match').checked;
    return v;
}
const currentOutput = () => ({ sampleRate: +$('rate').value, eightBit: $('eightbit').checked });

const STORE = 'voxtalk.settings.v1';
function saveSettings() {
    try {
        const s = { text: $('text').value, style: $('style').value, match: $('match').checked, rate: $('rate').value,
            format: $('format').value, eightbit: $('eightbit').checked, sliders: {} };
        for (const d of allSliders()) s.sliders[d.id] = +d.el.value;
        localStorage.setItem(STORE, JSON.stringify(s));
    } catch (e) { /* storage unavailable */ }
}
function restoreSettings() {
    try {
        const s = JSON.parse(localStorage.getItem(STORE) || 'null');
        if (!s) return;
        if (typeof s.text === 'string') $('text').value = s.text;
        if (s.style) $('style').value = s.style;
        $('match').checked = !!s.match;
        if (s.rate) $('rate').value = s.rate;
        if (s.format) $('format').value = s.format;
        $('eightbit').checked = !!s.eightbit;
        for (const [id, v] of Object.entries(s.sliders || {})) setSlider(id, v);
    } catch (e) { /* ignore */ }
}

// ------------------------------------------------------------ actions
function setBusy(b) {
    busy = b;
    const en = !b && engine;
    for (const id of ['speak', 'save', 'savewords']) $(id).disabled = !en;
}
const textOrComplain = () => {
    const t = $('text').value.replace(/[\r\n\t]+/g, ' ');
    if (!t.trim()) { status('Type something to say first.'); return null; }
    return t;
};
// ------------------------------------------------------------ jobs (Stop cancels)
let job = null;
function startJob() {
    job = { ctl: new AbortController() };
    setBusy(true);
    return job;
}
const isLive = (j) => job === j && !j.ctl.signal.aborted;
function endJob(j) { if (job === j) { job = null; setBusy(false); } }
async function failJob(j, e) {
    if (!isLive(j)) return;  // stopped by the user: stay quiet
    status('Error: ' + e.message);
    if (engine && engine.failed) await restartEngine();
}
function stopAll() {
    audio.stop();
    if (job) {
        job.ctl.abort();
        job = null;
        setBusy(false);
        status('Stopped.');
    }
}
const progressFor = (j) => (done, total) => { if (isLive(j)) status(`Rendering... ${done} / ${total}`); };

async function doSpeak() {
    audio.unlockAudio();
    const t = textOrComplain();
    if (!t || busy) return;
    const j = startJob();
    status('Rendering...');
    try {
        const opts = currentOutput();
        const x = await voice.render(engine, currentSettings(), opts, t, progressFor(j), j.ctl.signal);
        if (!isLive(j)) return;
        const secs = audio.play(x, opts.sampleRate);
        status(`Playing ${secs.toFixed(2)} s.`);
    } catch (e) { await failJob(j, e); }
    finally { endJob(j); }
}

function encode(x, opts) {
    const mp3 = $('format').value === 'mp3';
    return mp3 ? { data: audio.makeMp3(x, opts.sampleRate), ext: '.mp3', type: 'audio/mpeg' }
               : { data: audio.makeWav(x, opts.sampleRate, opts.eightBit), ext: '.wav', type: 'audio/wav' };
}

async function doSave() {
    const t = textOrComplain();
    if (!t || busy) return;
    const j = startJob();
    status('Rendering...');
    try {
        const opts = currentOutput();
        const x = await voice.render(engine, currentSettings(), opts, t, progressFor(j), j.ctl.signal);
        if (!isLive(j)) return;
        const f = encode(x, opts);
        const name = voice.suggestFileName(t) + f.ext;
        audio.download(f.data, name, f.type);
        status('Saved ' + name);
    } catch (e) { await failJob(j, e); }
    finally { endJob(j); }
}

async function doSaveWords() {
    const t = textOrComplain();
    if (!t || busy) return;
    const words = voice.splitWords(t);
    if (!words.length) { status('No words to export.'); return; }
    const j = startJob();
    try {
        const opts = currentOutput();
        const vs = { ...currentSettings(), style: 'words' };
        const files = [];
        const used = new Set();
        for (let i = 0; i < words.length; i++) {
            if (!isLive(j)) return;
            status(`Rendering words... ${i + 1} / ${words.length}`);
            if (used.has(words[i].stem)) continue;
            used.add(words[i].stem);
            const x = await voice.render(engine, vs, opts, words[i].text, null, j.ctl.signal);
            const f = encode(x, opts);
            files.push({ name: (words[i].stem || 'word') + f.ext, data: f.data });
        }
        if (!isLive(j)) return;
        audio.download(audio.makeZip(files), 'voxtalk_words.zip', 'application/zip');
        status(`Saved ${files.length} word files (voxtalk_words.zip).`);
    } catch (e) { await failJob(j, e); }
    finally { endJob(j); }
}

async function previewSound() {
    audio.unlockAudio();
    try { audio.play(await voice.soundEffect($('sound').value), 11025); } catch (e) { status('Error: ' + e.message); }
}

function insertSound() {
    const ta = $('text');
    const s = ta.selectionStart ?? ta.value.length, e = ta.selectionEnd ?? s;
    let ins = $('sound').value + ' ';
    if (s > 0 && !/\s/.test(ta.value[s - 1])) ins = ' ' + ins;
    ta.value = ta.value.slice(0, s) + ins + ta.value.slice(e);
    ta.selectionStart = ta.selectionEnd = s + ins.length;
    ta.focus();
    saveSettings();
}

// ------------------------------------------------------------ tabs & sentences
function initTabs() {
    const tabs = [...document.querySelectorAll('.tab')];
    const select = (tab) => {
        for (const t of tabs) {
            const on = t === tab;
            t.classList.toggle('active', on);
            t.setAttribute('aria-selected', on);
            t.tabIndex = on ? 0 : -1;
            $(t.getAttribute('aria-controls')).hidden = !on;
        }
    };
    tabs.forEach((t, i) => {
        t.addEventListener('click', () => select(t));
        t.addEventListener('keydown', (e) => {
            const d = e.key === 'ArrowRight' ? 1 : e.key === 'ArrowLeft' ? -1 : 0;
            if (d) { const n = tabs[(i + d + tabs.length) % tabs.length]; select(n); n.focus(); }
        });
    });
    select(tabs[0]);
    return select;
}

function initSentences(selectTab) {
    const list = $('sentence-list');
    for (const [src, line] of SENTENCES) {
        const it = document.createElement('div');
        it.className = 'item';
        it.tabIndex = 0;
        it.setAttribute('role', 'option');
        it.setAttribute('aria-selected', 'false');
        const a = document.createElement('span'); a.className = 'src'; a.textContent = src;
        const b = document.createElement('span'); b.textContent = line;
        it.append(a, b);
        const choose = () => {
            for (const o of list.children) o.setAttribute('aria-selected', o === it ? 'true' : 'false');
            $('text').value = line;
            $('style').value = 'words';
            saveSettings();
            selectTab(document.getElementById('tabbtn-speak'));
            status('Loaded a sentence. Press Speak.');
        };
        it.addEventListener('click', choose);
        it.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); choose(); } });
        list.append(it);
    }
}

// ------------------------------------------------------------ engine boot
async function fetchWithProgress(url, label) {
    const r = await fetch(url);
    if (!r.ok) throw new Error(`could not load ${url}`);
    const total = +r.headers.get('content-length') || 0;
    if (!r.body || !total) return r.arrayBuffer();
    const reader = r.body.getReader();
    const chunks = [];
    let got = 0;
    for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        chunks.push(value);
        got += value.length;
        status(`${label} ${Math.min(100, Math.round(got * 100 / total))}%`);
    }
    const out = new Uint8Array(got);
    let o = 0;
    for (const c of chunks) { out.set(c, o); o += c.length; }
    return out.buffer;
}
async function gunzip(buf) {
    if (typeof DecompressionStream === 'undefined') throw new Error('this browser cannot unpack the engine (DecompressionStream missing)');
    const stream = new Blob([buf]).stream().pipeThrough(new DecompressionStream('gzip'));
    return new Response(stream).arrayBuffer();
}

async function boot() {
    try {
        status('Loading the speech engine...');
        const [{ V86 }, kernel, packed] = await Promise.all([
            import('../vendor/libv86.mjs'),
            fetch('engine/kernel.bin').then((r) => r.arrayBuffer()),
            fetchWithProgress('engine/engine.vxrd.gz', 'Loading the speech engine...'),
        ]);
        status('Unpacking the speech engine...');
        const ramdisk = await gunzip(packed);
        engineFiles = { V86, kernel, ramdisk };
        status('Starting FlexTalk...');
        const e = createEngine({ V86, wasmPath: 'vendor/v86.wasm', kernel, ramdisk });
        await e.ready;
        engine = e;
        setBusy(false);
        status('Ready.');
    } catch (err) {
        status('Could not start the speech engine: ' + err.message);
    }
}

// Restart the emulated engine (files are already in memory) after a failure.
let engineFiles = null;
async function restartEngine() {
    if (!engineFiles) return;
    const old = engine;
    engine = null;
    setBusy(true);
    status('Restarting the speech engine...');
    if (old) old.destroy();
    try {
        const { V86, kernel, ramdisk } = engineFiles;
        const e = createEngine({ V86, wasmPath: 'vendor/v86.wasm', kernel, ramdisk });
        await e.ready;
        engine = e;
        status('The speech engine was restarted. Ready.');
    } catch (err) {
        status('Could not restart the speech engine: ' + err.message);
    }
    setBusy(false);
}

// ------------------------------------------------------------ init
addSliders($('voice-sliders'), VOICE_SLIDERS);
addSliders($('effect-sliders'), EFFECT_SLIDERS);
for (const n of voice.SOUND_EFFECTS) {
    const o = document.createElement('option');
    o.value = o.textContent = n;
    $('sound').append(o);
}
loadDefaults();
restoreSettings();
const selectTab = initTabs();
initSentences(selectTab);

$('speak').addEventListener('click', doSpeak);
$('stop').addEventListener('click', stopAll);
$('save').addEventListener('click', doSave);
$('savewords').addEventListener('click', doSaveWords);
$('reset').addEventListener('click', () => { loadDefaults(); saveSettings(); });
$('insert').addEventListener('click', insertSound);
$('sound').addEventListener('change', previewSound);
for (const id of ['text', 'style', 'match', 'rate', 'format', 'eightbit']) $(id).addEventListener('change', saveSettings);
$('text').addEventListener('keydown', (e) => { if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) { e.preventDefault(); doSpeak(); } });
setBusy(true);
boot();
