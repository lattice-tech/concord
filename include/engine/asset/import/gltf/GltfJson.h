#ifndef CONCORD_GLTF_JSON_H
#define CONCORD_GLTF_JSON_H

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Concord::Asset::Gltf {

/**
 * Minimal JSON value model for the glTF JSON subset.
 *
 * Only the node types glTF actually uses are modeled; numbers are stored as
 * double and re-cast on access. Strings are held as std::string with the
 * surrounding quotes already stripped and escapes already resolved.
 */
struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string str;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    bool IsObject() const { return type == Type::Object; }
    bool IsArray() const { return type == JsonValue::Type::Array; }
    bool IsNumber() const { return type == JsonValue::Type::Number; }
    bool IsString() const { return type == JsonValue::Type::String; }

    const JsonValue* Find(std::string_view key) const
    {
        if (type != Type::Object) {
            return nullptr;
        }
        for (const auto& [k, v] : object) {
            if (k == key) {
                return &v;
            }
        }
        return nullptr;
    }

    int IntOr(std::string_view key, int fallback) const
    {
        if (const JsonValue* v = Find(key); v != nullptr) {
            return v->IntegerOr(fallback);
        }
        return fallback;
    }

    int IntegerOr(int fallback) const
    {
        if (!IsNumber() || !std::isfinite(number) || std::trunc(number) != number ||
            number < static_cast<double>(std::numeric_limits<int>::min()) ||
            number > static_cast<double>(std::numeric_limits<int>::max())) {
            return fallback;
        }
        return static_cast<int>(number);
    }

    double NumOr(std::string_view key, double fallback) const
    {
        if (const JsonValue* v = Find(key); v != nullptr && v->IsNumber()) {
            return v->number;
        }
        return fallback;
    }

    std::string StrOr(std::string_view key, std::string fallback) const
    {
        if (const JsonValue* v = Find(key); v != nullptr && v->IsString()) {
            return v->str;
        }
        return fallback;
    }
};

/**
 * Recursive-descent JSON parser. Tracks position in the source text and builds
 * a JsonValue tree. Throws std::runtime_error on malformed input; the importer
 * catches that and returns an empty model rather than crashing.
 */
class JsonParser {
public:
    explicit JsonParser(std::string_view text) : m_text(text) {}

    /** Parses the whole document, returning the root value. */
    JsonValue Parse();

private:
    JsonValue ParseValue();
    JsonValue ParseObject();
    JsonValue ParseArray();
    std::string ParseString();
    JsonValue ParseBool();
    JsonValue ParseNumber();
    void SkipWs();

    std::string_view m_text;
    std::size_t m_pos = 0;
};

} // namespace Concord::Asset::Gltf

#endif // CONCORD_GLTF_JSON_H
