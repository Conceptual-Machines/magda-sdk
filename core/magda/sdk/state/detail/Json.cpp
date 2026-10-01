#include "magda/sdk/state/detail/Json.hpp"

#include <cmath>
#include <limits>

#include "magda/sdk/state/detail/NumberText.hpp"

namespace magda::sdk::detail {

namespace {

class Parser {
  public:
    Parser(std::string_view text, int maxDepth) : text_(text), maxDepth_(maxDepth) {}

    std::optional<JsonValue> parse(std::string& error) {
        JsonValue value;
        skipSpace();
        if (!parseValue(value, 0) || (skipSpace(), pos_ != text_.size())) {
            if (error_.empty())
                error_ = "trailing content after the document";
            error = error_ + " at offset " + std::to_string(errorAt_);
            return std::nullopt;
        }
        return value;
    }

  private:
    bool fail(const char* message) {
        if (error_.empty()) {
            error_ = message;
            errorAt_ = pos_;
        }
        return false;
    }

    void skipSpace() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' ||
                                       text_[pos_] == '\n' || text_[pos_] == '\r'))
            ++pos_;
    }

    bool consume(std::string_view literal) {
        if (text_.substr(pos_, literal.size()) != literal)
            return false;
        pos_ += literal.size();
        return true;
    }

    bool parseValue(JsonValue& out, int depth) {
        if (pos_ >= text_.size())
            return fail("unexpected end of input");

        const auto c = text_[pos_];
        if (c == '{')
            return parseObject(out, depth);
        if (c == '[')
            return parseArray(out, depth);
        if (c == '"') {
            out.type = JsonValue::Type::String;
            return parseString(out.string);
        }
        if (c == 't') {
            out.type = JsonValue::Type::Bool;
            out.boolean = true;
            return consume("true") || fail("invalid literal");
        }
        if (c == 'f') {
            out.type = JsonValue::Type::Bool;
            out.boolean = false;
            return consume("false") || fail("invalid literal");
        }
        if (c == 'n') {
            out.type = JsonValue::Type::Null;
            return consume("null") || fail("invalid literal");
        }
        return parseNumber(out);
    }

    bool parseObject(JsonValue& out, int depth) {
        if (depth >= maxDepth_)
            return fail("nesting too deep");

        out.type = JsonValue::Type::Object;
        ++pos_;
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }

        for (;;) {
            skipSpace();
            if (pos_ >= text_.size() || text_[pos_] != '"')
                return fail("expected an object key");

            std::string key;
            if (!parseString(key))
                return false;
            if (out.member(key) != nullptr)
                return fail("duplicate object key");

            skipSpace();
            if (pos_ >= text_.size() || text_[pos_] != ':')
                return fail("expected ':'");
            ++pos_;
            skipSpace();

            JsonValue value;
            if (!parseValue(value, depth + 1))
                return false;
            out.object.emplace_back(std::move(key), std::move(value));

            skipSpace();
            if (pos_ >= text_.size())
                return fail("unterminated object");
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    bool parseArray(JsonValue& out, int depth) {
        if (depth >= maxDepth_)
            return fail("nesting too deep");

        out.type = JsonValue::Type::Array;
        ++pos_;
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }

        for (;;) {
            skipSpace();
            JsonValue value;
            if (!parseValue(value, depth + 1))
                return false;
            out.array.push_back(std::move(value));

            skipSpace();
            if (pos_ >= text_.size())
                return fail("unterminated array");
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    bool parseHex4(unsigned& value) {
        if (pos_ + 4 > text_.size())
            return fail("truncated \\u escape");
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const auto c = text_[pos_ + static_cast<std::size_t>(i)];
            unsigned digit = 0;
            if (c >= '0' && c <= '9')
                digit = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f')
                digit = static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                digit = static_cast<unsigned>(c - 'A' + 10);
            else
                return fail("invalid \\u escape");
            value = value * 16 + digit;
        }
        pos_ += 4;
        return true;
    }

    static void appendUtf8(std::string& out, unsigned codePoint) {
        if (codePoint < 0x80) {
            out.push_back(static_cast<char>(codePoint));
        } else if (codePoint < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else if (codePoint < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
    }

    bool parseString(std::string& out) {
        ++pos_;
        std::size_t rawStart = pos_;

        for (;;) {
            if (pos_ >= text_.size())
                return fail("unterminated string");

            const auto c = static_cast<unsigned char>(text_[pos_]);
            if (c == '"') {
                out.append(text_.substr(rawStart, pos_ - rawStart));
                ++pos_;
                return isValidUtf8(out) || fail("string is not valid UTF-8");
            }
            if (c < 0x20)
                return fail("control character in string");
            if (c != '\\') {
                ++pos_;
                continue;
            }

            out.append(text_.substr(rawStart, pos_ - rawStart));
            ++pos_;
            if (pos_ >= text_.size())
                return fail("unterminated string");

            const auto escape = text_[pos_++];
            switch (escape) {
                case '"':
                case '\\':
                case '/':
                    out.push_back(escape);
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u': {
                    unsigned unit = 0;
                    if (!parseHex4(unit))
                        return false;
                    if (unit >= 0xD800 && unit <= 0xDBFF) {
                        unsigned low = 0;
                        if (!consume("\\u") || !parseHex4(low) || low < 0xDC00 || low > 0xDFFF)
                            return fail("lone surrogate escape");
                        unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                    } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
                        return fail("lone surrogate escape");
                    }
                    appendUtf8(out, unit);
                    break;
                }
                default:
                    return fail("invalid escape");
            }
            rawStart = pos_;
        }
    }

    bool parseNumber(JsonValue& out) {
        const auto start = pos_;
        const auto digit = [&] {
            return pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9';
        };

        if (pos_ < text_.size() && text_[pos_] == '-')
            ++pos_;
        if (!digit())
            return fail("invalid value");
        if (text_[pos_] == '0') {
            ++pos_;
        } else {
            while (digit())
                ++pos_;
        }

        bool integral = true;
        if (pos_ < text_.size() && text_[pos_] == '.') {
            integral = false;
            ++pos_;
            if (!digit())
                return fail("invalid number");
            while (digit())
                ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            integral = false;
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-'))
                ++pos_;
            if (!digit())
                return fail("invalid number");
            while (digit())
                ++pos_;
        }

        const auto token = text_.substr(start, pos_ - start);

        if (integral) {
            const bool negative = token.front() == '-';
            const auto digits = negative ? token.substr(1) : token;
            std::uint64_t magnitude = 0;
            bool fits = true;
            for (const auto c : digits) {
                const auto d = static_cast<std::uint64_t>(c - '0');
                if (magnitude > (std::numeric_limits<std::uint64_t>::max() - d) / 10) {
                    fits = false;
                    break;
                }
                magnitude = magnitude * 10 + d;
            }

            const auto limit =
                static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) +
                (negative ? 1u : 0u);
            if (fits && magnitude <= limit) {
                out.type = JsonValue::Type::Int;
                out.integer = negative ? static_cast<std::int64_t>(0 - magnitude)
                                       : static_cast<std::int64_t>(magnitude);
                return true;
            }
        }

        const auto value = parseNumberToken(token);
        if (!std::isfinite(value)) {
            pos_ = start;
            return fail("number out of range");
        }
        out.type = JsonValue::Type::Double;
        out.real = value;
        return true;
    }

    std::string_view text_;
    int maxDepth_;
    std::size_t pos_ = 0;
    std::string error_;
    std::size_t errorAt_ = 0;
};

}  // namespace

const JsonValue* JsonValue::member(std::string_view key) const {
    for (const auto& [name, value] : object)
        if (name == key)
            return &value;
    return nullptr;
}

std::optional<JsonValue> parseJson(std::string_view text, std::string& error, int maxDepth) {
    return Parser(text, maxDepth).parse(error);
}

bool isValidUtf8(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        std::size_t extra = 0;
        unsigned codePoint = 0;
        if (c < 0x80) {
            ++i;
            continue;
        }
        if (c >= 0xC2 && c <= 0xDF) {
            extra = 1;
            codePoint = c & 0x1Fu;
        } else if (c >= 0xE0 && c <= 0xEF) {
            extra = 2;
            codePoint = c & 0x0Fu;
        } else if (c >= 0xF0 && c <= 0xF4) {
            extra = 3;
            codePoint = c & 0x07u;
        } else {
            return false;
        }

        if (i + extra >= text.size())
            return false;
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0) != 0x80)
                return false;
            codePoint = (codePoint << 6) | (next & 0x3Fu);
        }

        if ((extra == 2 && codePoint < 0x800) || (extra == 3 && codePoint < 0x10000) ||
            codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
            return false;
        i += extra + 1;
    }
    return true;
}

bool appendJsonString(std::string& out, std::string_view text) {
    if (!isValidUtf8(text))
        return false;

    out.push_back('"');
    for (const auto raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    constexpr char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[c >> 4]);
                    out.push_back(hex[c & 0xF]);
                } else {
                    out.push_back(raw);
                }
        }
    }
    out.push_back('"');
    return true;
}

namespace {

void appendBreak(std::string& out, int indent) {
    out.push_back('\n');
    out.append(static_cast<std::size_t>(indent) * 2, ' ');
}

}  // namespace

bool appendJson(std::string& out, const JsonValue& value, int indent, std::string& error) {
    const bool pretty = indent >= 0;
    switch (value.type) {
        case JsonValue::Type::Null:
            out += "null";
            return true;
        case JsonValue::Type::Bool:
            out += value.boolean ? "true" : "false";
            return true;
        case JsonValue::Type::Int:
            out += std::to_string(value.integer);
            return true;
        case JsonValue::Type::Double:
            if (!std::isfinite(value.real)) {
                error = "non-finite number";
                return false;
            }
            out += writeDouble(value.real);
            return true;
        case JsonValue::Type::String:
            if (!appendJsonString(out, value.string)) {
                error = "string is not valid UTF-8";
                return false;
            }
            return true;
        case JsonValue::Type::Array: {
            if (value.array.empty()) {
                out += "[]";
                return true;
            }
            out.push_back('[');
            bool first = true;
            for (const auto& item : value.array) {
                out += first ? "" : ",";
                first = false;
                if (pretty)
                    appendBreak(out, indent + 1);
                if (!appendJson(out, item, pretty ? indent + 1 : -1, error))
                    return false;
            }
            if (pretty)
                appendBreak(out, indent);
            out.push_back(']');
            return true;
        }
        case JsonValue::Type::Object: {
            if (value.object.empty()) {
                out += "{}";
                return true;
            }
            out.push_back('{');
            bool first = true;
            for (const auto& [name, member] : value.object) {
                out += first ? "" : ",";
                first = false;
                if (pretty)
                    appendBreak(out, indent + 1);
                if (!appendJsonString(out, name)) {
                    error = "member name is not valid UTF-8";
                    return false;
                }
                out += pretty ? ": " : ":";
                if (!appendJson(out, member, pretty ? indent + 1 : -1, error))
                    return false;
            }
            if (pretty)
                appendBreak(out, indent);
            out.push_back('}');
            return true;
        }
    }
    return false;
}

}  // namespace magda::sdk::detail
