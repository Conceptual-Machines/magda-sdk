#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <type_traits>

namespace magda {

/**
 * @brief Non-owning view of planar audio: channel pointers and a frame count.
 *
 * The pointers are held inline, so building a view never allocates and is safe
 * on the audio thread. Channels beyond kMaxChannels are ignored.
 */
template <typename Sample>
class BufferViewT {
  public:
    static constexpr int kMaxChannels = 8;

    BufferViewT() = default;

    /// @p channels is one pointer per channel; each is advanced by @p startFrame.
    BufferViewT(Sample* const* channels, int numChannels, int numFrames, int startFrame = 0)
        : numChannels_(std::clamp(numChannels, 0, kMaxChannels)), numFrames_(std::max(0, numFrames)) {
        for (int c = 0; c < numChannels_; ++c)
            channels_[static_cast<std::size_t>(c)] = channels[c] + startFrame;
    }

    /// A mutable view reads as a const one.
    template <typename Other>
        requires(std::is_const_v<Sample> && std::is_same_v<std::remove_const_t<Sample>, Other>)
    BufferViewT(const BufferViewT<Other>& other)
        : numChannels_(other.numChannels()), numFrames_(other.numFrames()) {
        for (int c = 0; c < numChannels_; ++c)
            channels_[static_cast<std::size_t>(c)] = other.channel(c);
    }

    int numChannels() const {
        return numChannels_;
    }

    int numFrames() const {
        return numFrames_;
    }

    Sample* channel(int index) const {
        return channels_[static_cast<std::size_t>(index)];
    }

  private:
    std::array<Sample*, kMaxChannels> channels_{};
    int numChannels_ = 0;
    int numFrames_ = 0;
};

using BufferView = BufferViewT<float>;
using ConstBufferView = BufferViewT<const float>;

}  // namespace magda
