// st80-2026 — Json.cpp
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Recursive-descent parser + naive serializer. Tolerant of the
// basics (whitespace, C++-style "//" comments inside values are
// NOT accepted — keep manifest files strictly RFC 8259).

#include "Json.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace st80 {
namespace {

class Parser {
public:
    explicit Parser(const std::string &text) : s_(text) {}

    JsonValue parseValue() {
        skipWs();
        if (eof()) { fail(); return {}; }
        char c = peek();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return JsonValue(parseString());
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') return parseNull();
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        fail();
        return {};
    }

    bool failed() const { return failed_; }

private:
    const std::string &s_;
    size_t             i_ = 0;
    bool               failed_ = false;

    bool eof() const          { return i_ >= s_.size(); }
    char peek() const         { return s_[i_]; }
    char get()                { return s_[i_++]; }
    void fail()               { failed_ = true; }

    void skipWs() {
        while (!eof()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++i_;
            else break;
        }
    }

    bool expect(char c) {
        if (eof() || get() != c) { fail(); return false; }
        return true;
    }

    JsonValue parseObject() {
        JsonValue out = JsonValue::Object();
        if (!expect('{')) return {};
        skipWs();
        if (!eof() && peek() == '}') { ++i_; return out; }
        for (;;) {
            skipWs();
            if (eof() || peek() != '"') { fail(); return {}; }
            std::string key = parseString();
            if (failed_) return {};
            skipWs();
            if (!expect(':')) return {};
            JsonValue v = parseValue();
            if (failed_) return {};
            out.mutableObject()[key] = std::move(v);
            skipWs();
            if (eof()) { fail(); return {}; }
            char c = get();
            if (c == ',') continue;
            if (c == '}') return out;
            fail();
            return {};
        }
    }

    JsonValue parseArray() {
        JsonValue out = JsonValue::Array();
        if (!expect('[')) return {};
        skipWs();
        if (!eof() && peek() == ']') { ++i_; return out; }
        for (;;) {
            JsonValue v = parseValue();
            if (failed_) return {};
            out.mutableArray().push_back(std::move(v));
            skipWs();
            if (eof()) { fail(); return {}; }
            char c = get();
            if (c == ',') continue;
            if (c == ']') return out;
            fail();
            return {};
        }
    }

    std::string parseString() {
        std::string out;
        if (!expect('"')) return {};
        while (!eof()) {
            char c = get();
            if (c == '"') return out;
            if (c == '\\') {
                if (eof()) { fail(); return {}; }
                char esc = get();
                switch (esc) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        // Minimal \uXXXX → UTF-8. No surrogate pair handling;
                        // our manifests are ASCII-only, so this is defensive.
                        if (i_ + 4 > s_.size()) { fail(); return {}; }
                        unsigned cp = 0;
                        for (int k = 0; k < 4; ++k) {
                            char h = s_[i_++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9')      cp |= (h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                            else { fail(); return {}; }
                        }
                        if (cp < 0x80) out += static_cast<char>(cp);
                        else if (cp < 0x800) {
                            out += static_cast<char>(0xC0 | (cp >> 6));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            out += static_cast<char>(0xE0 | (cp >> 12));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: fail(); return {};
                }
            } else {
                out += c;
            }
        }
        fail();
        return {};
    }

    JsonValue parseBool() {
        if (i_ + 4 <= s_.size() && s_.compare(i_, 4, "true") == 0) {
            i_ += 4;
            return JsonValue(true);
        }
        if (i_ + 5 <= s_.size() && s_.compare(i_, 5, "false") == 0) {
            i_ += 5;
            return JsonValue(false);
        }
        fail();
        return {};
    }

    JsonValue parseNull() {
        if (i_ + 4 <= s_.size() && s_.compare(i_, 4, "null") == 0) {
            i_ += 4;
            return JsonValue();
        }
        fail();
        return {};
    }

    JsonValue parseNumber() {
        size_t start = i_;
        if (!eof() && peek() == '-') ++i_;
        while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) ++i_;
        if (!eof() && peek() == '.') {
            ++i_;
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) ++i_;
        }
        if (!eof() && (peek() == 'e' || peek() == 'E')) {
            ++i_;
            if (!eof() && (peek() == '+' || peek() == '-')) ++i_;
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) ++i_;
        }
        if (start == i_) { fail(); return {}; }
        std::string s(s_, start, i_ - start);
        return JsonValue(std::strtod(s.c_str(), nullptr));
    }
};

void writeString(std::string &out, const std::string &s) {
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char esc[8];
                    std::snprintf(esc, sizeof(esc), "\\u%04x",
                                  static_cast<unsigned char>(c));
                    out += esc;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

void writeValue(std::string &out, const JsonValue &v,
                int indent, int depth) {
    auto pad = [&](int d) {
        if (indent > 0) out += '\n';
        for (int i = 0; i < indent * d; ++i) out += ' ';
    };

    switch (v.type()) {
        case JsonType::Null:   out += "null"; return;
        case JsonType::Bool:   out += v.asBool() ? "true" : "false"; return;
        case JsonType::Number: {
            char buf[64];
            double n = v.asNumber();
            // Prefer integer form if the number is whole and fits.
            if (n == static_cast<double>(static_cast<long long>(n))) {
                std::snprintf(buf, sizeof(buf), "%lld",
                              static_cast<long long>(n));
            } else {
                std::snprintf(buf, sizeof(buf), "%.17g", n);
            }
            out += buf;
            return;
        }
        case JsonType::String: writeString(out, v.asString()); return;
        case JsonType::Array: {
            const auto &arr = v.asArray();
            if (arr.empty()) { out += "[]"; return; }
            out += '[';
            for (size_t i = 0; i < arr.size(); ++i) {
                pad(depth + 1);
                writeValue(out, arr[i], indent, depth + 1);
                if (i + 1 < arr.size()) out += ',';
            }
            pad(depth);
            out += ']';
            return;
        }
        case JsonType::Object: {
            const auto &obj = v.asObject();
            if (obj.empty()) { out += "{}"; return; }
            out += '{';
            size_t i = 0;
            for (const auto &kv : obj) {
                pad(depth + 1);
                writeString(out, kv.first);
                out += ": ";
                writeValue(out, kv.second, indent, depth + 1);
                if (++i < obj.size()) out += ',';
            }
            pad(depth);
            out += '}';
            return;
        }
    }
}

}  // namespace

JsonValue JsonValue::parse(const std::string &text) {
    Parser p(text);
    JsonValue v = p.parseValue();
    if (p.failed()) return JsonValue{};
    return v;
}

std::string JsonValue::dump(int indent) const {
    std::string out;
    writeValue(out, *this, indent, 0);
    return out;
}

}  // namespace st80
