import { WamNode, WebAudioModule, addFunctionModule } from '@webaudiomodules/sdk';

import getMagdaDeviceAbi from './getMagdaDeviceAbi.js';
import getMagdaWamProcessor from './getMagdaWamProcessor.js';

/**
 * A WebAudioModule class for one device of a MAGDA wasm module. WAM hosts construct it with
 * (groupId, audioContext) only, so the module's files are bound here.
 *
 * @param {{ wasmUrl: string | URL, descriptorUrl: string | URL, deviceType: string, channels?: number }} options
 */
const createMagdaWam = ({ wasmUrl, descriptorUrl, deviceType, channels = 2 }) => {
	const wasmBytes = new Map();
	// The worklet cannot fetch: the main thread loads the bytes once per URL.
	const loadWasm = (url) => {
		const key = String(url);
		if (!wasmBytes.has(key)) wasmBytes.set(key, fetch(url).then((response) => response.arrayBuffer()));
		return wasmBytes.get(key);
	};

	return class MagdaWebAudioModule extends WebAudioModule {
		constructor(groupId, audioContext) {
			super(groupId, audioContext);
			this._descriptorUrl = String(descriptorUrl);
		}

		async initialize(state) {
			await this._loadDescriptor();
			this._wasmBytes = await loadWasm(wasmUrl);
			return super.initialize(state);
		}

		async createAudioNode(initialState) {
			const { audioWorklet } = this.audioContext;
			await WamNode.addModules(this.audioContext, this.moduleId);
			await addFunctionModule(audioWorklet, getMagdaDeviceAbi, this.moduleId);
			await addFunctionModule(audioWorklet, getMagdaWamProcessor, this.moduleId);
			const node = new WamNode(this, {
				numberOfInputs: this.descriptor.hasAudioInput ? 1 : 0,
				numberOfOutputs: 1,
				outputChannelCount: [channels],
				processorOptions: {
					wasmBytes: this._wasmBytes, deviceType, channels,
					rendering: typeof OfflineAudioContext !== 'undefined' && this.audioContext instanceof OfflineAudioContext,
				},
			});
			await node._initialize();
			if (initialState) await node.setState(initialState);
			return node;
		}
	};
};

export default createMagdaWam;
