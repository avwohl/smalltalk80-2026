// st80-2026 — Json.hpp (Windows)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Minimal, allocator-y JSON reader/writer. Scoped down to the
// shapes the launcher needs: objects, arrays, strings, numbers,
// booleans, null. No streaming, no schema, no exceptions — errors
// surface as `JsonValue::isNull()` on the returned root.
//
// We roll our own rather than pull a third-party header-only lib
// so the pure-Win32 app stays source-only with no extras.

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace st80 {

enum class JsonType {
    Null, Bool, Number, String, Array, Object
};

class JsonValue {
public:
    JsonValue() = default;
    explicit JsonValue(bool v)            : type_(JsonType::Bool),   b_(v) {}
    explicit JsonValue(double v)          : type_(JsonType::Number), n_(v) {}
    explicit JsonValue(std::string v)     : type_(JsonType::String), s_(std::move(v)) {}

    static JsonValue Array()  { JsonValue v; v.type_ = JsonType::Array;  return v; }
    static JsonValue Object() { JsonValue v; v.type_ = JsonType::Object; return v; }

    JsonType type() const { return type_; }
    bool isNull()   const { return type_ == JsonType::Null; }
    bool isBool()   const { return type_ == JsonType::Bool; }
    bool isNumber() const { return type_ == JsonType::Number; }
    bool isString() const { return type_ == JsonType::String; }
    bool isArray()  const { return type_ == JsonType::Array; }
    bool isObject() const { return type_ == JsonType::Object; }

    bool         asBool(bool d = false)                  const { return type_ == JsonType::Bool   ? b_ : d; }
    double       asNumber(double d = 0.0)                const { return type_ == JsonType::Number ? n_ : d; }
    const std::string &asString() const { static const std::string empty; return type_ == JsonType::String ? s_ : empty; }

    const std::vector<JsonValue> &asArray()  const { static const std::vector<JsonValue> empty; return type_ == JsonType::Array  ? a_ : empty; }
    const std::map<std::string, JsonValue> &asObject() const { static const std::map<std::string, JsonValue> empty; return type_ == JsonType::Object ? o_ : empty; }

    std::vector<JsonValue> &mutableArray()              { return a_; }
    std::map<std::string, JsonValue> &mutableObject()   { return o_; }

    const JsonValue &get(const std::string &key) const {
        static const JsonValue null;
        if (type_ != JsonType::Object) return null;
        auto it = o_.find(key);
        return it == o_.end() ? null : it->second;
    }

    std::string getString(const std::string &key, const std::string &d = {}) const {
        const auto &v = get(key);
        return v.isString() ? v.asString() : d;
    }

    double getNumber(const std::string &key, double d = 0.0) const {
        const auto &v = get(key);
        return v.isNumber() ? v.asNumber() : d;
    }

    // Parse `text`. On error, returns a null JsonValue.
    static JsonValue parse(const std::string &text);

    // Serialize. Pretty-prints with 2-space indent.
    std::string dump(int indent = 0) const;

private:
    JsonType type_ = JsonType::Null;
    bool                        b_ = false;
    double                      n_ = 0.0;
    std::string                 s_;
    std::vector<JsonValue>      a_;
    std::map<std::string, JsonValue> o_;
};

}  // namespace st80
