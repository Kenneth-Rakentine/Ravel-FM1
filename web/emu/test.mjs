// SPDX-License-Identifier: GPL-3.0-only
// Smoke test for the browser emulator: boot, play a note, check sound and screen. node web/emu/test.mjs
import { readFileSync } from 'node:fs';
const wasm = readFileSync(new URL('./ravel.wasm', import.meta.url));
const { instance } = await WebAssembly.instantiate(wasm, {});
const rv = instance.exports;
const imports = WebAssembly.Module.imports(new WebAssembly.Module(wasm));
if (imports.length) throw new Error('unexpected imports: ' + imports.map(i => i.name).join(', '));
rv.rv_boot();
rv.rv_run(200);
rv.rv_set_keys(1 << 12);
let peak = 0, frames = 0;
for (let t = 0; t < 30; t++) {
  const n = rv.rv_run(16);
  frames += n;
  const a = new Float32Array(rv.memory.buffer, rv.rv_audio(), n * 2);
  for (const v of a) peak = Math.max(peak, Math.abs(v));
}
rv.rv_set_keys(0);
const fb = new Uint16Array(rv.memory.buffer, rv.rv_fb(), 240 * 240);
const colours = new Set(fb).size;
console.log(`frames ${frames} (expect ~${Math.round(rv.rv_sample_rate() * 0.48)}), peak ${(20 * Math.log10(peak)).toFixed(1)} dB, ${colours} screen colours, ${rv.rv_param_count()} params`);
if (peak < 0.01 || colours < 4 || Math.abs(frames - rv.rv_sample_rate() * 0.48) > 600) { console.log('FAIL'); process.exit(1); }
console.log('ok');
