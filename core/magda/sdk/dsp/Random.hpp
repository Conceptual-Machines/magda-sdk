#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <random>

/**
 * @file Random.hpp
 * @brief Small seedable random generators with no allocation.
 */

namespace magda::sdk {

/**
 * @brief juce::Random's 48-bit LCG: the same sequence for the same seed and call pattern.
 *
 * Ported call for call so a pattern that was seeded once keeps its result. Not for audio-rate
 * noise; use Xoshiro256 for new code.
 */
class Lcg48Random {
  public:
    /** @brief Seeded from the system entropy source. Not for the audio thread. */
    Lcg48Random() : seed_(entropySeed()) {}

    explicit Lcg48Random(std::int64_t seed) noexcept : seed_(seed) {}

    void setSeed(std::int64_t seed) noexcept {
        seed_ = seed;
    }

    /** @brief 32 bits: the top of the 48-bit state. */
    int nextInt() noexcept {
        seed_ = static_cast<std::int64_t>(
            ((static_cast<std::uint64_t>(seed_) * 0x5deece66dULL) + 11) & 0xffffffffffffULL);
        return static_cast<int>(seed_ >> 16);
    }

    /** @brief In [0, maxValue); @p maxValue must be positive. */
    int nextInt(int maxValue) noexcept {
        return static_cast<int>(
            ((static_cast<unsigned int>(nextInt())) * static_cast<std::uint64_t>(maxValue)) >> 32);
    }

    /** @brief In [start, start + length). */
    int nextInt(int start, int length) noexcept {
        return start + nextInt(length);
    }

    std::int64_t nextInt64() noexcept {
        const auto hi = static_cast<std::uint64_t>(static_cast<unsigned int>(nextInt())) << 32;
        return static_cast<std::int64_t>(hi | static_cast<std::uint64_t>(
                                                  static_cast<unsigned int>(nextInt())));
    }

    bool nextBool() noexcept {
        return (nextInt() & 0x40000000) != 0;
    }

    /** @brief In [0, 1). */
    float nextFloat() noexcept {
        const float result = static_cast<float>(static_cast<std::uint32_t>(nextInt())) /
                             (static_cast<float>(std::numeric_limits<std::uint32_t>::max()) + 1.0f);
        const float cap = 1.0f - std::numeric_limits<float>::epsilon();
        return result < cap ? result : cap;
    }

    /** @brief In [0, 1). */
    double nextDouble() noexcept {
        return static_cast<std::uint32_t>(nextInt()) /
               (std::numeric_limits<std::uint32_t>::max() + 1.0);
    }

  private:
    static std::int64_t entropySeed() {
        std::random_device device;
        const auto bits = (static_cast<std::uint64_t>(device()) << 32) | device();
        const auto ticks = static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        return static_cast<std::int64_t>(bits ^ ticks);
    }

    std::int64_t seed_;
};

/** @brief splitmix64: one 64-bit state, used to seed larger generators. */
class SplitMix64 {
  public:
    explicit SplitMix64(std::uint64_t seed) noexcept : state_(seed) {}

    std::uint64_t next() noexcept {
        std::uint64_t z = (state_ += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

  private:
    std::uint64_t state_;
};

/** @brief xoshiro256**: fast, 256-bit state, seeded through splitmix64. */
class Xoshiro256 {
  public:
    explicit Xoshiro256(std::uint64_t seed) noexcept {
        SplitMix64 mix(seed);
        for (auto& word : s_)
            word = mix.next();
    }

    std::uint64_t next() noexcept {
        const std::uint64_t result = rotl(s_[1] * 5, 7) * 9;
        const std::uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return result;
    }

    /** @brief In [0, 1), 53 bits. */
    double nextDouble() noexcept {
        return static_cast<double>(next() >> 11) * 0x1.0p-53;
    }

    /** @brief In [0, 1), 24 bits. */
    float nextFloat() noexcept {
        return static_cast<float>(next() >> 40) * 0x1.0p-24f;
    }

    /** @brief In [0, bound) by multiply-shift; @p bound must be positive. */
    std::uint32_t nextBelow(std::uint32_t bound) noexcept {
        return static_cast<std::uint32_t>(((next() >> 32) * bound) >> 32);
    }

  private:
    static std::uint64_t rotl(std::uint64_t x, int k) noexcept {
        return (x << k) | (x >> (64 - k));
    }

    std::array<std::uint64_t, 4> s_{};
};

}  // namespace magda::sdk
