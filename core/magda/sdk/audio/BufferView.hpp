#pragma once

#include <type_traits>

namespace magda {

/**
 * @brief Non-owning view of planar audio: the host's channel-pointer array and a frame count.
 *
 * The view is already sliced to the block, so frame 0 is the block's first frame. It holds the
 * host's array rather than a copy: no channel cap, no allocation, safe on the audio thread. The
 * array must outlive the view.
 */
template <typename Sample>
class BufferViewT {
  public:
    BufferViewT() = default;

    BufferViewT(Sample* const* channels, int numChannels, int numFrames)
        : channels_(channels),
          numChannels_(channels != nullptr && numChannels > 0 ? numChannels : 0),
          numFrames_(numFrames > 0 ? numFrames : 0) {}

    /// A mutable view reads as a const one (float* const* converts to const float* const*).
    template <typename Other>
        requires(std::is_const_v<Sample> && std::is_same_v<std::remove_const_t<Sample>, Other>)
    BufferViewT(const BufferViewT<Other>& other)
        : channels_(other.channels()),
          numChannels_(other.numChannels()),
          numFrames_(other.numFrames()) {}

    int numChannels() const {
        return numChannels_;
    }

    int numFrames() const {
        return numFrames_;
    }

    Sample* channel(int index) const {
        return channels_[index];
    }

    /// The host's pointer array, @ref numChannels entries.
    Sample* const* channels() const {
        return channels_;
    }

  private:
    Sample* const* channels_ = nullptr;
    int numChannels_ = 0;
    int numFrames_ = 0;
};

using BufferView = BufferViewT<float>;
using ConstBufferView = BufferViewT<const float>;

}  // namespace magda
