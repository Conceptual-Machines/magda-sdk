// Binds magda_sdk_meter_demo.wasm (hosts/canvas/meter_demo.cpp), in node or a page.

export async function loadMeterDemo(bytes) {
	const { instance } = await WebAssembly.instantiate(bytes, {
		env: { emscripten_notify_memory_growth: () => {} },
	});
	const exports = instance.exports;
	const memory = exports.memory;
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
		runCase: (scenariosText, index) =>
			withString(scenariosText, (pointer) => parse(exports.meter_demo_run_case(pointer, index))),
		setLevels: (left, right) => exports.meter_demo_live_set_levels(left, right),
		advance: (elapsedMs) => exports.meter_demo_live_advance(elapsedMs) !== 0,
		paint: (width, height, horizontal = false, zeroDbY = -1) =>
			parse(exports.meter_demo_live_paint(width, height, horizontal ? 1 : 0, zeroDbY)),
		defaultColour: (role) => exports.meter_demo_default_colour(role) >>> 0,
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
