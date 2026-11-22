// Runs the original FlexTalk engine inside v86 (x86 emulator in WebAssembly) on
// voxtalk's small "mini-Windows" guest kernel, and exchanges requests with it
// through a mailbox in guest memory.
const MAILBOX = 0x80000, MB_TEXT = 0x81000, RAMDISK = 0x01000000, MEM = 128 * 1024 * 1024;
const PARAM_ORDER = ['pitch', 'expression', 'rate', 'headSize', 'prominences', 'lowering', 'volume'];
const u32 = (arr) => new Uint8Array(new Uint32Array(arr).buffer);

export function createEngine({ V86, wasmPath, kernel, ramdisk, onLog }) {
    let readyResolve, readyReject;
    const ready = new Promise((res, rej) => { readyResolve = res; readyReject = rej; });
    const emulator = new V86({
        wasm_path: wasmPath,
        memory_size: MEM,
        vga_memory_size: 2 * 1024 * 1024,
        multiboot: { buffer: kernel },
        autostart: true,
        disable_keyboard: true,
        disable_mouse: true,
        disable_speaker: true,
    });

    let line = '';
    let pending = null;  // { resolve, reject }
    const queue = [];
    let running = true;

    const pause = () => { if (running) { emulator.stop(); running = false; } };
    const resume = () => { if (!running) { emulator.run(); running = true; } };

    function post(job) {
        pending = job;
        const params = new Int32Array(8);
        PARAM_ORDER.forEach((k, i) => { params[i] = Math.round(job.params[k]); });
        emulator.write_memory(new Uint8Array(params.buffer), MAILBOX + 12);
        const text = job.text.slice(0, 16000);
        const w = new Uint16Array(text.length);
        for (let i = 0; i < text.length; i++) w[i] = text.charCodeAt(i);
        emulator.write_memory(new Uint8Array(w.buffer), MB_TEXT);
        emulator.write_memory(u32([text.length]), MAILBOX + 44);
        emulator.write_memory(u32([2]), MAILBOX + 4);
        resume();
    }
    function next() { if (!pending && queue.length) post(queue.shift()); }

    emulator.add_listener('serial0-output-byte', (b) => {
        const c = String.fromCharCode(b);
        if (c !== '\n') { line += c; return; }
        const l = line.trim();
        line = '';
        if (onLog && l) onLog(l);
        if (l === 'BOOT') {
            emulator.write_memory(new Uint8Array(ramdisk), RAMDISK);
            emulator.write_memory(u32([RAMDISK, ramdisk.byteLength, MEM]), MAILBOX + 188);
            emulator.write_memory(u32([6]), MAILBOX + 4);
        } else if (l === 'READY') {
            pause();
            readyResolve();
            next();
        } else if (l === 'DONE' || l === 'ERROR') {
            const mb = new DataView(emulator.read_memory(MAILBOX, 192).slice().buffer);
            const addr = mb.getUint32(48, true), len = mb.getUint32(52, true);
            const job = pending;
            pending = null;
            if (l === 'DONE') {
                const bytes = emulator.read_memory(addr, len * 2).slice();
                job.resolve(new Int16Array(bytes.buffer));
            } else {
                let msg = '';
                for (let i = 60; i < 188 && mb.getUint8(i); i++) msg += String.fromCharCode(mb.getUint8(i));
                job.reject(new Error('Engine error: ' + msg));
            }
            if (queue.length) next(); else pause();
        } else if (l.startsWith('PANIC') || l.startsWith('ERROR')) {
            const err = new Error('Engine failure: ' + l);
            readyReject(err);
            if (pending) { pending.reject(err); pending = null; }
        }
    });

    return {
        ready,
        speak(params, text) {
            return new Promise((resolve, reject) => {
                queue.push({ params, text, resolve, reject });
                ready.then(next, reject);
            });
        },
        destroy() { emulator.destroy(); },
    };
}
