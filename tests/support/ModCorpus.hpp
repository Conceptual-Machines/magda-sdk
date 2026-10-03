#pragma once

/**
 * @file ModCorpus.hpp
 * @brief A deterministic corpus that drives every modulator and hashes what comes out.
 *
 * Included by the SDK tests and by magda-core's bridge test. The includer defines
 * MOD_CORPUS_NS (the namespace holding the modulator types), MOD_CORPUS_TRIGGER and
 * MOD_CORPUS_RATE (the trigger-mode and rate-type enums), and passes runCorpus a
 * maker with `using Block` and `static Block make(const Spec&, const ModTiming&)`.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <magda/sdk/curve/CurveTypes.hpp>
#include <string>
#include <utility>
#include <vector>

namespace magda::modcorpus {

namespace M = MOD_CORPUS_NS;
using Trigger = MOD_CORPUS_TRIGGER;
using Rate = MOD_CORPUS_RATE;

/// One block as the transport would describe it.
struct Spec {
    int numSamples = 0;
    bool playing = false;
    double beatStart = 0.0;
    double beatLen = 0.0;
    double secStart = 0.0;
};

class Hash {
  public:
    void bytes(const void* data, std::size_t size) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            h_ ^= p[i];
            h_ *= 1099511628211ULL;
        }
    }
    void f(float v) {
        bytes(&v, sizeof v);
    }
    void d(double v) {
        bytes(&v, sizeof v);
    }
    std::uint64_t value() const {
        return h_;
    }

  private:
    std::uint64_t h_ = 14695981039346656037ULL;
};

struct Entry {
    std::string name;
    std::uint64_t hash;
};

/// A transport that rolls, stops and locates on a fixed script.
template <class Mk> class Timeline {
  public:
    Timeline(double sampleRate, int blockSize, double bpm, int numerator, int denominator)
        : sr_(sampleRate),
          bs_(blockSize),
          bpm_(bpm),
          timing_{sampleRate, bpm, numerator, denominator} {}

    const M::ModTiming& timing() const {
        return timing_;
    }

    typename Mk::Block next(int index, bool alwaysPlaying = false) {
        const bool playing = alwaysPlaying || (index % 40) < 28;
        if (index == 60 && !alwaysPlaying) {
            beat_ = 3.25;
            sec_ = beat_ * 60.0 / bpm_;
        }
        const double len = static_cast<double>(bs_) / sr_;
        Spec spec;
        spec.numSamples = bs_;
        spec.playing = playing;
        spec.beatStart = beat_;
        spec.beatLen = playing ? len * bpm_ / 60.0 : 0.0;
        spec.secStart = sec_;
        if (playing) {
            beat_ += spec.beatLen;
            sec_ += len;
        }
        return Mk::make(spec, timing_);
    }

  private:
    double sr_;
    int bs_;
    double bpm_;
    M::ModTiming timing_;
    double beat_ = 0.0;
    double sec_ = 0.0;
};

inline constexpr std::array<double, 3> kRates{44100.0, 48000.0, 96000.0};
inline constexpr std::array<int, 4> kBlocks{1, 64, 480, 512};

inline std::vector<sdk::CurvePointData> drawnCurve() {
    std::vector<sdk::CurvePointData> pts(5);
    pts[0].phase = 0.0f;
    pts[0].value = 0.1f;
    pts[1].phase = 0.2f;
    pts[1].value = 0.9f;
    pts[1].tension = 1.5f;
    pts[2].phase = 0.45f;
    pts[2].value = 0.3f;
    pts[2].curveType = 2;
    pts[3].phase = 0.7f;
    pts[3].value = 0.8f;
    pts[3].curveType = 1;
    pts[3].outHandleX = 0.05f;
    pts[3].outHandleY = -0.3f;
    pts[4].phase = 1.0f;
    pts[4].value = 0.4f;
    pts[4].curveType = 3;
    return pts;
}

template <class Mk> inline void lfoEntries(std::vector<Entry>& out) {
    const std::vector<sdk::CurvePointData> none;
    const auto drawn = drawnCurve();
    const std::array waves{sdk::LFOWaveform::Sine,       sdk::LFOWaveform::Triangle,
                           sdk::LFOWaveform::Square,     sdk::LFOWaveform::Saw,
                           sdk::LFOWaveform::ReverseSaw, sdk::LFOWaveform::Custom};

    {
        Hash h;
        for (double sr : kRates)
            for (int bs : kBlocks)
                for (float hz : {0.37f, 5.0f, 50.0f})
                    for (float offset : {0.0f, 0.25f})
                        for (std::size_t w = 0; w < waves.size() + 7; ++w) {
                            M::LfoSettings s;
                            s.wave = w < waves.size() ? waves[w] : sdk::LFOWaveform::Custom;
                            s.preset = static_cast<sdk::CurvePreset>(
                                w < waves.size() ? 0 : w - waves.size());
                            s.rate.hz = hz;
                            s.phaseOffset = offset;
                            M::LfoState st;
                            Timeline<Mk> tl(sr, bs, 120.0, 4, 4);
                            for (int i = 0; i < 96; ++i) {
                                const auto b = tl.next(i);
                                h.f(M::advanceLfo(st, s, none, b, tl.timing()));
                                h.f(st.phase);
                            }
                        }
        out.push_back({"lfo.free.shapes", h.value()});
    }

    {
        Hash h;
        for (bool oneShot : {false, true})
            for (bool invert : {false, true})
                for (bool loop : {false, true})
                    for (float hz : {0.5f, 3.0f, 19.0f}) {
                        M::LfoSettings s;
                        s.wave = sdk::LFOWaveform::Custom;
                        s.rate.hz = hz;
                        s.oneShot = oneShot;
                        s.invertOutput = invert;
                        s.useLoopRegion = loop;
                        s.loopStart = 0.3f;
                        s.loopEnd = 0.8f;
                        M::LfoState st;
                        Timeline<Mk> tl(48000.0, 128, 120.0, 4, 4);
                        for (int i = 0; i < 400; ++i) {
                            const auto b = tl.next(i);
                            h.f(M::advanceLfo(st, s, drawn, b, tl.timing()));
                            h.f(st.phase);
                            h.d(st.cycles);
                        }
                    }
        out.push_back({"lfo.custom.loop", h.value()});
    }

    for (auto sync : {M::ModSync::Free, M::ModSync::Transport}) {
        Hash h;
        for (double sr : {44100.0, 48000.0})
            for (int bs : {480, 512})
                for (double bpm : {120.0, 97.5})
                    for (auto sig : {std::pair{4, 4}, std::pair{3, 4}, std::pair{6, 8}})
                        for (int rate = 0; rate <= 23; ++rate)
                            for (float offset : {0.0f, 0.3f}) {
                                M::LfoSettings s;
                                s.sync = sync;
                                s.tempoSync = true;
                                s.rate.rateType = rate;
                                s.phaseOffset = offset;
                                M::LfoState st;
                                Timeline<Mk> tl(sr, bs, bpm, sig.first, sig.second);
                                for (int i = 0; i < 90; ++i) {
                                    const auto b = tl.next(i);
                                    h.f(M::advanceLfo(st, s, none, b, tl.timing()));
                                    h.f(st.phase);
                                }
                            }
        out.push_back(
            {sync == M::ModSync::Free ? "lfo.sync.free" : "lfo.sync.transport", h.value()});
    }

    {
        Hash h;
        for (bool synced : {false, true})
            for (bool oneShot : {false, true})
                for (bool startGated : {false, true})
                    for (auto trig : {Trigger::MIDI, Trigger::Audio}) {
                        M::LfoSettings s;
                        s.sync = M::ModSync::Note;
                        s.trigger = trig;
                        s.tempoSync = synced;
                        s.rate.hz = 4.0f;
                        s.rate.rateType = static_cast<int>(Rate::Quarter);
                        s.oneShot = oneShot;
                        s.startGated = startGated;
                        s.gateOnTrigger = true;
                        s.wave = sdk::LFOWaveform::Triangle;
                        M::LfoState st;
                        Timeline<Mk> tl(48000.0, 256, 133.0, 4, 4);
                        for (int i = 0; i < 160; ++i) {
                            if (i == 10 || i == 55 || i == 100) {
                                st.gated = false;
                                st.heldNotes = 1;
                                M::restartLfo(st, s);
                            }
                            if (i == 40 || i == 90)
                                st.gated = true;
                            if (i == 56)
                                st.forceZero = true;
                            if (i == 120)
                                s.trigger = Trigger::Free;
                            const auto b = tl.next(i, true);
                            h.f(M::advanceLfo(st, s, none, b, tl.timing()));
                            h.f(st.phase);
                            h.d(st.cycles);
                        }
                    }
        out.push_back({"lfo.note.trigger", h.value()});
    }
}

template <class Mk> inline void adsrEntries(std::vector<Entry>& out) {
    {
        Hash h;
        for (double sr : kRates)
            for (int bs : kBlocks)
                for (float ms : {0.0f, 1.0f, 40.0f, 700.0f})
                    for (float curve : {-0.8f, 0.0f, 0.5f})
                        for (float sustain : {0.0f, 0.6f, 1.0f}) {
                            M::AdsrSettings s;
                            s.attackMs = ms;
                            s.decayMs = ms * 2.0f;
                            s.releaseMs = ms * 3.0f;
                            s.sustain = sustain;
                            s.attackCurve = curve;
                            s.decayCurve = -curve;
                            s.releaseCurve = curve;
                            M::AdsrState st;
                            Timeline<Mk> tl(sr, bs, 120.0, 4, 4);
                            for (int i = 0; i < 120; ++i) {
                                const auto b = tl.next(i);
                                h.f(M::advanceAdsr(st, s, b, tl.timing()));
                                h.f(st.stagePhase);
                                h.d(st.timeInStage);
                            }
                        }
        out.push_back({"adsr.free", h.value()});
    }

    {
        Hash h;
        for (bool fromZero : {false, true})
            for (double sr : {44100.0, 48000.0})
                for (int bs : {64, 512})
                    for (float ms : {5.0f, 60.0f, 400.0f}) {
                        M::AdsrSettings s;
                        s.sync = M::ModSync::Note;
                        s.trigger = Trigger::MIDI;
                        s.attackMs = ms;
                        s.decayMs = ms;
                        s.releaseMs = ms * 2.0f;
                        s.sustain = 0.5f;
                        s.attackCurve = 0.4f;
                        s.releaseCurve = -0.4f;
                        M::AdsrState st;
                        Timeline<Mk> tl(sr, bs, 120.0, 4, 4);
                        const int step = std::max(1, static_cast<int>(sr * 0.05 / bs));
                        for (int i = 0; i < 240; ++i) {
                            const int phase = i % (step * 10);
                            if (phase == 0) {
                                st.gated = false;
                                M::restartAdsr(st, s, fromZero);
                            } else if (phase == step * 4) {
                                st.gated = true;
                            } else if (phase == step * 6) {
                                st.gated = false;
                                M::restartAdsr(st, s, fromZero);
                            }
                            if (i == 150)
                                st.forceZero = true;
                            const auto b = tl.next(i, true);
                            h.f(M::advanceAdsr(st, s, b, tl.timing()));
                            h.f(st.stagePhase);
                            h.d(st.timeInStage);
                        }
                    }
        out.push_back({"adsr.note.retrigger", h.value()});
    }

    {
        Hash h;
        for (double sr : {44100.0, 48000.0})
            for (int bs : {480, 512})
                for (double bpm : {120.0, 97.5})
                    for (auto sig : {std::pair{4, 4}, std::pair{3, 4}, std::pair{6, 8}})
                        for (int rate : {0, 1, 5, 10, 13, 16, 20, 23})
                            for (auto sync : {M::ModSync::Free, M::ModSync::Transport}) {
                                M::AdsrSettings s;
                                s.sync = sync;
                                s.tempoSync = true;
                                s.rateType = rate;
                                s.sustain = 0.4f;
                                M::AdsrState st;
                                Timeline<Mk> tl(sr, bs, bpm, sig.first, sig.second);
                                for (int i = 0; i < 140; ++i) {
                                    if (i == 70)
                                        s.tempoSync = false;
                                    if (i == 100)
                                        s.tempoSync = true;
                                    const auto b = tl.next(i);
                                    h.f(M::advanceAdsr(st, s, b, tl.timing()));
                                    h.f(st.stagePhase);
                                    h.d(st.timeInStage);
                                }
                            }
        out.push_back({"adsr.sync", h.value()});
    }

    {
        Hash h;
        const M::AdsrSettings s;
        const M::ModTiming t{48000.0, 120.0, 4, 4};
        for (float v : {0.0f, 0.1f, 0.5f, 0.9f, 1.0f})
            for (float c : {-1.0f, -0.3f, 0.0f, 0.3f, 1.0f})
                for (float a : {0.0f, 0.5f, 1.0f})
                    h.f(M::adsrSegmentAt(v, a, 1.0f - a, c));
        h.d(M::adsrStageSeconds(250.0f, s, t));
        out.push_back({"adsr.segment", h.value()});
    }
}

template <class Mk> inline void randomEntries(std::vector<Entry>& out) {
    {
        Hash h;
        for (std::uint64_t seed : {0ULL, 1ULL, 0xDEADBEEFULL, 0x123456789ABCDEF0ULL})
            for (auto type : {M::RandomShape::Stepped, M::RandomShape::Noise})
                for (float shape : {0.0f, 0.5f, 1.0f})
                    for (float smooth : {0.0f, 0.6f})
                        for (float depth : {0.2f, 1.0f})
                            for (double sr : {44100.0, 48000.0})
                                for (int bs : {64, 480}) {
                                    M::RandomSettings s;
                                    s.type = type;
                                    s.rate.hz = 7.0f;
                                    s.shape = shape;
                                    s.smooth = smooth;
                                    s.stepDepth = depth;
                                    M::RandomState st;
                                    M::seedRandom(st, seed);
                                    Timeline<Mk> tl(sr, bs, 120.0, 4, 4);
                                    for (int i = 0; i < 100; ++i) {
                                        const auto b = tl.next(i);
                                        h.f(M::advanceRandom(st, s, b, tl.timing()));
                                        h.f(st.phase);
                                        h.d(st.cycles);
                                    }
                                }
        out.push_back({"random.free", h.value()});
    }

    for (auto sync : {M::ModSync::Free, M::ModSync::Transport}) {
        Hash h;
        for (double sr : {44100.0, 48000.0})
            for (int bs : {480, 512})
                for (double bpm : {120.0, 97.5})
                    for (auto sig : {std::pair{4, 4}, std::pair{3, 4}, std::pair{6, 8}})
                        for (int rate = 1; rate <= 23; ++rate) {
                            M::RandomSettings s;
                            s.sync = sync;
                            s.tempoSync = true;
                            s.rate.rateType = rate;
                            s.shape = 0.5f;
                            M::RandomState st;
                            M::seedRandom(st, 42);
                            Timeline<Mk> tl(sr, bs, bpm, sig.first, sig.second);
                            for (int i = 0; i < 90; ++i) {
                                const auto b = tl.next(i);
                                h.f(M::advanceRandom(st, s, b, tl.timing()));
                                h.f(st.phase);
                            }
                        }
        out.push_back(
            {sync == M::ModSync::Free ? "random.sync.free" : "random.sync.transport", h.value()});
    }

    {
        Hash h;
        M::RandomSettings s;
        s.sync = M::ModSync::Note;
        s.rate.hz = 3.0f;
        s.shape = 0.7f;
        M::RandomState st;
        M::seedRandom(st, 99);
        Timeline<Mk> tl(48000.0, 256, 120.0, 4, 4);
        for (int i = 0; i < 200; ++i) {
            if (i % 37 == 5)
                M::restartRandom(st, s);
            const auto b = tl.next(i, true);
            h.f(M::advanceRandom(st, s, b, tl.timing()));
            h.f(st.phase);
        }
        out.push_back({"random.note.restart", h.value()});
    }
}

/// A burst-and-silence signal that is the same on every run.
inline std::vector<float> followerSignal(int count, int block, std::uint32_t& rng) {
    std::vector<float> v(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        rng = rng * 1664525u + 1013904223u;
        const float noise =
            static_cast<float>(rng >> 8) / static_cast<float>(1 << 24) * 2.0f - 1.0f;
        const float tone = std::sin(0.05f * static_cast<float>(i + block * count));
        v[static_cast<std::size_t>(i)] =
            ((block / 6) % 2 == 0) ? 0.8f * tone + 0.2f * noise : 0.01f * noise;
    }
    return v;
}

template <class Mk> inline void followerEntries(std::vector<Entry>& out) {
    Hash h;
    for (double sr : {32000.0, 44100.0, 48000.0, 96000.0})
        for (int bs : {64, 480, 512})
            for (float attack : {1.0f, 20.0f, 300.0f})
                for (float release : {5.0f, 150.0f})
                    for (float hold : {0.0f, 40.0f})
                        for (int bands = 0; bands < 4; ++bands) {
                            M::FollowerSettings s;
                            s.gainDb = bands == 3 ? 6.0f : -3.0f;
                            s.attackMs = attack;
                            s.releaseMs = release;
                            s.holdMs = hold;
                            s.highPass = (bands & 1) != 0;
                            s.lowPass = (bands & 2) != 0;
                            M::FollowerState st;
                            std::vector<float> scratch(static_cast<std::size_t>(bs));
                            std::uint32_t rng = 12345;
                            Timeline<Mk> tl(sr, bs, 120.0, 4, 4);
                            for (int i = 0; i < 40; ++i) {
                                if (i == 20) {
                                    s.highPassHz = 800.0f;
                                    s.lowPassHz = 5000.0f;
                                }
                                const auto sig = followerSignal(bs, i, rng);
                                M::detectFollowerSource(st, s, sig, sr, scratch);
                                const auto b = tl.next(i, true);
                                h.f(st.sourcePeak);
                                h.f(M::advanceFollower(st, s, b, tl.timing()));
                            }
                        }
    {
        M::FollowerSettings s;
        M::FollowerState st;
        std::vector<float> scratch(8);
        std::vector<float> sig{0.25f, std::nanf(""), -0.75f, 0.5f, 0.0f, -0.0f, 0.1f, -0.2f};
        M::detectFollowerSource(st, s, sig, 48000.0, scratch);
        h.f(st.sourcePeak);
        std::vector<float> empty;
        M::detectFollowerSource(st, s, empty, 48000.0, scratch);
        h.f(st.sourcePeak);
    }
    out.push_back({"follower.all", h.value()});
}

template <class Mk> inline std::vector<Entry> runCorpus() {
    std::vector<Entry> out;
    lfoEntries<Mk>(out);
    adsrEntries<Mk>(out);
    randomEntries<Mk>(out);
    followerEntries<Mk>(out);
    return out;
}

}  // namespace magda::modcorpus
