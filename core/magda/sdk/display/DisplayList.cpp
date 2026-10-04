#include "magda/sdk/display/DisplayList.hpp"

#include <cmath>
#include <cstdio>
#include <utility>

#include "magda/sdk/state/detail/Json.hpp"
#include "magda/sdk/state/detail/NumberText.hpp"

namespace magda::sdk::display {

namespace {

constexpr std::array<std::string_view, kNumColourRoles> kRoleNames{
    "background", "surface",  "border",   "text",      "textDim",
    "accent",     "meterLow", "meterMid", "meterHigh", "meterClip",
};

constexpr std::array<std::uint32_t, kNumColourRoles> kDefaultPalette{
    0xFF1E1E1E,  // background
    0xFF2A2A2A,  // surface
    0xFF444444,  // border
    0xFFE0E0E0,  // text
    0xFF909090,  // textDim
    0xFF5B9BD5,  // accent
    0xFF55AA55,  // meterLow
    0xFFAAAA55,  // meterMid
    0xFFAA5555,  // meterHigh
    0xFFFF3B3B,  // meterClip
};

std::pair<char, int> verbText(Path::Verb verb) {
    switch (verb) {
        case Path::Verb::Move:
            return {'M', 2};
        case Path::Verb::Line:
            return {'L', 2};
        case Path::Verb::Quad:
            return {'Q', 4};
        case Path::Verb::Cubic:
            return {'C', 6};
        case Path::Verb::Close:
            break;
    }
    return {'Z', 0};
}

class Writer {
  public:
    explicit Writer(JsonOptions options) : options_(options) {}

    void number(float value) {
        if (!std::isfinite(value))
            value = 0.0f;
        if (options_.decimals < 0) {
            out += detail::writeFloat(value);
            return;
        }
        const double scale = std::pow(10.0, options_.decimals);
        double rounded = std::round(static_cast<double>(value) * scale) / scale;
        if (rounded == 0.0)
            rounded = 0.0;  // no "-0.0"
        out += detail::writeDouble(rounded);
    }

    void numbers(std::initializer_list<float> values) {
        out += '[';
        bool first = true;
        for (const float value : values) {
            if (!first)
                out += ',';
            first = false;
            number(value);
        }
        out += ']';
    }

    void rect(const Rect& r) {
        numbers({r.x, r.y, r.width, r.height});
    }

    void colour(const Colour& c) {
        out += '{';
        if (c.role) {
            out += "\"role\":\"";
            out += colourRoleName(*c.role);
            out += '"';
        } else {
            char hex[16];
            std::snprintf(hex, sizeof(hex), "\"#%08X\"", static_cast<unsigned>(c.argb));
            out += "\"argb\":";
            out += hex;
        }
        if (c.alpha) {
            out += ",\"alpha\":";
            number(*c.alpha);
        }
        out += '}';
    }

    void paint(const Paint& p) {
        if (const auto* solid = std::get_if<Colour>(&p)) {
            out += "{\"colour\":";
            colour(*solid);
            out += '}';
            return;
        }
        const auto& gradient = std::get<LinearGradient>(p);
        out += "{\"linear\":{\"from\":";
        numbers({gradient.from.x, gradient.from.y});
        out += ",\"to\":";
        numbers({gradient.to.x, gradient.to.y});
        out += ",\"stops\":[";
        for (std::size_t i = 0; i < gradient.stops.size(); ++i) {
            if (i > 0)
                out += ',';
            out += '[';
            number(gradient.stops[i].position);
            out += ',';
            colour(gradient.stops[i].colour);
            out += ']';
        }
        out += "]}}";
    }

    void path(const Path& p) {
        out += '[';
        bool first = true;
        for (const auto& element : p.elements()) {
            if (!first)
                out += ',';
            first = false;
            const auto [letter, count] = verbText(element.verb);
            out += "[\"";
            out += letter;
            out += '"';
            for (int i = 0; i < count; ++i) {
                out += ',';
                number(element.points[static_cast<std::size_t>(i)]);
            }
            out += ']';
        }
        out += ']';
    }

    void command(const Command& c) {
        std::visit([this](const auto& cmd) { write(cmd); }, c);
    }

    std::string out;

  private:
    void write(const FillRect& c) {
        out += "{\"op\":\"fillRect\",\"rect\":";
        rect(c.rect);
        radius(c.cornerRadius);
        out += ",\"paint\":";
        paint(c.paint);
        out += '}';
    }
    void write(const StrokeRect& c) {
        out += "{\"op\":\"strokeRect\",\"rect\":";
        rect(c.rect);
        radius(c.cornerRadius);
        out += ",\"lineWidth\":";
        number(c.lineWidth);
        out += ",\"paint\":";
        paint(c.paint);
        out += '}';
    }
    void write(const FillPath& c) {
        out += "{\"op\":\"fillPath\",\"path\":";
        path(c.path);
        out += ",\"paint\":";
        paint(c.paint);
        out += '}';
    }
    void write(const StrokePath& c) {
        out += "{\"op\":\"strokePath\",\"path\":";
        path(c.path);
        out += ",\"lineWidth\":";
        number(c.lineWidth);
        out += ",\"paint\":";
        paint(c.paint);
        out += '}';
    }
    void write(const Text& c) {
        out += "{\"op\":\"text\",\"text\":";
        if (!detail::appendJsonString(out, c.text))
            out += "\"\"";
        out += ",\"rect\":";
        rect(c.rect);
        out += ",\"fontSize\":";
        number(c.fontSize);
        out += ",\"justification\":\"";
        out += c.justification == Justification::Centre  ? "centre"
               : c.justification == Justification::Right ? "right"
                                                         : "left";
        out += "\",\"colour\":";
        colour(c.colour);
        out += '}';
    }
    void write(const ClipRect& c) {
        out += "{\"op\":\"clipRect\",\"rect\":";
        rect(c.rect);
        out += '}';
    }
    void write(const Save&) {
        out += "{\"op\":\"save\"}";
    }
    void write(const Restore&) {
        out += "{\"op\":\"restore\"}";
    }

    void radius(float cornerRadius) {
        if (cornerRadius > 0.0f) {
            out += ",\"radius\":";
            number(cornerRadius);
        }
    }

    JsonOptions options_;
};

Path& append(Path& path, std::vector<Path::Element>& elements, Path::Verb verb,
             std::initializer_list<float> points) {
    Path::Element element;
    element.verb = verb;
    std::size_t i = 0;
    for (const float value : points)
        element.points[i++] = value;
    elements.push_back(element);
    return path;
}

}  // namespace

std::string_view colourRoleName(ColourRole role) {
    return kRoleNames[static_cast<std::size_t>(role)];
}

std::optional<ColourRole> colourRoleFromName(std::string_view name) {
    for (std::size_t i = 0; i < kRoleNames.size(); ++i)
        if (kRoleNames[i] == name)
            return static_cast<ColourRole>(i);
    return std::nullopt;
}

std::uint32_t defaultColour(ColourRole role) {
    return kDefaultPalette[static_cast<std::size_t>(role)];
}

Path& Path::moveTo(float x, float y) {
    return append(*this, elements_, Verb::Move, {x, y});
}
Path& Path::lineTo(float x, float y) {
    return append(*this, elements_, Verb::Line, {x, y});
}
Path& Path::quadTo(float cx, float cy, float x, float y) {
    return append(*this, elements_, Verb::Quad, {cx, cy, x, y});
}
Path& Path::cubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y) {
    return append(*this, elements_, Verb::Cubic, {c1x, c1y, c2x, c2y, x, y});
}
Path& Path::close() {
    return append(*this, elements_, Verb::Close, {});
}

void DisplayList::reset(float width, float height) {
    width_ = width;
    height_ = height;
    commands_.clear();
}

void DisplayList::fillRect(Rect rect, Paint paint, float cornerRadius) {
    commands_.emplace_back(FillRect{rect, cornerRadius, std::move(paint)});
}

void DisplayList::strokeRect(Rect rect, Paint paint, float lineWidth, float cornerRadius) {
    commands_.emplace_back(StrokeRect{rect, cornerRadius, lineWidth, std::move(paint)});
}

void DisplayList::fillPath(Path path, Paint paint) {
    commands_.emplace_back(FillPath{std::move(path), std::move(paint)});
}

void DisplayList::strokePath(Path path, Paint paint, float lineWidth) {
    commands_.emplace_back(StrokePath{std::move(path), lineWidth, std::move(paint)});
}

void DisplayList::text(std::string text, Rect rect, Colour colour, float fontSize,
                       Justification justification) {
    commands_.emplace_back(Text{std::move(text), rect, fontSize, justification, colour});
}

void DisplayList::clipRect(Rect rect) {
    commands_.emplace_back(ClipRect{rect});
}

void DisplayList::save() {
    commands_.emplace_back(Save{});
}

void DisplayList::restore() {
    commands_.emplace_back(Restore{});
}

std::string toJson(const DisplayList& list, JsonOptions options) {
    Writer writer(options);
    auto& out = writer.out;
    out += "{\"format\":\"magda.display-list\",\"version\":1,\"width\":";
    writer.number(list.width());
    out += ",\"height\":";
    writer.number(list.height());
    out += ",\"commands\":[";
    bool first = true;
    for (const auto& command : list.commands()) {
        out += first ? "\n" : ",\n";
        first = false;
        writer.command(command);
    }
    out += "\n]}\n";
    return out;
}

}  // namespace magda::sdk::display
