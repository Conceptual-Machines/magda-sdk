#include "magda/sdk/dsp/Fft.hpp"

#include <pffft/pffft.h>

#include <cmath>
#include <cstring>
#include <utility>

namespace magda::sdk {

namespace {

bool isPowerOfTwo(int n) noexcept {
    return n > 0 && (n & (n - 1)) == 0;
}

}  // namespace

RealFft::~RealFft() {
    release();
}

RealFft::RealFft(RealFft&& other) noexcept
    : setup_(std::exchange(other.setup_, nullptr)),
      input_(std::exchange(other.input_, nullptr)),
      output_(std::exchange(other.output_, nullptr)),
      work_(std::exchange(other.work_, nullptr)),
      size_(std::exchange(other.size_, 0)) {}

RealFft& RealFft::operator=(RealFft&& other) noexcept {
    if (this != &other) {
        release();
        setup_ = std::exchange(other.setup_, nullptr);
        input_ = std::exchange(other.input_, nullptr);
        output_ = std::exchange(other.output_, nullptr);
        work_ = std::exchange(other.work_, nullptr);
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}

void RealFft::release() noexcept {
    if (setup_ != nullptr)
        pffft_destroy_setup(setup_);
    pffft_aligned_free(input_);
    pffft_aligned_free(output_);
    pffft_aligned_free(work_);
    setup_ = nullptr;
    input_ = output_ = work_ = nullptr;
    size_ = 0;
}

bool RealFft::prepare(int size) {
    release();
    if (!isPowerOfTwo(size) || size < kMinSize || size > kMaxSize)
        return false;

    const auto bytes = static_cast<std::size_t>(size) * sizeof(float);
    setup_ = pffft_new_setup(size, PFFFT_REAL);
    input_ = static_cast<float*>(pffft_aligned_malloc(bytes));
    output_ = static_cast<float*>(pffft_aligned_malloc(bytes));
    work_ = static_cast<float*>(pffft_aligned_malloc(bytes));
    if (setup_ == nullptr || input_ == nullptr || output_ == nullptr || work_ == nullptr) {
        release();
        return false;
    }
    size_ = size;
    return true;
}

// pffft's ordered real output is [re0, reN/2, re1, im1, ..., re(N/2-1), im(N/2-1)].
void RealFft::forward(const float* time, float* spectrum) noexcept {
    std::memcpy(input_, time, static_cast<std::size_t>(size_) * sizeof(float));
    pffft_transform_ordered(setup_, input_, output_, work_, PFFFT_FORWARD);

    std::memcpy(spectrum, output_, static_cast<std::size_t>(size_) * sizeof(float));
    spectrum[0] = output_[0];
    spectrum[1] = 0.0f;
    spectrum[size_] = output_[1];
    spectrum[size_ + 1] = 0.0f;
}

void RealFft::forwardMagnitude(const float* time, float* magnitude) noexcept {
    std::memcpy(input_, time, static_cast<std::size_t>(size_) * sizeof(float));
    pffft_transform_ordered(setup_, input_, output_, work_, PFFFT_FORWARD);

    magnitude[0] = std::abs(output_[0]);
    magnitude[size_ / 2] = std::abs(output_[1]);
    for (int k = 1; k < size_ / 2; ++k) {
        const float re = output_[2 * k];
        const float im = output_[2 * k + 1];
        magnitude[k] = std::sqrt(re * re + im * im);
    }
}

void RealFft::inverse(const float* spectrum, float* time) noexcept {
    std::memcpy(input_, spectrum, static_cast<std::size_t>(size_) * sizeof(float));
    input_[0] = spectrum[0];
    input_[1] = spectrum[size_];
    pffft_transform_ordered(setup_, input_, output_, work_, PFFFT_BACKWARD);

    const float scale = 1.0f / static_cast<float>(size_);
    for (int i = 0; i < size_; ++i)
        time[i] = output_[i] * scale;
}

}  // namespace magda::sdk
