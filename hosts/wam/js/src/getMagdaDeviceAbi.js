/**
 * The wasm module's flat glue over the C ABI (hosts/wam/reactor.cpp), synchronous so it runs in an
 * AudioWorklet. Self-contained: addFunctionModule stringifies it into the worklet scope, where it
 * registers itself on the WAM module scope for @p moduleId.
 *
 * @param {string} [moduleId]
 */
const getMagdaDeviceAbi = (moduleId) => {
	// AudioWorkletGlobalScope has no TextEncoder or TextDecoder.
	const encodeUtf8 = (text) => {
		const bytes = [];
		for (const char of text) {
			const c = char.codePointAt(0);
			if (c < 0x80) bytes.push(c);
			else if (c < 0x800) bytes.push(0xc0 | (c >> 6), 0x80 | (c & 63));
			else if (c < 0x10000) bytes.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
			else bytes.push(0xf0 | (c >> 18), 0x80 | ((c >> 12) & 63), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
		}
		return Uint8Array.from(bytes);
	};

	const decodeUtf8 = (bytes) => {
		let text = '';
		for (let i = 0; i < bytes.length;) {
			const b = bytes[i++];
			let c = b;
			if (b >= 0xf0) c = ((b & 7) << 18) | ((bytes[i++] & 63) << 12) | ((bytes[i++] & 63) << 6) | (bytes[i++] & 63);
			else if (b >= 0xe0) c = ((b & 15) << 12) | ((bytes[i++] & 63) << 6) | (bytes[i++] & 63);
			else if (b >= 0xc0) c = ((b & 31) << 6) | (bytes[i++] & 63);
			text += String.fromCodePoint(c);
		}
		return text;
	};

	class MagdaDeviceModule {
		/** @param {BufferSource | WebAssembly.Module} source */
		constructor(source) {
			const module = source instanceof WebAssembly.Module ? source : new WebAssembly.Module(source);
			const imports = { env: { emscripten_notify_memory_growth: () => {} } };
			this.instance = new WebAssembly.Instance(module, imports);
			this.abi = this.instance.exports;
			if (this.abi._initialize) this.abi._initialize();
		}

		// Views are rebuilt only when memory grows, so the audio path allocates nothing.
		_views() {
			const { buffer } = this.abi.memory;
			if (this._buffer !== buffer) {
				this._buffer = buffer;
				this._u8 = new Uint8Array(buffer);
				this._f32 = new Float32Array(buffer);
			}
		}

		get heapU8() { this._views(); return this._u8; }

		get heapF32() { this._views(); return this._f32; }

		readString(pointer) {
			if (!pointer) return null;
			const heap = this.heapU8;
			let end = pointer;
			while (heap[end]) end++;
			return decodeUtf8(heap.subarray(pointer, end));
		}

		/** @returns {[number, number]} pointer and byte count; free the pointer. */
		writeBytes(bytes) {
			const pointer = this.abi.malloc(Math.max(1, bytes.length));
			this.heapU8.set(bytes, pointer);
			return [pointer, bytes.length];
		}

		get deviceTypes() {
			const types = [];
			const count = this.abi.magda_wam_type_count();
			for (let i = 0; i < count; i++) types.push(this.readString(this.abi.magda_wam_type_at(i)));
			return types;
		}

		/** @returns {MagdaDevice} */
		create(deviceType) {
			const [name] = this.writeBytes(encodeUtf8(`${deviceType}\0`));
			const handle = this.abi.magda_wam_create(name);
			this.abi.free(name);
			if (!handle) throw new Error(`unknown device type ${deviceType}`);
			return new MagdaDevice(this, handle);
		}
	}

	class MagdaDevice {
		constructor(module, handle) {
			this.module = module;
			this.abi = module.abi;
			this.handle = handle;
			this.channelCapacity = 0;
			this.frameCapacity = 0;
			this.channelBuffers = 0;
			this.channelPointers = 0;
			this.midiBuffer = this.abi.malloc(256);
			this.outSize = this.abi.malloc(8);
		}

		get lastError() { return this.module.readString(this.abi.magda_wam_last_error(this.handle)); }

		/** Control. Also sizes the scratch buffers process() copies through. */
		prepare(sampleRate, maxBlockSize, maxChannels = 2) {
			this._freeBuffers();
			this.channelCapacity = maxChannels;
			this.frameCapacity = maxBlockSize;
			this.channelBuffers = this.abi.malloc(4 * maxChannels * maxBlockSize);
			this.channelPointers = this.abi.malloc(4 * maxChannels);
			const pointers = new Uint32Array(this.abi.memory.buffer, this.channelPointers, maxChannels);
			for (let c = 0; c < maxChannels; c++) pointers[c] = this.channelBuffers + 4 * c * maxBlockSize;
			return this.abi.magda_wam_prepare(this.handle, sampleRate, maxBlockSize);
		}

		release() { this.abi.magda_wam_release(this.handle); }

		reset() { this.abi.magda_wam_reset(this.handle); }

		get latency() { return this.abi.magda_wam_latency(this.handle); }

		/** Samples, or -1 for a tail that never decays. */
		get tail() { return this.abi.magda_wam_tail(this.handle); }

		/**
		 * Audio. In place over @p channels, frames [start, end). No allocation.
		 * @param {Float32Array[]} channels
		 * @param {{ playing?: boolean, rendering?: boolean, startSeconds: number, bpm?: number } | null} [transport]
		 *   the timeline at @p start; a bpm above zero is a constant tempo
		 */
		process(channels, start = 0, end = channels.length ? channels[0].length : 0, transport = null) {
			const frames = end - start;
			const count = Math.min(channels.length, this.channelCapacity);
			if (frames <= 0 || frames > this.frameCapacity) return frames === 0 ? 0 : -1;
			let heap = this.module.heapF32;
			for (let c = 0; c < count; c++) {
				const channel = channels[c];
				const offset = (this.channelBuffers >> 2) + c * this.frameCapacity - start;
				for (let i = start; i < end; i++) heap[offset + i] = channel[i];
			}
			const flags = transport ? (transport.playing ? 1 : 0) | (transport.rendering ? 2 : 0) : -1;
			const result = this.abi.magda_wam_process(this.handle, this.channelPointers, count, frames, flags,
				transport ? transport.startSeconds : 0, transport?.bpm ?? 0);
			heap = this.module.heapF32;
			for (let c = 0; c < count; c++) {
				const channel = channels[c];
				const offset = (this.channelBuffers >> 2) + c * this.frameCapacity - start;
				for (let i = start; i < end; i++) channel[i] = heap[offset + i];
			}
			return result;
		}

		get parameterCount() { return this.abi.magda_wam_param_count(this.handle); }

		isOffered(slot) { return this.abi.magda_wam_param_offered(this.handle, slot) !== 0; }

		/** The slot's manifest entry, parsed; null past the instance's count. */
		parameterDescriptor(slot) {
			const text = this.module.readString(this.abi.magda_wam_param_descriptor(this.handle, slot));
			return text === null ? null : JSON.parse(text);
		}

		setParameter(slot, normalized) { return this.abi.magda_wam_set_param(this.handle, slot, normalized); }

		getParameter(slot) { return this.abi.magda_wam_get_param(this.handle, slot); }

		toReal(slot, normalized) { return this.abi.magda_wam_param_to_real(this.handle, slot, normalized); }

		toNormalized(slot, real) { return this.abi.magda_wam_param_to_normalized(this.handle, slot, real); }

		/** Audio. @param {ArrayLike<number>} bytes at most 256 */
		midi(bytes, sampleOffset = 0) {
			if (bytes.length > 256) return -1;
			const heap = this.module.heapU8;
			for (let i = 0; i < bytes.length; i++) heap[this.midiBuffer + i] = bytes[i];
			return this.abi.magda_wam_midi(this.handle, this.midiBuffer, bytes.length, sampleOffset);
		}

		get midiOutCount() { return this.abi.magda_wam_midi_out_count(this.handle); }

		/** Audio, after process: [{ bytes, sampleOffset }]. */
		midiOut() {
			const events = [];
			const count = this.abi.magda_wam_midi_out_count(this.handle);
			for (let i = 0; i < count; i++) {
				const pointer = this.abi.magda_wam_midi_out_at(this.handle, i, this.outSize, this.outSize + 4);
				const [size, sampleOffset] = new Int32Array(this.abi.memory.buffer, this.outSize, 2);
				events.push({ bytes: this.module.heapU8.slice(pointer, pointer + size), sampleOffset });
			}
			return events;
		}

		/** The device state document's text. */
		getState() { return this.module.readString(this.abi.magda_wam_get_state(this.handle)); }

		setState(json) {
			const [pointer, size] = this.module.writeBytes(encodeUtf8(json));
			const result = this.abi.magda_wam_set_state(this.handle, pointer, size);
			this.abi.free(pointer);
			return result;
		}

		/** MAGDA_NOTIFY_* flags pending since the last take. */
		takeNotifications() { return this.abi.magda_wam_take_notifications(this.handle) >>> 0; }

		/** The state patches merged since the last take, or null. */
		takeStatePatch() { return this.module.readString(this.abi.magda_wam_take_state_patch(this.handle)); }

		/** The instance's parameter manifest, parsed. */
		get manifest() {
			const text = this.module.readString(this.abi.magda_wam_get_manifest(this.handle));
			return text === null ? null : JSON.parse(text);
		}

		destroy() {
			this.abi.magda_wam_destroy(this.handle);
			this._freeBuffers();
			this.abi.free(this.midiBuffer);
			this.abi.free(this.outSize);
			this.handle = 0;
		}

		_freeBuffers() {
			if (this.channelBuffers) this.abi.free(this.channelBuffers);
			if (this.channelPointers) this.abi.free(this.channelPointers);
			this.channelBuffers = 0;
			this.channelPointers = 0;
		}
	}

	// MAGDA_NOTIFY_* in magda_device.h.
	const Notify = { STATE_CHANGED: 1, PARAMETERS_CHANGED: 2, PROPERTIES_CHANGED: 4 };

	if (moduleId && globalThis.webAudioModules) {
		const scope = globalThis.webAudioModules.getModuleScope(moduleId);
		scope.MagdaDeviceModule = MagdaDeviceModule;
		scope.MagdaNotify = Notify;
	}

	return { MagdaDeviceModule, MagdaDevice, Notify };
};

export default getMagdaDeviceAbi;
