#include <catch2/catch_test_macros.hpp>
#include <magda/sdk/mod/ModAdsr.hpp>
#include <magda/sdk/mod/ModFollower.hpp>
#include <magda/sdk/mod/ModLfo.hpp>
#include <magda/sdk/mod/ModRandom.hpp>

#define MOD_CORPUS_NS magda::sdk
#define MOD_CORPUS_TRIGGER magda::sdk::LFOTriggerMode
#define MOD_CORPUS_RATE magda::sdk::ModRateType
#include <support/ModCorpus.hpp>

/**
 * @file test_mod_sample_identity.cpp
 * @brief The modulator cores' output over a fixed corpus, pinned to what magda-core produced before
 * the move (#2932).
 */

namespace {

/// A hand-assembled block, as magda-core builds one with no tempo map.
struct BareBlock {
    using Block = magda::sdk::ModBlock;

    static Block make(const magda::modcorpus::Spec& spec, const magda::sdk::ModTiming& timing) {
        const double beatEnd = spec.beatStart + spec.beatLen;
        const double beatLength = beatEnd - spec.beatStart;
        const double barBeats = magda::sdk::barBeatsOf(timing.numerator, timing.denominator);

        Block block;
        block.numSamples = spec.numSamples;
        block.playing = spec.playing;
        block.secondsStart = spec.secStart;
        block.barPosition =
            spec.beatStart / std::max(4.0 * timing.numerator / timing.denominator, 1.0e-6);
        block.barsElapsed = spec.playing && beatLength > 0.0
                                ? beatLength / std::max(barBeats, 1.0e-6)
                                : std::max(spec.numSamples, 0) / std::max(timing.sampleRate, 1.0) *
                                      timing.bpm / 60.0 / std::max(barBeats, 1.0e-6);
        return block;
    }
};

}  // namespace

TEST_CASE("The corpus is deterministic", "[mod][identity]") {
    const auto first = magda::modcorpus::runCorpus<BareBlock>();
    const auto second = magda::modcorpus::runCorpus<BareBlock>();

    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i)
        CHECK(first[i].hash == second[i].hash);
}

// Float results differ in the last bit across compilers and platforms, so the
// exact pins hold where they were captured. Everywhere else the behavioural
// tests in test_mod_*.cpp are the check.
#if defined(__APPLE__) && defined(__aarch64__)
TEST_CASE("The modulators reproduce magda-core's pre-move output", "[mod][identity]") {
    const std::vector<magda::modcorpus::Entry> pinned{
        {"lfo.free.shapes", 0xbcc218008662c36aULL},
        {"lfo.custom.loop", 0x2b54e548bae2de25ULL},
        {"lfo.sync.free", 0x1f9e536a9e954495ULL},
        {"lfo.sync.transport", 0x1619738fa3a549b3ULL},
        {"lfo.note.trigger", 0x6ae7ad52eb1bf8d5ULL},
        {"adsr.free", 0x4b482912e6c9c032ULL},
        {"adsr.note.retrigger", 0xc7160c486e54a394ULL},
        {"adsr.sync", 0x8bc3f30c0a0dcdfeULL},
        {"adsr.segment", 0x19656e9e4771a003ULL},
        {"random.free", 0x592203b5acdc8350ULL},
        {"random.sync.free", 0x351110d2f69d3c63ULL},
        {"random.sync.transport", 0xd0bff8d654d6a20cULL},
        {"random.note.restart", 0x5ceeb779fcc17ddbULL},
        {"follower.all", 0x60cbfa401f107e5dULL},
    };

    const auto actual = magda::modcorpus::runCorpus<BareBlock>();
    REQUIRE(actual.size() == pinned.size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        INFO(actual[i].name);
        CHECK(actual[i].name == pinned[i].name);
        CHECK(actual[i].hash == pinned[i].hash);
    }
}
#endif
