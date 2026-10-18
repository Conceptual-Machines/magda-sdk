#include "magda/sdk/sequencer/TimeBend.hpp"

#include <array>
#include <charconv>
#include <string>

#include "magda/sdk/curve/ModCurve.hpp"
#include "magda/sdk/state/detail/NumberText.hpp"

namespace magda::sdk::sequencer {

namespace {

constexpr std::string_view kNodeType = "TimeBend";
constexpr std::string_view kPointType = "Point";

constexpr int kLinear = static_cast<int>(CurveInterpolation::Linear);
constexpr int kHardCorner = static_cast<int>(CurveInterpolation::HardCorner);

int clampBars(int bars) {
    return bars <= 1 ? 1 : bars <= 2 ? 2 : 4;
}

bool isStraight(const CurvePointData& point) {
    return point.tension == 0.0f && point.outHandleX == 0.0f && point.outHandleY == 0.0f &&
           point.inHandleX == 0.0f && point.inHandleY == 0.0f;
}

}  // namespace

void TimeBend::setCurve(std::span<const CurvePointData> curve) {
    points = {};
    count = static_cast<int>(std::min<std::size_t>(curve.size(), kMaxTimeBendPoints));
    std::copy_n(curve.begin(), count, points.begin());
    sanitise();
}

double TimeBend::cycleBeats(double beatsPerBar) const {
    return std::max(beatsPerBar, 0.0) * clampBars(bars) / std::clamp(cycles, 1, kMaxTimeBendCycles);
}

double TimeBend::read(double phase) const {
    const auto t = std::clamp(phase, 0.0, 1.0);
    const auto y = modcurve::pointsPrecise(curve(), t);
    return t + static_cast<double>(std::clamp(amount, 0.0f, 1.0f)) * (y - t);
}

bool TimeBend::isActive() const {
    if (!on || amount <= 0.0f || count < 2)
        return false;
    const auto c = curve();
    const bool diagonal = c.size() == 2 && c[0].value == 0.0f && c[1].value == 1.0f &&
                          c[0].curveType == kLinear && isStraight(c[0]);
    return !diagonal;
}

void TimeBend::sanitise() {
    amount = std::isfinite(amount) ? std::clamp(amount, 0.0f, 1.0f) : 1.0f;
    bars = clampBars(bars);
    cycles = std::clamp(cycles, 1, kMaxTimeBendCycles);
    count = std::clamp(count, 0, kMaxTimeBendPoints);

    if (count < 2) {
        *this = TimeBend{.on = on, .amount = amount, .bars = bars, .cycles = cycles};
        return;
    }

    float previous = 0.0f;
    for (int i = 0; i < count; ++i) {
        auto& p = points[static_cast<std::size_t>(i)];
        const auto finite = [](float v) { return std::isfinite(v) ? v : 0.0f; };
        p.phase = std::clamp(finite(p.phase), previous, 1.0f);
        p.value = std::clamp(finite(p.value), 0.0f, 1.0f);
        p.tension = finite(p.tension);
        p.inHandleX = finite(p.inHandleX);
        p.inHandleY = finite(p.inHandleY);
        p.outHandleX = finite(p.outHandleX);
        p.outHandleY = finite(p.outHandleY);
        // A step would jump the head, which the crossing grid plays as a roll, not a skip.
        if (p.curveType != kHardCorner)
            p.curveType = kLinear;
        previous = p.phase;
    }
    points[0].phase = 0.0f;
    points[static_cast<std::size_t>(count - 1)].phase = 1.0f;
    std::fill(points.begin() + count, points.end(), CurvePointData{});
}

TimeBend makeTimeBend(std::span<const TimeBendKnot> knots) {
    TimeBend bend;
    bend.points = {};
    bend.count = static_cast<int>(std::min<std::size_t>(knots.size(), kMaxTimeBendPoints));
    for (int i = 0; i < bend.count; ++i) {
        const auto& knot = knots[static_cast<std::size_t>(i)];
        auto& point = bend.points[static_cast<std::size_t>(i)];
        point.phase = knot.x;
        point.value = knot.y;
        if (knot.bend == 0.0f || i + 1 >= bend.count)
            continue;
        // The shaper's control is the straight midpoint plus half the bend.
        const auto& next = knots[static_cast<std::size_t>(i + 1)];
        point.outHandleX = 0.5f * (next.x - knot.x);
        point.outHandleY = 0.5f * (next.y - knot.y) + 0.5f * knot.bend;
    }
    bend.sanitise();
    return bend;
}

TimeBend makeTimeBend(TimeBendShape shape) {
    const auto make = [](std::initializer_list<TimeBendKnot> knots) {
        return makeTimeBend(std::span<const TimeBendKnot>{knots.begin(), knots.size()});
    };
    switch (shape) {
        case TimeBendShape::Straight:
            return make({{0.0f, 0.0f}, {1.0f, 1.0f}});
        case TimeBendShape::Rush:
            return make({{0.0f, 0.0f, 0.6f}, {1.0f, 1.0f}});
        case TimeBendShape::Drag:
            return make({{0.0f, 0.0f, -0.6f}, {1.0f, 1.0f}});
        case TimeBendShape::RushThenDrag:
            return make({{0.0f, 0.0f, 0.6f}, {0.5f, 0.5f, -0.6f}, {1.0f, 1.0f}});
        case TimeBendShape::HoldThenCatchUp:
            return make({{0.0f, 0.0f}, {0.5f, 0.0f}, {1.0f, 1.0f}});
        case TimeBendShape::Backwards:
            return make({{0.0f, 1.0f}, {1.0f, 0.0f}});
        case TimeBendShape::ThereAndBack:
            return make({{0.0f, 0.0f}, {0.5f, 1.0f}, {1.0f, 0.0f}});
    }
    return {};
}

std::string_view timeBendShapeName(TimeBendShape shape) {
    switch (shape) {
        case TimeBendShape::Straight:
            return "Straight";
        case TimeBendShape::Rush:
            return "Rush";
        case TimeBendShape::Drag:
            return "Drag";
        case TimeBendShape::RushThenDrag:
            return "Rush, then drag";
        case TimeBendShape::HoldThenCatchUp:
            return "Hold, then catch up";
        case TimeBendShape::Backwards:
            return "Backwards";
        case TimeBendShape::ThereAndBack:
            return "There and back";
    }
    return {};
}

StateNode writeTimeBend(const TimeBend& bend) {
    StateNode node{std::string(kNodeType)};
    node.setBool("on", bend.on);
    node.setDouble("amount", bend.amount);
    node.setInt("bars", bend.bars);
    node.setInt("cycles", bend.cycles);
    for (const auto& p : bend.curve()) {
        StateNode point{std::string(kPointType)};
        point.setDouble("x", p.phase);
        point.setDouble("y", p.value);
        if (p.curveType != kLinear)
            point.setInt("type", p.curveType);
        if (p.tension != 0.0f)
            point.setDouble("tension", p.tension);
        if (p.outHandleX != 0.0f || p.outHandleY != 0.0f) {
            point.setDouble("outX", p.outHandleX);
            point.setDouble("outY", p.outHandleY);
        }
        if (p.inHandleX != 0.0f || p.inHandleY != 0.0f) {
            point.setDouble("inX", p.inHandleX);
            point.setDouble("inY", p.inHandleY);
        }
        node.addChild(std::move(point));
    }
    return node;
}

std::optional<TimeBend> readTimeBend(const StateNode& node) {
    if (node.type() != kNodeType)
        return std::nullopt;

    TimeBend bend;
    bend.on = node.getBool("on", false);
    bend.amount = static_cast<float>(node.getDouble("amount", 1.0));
    bend.bars = node.getInt("bars", 1);
    bend.cycles = node.getInt("cycles", 1);

    std::array<CurvePointData, kMaxTimeBendPoints> read{};
    std::size_t n = 0;
    for (const auto& child : node.children()) {
        if (child.type() != kPointType || n == read.size())
            continue;
        auto& p = read[n++];
        p.phase = static_cast<float>(child.getDouble("x"));
        p.value = static_cast<float>(child.getDouble("y"));
        p.curveType = child.getInt("type", kLinear);
        p.tension = static_cast<float>(child.getDouble("tension"));
        p.outHandleX = static_cast<float>(child.getDouble("outX"));
        p.outHandleY = static_cast<float>(child.getDouble("outY"));
        p.inHandleX = static_cast<float>(child.getDouble("inX"));
        p.inHandleY = static_cast<float>(child.getDouble("inY"));
    }

    // Fewer than two points keeps the diagonal.
    if (n >= 2)
        bend.setCurve({read.data(), n});
    else
        bend.sanitise();
    return bend;
}

namespace {

void appendNumber(std::string& out, float value) {
    out += detail::writeFloat(value);
}

bool isNumberText(std::string_view field) {
    return !field.empty() && field.find_first_not_of("0123456789+-.eE") == std::string_view::npos;
}

/// Reads one number up to @p separator, consuming it; false on anything else.
template <typename T> bool takeNumber(std::string_view& text, char separator, T& value) {
    const auto end = text.find(separator);
    const auto field = text.substr(0, end);
    if (!isNumberText(field))
        return false;
    if constexpr (std::is_floating_point_v<T>) {
        // Floating from_chars is not on every deployment target this builds for.
        const double parsed = detail::parseNumberToken(field);
        if (!std::isfinite(parsed))
            return false;
        value = static_cast<T>(parsed);
    } else {
        const auto [ptr, ec] = std::from_chars(field.data(), field.data() + field.size(), value);
        if (ec != std::errc{} || ptr != field.data() + field.size())
            return false;
    }
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    return true;
}

}  // namespace

std::string timeBendToText(const TimeBend& bend) {
    std::string out;
    out += bend.on ? "1|" : "0|";
    appendNumber(out, bend.amount);
    out += '|' + std::to_string(bend.bars) + '|' + std::to_string(bend.cycles) + '|';
    for (const auto& p : bend.curve()) {
        for (const float v : {p.phase, p.value}) {
            appendNumber(out, v);
            out += ',';
        }
        out += std::to_string(p.curveType) + ',';
        for (const float v : {p.tension, p.outHandleX, p.outHandleY, p.inHandleX}) {
            appendNumber(out, v);
            out += ',';
        }
        appendNumber(out, p.inHandleY);
        out += ';';
    }
    return out;
}

std::optional<TimeBend> timeBendFromText(std::string_view text) {
    TimeBend bend;
    int on = 0;
    if (!takeNumber(text, '|', on) || !takeNumber(text, '|', bend.amount) ||
        !takeNumber(text, '|', bend.bars) || !takeNumber(text, '|', bend.cycles))
        return std::nullopt;
    bend.on = on != 0;

    std::array<CurvePointData, kMaxTimeBendPoints> read{};
    std::size_t n = 0;
    while (!text.empty()) {
        if (n == read.size())
            return std::nullopt;
        auto& p = read[n++];
        if (!takeNumber(text, ',', p.phase) || !takeNumber(text, ',', p.value) ||
            !takeNumber(text, ',', p.curveType) || !takeNumber(text, ',', p.tension) ||
            !takeNumber(text, ',', p.outHandleX) || !takeNumber(text, ',', p.outHandleY) ||
            !takeNumber(text, ',', p.inHandleX) || !takeNumber(text, ';', p.inHandleY))
            return std::nullopt;
    }

    if (n >= 2)
        bend.setCurve({read.data(), n});
    else
        bend.sanitise();
    return bend;
}

}  // namespace magda::sdk::sequencer
