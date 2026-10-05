/**
 * The WamProcessor over a MAGDA device. Stringified into the worklet by addFunctionModule, after
 * the WAM SDK's processor and getMagdaDeviceAbi, so it reads both from the module scope.
 *
 * @param {string} moduleId
 */
const getMagdaWamProcessor = (moduleId) => {
	const scope = globalThis.webAudioModules.getModuleScope(moduleId);
	const {
		WamProcessor, WamParameter, WamParameterInfo, WamParameterInterpolator, MagdaDeviceModule, MagdaNotify,
	} = scope;

	class MagdaWamProcessor extends WamProcessor {
		constructor(options) {
			super(options);
			const { wasmBytes, deviceType, channels, rendering } = options.processorOptions;
			const device = new MagdaDeviceModule(wasmBytes).create(deviceType);
			this._device = device;
			this._channels = channels ?? 2;
			// The render quantum is fixed at 128; process() slices never exceed it.
			device.prepare(globalThis.sampleRate, this._samplesPerQuantum, this._channels);
			if (device.takeNotifications() & MagdaNotify.STATE_CHANGED) device.takeStatePatch();
			this._compensationDelay = device.latency / globalThis.sampleRate;

			// WAM cannot add parameters: the ids are a fresh instance's slots, re-described on a change.
			this._ids = [];
			this._defaults = [];
			for (let slot = 0; slot < device.parameterCount; slot++) {
				this._ids.push(device.parameterDescriptor(slot).id);
				this._defaults.push(device.getParameter(slot));
			}
			this._describe();
			this._applied = new Float32Array(this._ids.length).fill(-1);
			this._scratch = Array.from({ length: this._channels }, () => new Float32Array(this._samplesPerQuantum));
			this._transport = null;
			this._timeline = { playing: false, rendering: !!rendering, startSeconds: 0, bpm: 0 };
		}

		/** A null descriptor is a slot past the instance's count: kept registered, out of use. */
		_describe() {
			this._descriptors = this._ids.map((_, slot) => this._device.parameterDescriptor(slot));
		}

		_infoFor(slot) {
			const id = this._ids[slot];
			const descriptor = this._descriptors[slot];
			if (!descriptor) return new WamParameterInfo(id, { type: 'float', label: 'Unused', minValue: 0, maxValue: 1 });
			const kind = descriptor.scale?.kind;
			const label = descriptor.name ?? descriptor.id;
			const defaultValue = this._toWamValue(slot, this._defaults[slot], kind, descriptor);
			if (kind === 'boolean') return new WamParameterInfo(id, { type: 'boolean', label, defaultValue });
			if (kind === 'discrete' && descriptor.scale.choices?.length > 1) {
				const choices = descriptor.scale.choices.map((choice) => choice.label);
				return new WamParameterInfo(id, { type: 'choice', label, choices, defaultValue });
			}
			return new WamParameterInfo(id, {
				type: 'float', label, defaultValue, minValue: 0, maxValue: 1, units: descriptor.unit ?? '',
			});
		}

		_generateWamParameterInfo() {
			return Object.fromEntries(this._ids.map((id, slot) => [id, this._infoFor(slot)]));
		}

		/** The device's normalized position, in the parameter's WAM domain. */
		_toWamValue(slot, normalized, kind = this._descriptors[slot]?.scale?.kind, descriptor = this._descriptors[slot]) {
			if (kind === 'boolean') return normalized >= 0.5 ? 1 : 0;
			const count = descriptor?.scale?.choices?.length ?? 0;
			if (kind === 'discrete' && count > 1) {
				return Math.min(Math.max(Math.round(this._device.toReal(slot, normalized)), 0), count - 1);
			}
			return normalized;
		}

		/** A WAM value in the parameter's own domain, to the device's normalized position. */
		_toNormalized(slot, value) {
			const { type } = this._parameterInfo[this._ids[slot]];
			if (type === 'boolean') return value >= 0.5 ? 1 : 0;
			if (type === 'choice') return this._device.toNormalized(slot, value);
			return value;
		}

		/** Re-describes every registered slot and re-reads its value from the device. */
		_refreshParameters() {
			this._describe();
			this._ids.forEach((id, slot) => {
				const info = this._infoFor(slot);
				const normalized = this._device.getParameter(slot);
				const parameter = new WamParameter(info);
				parameter.value = this._toWamValue(slot, normalized);
				const interpolator = new WamParameterInterpolator(info, 256);
				interpolator.setStartValue(parameter.value);
				this._parameterInfo[id] = info;
				this._parameterState[id] = parameter;
				this._parameterInterpolators[id] = interpolator;
				this._applied[slot] = normalized;
			});
		}

		/** Takes what the device has pending; the worklet is its control thread between quanta. */
		_applyNotifications() {
			const flags = this._device.takeNotifications();
			if (flags & MagdaNotify.PARAMETERS_CHANGED) this._refreshParameters();
			if (flags & MagdaNotify.STATE_CHANGED) this._device.takeStatePatch();
			if (flags & MagdaNotify.PROPERTIES_CHANGED) this._compensationDelay = this._device.latency / globalThis.sampleRate;
		}

		_onTransport(transportData) {
			this._transport = transportData;
		}

		/**
		 * The timeline at @p startSample, or null before the host sent a transport. WAM counts the
		 * bar in time-signature beats at the tempo.
		 */
		_timelineAt(startSample) {
			const transport = this._transport;
			if (!transport) return null;
			const timeline = this._timeline;
			const bpm = transport.tempo > 0 ? transport.tempo : 0;
			let beats = transport.currentBar * transport.timeSigNumerator;
			if (transport.playing && bpm > 0) {
				const now = globalThis.currentTime + startSample / globalThis.sampleRate;
				beats += Math.max(0, now - transport.currentBarStarted) * bpm / 60;
			}
			timeline.playing = !!transport.playing;
			timeline.bpm = bpm;
			timeline.startSeconds = bpm > 0 ? beats * 60 / bpm : 0;
			return timeline;
		}

		_applyParameters() {
			for (let slot = 0; slot < this._ids.length; slot++) {
				const state = this._parameterState[this._ids[slot]];
				if (!state || !this._descriptors[slot]) continue;
				const normalized = this._toNormalized(slot, state.value);
				if (normalized !== this._applied[slot]) {
					this._device.setParameter(slot, normalized);
					this._applied[slot] = normalized;
				}
			}
		}

		_onMidi(midiData) {
			this._device.midi(midiData.bytes, 0);
		}

		_onSysex(sysexData) {
			this._device.midi(sysexData.bytes, 0);
		}

		_process(startSample, endSample, inputs, outputs) {
			this._applyParameters();
			const input = inputs[0] ?? [];
			const output = outputs[0];
			for (let c = 0; c < this._channels; c++) {
				const buffer = this._scratch[c];
				const source = input[c] ?? input[0];
				for (let i = startSample; i < endSample; i++) buffer[i] = source ? source[i] : 0;
			}
			this._device.process(this._scratch, startSample, endSample, this._timelineAt(startSample));
			for (let c = 0; c < output.length; c++) {
				const buffer = this._scratch[Math.min(c, this._channels - 1)];
				for (let i = startSample; i < endSample; i++) output[c][i] = buffer[i];
			}
			if (this._device.midiOutCount > 0) {
				const events = this._device.midiOut();
				const time = globalThis.currentTime;
				this.emitEvents(...events.map(({ bytes, sampleOffset }) => (bytes.length > 3
					? { type: 'wam-sysex', time: time + (startSample + sampleOffset) / globalThis.sampleRate, data: { bytes } }
					: { type: 'wam-midi', time: time + (startSample + sampleOffset) / globalThis.sampleRate, data: { bytes: Array.from(bytes) } })));
			}
			if (endSample === this._samplesPerQuantum) this._applyNotifications();
		}

		/** The plugin state shared with the JUCE host (docs/abi.md). */
		_getState() {
			const parameters = {};
			this._ids.forEach((id, slot) => {
				parameters[id] = this._device.getParameter(slot);
			});
			return { format: 'magda.plugin-state', version: 1, parameters, state: JSON.parse(this._device.getState()) };
		}

		_setState(state) {
			if (!state) return;
			if (state.state) this._device.setState(JSON.stringify(state.state));
			this._applyNotifications();
			if (state.parameters) {
				const values = {};
				this._ids.forEach((id, slot) => {
					if (id in state.parameters) values[id] = { id, value: this._toWamValue(slot, state.parameters[id]), normalized: false };
				});
				this._setParameterValues(values, false);
			}
			if (state.parameterValues) this._setParameterValues(state.parameterValues, false);
		}

		destroy() {
			this._device.destroy();
			super.destroy();
		}
	}

	if (globalThis.AudioWorkletProcessor) {
		try {
			globalThis.registerProcessor(moduleId, MagdaWamProcessor);
		} catch (error) {
			// Already registered by an earlier instance of the same module.
		}
	}

	return MagdaWamProcessor;
};

export default getMagdaWamProcessor;
