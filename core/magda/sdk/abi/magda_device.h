/**
 * @file magda_device.h
 * @brief The C ABI over any SDK device, version 1 (docs/abi.md, normative).
 *
 * One exported symbol, magda_module_entry, hands out a versioned table. Every tagged struct opens
 * with struct_tag and struct_size; array elements have frozen layouts and never carry them.
 */
#ifndef MAGDA_SDK_ABI_MAGDA_DEVICE_H
#define MAGDA_SDK_ABI_MAGDA_DEVICE_H

#include <stdint.h>

#if defined(_WIN32)
    #define MAGDA_MODULE_EXPORT __declspec(dllexport)
#else
    #define MAGDA_MODULE_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MAGDA_ABI_VERSION 1

typedef int32_t magda_status;

enum {
    MAGDA_OK = 0,
    MAGDA_ERR_ARGUMENT = -1,
    /** Called out of lifecycle order, such as process before prepare. */
    MAGDA_ERR_STATE = -2,
    MAGDA_ERR_UNSUPPORTED = -3,
    /** A bounded buffer is full; nothing was added. */
    MAGDA_ERR_FULL = -4,
    /** A state the device refused, or a document this build cannot read. */
    MAGDA_ERR_REJECTED = -5,
    /** The caller's buffer is too small; *size holds the length needed. */
    MAGDA_ERR_BUFFER = -6
};

#define MAGDA_FOURCC(a, b, c, d)                                                                   \
    ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

enum {
    MAGDA_TAG_MODULE = MAGDA_FOURCC('M', 'm', 'o', 'd'),
    MAGDA_TAG_DEVICE_API = MAGDA_FOURCC('M', 'a', 'p', 'i'),
    MAGDA_TAG_HOST = MAGDA_FOURCC('M', 'h', 's', 't'),
    MAGDA_TAG_PROPERTIES = MAGDA_FOURCC('M', 'p', 'r', 'p'),
    MAGDA_TAG_PREPARE = MAGDA_FOURCC('M', 'p', 'r', 'e'),
    MAGDA_TAG_PROCESS = MAGDA_FOURCC('M', 'p', 'r', 'c'),
    MAGDA_TAG_TRANSPORT = MAGDA_FOURCC('M', 't', 'r', 'n')
};

typedef struct magda_device magda_device;

/* ---- Frozen array elements ------------------------------------------------------------------ */

typedef struct magda_device_type {
    /** The device type, also the state document's "device". */
    const char* id;
    const char* name;
    /** The type-level parameter manifest (docs/parameter-manifest.md), NUL-terminated. */
    const char* manifest;
    uint32_t manifest_size;
    uint32_t reserved[3];
} magda_device_type;

/** One message. Up to three bytes sit in short_data with data null; longer ones are at data. */
typedef struct magda_midi_event {
    int32_t sample;
    /** How far into sample, in [0, 1). */
    float fraction;
    uint32_t source_id;
    uint32_t size;
    uint8_t short_data[4];
    const uint8_t* data;
    uint32_t reserved[2];
} magda_midi_event;

/** Linear from start_value at start_sample to the next segment's start, or the block's end. */
typedef struct magda_param_segment {
    int32_t start_sample;
    float start_value;
    float end_value;
    uint32_t reserved;
} magda_param_segment;

/* ---- Tagged structs ------------------------------------------------------------------------- */

enum {
    MAGDA_PROPERTY_TAKES_MIDI = 1u << 0,
    MAGDA_PROPERTY_PRODUCES_MIDI = 1u << 1,
    MAGDA_PROPERTY_FORWARDS_MIDI = 1u << 2,
    MAGDA_PROPERTY_TAKES_AUDIO = 1u << 3,
    MAGDA_PROPERTY_SYNTH = 1u << 4,
    MAGDA_PROPERTY_AUDIO_WITHOUT_INPUT = 1u << 5,
    /** The sidechain is MIDI from another track, on the MIDI input. */
    MAGDA_PROPERTY_MIDI_SIDECHAIN = 1u << 6,
    /** The device's parameter set is part of its state (parameterSource "state"). */
    MAGDA_PROPERTY_STATE_PARAMETERS = 1u << 7
};

typedef struct magda_properties {
    uint32_t struct_tag;
    uint32_t struct_size;
    uint32_t flags;
    /** Channels the device reads; 0 lets the host decide. */
    int32_t input_channels;
    /** Channels the device always writes; 0 follows the input. */
    int32_t output_channels;
    /** Channels of an audio sidechain key; 0 for none. */
    int32_t sidechain_channels;
    int32_t device_version;
} magda_properties;

typedef struct magda_prepare {
    uint32_t struct_tag;
    uint32_t struct_size;
    double sample_rate;
    int32_t max_frames;
    /** MIDI input events a process call may carry; 0 means 1024. */
    int32_t max_midi_events;
} magda_prepare;

typedef struct magda_midi_in {
    const magda_midi_event* events;
    int32_t count;
    /** MAGDA_MIDI_ALL_NOTES_OFF: the host's panic. */
    uint32_t flags;
} magda_midi_in;

/** Host storage the device writes into: events, and the bytes of long messages. */
typedef struct magda_midi_out {
    magda_midi_event* events;
    int32_t capacity;
    int32_t count;
    uint8_t* bytes;
    uint32_t byte_capacity;
    uint32_t bytes_used;
    uint32_t flags;
    uint32_t reserved;
} magda_midi_out;

enum { MAGDA_MIDI_ALL_NOTES_OFF = 1u << 0 };

typedef struct magda_tempo_map {
    void* context;
    double (*beats_at_seconds)(void* context, double seconds);
    double (*bpm_at_seconds)(void* context, double seconds);
} magda_tempo_map;

enum {
    MAGDA_TEMPO_NONE = 0,
    /** bpm holds, with beat 0 at timeline zero. */
    MAGDA_TEMPO_CONSTANT = 1,
    MAGDA_TEMPO_MAP = 2
};

enum { MAGDA_TRANSPORT_PLAYING = 1u << 0, MAGDA_TRANSPORT_RENDERING = 1u << 1 };

typedef struct magda_transport {
    uint32_t struct_tag;
    uint32_t struct_size;
    uint32_t flags;
    int32_t tempo_kind;
    double block_start_seconds;
    double block_end_seconds;
    double bpm;
    const magda_tempo_map* tempo_map;
} magda_transport;

typedef struct magda_process {
    uint32_t struct_tag;
    uint32_t struct_size;
    /** At most the prepared max_frames. */
    int32_t num_frames;
    int32_t num_channels;
    /** Planar, in place: input on entry, output on exit. */
    float* const* channels;
    /** Null for no key, which is not a silent one. */
    const float* const* sidechain;
    int32_t sidechain_channels;
    int32_t live_source_count;
    const uint32_t* live_source_ids;
    /** Both set or both null. */
    const magda_midi_in* midi_in;
    magda_midi_out* midi_out;
    /** Null when the host has none. */
    const magda_transport* transport;
} magda_process;

enum {
    /** take_state_patch has a patch; the held document already carries it. */
    MAGDA_NOTIFY_STATE_CHANGED = 1u << 0,
    /** The parameter count, offered slots or descriptors changed. */
    MAGDA_NOTIFY_PARAMETERS_CHANGED = 1u << 1,
    /** The device was re-prepared: re-read properties and latency. */
    MAGDA_NOTIFY_PROPERTIES_CHANGED = 1u << 2
};

typedef struct magda_host {
    uint32_t struct_tag;
    uint32_t struct_size;
    void* context;
    /** Control thread, optional: notifications are pending; take them. */
    void (*notify)(void* context, magda_device* device);
} magda_host;

/**
 * Text out: the length without the terminator goes to *size; with capacity of at least
 * *size + 1 the text and a NUL are written, otherwise MAGDA_ERR_BUFFER and nothing is.
 */
typedef magda_status (*magda_text_fn)(magda_device* device, char* buffer, int32_t capacity,
                                      int32_t* size);

typedef struct magda_device_api {
    uint32_t struct_tag;
    uint32_t struct_size;

    /* Control. */
    magda_status (*create)(const char* device_type, const magda_host* host, magda_device** out);
    void (*destroy)(magda_device* device);
    magda_status (*get_properties)(magda_device* device, magda_properties* out);
    magda_status (*prepare)(magda_device* device, const magda_prepare* prepare);
    void (*release)(magda_device* device);
    magda_status (*reset)(magda_device* device);
    int32_t (*latency)(magda_device* device);
    /** Any thread. Samples, or -1 for a tail that never decays. */
    int64_t (*tail)(magda_device* device);

    /* Audio. */
    magda_status (*process)(magda_device* device, const magda_process* process);
    magda_status (*set_param)(magda_device* device, int32_t slot, float normalized);
    magda_status (*set_param_segments)(magda_device* device, int32_t slot,
                                       const magda_param_segment* segments, int32_t count);

    /* Control. */
    int32_t (*param_count)(magda_device* device);
    int32_t (*param_offered)(magda_device* device, int32_t slot);
    /** One parameter as its manifest entry. */
    magda_status (*param_descriptor)(magda_device* device, int32_t slot, char* buffer,
                                     int32_t capacity, int32_t* size);
    /** Control, and safe during process. */
    float (*param_value)(magda_device* device, int32_t slot);
    float (*param_to_real)(magda_device* device, int32_t slot, float normalized);
    float (*param_to_normalized)(magda_device* device, int32_t slot, float real);
    /** The instance's manifest; differs from the type's when parameters come from state. */
    magda_text_fn get_manifest;

    magda_text_fn get_state;
    magda_status (*set_state)(magda_device* device, const char* json, int32_t size);

    uint32_t (*take_notifications)(magda_device* device);
    magda_text_fn take_state_patch;

    /** Why the last control call failed; empty when it succeeded. Valid until the next one. */
    const char* (*last_error)(magda_device* device);
} magda_device_api;

typedef struct magda_module {
    uint32_t struct_tag;
    uint32_t struct_size;
    int32_t abi_version;
    int32_t state_schema;
    const char* sdk_version;
    int32_t device_type_count;
    const magda_device_type* device_types;
    const magda_device_api* api;
    /** Reserved: null for every id in version 1. */
    const void* (*get_extension)(const char* id);
} magda_module;

/** Null when the module cannot serve a host built against @p host_abi_version. */
MAGDA_MODULE_EXPORT const magda_module* magda_module_entry(int32_t host_abi_version);

#ifdef __cplusplus
}
#endif

#endif
