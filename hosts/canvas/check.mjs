// Runs the meter, curve editor and waveform view golden scenarios through the wasm build in node and compares
// each frame with the native golden (tests/golden), and the JS palette with the C++ one.
//
// node check.mjs --wasm <magda_sdk_ui_demo.wasm> [--tolerance 1.5e-3]

import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { colourRoles, defaultPalette } from './js/display-list.js';
import { firstDifference, loadUiDemo } from './js/ui-demo-wasm.js';

const args = Object.fromEntries(process.argv.slice(2).reduce((pairs, value, i, all) => {
	if (i % 2 === 0) pairs.push([value.replace(/^--/, ''), all[i + 1]]);
	return pairs;
}, []));
if (!args.wasm) {
	console.error('usage: check.mjs --wasm <magda_sdk_ui_demo.wasm>');
	process.exit(2);
}
const tolerance = Number(args.tolerance ?? 1.5e-3);
const goldenRoot = join(dirname(fileURLToPath(import.meta.url)), '../../tests/golden');

const demo = await loadUiDemo(readFileSync(args.wasm));

let failures = 0;
for (const [suite, core] of [
	['meter', demo.meter], ['curve-editor', demo.curveEditor], ['waveform-view', demo.waveform],
]) {
	const dir = join(goldenRoot, suite);
	const scenariosText = readFileSync(join(dir, 'scenarios.json'), 'utf8');
	JSON.parse(scenariosText).cases.forEach((c, index) => {
		const golden = JSON.parse(readFileSync(join(dir, `${c.name}.json`), 'utf8'));
		const difference = firstDifference(core.runCase(scenariosText, index), golden, tolerance);
		console.log(`${difference ? 'FAIL' : 'ok  '} ${suite}/${c.name}${difference ? `: ${difference}` : ''}`);
		if (difference) failures++;
	});
}

colourRoles.forEach((role, index) => {
	if (demo.defaultColour(index) !== defaultPalette[role] >>> 0) {
		console.log(`FAIL palette ${role}`);
		failures++;
	}
});

process.exit(failures === 0 ? 0 : 1);
