#pragma once

#include <cstddef>

struct PFFFT_Setup;

/**
 * @file Fft.hpp
 * @brief Real-input FFT for power-of-two sizes, on the vendored pffft.
 */

namespace magda::sdk {

/**
 * @brief Forward and inverse real FFT of one fixed size.
 *
 * A spectrum is size / 2 + 1 bins stored as interleaved (re, im) floats, so 2 * (size / 2 + 1)
 * values; bin 0 and bin size / 2 have a zero imaginary part. The forward transform is the plain
 * DFT with no scaling and the inverse divides by size, so inverse(forward(x)) returns x. One
 * instance is not safe to share between threads.
 */
class RealFft {
  public:
    /// Smallest size the backing library handles with SIMD.
    static constexpr int kMinSize = 32;
    static constexpr int kMaxSize = 1 << 24;

    RealFft() = default;
    ~RealFft();
    RealFft(const RealFft&) = delete;
    RealFft& operator=(const RealFft&) = delete;
    RealFft(RealFft&& other) noexcept;
    RealFft& operator=(RealFft&& other) noexcept;

    /**
     * @brief Set the transform size. Allocates; everything after it does not.
     * @return False, leaving the instance unprepared, when @p size is not a power of two in
     *         [kMinSize, kMaxSize].
     */
    bool prepare(int size);

    bool isPrepared() const noexcept {
        return setup_ != nullptr;
    }

    int size() const noexcept {
        return size_;
    }

    /** @brief Values in a spectrum: 2 * (size() / 2 + 1). */
    int spectrumValues() const noexcept {
        return size_ + 2;
    }

    /** @brief Transform size() samples of @p time into @p spectrum (spectrumValues() floats). */
    void forward(const float* time, float* spectrum) noexcept;

    /** @brief The bin magnitudes only: @p magnitude holds size() / 2 + 1 floats. */
    void forwardMagnitude(const float* time, float* magnitude) noexcept;

    /** @brief Transform @p spectrum (spectrumValues() floats) into size() samples. */
    void inverse(const float* spectrum, float* time) noexcept;

  private:
    void release() noexcept;

    PFFFT_Setup* setup_ = nullptr;
    float* input_ = nullptr;
    float* output_ = nullptr;
    float* work_ = nullptr;
    int size_ = 0;
};

}  // namespace magda::sdk
