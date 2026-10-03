/**
 * The WamProcessor over a MAGDA device. Stringified into the worklet by addFunctionModule, after
 * the WAM SDK's processor and getMagdaDeviceAbi, so it reads both from the module scope.
 *
 * @param {string} moduleId
 */
const getMagdaWamProcessor = (moduleId) => {
	const scope = globalThis.webAudioModules.getModuleScope(moduleId);
	const { WamProcessor, WamParameterInfo, MagdaDeviceModule } = scope;

	class MagdaWamProcessor extends WamProcessor {
		constructor(options) {
			super(options);
			const { wasmBytes, deviceType, channels } = options.processorOptions;
			this._device = new MagdaDeviceModule(wasmBytes).create(deviceType);
			this._channels = channels ?? 2;
			// The render quantum is fixed at 128; process() slices never exceed it.
			this._device.prepare(globalThis.sampleRate, this._samplesPerQuantum, this._channels);
			this._parameters = this._device.manifest.parameters;
			this._applied = new Float32Array(this._parameters.length).fill(-1);
			this._scratch = Array.from({ length: this._channels }, () => new Float32Array(this._samplesPerQuantum));
		}

		_generateWamParameterInfo() {
			const info = {};
			const device = this._device;
			this._parameters.forEach((parameter, slot) => {
				const normalized = device.getParameter(slot);
				const kind = parameter.scale?.kind;
				const label = parameter.name ?? parameter.id;
				if (kind === 'boolean') {
					info[parameter.id] = new WamParameterInfo(parameter.id, {
						type: 'boolean', label, defaultValue: normalized >= 0.5 ? 1 : 0,
					});
				} else if (kind === 'discrete' && parameter.scale.choices?.length > 1) {
					const choices = parameter.scale.choices.map((choice) => choice.label);
					const index = Math.round(device.toReal(slot, normalized));
					info[parameter.id] = new WamParameterInfo(parameter.id, {
						type: 'choice', label, choices, defaultValue: Math.min(Math.max(index, 0), choices.length - 1),
					});
				} else {
					info[parameter.id] = new WamParameterInfo(parameter.id, {
						type: 'float', label, defaultValue: normalized, minValue: 0, maxValue: 1, units: parameter.unit ?? '',
					});
				}
			});
			return info;
		}

		/** A WAM value in the parameter's own domain, to the device's normalized position. */
		_toNormalized(slot, value) {
			const { type } = this._parameterInfo[this._parameters[slot].id];
			if (type === 'boolean') return value >= 0.5 ? 1 : 0;
			if (type === 'choice') return this._device.toNormalized(slot, value);
			return value;
		}

		_applyParameters() {
			for (let slot = 0; slot < this._parameters.length; slot++) {
				const state = this._parameterState[this._parameters[slot].id];
				if (!state) continue;
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
			this._device.process(this._scratch, startSample, endSample);
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
		}

		/** The plugin state shared with the JUCE host (docs/abi.md). */
		_getState() {
			const parameters = {};
			this._parameters.forEach((parameter, slot) => {
				parameters[parameter.id] = this._device.getParameter(slot);
			});
			return { format: 'magda.plugin-state', version: 1, parameters, state: JSON.parse(this._device.getState()) };
		}

		_setState(state) {
			if (!state) return;
			if (state.state) this._device.setState(JSON.stringify(state.state));
			if (state.parameters) {
				const values = {};
				this._parameters.forEach((parameter, slot) => {
					if (!(parameter.id in state.parameters)) return;
					const normalized = state.parameters[parameter.id];
					const { type } = this._parameterInfo[parameter.id];
					let value = normalized;
					if (type === 'boolean') value = normalized >= 0.5 ? 1 : 0;
					else if (type === 'choice') value = Math.round(this._device.toReal(slot, normalized));
					values[parameter.id] = { id: parameter.id, value, normalized: false };
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
