// Parity harness (docs/parity.md): renders a corpus through the native renderer and through the
// wasm module in node, and fails when any case differs by more than the tolerance.
//
// node parity.mjs --wasm <module.wasm> --render <module_render> --corpus <corpus.json>
//                 [--tolerance 1e-5]

import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import getMagdaDeviceAbi from '../wam/js/src/getMagdaDeviceAbi.js';

const args = Object.fromEntries(process.argv.slice(2).reduce((pairs, value, i, all) => {
	if (i % 2 === 0) pairs.push([value.replace(/^--/, ''), all[i + 1]]);
	return pairs;
}, []));
if (!args.wasm || !args.render || !args.corpus) {
	console.error('usage: parity.mjs --wasm <module.wasm> --render <renderer> --corpus <corpus.json>');
	process.exit(2);
}
const tolerance = Number(args.tolerance ?? 1e-5);

// The same formula as render.cpp.
const inputSample = (kind, frequency, frame, sampleRate) => {
	if (kind === 'impulse') return frame === 0 ? 1 : 0;
	if (kind === 'sine') return Math.fround(0.5 * Math.sin(2 * Math.PI * frequency * frame / sampleRate));
	return 0;
};

const { MagdaDeviceModule } = getMagdaDeviceAbi();
const module = new MagdaDeviceModule(readFileSync(args.wasm));

const renderWasm = (spec) => {
	const sampleRate = spec.sampleRate ?? 48000;
	const blockSize = spec.blockSize ?? 128;
	const channels = spec.channels ?? 2;
	const frames = spec.frames ?? sampleRate;
	const device = module.create(spec.device);
	if (spec.state && device.setState(JSON.stringify(spec.state)) !== 0)
		console.error(`${spec.name}: state: ${device.lastError}`);
	device.prepare(sampleRate, blockSize, channels);
	const slots = Object.fromEntries(device.manifest.parameters.map((p) => [p.id, p.index]));
	for (const [id, value] of Object.entries(spec.parameters ?? {}))
		if (id in slots) device.setParameter(slots[id], value);

	const output = Array.from({ length: channels }, () => new Float32Array(frames));
	const input = spec.input ?? 'silence';
	const inputFrequency = spec.inputFrequency ?? 440;
	for (let start = 0; start < frames; start += blockSize) {
		const end = Math.min(frames, start + blockSize);
		for (const channel of output)
			for (let i = start; i < end; i++) channel[i] = inputSample(input, inputFrequency, i, sampleRate);
		for (const event of spec.midi ?? [])
			if (event.sample >= start && event.sample < end) device.midi(event.bytes, event.sample - start);
		device.process(output, start, end);
	}
	device.destroy();
	return output;
};

const corpus = JSON.parse(readFileSync(args.corpus, 'utf8'));
const outDir = mkdtempSync(join(tmpdir(), 'magda-parity-'));
let failed = 0;
try {
	execFileSync(args.render, ['--corpus', args.corpus, '--out', outDir], { stdio: 'inherit' });
	for (const spec of corpus.cases) {
		const native = new Float32Array(readFileSync(join(outDir, `${spec.name}.f32`)).buffer.slice(0));
		const wasm = renderWasm(spec);
		const frames = wasm[0].length;
		let maxDiff = 0;
		let peak = 0;
		wasm.forEach((channel, c) => {
			for (let i = 0; i < frames; i++) {
				maxDiff = Math.max(maxDiff, Math.abs(channel[i] - native[c * frames + i]));
				peak = Math.max(peak, Math.abs(channel[i]));
			}
		});
		const silent = peak === 0 && !spec.expectSilence;
		const ok = maxDiff <= tolerance && native.length === frames * wasm.length && !silent;
		if (!ok) failed++;
		console.log(`${ok ? 'PASS' : 'FAIL'} ${spec.name}  max diff ${maxDiff.toExponential(2)}  peak ${peak.toFixed(4)}${silent ? '  (silent)' : ''}`);
	}
} finally {
	rmSync(outDir, { recursive: true, force: true });
}
console.log(failed ? `${failed} of ${corpus.cases.length} cases differ` : `all ${corpus.cases.length} cases match`);
process.exit(failed ? 1 : 0);
