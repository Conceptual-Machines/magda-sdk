// Runs the meter golden scenarios through the wasm build in node and compares each frame with
// the native golden (tests/golden/meter), and the JS palette with the C++ one.
//
// node check.mjs --wasm <magda_sdk_meter_demo.wasm> [--tolerance 1.5e-3]

import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { colourRoles, defaultPalette } from './js/display-list.js';
import { firstDifference, loadMeterDemo } from './js/meter-demo-wasm.js';

const args = Object.fromEntries(process.argv.slice(2).reduce((pairs, value, i, all) => {
	if (i % 2 === 0) pairs.push([value.replace(/^--/, ''), all[i + 1]]);
	return pairs;
}, []));
if (!args.wasm) {
	console.error('usage: check.mjs --wasm <magda_sdk_meter_demo.wasm>');
	process.exit(2);
}
const tolerance = Number(args.tolerance ?? 1.5e-3);
const goldenDir = join(dirname(fileURLToPath(import.meta.url)), '../../tests/golden/meter');

const demo = await loadMeterDemo(readFileSync(args.wasm));
const scenariosText = readFileSync(join(goldenDir, 'scenarios.json'), 'utf8');
const scenarios = JSON.parse(scenariosText);

let failures = 0;
scenarios.cases.forEach((c, index) => {
	const golden = JSON.parse(readFileSync(join(goldenDir, `${c.name}.json`), 'utf8'));
	const difference = firstDifference(demo.runCase(scenariosText, index), golden, tolerance);
	console.log(`${difference ? 'FAIL' : 'ok  '} ${c.name}${difference ? `: ${difference}` : ''}`);
	if (difference) failures++;
});

colourRoles.forEach((role, index) => {
	if (demo.defaultColour(index) !== defaultPalette[role] >>> 0) {
		console.log(`FAIL palette ${role}`);
		failures++;
	}
});

process.exit(failures === 0 ? 0 : 1);
