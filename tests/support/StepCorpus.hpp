#pragma once

/**
 * @file StepCorpus.hpp
 * @brief A deterministic corpus that plays saved patterns through the step sequencers and hashes
 * the notes that come out.
 *
 * Included by the SDK tests and by magda-core's. The includer defines STEP_CORPUS_NS, the
 * namespace holding the sequencer types. Each note is (sample, note, velocity, length), all
 * integers; the sequence hash leaves the sample out, the full hash keeps it.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace magda::stepcorpus {

namespace seq = STEP_CORPUS_NS;

/// Where the transport goes over a case.
enum class Script { Plain, MidStart, StartStop, Loop, RateSwitch };

struct Spec {
    const char* name = "";
    bool poly = false;
    double sampleRate = 48000.0;
    int blockSize = 512;
    double bpm = 120.0;
    seq::StepClock::Rate rate = seq::StepClock::Rate::Sixteenth;
    seq::StepClock::Direction direction = seq::StepClock::Direction::Forward;
    float swing = 0.0f;
    float gate = 0.8f;
    int accent = 120;
    int normal = 90;
    float ramp = 0.0f;
    float skew = 0.0f;
    int cycles = 1;
    bool hardAngle = false;
    float quantize = 0.0f;
    int quantizeSub = 16;
    int length = 16;
    std::uint32_t seed = 1;
    Script script = Script::Plain;
};

struct Entry {
    std::string name;
    std::size_t count = 0;
    std::uint64_t sequenceHash = 0;
    std::uint64_t fullHash = 0;

    bool operator==(const Entry&) const = default;
};

/// One note-on as the host would schedule it, in whole samples from the start of the run.
struct Played {
    long long sample = 0;
    int note = 0;
    int velocity = 0;
    long long length = 0;
};

class Hash {
  public:
    void add(long long v) {
        for (int i = 0; i < 8; ++i) {
            h_ ^= static_cast<std::uint64_t>(v >> (8 * i)) & 0xffU;
            h_ *= 1099511628211ULL;
        }
    }
    std::uint64_t value() const {
        return h_;
    }

  private:
    std::uint64_t h_ = 14695981039346656037ULL;
};

/// Records note events with the block they arrived in.
class Recorder : public seq::NoteSink {
  public:
    struct Raw {
        long long block = 0;
        double time = 0.0;
        int note = 0;
        int velocity = 0;
        bool on = false;
    };

    void addNoteEvent(const seq::NoteEvent& event) override {
        events.push_back(
            {block, event.timeInBlock, event.noteNumber, event.velocity, event.isNoteOn});
    }

    long long block = 0;
    std::vector<Raw> events;
};

inline std::uint32_t nextLcg(std::uint32_t& s) {
    s = s * 1664525U + 1013904223U;
    return s >> 8U;
}

inline seq::MonoPattern monoPattern(int length, std::uint32_t seed) {
    seq::MonoPattern p;
    p.length = length;
    std::uint32_t s = seed;
    for (auto& step : p.steps) {
        step.noteNumber = 36 + static_cast<int>(nextLcg(s) % 24U);
        step.octaveShift = static_cast<int>(nextLcg(s) % 5U) - 2;
        step.gate = nextLcg(s) % 8U != 0U;
        step.accent = nextLcg(s) % 4U == 0U;
        step.glide = nextLcg(s) % 6U == 0U;
        step.tie = nextLcg(s) % 7U == 0U;
    }
    return p;
}

inline seq::PolyPattern polyPattern(int length, std::uint32_t seed) {
    seq::PolyPattern p;
    p.length = length;
    std::uint32_t s = seed;
    for (auto& step : p.steps) {
        step.gate = nextLcg(s) % 8U != 0U;
        step.tie = nextLcg(s) % 7U == 0U;
        const auto roll = nextLcg(s) % 4U;
        step.probability = roll == 0U ? 0.5f : (roll == 1U ? 0.75f : 1.0f);
        step.velocity = 60 + static_cast<int>(nextLcg(s) % 60U);
        step.noteCount = 1 + static_cast<int>(nextLcg(s) % 4U);
        for (int n = 0; n < step.noteCount; ++n) {
            auto& note = step.notes[static_cast<std::size_t>(n)];
            note.noteNumber = 40 + static_cast<int>(nextLcg(s) % 36U);
            note.velocity = nextLcg(s) % 3U == 0U ? 50 + static_cast<int>(nextLcg(s) % 70U) : 0;
        }
    }
    return p;
}

/// One block of the script: where the transport is, and which rate plays.
struct Block {
    seq::StepClock::BlockTiming timing;
    seq::StepClock::Rate rate;
};

inline std::vector<Block> blocksFor(const Spec& spec) {
    std::vector<Block> blocks;
    double beat = spec.script == Script::MidStart ? 2.123 : 0.0;
    const double perBlock = static_cast<double>(spec.blockSize) / spec.sampleRate * spec.bpm / 60.0;
    const int perSecond = std::max(1, static_cast<int>(spec.sampleRate / spec.blockSize));
    auto rate = spec.rate;

    auto run = [&](double seconds, bool playing) {
        const int n = std::max(1, static_cast<int>(seconds * perSecond));
        for (int i = 0; i < n; ++i) {
            const double end = playing ? beat + perBlock : beat;
            blocks.push_back({{beat, end, playing, spec.blockSize}, rate});
            beat = end;
        }
    };

    switch (spec.script) {
        case Script::Plain:
        case Script::MidStart:
            run(6.0, true);
            break;
        case Script::StartStop:
            run(3.0, true);
            run(0.5, false);
            beat = 5.3;
            run(3.0, true);
            run(0.2, false);
            beat = 0.0;
            run(2.0, true);
            break;
        case Script::Loop:
            run(3.0, true);
            beat = 1.0;
            run(3.0, true);
            beat = 40.5;
            run(3.0, true);
            break;
        case Script::RateSwitch:
            run(3.0, true);
            rate = seq::StepClock::Rate::Eighth;
            run(3.0, true);
            break;
    }
    return blocks;
}

inline Entry play(const Spec& spec) {
    const auto blocks = blocksFor(spec);
    Recorder recorder;

    if (spec.poly) {
        const auto pattern = polyPattern(32, 0xC0FFEEU);
        seq::PolyStepSequencer sequencer;
        sequencer.setSampleRate(spec.sampleRate);
        sequencer.setRandomSeed(spec.seed);
        sequencer.setDirectionSeed(static_cast<std::int64_t>(spec.seed) * 7919 + 1);
        sequencer.reset(nullptr);
        auto shaped = pattern;
        shaped.length = spec.length;
        for (const auto& block : blocks) {
            seq::PolyStepSequencer::Params params;
            params.rate = block.rate;
            params.direction = spec.direction;
            params.swing = spec.swing;
            params.gateLength = spec.gate;
            params.ramp = spec.ramp;
            params.skew = spec.skew;
            params.rampCycles = spec.cycles;
            params.hardAngle = spec.hardAngle;
            params.quantize = spec.quantize;
            params.quantizeSub = spec.quantizeSub;
            sequencer.processBlock(block.timing, shaped, params, recorder);
            ++recorder.block;
        }
        sequencer.reset(&recorder);
    } else {
        auto pattern = monoPattern(32, 0xBEEF01U);
        pattern.length = spec.length;
        seq::MonoStepSequencer sequencer;
        sequencer.setSampleRate(spec.sampleRate);
        sequencer.setDirectionSeed(static_cast<std::int64_t>(spec.seed) * 7919 + 1);
        sequencer.reset(nullptr);
        for (const auto& block : blocks) {
            seq::MonoStepSequencer::Params params;
            params.rate = block.rate;
            params.direction = spec.direction;
            params.swing = spec.swing;
            params.gateLength = spec.gate;
            params.accentVelocity = spec.accent;
            params.normalVelocity = spec.normal;
            params.ramp = spec.ramp;
            params.skew = spec.skew;
            params.rampCycles = spec.cycles;
            params.hardAngle = spec.hardAngle;
            params.quantize = spec.quantize;
            params.quantizeSub = spec.quantizeSub;
            sequencer.processBlock(block.timing, pattern, params, recorder);
            ++recorder.block;
        }
        sequencer.reset(&recorder);
    }

    // Pair each note-on with the note-off that releases it.
    struct Open {
        std::size_t index;
        int note;
    };
    std::vector<Played> played;
    std::vector<Open> open;
    long long lastSample = 0;
    for (const auto& event : recorder.events) {
        const long long sample =
            event.block * spec.blockSize + std::llround(event.time * spec.sampleRate);
        lastSample = sample;
        if (event.on) {
            played.push_back({sample, event.note, event.velocity, 0});
            open.push_back({played.size() - 1, event.note});
            continue;
        }
        for (auto it = open.begin(); it != open.end(); ++it) {
            if (it->note == event.note) {
                played[it->index].length = sample - played[it->index].sample;
                open.erase(it);
                break;
            }
        }
    }
    for (const auto& unreleased : open)
        played[unreleased.index].length = lastSample - played[unreleased.index].sample;

    Hash sequence;
    Hash full;
    for (const auto& p : played) {
        sequence.add(p.note);
        sequence.add(p.velocity);
        full.add(p.sample);
        full.add(p.note);
        full.add(p.velocity);
        full.add(p.length);
    }
    return {spec.name, played.size(), sequence.value(), full.value()};
}

inline std::vector<Spec> specs() {
    using R = seq::StepClock::Rate;
    using D = seq::StepClock::Direction;
    std::vector<Spec> all;
    auto add = [&](Spec s) { all.push_back(s); };

    // Mono
    add({.name = "mono.basic"});
    add({.name = "mono.swing", .sampleRate = 44100.0, .blockSize = 256, .swing = 0.5f});
    add({.name = "mono.eighth.short",
         .sampleRate = 96000.0,
         .blockSize = 1024,
         .rate = R::Eighth,
         .swing = 0.33f,
         .gate = 0.3f});
    add({.name = "mono.triplet.reverse",
         .blockSize = 480,
         .rate = R::TripletSixteenth,
         .direction = D::Reverse});
    add({.name = "mono.dotted.pingpong",
         .sampleRate = 44100.0,
         .blockSize = 128,
         .rate = R::DottedEighth,
         .direction = D::PingPong,
         .length = 7});
    add({.name = "mono.random", .direction = D::Random});
    add({.name = "mono.ramp", .ramp = 0.6f, .skew = 0.2f, .cycles = 2});
    add({.name = "mono.ramp.hard", .ramp = -0.5f, .skew = -0.3f, .cycles = 3, .hardAngle = true});
    add({.name = "mono.quantize", .swing = 0.4f, .ramp = 0.3f, .quantize = 0.7f});
    add({.name = "mono.long.bigblock",
         .blockSize = 2048,
         .bpm = 174.0,
         .rate = R::ThirtySecond,
         .gate = 1.0f,
         .length = 32});
    add({.name = "mono.accents",
         .sampleRate = 44100.0,
         .blockSize = 333,
         .rate = R::Quarter,
         .accent = 127,
         .normal = 60});
    add({.name = "mono.midstart",
         .sampleRate = 44100.0,
         .swing = 0.2f,
         .script = Script::MidStart});
    add({.name = "mono.startstop", .rate = R::Eighth, .swing = 0.25f, .script = Script::StartStop});
    add({.name = "mono.loop", .sampleRate = 44100.0, .blockSize = 441, .script = Script::Loop});
    add({.name = "mono.rateswitch", .script = Script::RateSwitch});
    add({.name = "mono.ramp.startstop",
         .ramp = 0.5f,
         .skew = 0.1f,
         .cycles = 2,
         .script = Script::StartStop});

    // Poly
    add({.name = "poly.basic", .poly = true});
    add({.name = "poly.swing",
         .poly = true,
         .sampleRate = 44100.0,
         .blockSize = 256,
         .swing = 0.5f});
    add({.name = "poly.seeded", .poly = true, .seed = 12345});
    add({.name = "poly.eighth.short",
         .poly = true,
         .sampleRate = 96000.0,
         .blockSize = 1024,
         .rate = R::Eighth,
         .swing = 0.33f,
         .gate = 0.3f,
         .seed = 7});
    add({.name = "poly.triplet.reverse",
         .poly = true,
         .blockSize = 480,
         .rate = R::TripletSixteenth,
         .direction = D::Reverse});
    add({.name = "poly.pingpong",
         .poly = true,
         .sampleRate = 44100.0,
         .blockSize = 128,
         .rate = R::DottedEighth,
         .direction = D::PingPong,
         .length = 7});
    add({.name = "poly.random", .poly = true, .direction = D::Random, .seed = 99});
    add({.name = "poly.ramp", .poly = true, .ramp = 0.6f, .skew = 0.2f, .cycles = 2});
    add({.name = "poly.quantize",
         .poly = true,
         .swing = 0.4f,
         .ramp = -0.3f,
         .hardAngle = true,
         .quantize = 0.7f});
    add({.name = "poly.long.bigblock",
         .poly = true,
         .blockSize = 2048,
         .bpm = 174.0,
         .rate = R::ThirtySecond,
         .gate = 1.0f,
         .length = 32,
         .seed = 3});
    add({.name = "poly.startstop",
         .poly = true,
         .rate = R::Eighth,
         .swing = 0.25f,
         .seed = 5,
         .script = Script::StartStop});
    add({.name = "poly.loop",
         .poly = true,
         .sampleRate = 44100.0,
         .blockSize = 441,
         .script = Script::Loop});
    add({.name = "poly.rateswitch", .poly = true, .seed = 11, .script = Script::RateSwitch});
    return all;
}

inline std::vector<Entry> runCorpus() {
    std::vector<Entry> out;
    for (const auto& spec : specs())
        out.push_back(play(spec));
    return out;
}

}  // namespace magda::stepcorpus
