/**
 * @file magda_device.h
 * @brief The C ABI over any SDK device: the door every host uses (docs/abi.md).
 *
 * Threads follow the device contract: the audio calls are process, set_param and midi; every
 * other call is control and never concurrent with them. Text a call returns is owned by the
 * device and valid until the next text-returning call on the same device.
 */
#ifndef MAGDA_SDK_ABI_MAGDA_DEVICE_H
#define MAGDA_SDK_ABI_MAGDA_DEVICE_H

#include <stdint.h>

#if defined(_WIN32)
    #define MAGDA_DEVICE_API
#else
    #define MAGDA_DEVICE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MAGDA_DEVICE_ABI_VERSION 1

typedef struct magda_device magda_device;

enum {
    MAGDA_OK = 0,
    MAGDA_ERR_ARGUMENT = -1,
    /** Called out of lifecycle order, such as process before prepare. */
    MAGDA_ERR_STATE = -2,
    MAGDA_ERR_UNSUPPORTED = -3,
    /** A bounded queue is full; nothing was added. */
    MAGDA_ERR_FULL = -4,
    /** A state the device refused, or a document this build cannot read. */
    MAGDA_ERR_REJECTED = -5
};

MAGDA_DEVICE_API int magda_device_abi_version(void);

/** The device types this module exposes, in a fixed order. */
MAGDA_DEVICE_API int magda_device_type_count(void);
MAGDA_DEVICE_API const char* magda_device_type_at(int index);

/** Null for an unknown type. */
MAGDA_DEVICE_API magda_device* magda_device_create(const char* device_type);
MAGDA_DEVICE_API void magda_device_destroy(magda_device* device);

MAGDA_DEVICE_API int magda_device_prepare(magda_device* device, double sample_rate,
                                          int max_block_size);
MAGDA_DEVICE_API void magda_device_reset(magda_device* device);
MAGDA_DEVICE_API int magda_device_latency(const magda_device* device);

/**
 * Audio. Planar, in place. Any frame count: blocks past the prepared size are split, and MIDI
 * queued since the last call lands in the block it addresses.
 */
MAGDA_DEVICE_API int magda_device_process(magda_device* device, float* const* channels,
                                          int num_channels, int num_frames);

/** Audio. Normalized to [0, 1]; applies from the next process. */
MAGDA_DEVICE_API int magda_device_set_param(magda_device* device, int slot, float normalized);
MAGDA_DEVICE_API float magda_device_get_param(const magda_device* device, int slot);
MAGDA_DEVICE_API int magda_device_param_count(const magda_device* device);

/** The real value of @p normalized for @p slot, through the manifest's reference conversion. */
MAGDA_DEVICE_API float magda_device_param_to_real(const magda_device* device, int slot,
                                                  float normalized);
MAGDA_DEVICE_API float magda_device_param_to_normalized(const magda_device* device, int slot,
                                                        float real);

/** Audio. One message at @p sample_offset into the next process call's frames. */
MAGDA_DEVICE_API int magda_device_midi(magda_device* device, const uint8_t* bytes, int size,
                                       int sample_offset);

/** Audio, after process: the MIDI the device emitted in that call. */
MAGDA_DEVICE_API int magda_device_midi_out_count(const magda_device* device);
MAGDA_DEVICE_API const uint8_t* magda_device_midi_out_at(const magda_device* device, int index,
                                                         int* size, int* sample_offset);

/** The device state document (docs/device-state.md); null on failure. */
MAGDA_DEVICE_API const char* magda_device_get_state(magda_device* device);
MAGDA_DEVICE_API int magda_device_set_state(magda_device* device, const char* json, int size);

/** The parameter manifest (docs/parameter-manifest.md); null on failure. */
MAGDA_DEVICE_API const char* magda_device_get_manifest(magda_device* device);

/** Offline analysis of a mono buffer, as the device's JSON; null when unsupported or failed. */
MAGDA_DEVICE_API const char* magda_device_analyze(magda_device* device, const float* mono,
                                                  int num_samples, double sample_rate);

/** Why the last failing call on @p device failed; empty when none did. */
MAGDA_DEVICE_API const char* magda_device_last_error(const magda_device* device);

#ifdef __cplusplus
}
#endif

#endif
