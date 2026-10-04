#include "DisplayListGraphics.hpp"

namespace magda::sdk::juce_host {

namespace {

using namespace display;

juce::Rectangle<float> toJuce(const Rect& r) {
    return {r.x, r.y, r.width, r.height};
}

juce::Colour resolveColour(const Colour& colour, const ColourResolver& resolve) {
    auto resolved = colour.role ? resolve(*colour.role) : juce::Colour(colour.argb);
    if (colour.brighter != 0.0f)
        resolved = resolved.brighter(colour.brighter);
    return colour.alpha ? resolved.withAlpha(*colour.alpha) : resolved;
}

juce::PathStrokeType strokeType(const StrokePath& c) {
    const auto join = c.join == LineJoin::Round   ? juce::PathStrokeType::curved
                      : c.join == LineJoin::Bevel ? juce::PathStrokeType::beveled
                                                  : juce::PathStrokeType::mitered;
    const auto cap = c.cap == LineCap::Round    ? juce::PathStrokeType::rounded
                     : c.cap == LineCap::Square ? juce::PathStrokeType::square
                                                : juce::PathStrokeType::butt;
    return {c.lineWidth, join, cap};
}

void setPaint(juce::Graphics& g, const Paint& paint, const ColourResolver& resolve) {
    if (const auto* solid = std::get_if<Colour>(&paint)) {
        g.setColour(resolveColour(*solid, resolve));
        return;
    }
    const auto& linear = std::get<LinearGradient>(paint);
    if (linear.stops.empty()) {
        g.setColour(juce::Colours::transparentBlack);
        return;
    }
    juce::ColourGradient gradient(
        resolveColour(linear.stops.front().colour, resolve), linear.from.x, linear.from.y,
        resolveColour(linear.stops.back().colour, resolve), linear.to.x, linear.to.y, false);
    for (std::size_t i = 1; i + 1 < linear.stops.size(); ++i)
        gradient.addColour(juce::jlimit(0.0, 1.0, static_cast<double>(linear.stops[i].position)),
                           resolveColour(linear.stops[i].colour, resolve));
    g.setGradientFill(gradient);
}

juce::Path toJuce(const Path& path) {
    juce::Path out;
    for (const auto& element : path.elements()) {
        const auto& p = element.points;
        switch (element.verb) {
            case Path::Verb::Move:
                out.startNewSubPath(p[0], p[1]);
                break;
            case Path::Verb::Line:
                out.lineTo(p[0], p[1]);
                break;
            case Path::Verb::Quad:
                out.quadraticTo(p[0], p[1], p[2], p[3]);
                break;
            case Path::Verb::Cubic:
                out.cubicTo(p[0], p[1], p[2], p[3], p[4], p[5]);
                break;
            case Path::Verb::Close:
                out.closeSubPath();
                break;
        }
    }
    return out;
}

juce::Justification toJuce(Justification justification) {
    switch (justification) {
        case Justification::Centre:
            return juce::Justification::centred;
        case Justification::Right:
            return juce::Justification::centredRight;
        case Justification::Left:
            break;
    }
    return juce::Justification::centredLeft;
}

struct Interpreter {
    juce::Graphics& g;
    const ColourResolver& resolve;
    const FontResolver& font;

    void operator()(const FillRect& c) const {
        setPaint(g, c.paint, resolve);
        if (c.cornerRadius > 0.0f)
            g.fillRoundedRectangle(toJuce(c.rect), c.cornerRadius);
        else
            g.fillRect(toJuce(c.rect));
    }
    void operator()(const StrokeRect& c) const {
        setPaint(g, c.paint, resolve);
        if (c.cornerRadius > 0.0f)
            g.drawRoundedRectangle(toJuce(c.rect), c.cornerRadius, c.lineWidth);
        else
            g.drawRect(toJuce(c.rect), c.lineWidth);
    }
    void operator()(const FillPath& c) const {
        setPaint(g, c.paint, resolve);
        g.fillPath(toJuce(c.path));
    }
    void operator()(const StrokePath& c) const {
        setPaint(g, c.paint, resolve);
        g.strokePath(toJuce(c.path), strokeType(c));
    }
    void operator()(const FillEllipse& c) const {
        setPaint(g, c.paint, resolve);
        g.fillEllipse(toJuce(c.rect));
    }
    void operator()(const StrokeEllipse& c) const {
        setPaint(g, c.paint, resolve);
        g.drawEllipse(toJuce(c.rect), c.lineWidth);
    }
    void operator()(const Text& c) const {
        g.setColour(resolveColour(c.colour, resolve));
        g.setFont(font ? font(c.fontSize) : juce::Font(juce::FontOptions(c.fontSize)));
        g.drawText(juce::String::fromUTF8(c.text.c_str()), toJuce(c.rect), toJuce(c.justification),
                   false);
    }
    void operator()(const ClipRect& c) const {
        g.reduceClipRegion(toJuce(c.rect).getSmallestIntegerContainer());
    }
    void operator()(const Save&) const {
        g.saveState();
    }
    void operator()(const Restore&) const {
        g.restoreState();
    }
};

}  // namespace

juce::Colour defaultColour(display::ColourRole role) {
    return juce::Colour(display::defaultColour(role));
}

void drawDisplayList(juce::Graphics& g, const display::DisplayList& list,
                     const ColourResolver& resolve, const FontResolver& font) {
    juce::Graphics::ScopedSaveState state(g);
    const Interpreter interpreter{g, resolve, font};
    for (const auto& command : list.commands())
        std::visit(interpreter, command);
}

}  // namespace magda::sdk::juce_host
