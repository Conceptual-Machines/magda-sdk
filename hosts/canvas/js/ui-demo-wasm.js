// Binds magda_sdk_ui_demo.wasm (hosts/canvas/ui_demo.cpp), in node or a page.

/** Pointer event types and modifier bits, as ui_demo.cpp reads them. */
export const pointerType = { down: 0, drag: 1, up: 2, double: 3, move: 4, exit: 5 };
export const modifierBits = { shift: 1, command: 2, alt: 4, popup: 8 };
export const responseBits = { repaint: 1, preview: 2, commit: 4, loop: 8, handled: 16 };

/**
 * @p measureText(text, fontSize) sizes the curve editor's tooltip; without it, text is six units
 * per character.
 */
export async function loadUiDemo(bytes, measureText = (text) => 6 * text.length) {
	let memory;
	const decoder = new TextDecoder();
	const { instance } = await WebAssembly.instantiate(bytes, {
		env: {
			emscripten_notify_memory_growth: () => {},
			measure_text: (pointer, length, fontSize) =>
				measureText(decoder.decode(new Uint8Array(memory.buffer, pointer, length)), fontSize),
		},
	});
	const exports = instance.exports;
	memory = exports.memory;
	exports._initialize?.();

	const readString = (pointer) => {
		if (pointer === 0) return null;
		const bytesView = new Uint8Array(memory.buffer);
		let end = pointer;
		while (bytesView[end] !== 0) end++;
		return new TextDecoder().decode(bytesView.subarray(pointer, end));
	};

	const withString = (text, body) => {
		const encoded = new TextEncoder().encode(text);
		const pointer = exports.malloc(encoded.length + 1);
		const view = new Uint8Array(memory.buffer, pointer, encoded.length + 1);
		view.set(encoded);
		view[encoded.length] = 0;
		try {
			return body(pointer);
		} finally {
			exports.free(pointer);
		}
	};

	const parse = (pointer) => {
		const text = readString(pointer);
		return text === null ? null : JSON.parse(text);
	};

	return {
		defaultColour: (role) => exports.ui_demo_default_colour(role) >>> 0,
		meter: {
			runCase: (scenariosText, index) =>
				withString(scenariosText, (pointer) => parse(exports.meter_demo_run_case(pointer, index))),
			setLevels: (left, right) => exports.meter_demo_live_set_levels(left, right),
			advance: (elapsedMs) => exports.meter_demo_live_advance(elapsedMs) !== 0,
			paint: (width, height, horizontal = false, zeroDbY = -1) =>
				parse(exports.meter_demo_live_paint(width, height, horizontal ? 1 : 0, zeroDbY)),
		},
		curveEditor: {
			runCase: (scenariosText, index) =>
				withString(scenariosText, (pointer) => parse(exports.curve_demo_run_case(pointer, index))),
			configure: (scenarioCase) =>
				withString(JSON.stringify(scenarioCase), (pointer) => exports.curve_demo_live_configure(pointer)),
			resize: (width, height) => exports.curve_demo_live_resize(width, height),
			pointer: (type, x, y, mods = 0) => exports.curve_demo_live_pointer(type, x, y, mods),
			key: (code, character, mods = 0) => exports.curve_demo_live_key(code, character, mods),
			indicator: (phase, value, trigger) => exports.curve_demo_live_indicator(phase, value, trigger ? 1 : 0),
			cursor: (mods) => exports.curve_demo_live_cursor(mods),
			render: () => parse(exports.curve_demo_live_render()),
			points: () => readString(exports.curve_demo_live_points()),
			before: () => readString(exports.curve_demo_live_before()),
			setPoints: (curveText) =>
				withString(curveText, (pointer) => exports.curve_demo_live_set_points(pointer)) !== 0,
		},
	};
}

/** The path of the first difference between two parsed display lists, or null. */
export function firstDifference(a, b, tolerance, path = '$') {
	if (typeof a === 'number' && typeof b === 'number')
		return Math.abs(a - b) <= tolerance ? null : `${path}: ${a} vs ${b}`;
	if (typeof a !== typeof b || Array.isArray(a) !== Array.isArray(b)) return path;
	if (Array.isArray(a)) {
		if (a.length !== b.length) return `${path} (length ${a.length} vs ${b.length})`;
		for (let i = 0; i < a.length; i++) {
			const d = firstDifference(a[i], b[i], tolerance, `${path}[${i}]`);
			if (d) return d;
		}
		return null;
	}
	if (a !== null && typeof a === 'object') {
		const keys = Object.keys(a);
		if (keys.join() !== Object.keys(b).join()) return `${path} (members)`;
		for (const key of keys) {
			const d = firstDifference(a[key], b[key], tolerance, `${path}.${key}`);
			if (d) return d;
		}
		return null;
	}
	return a === b ? null : path;
}
