#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "magda/sdk/display/ColourRole.hpp"

/**
 * @file DisplayList.hpp
 * @brief The draw commands a UI core emits and every shell interprets (docs/display-list.md).
 */

namespace magda::sdk::display {

struct Point {
    float x = 0.0f;
    float y = 0.0f;
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    float right() const {
        return x + width;
    }
    float bottom() const {
        return y + height;
    }
};

/**
 * @brief A theme role or a literal colour, with an optional alpha.
 *
 * The alpha replaces the resolved colour's own, as juce::Colour::withAlpha does.
 */
struct Colour {
    std::optional<ColourRole> role;
    std::uint32_t argb = 0xFF000000;
    std::optional<float> alpha;

    static Colour of(ColourRole r) {
        return {r, 0xFF000000, std::nullopt};
    }
    static Colour literal(std::uint32_t argbValue) {
        return {std::nullopt, argbValue, std::nullopt};
    }
    Colour withAlpha(float a) const {
        auto copy = *this;
        copy.alpha = a;
        return copy;
    }
};

struct GradientStop {
    float position = 0.0f;
    Colour colour;
};

/// Stops are sorted by position and run from 0 to 1.
struct LinearGradient {
    Point from;
    Point to;
    std::vector<GradientStop> stops;
};

using Paint = std::variant<Colour, LinearGradient>;

class Path {
  public:
    enum class Verb : std::uint8_t { Move, Line, Quad, Cubic, Close };

    struct Element {
        Verb verb = Verb::Move;
        std::array<float, 6> points{};
    };

    Path& moveTo(float x, float y);
    Path& lineTo(float x, float y);
    Path& quadTo(float cx, float cy, float x, float y);
    Path& cubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y);
    Path& close();

    const std::vector<Element>& elements() const {
        return elements_;
    }

  private:
    std::vector<Element> elements_;
};

enum class Justification : std::uint8_t { Left, Centre, Right };

struct FillRect {
    Rect rect;
    float cornerRadius = 0.0f;
    Paint paint;
};

struct StrokeRect {
    Rect rect;
    float cornerRadius = 0.0f;
    float lineWidth = 1.0f;
    Paint paint;
};

struct FillPath {
    Path path;
    Paint paint;
};

struct StrokePath {
    Path path;
    float lineWidth = 1.0f;
    Paint paint;
};

/// One line, vertically centred in @c rect, clipped to it.
struct Text {
    std::string text;
    Rect rect;
    float fontSize = 12.0f;
    Justification justification = Justification::Left;
    Colour colour;
};

/// Intersects the clip with @c rect until the matching Restore.
struct ClipRect {
    Rect rect;
};

struct Save {};
struct Restore {};

using Command =
    std::variant<FillRect, StrokeRect, FillPath, StrokePath, Text, ClipRect, Save, Restore>;

/// Commands in paint order over a surface of @c width by @c height, origin top left.
class DisplayList {
  public:
    DisplayList() = default;
    DisplayList(float width, float height) : width_(width), height_(height) {}

    /// Empties the list for a new frame, keeping its capacity.
    void reset(float width, float height);

    void fillRect(Rect rect, Paint paint, float cornerRadius = 0.0f);
    void strokeRect(Rect rect, Paint paint, float lineWidth, float cornerRadius = 0.0f);
    void fillPath(Path path, Paint paint);
    void strokePath(Path path, Paint paint, float lineWidth);
    void text(std::string text, Rect rect, Colour colour, float fontSize,
              Justification justification = Justification::Left);
    void clipRect(Rect rect);
    void save();
    void restore();

    float width() const {
        return width_;
    }
    float height() const {
        return height_;
    }
    const std::vector<Command>& commands() const {
        return commands_;
    }

  private:
    float width_ = 0.0f;
    float height_ = 0.0f;
    std::vector<Command> commands_;
};

struct JsonOptions {
    /// Rounds every number to this many decimals; negative writes the shortest exact text.
    int decimals = -1;
};

/// The wire form, `{"format": "magda.display-list", "version": 1, ...}`.
std::string toJson(const DisplayList& list, JsonOptions options = {});

}  // namespace magda::sdk::display
