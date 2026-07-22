#include "engine/asset/import/gltf/GltfJson.h"

#include <stdexcept>
#include <string>

namespace Concord::Asset::Gltf {

JsonValue JsonParser::Parse()
{
    SkipWs();
    JsonValue v = ParseValue();
    SkipWs();
    return v;
}

JsonValue JsonParser::ParseValue()
{
    SkipWs();
    if (m_pos >= m_text.size()) {
        throw std::runtime_error("unexpected end of JSON");
    }
    const char c = m_text[m_pos];
    if (c == '{') return ParseObject();
    if (c == '[') return ParseArray();
    if (c == '"') { JsonValue v; v.type = JsonValue::Type::String; v.str = ParseString(); return v; }
    if (c == 't' || c == 'f') return ParseBool();
    if (c == 'n') {
        if (m_pos + 4 > m_text.size() || m_text.compare(m_pos, 4, "null") != 0) {
            throw std::runtime_error("bad JSON literal");
        }
        m_pos += 4;
        JsonValue v;
        return v;
    }
    return ParseNumber();
}

JsonValue JsonParser::ParseObject()
{
    ++m_pos; // {
    JsonValue v;
    v.type = JsonValue::Type::Object;
    SkipWs();
    if (m_pos < m_text.size() && m_text[m_pos] == '}') { ++m_pos; return v; }
    while (true) {
        SkipWs();
        std::string key = ParseString();
        SkipWs();
        if (m_pos >= m_text.size() || m_text[m_pos] != ':') {
            throw std::runtime_error("expected ':' in object");
        }
        ++m_pos; // :
        v.object.emplace_back(std::move(key), ParseValue());
        SkipWs();
        if (m_pos >= m_text.size()) {
            throw std::runtime_error("unterminated object");
        }
        if (m_text[m_pos] == ',') { ++m_pos; continue; }
        if (m_text[m_pos] == '}') { ++m_pos; break; }
        throw std::runtime_error("expected ',' or '}' in object");
    }
    return v;
}

JsonValue JsonParser::ParseArray()
{
    ++m_pos; // [
    JsonValue v;
    v.type = JsonValue::Type::Array;
    SkipWs();
    if (m_pos < m_text.size() && m_text[m_pos] == ']') { ++m_pos; return v; }
    while (true) {
        v.array.push_back(ParseValue());
        SkipWs();
        if (m_pos >= m_text.size()) {
            throw std::runtime_error("unterminated array");
        }
        if (m_text[m_pos] == ',') { ++m_pos; continue; }
        if (m_text[m_pos] == ']') { ++m_pos; break; }
        throw std::runtime_error("expected ',' or ']' in array");
    }
    return v;
}

std::string JsonParser::ParseString()
{
    if (m_pos >= m_text.size() || m_text[m_pos] != '"') {
        throw std::runtime_error("expected string");
    }
    ++m_pos;
    std::string out;
    while (m_pos < m_text.size() && m_text[m_pos] != '"') {
        const char c = m_text[m_pos++];
        if (c == '\\' && m_pos < m_text.size()) {
            const char e = m_text[m_pos++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (m_pos + 4 > m_text.size()) throw std::runtime_error("bad \\u escape");
                    const unsigned cp = std::stoul(std::string(m_text.substr(m_pos, 4)), nullptr, 16);
                    m_pos += 4;
                    if (cp < 0x80) {
                        out.push_back(static_cast<char>(cp));
                    } else if (cp < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default: out.push_back(e); break;
            }
        } else {
            out.push_back(c);
        }
    }
    if (m_pos >= m_text.size()) throw std::runtime_error("unterminated string");
    ++m_pos; // closing quote
    return out;
}

JsonValue JsonParser::ParseBool()
{
    JsonValue v;
    v.type = JsonValue::Type::Bool;
    if (m_pos + 4 <= m_text.size() && m_text.compare(m_pos, 4, "true") == 0) {
        v.boolean = true;
        m_pos += 4;
    } else if (m_pos + 5 <= m_text.size() && m_text.compare(m_pos, 5, "false") == 0) {
        v.boolean = false;
        m_pos += 5;
    } else {
        throw std::runtime_error("bad JSON boolean");
    }
    return v;
}

JsonValue JsonParser::ParseNumber()
{
    const std::size_t start = m_pos;
    while (m_pos < m_text.size()) {
        const char c = m_text[m_pos];
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') {
            ++m_pos;
        } else {
            break;
        }
    }
    JsonValue v;
    v.type = JsonValue::Type::Number;
    v.number = std::stod(std::string(m_text.substr(start, m_pos - start)));
    return v;
}

void JsonParser::SkipWs()
{
    while (m_pos < m_text.size()) {
        const char c = m_text[m_pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++m_pos;
        } else {
            break;
        }
    }
}

} // namespace Concord::Asset::Gltf
