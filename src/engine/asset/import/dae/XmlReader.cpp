#include "engine/asset/import/dae/XmlReader.h"

#include <cstring>
#include <string>

namespace Concord::Asset::Dae {

namespace {

/** Strips a namespace prefix ("ns:tag" -> "tag"); Collada rarely uses prefixes. */
std::string_view LocalName(std::string_view qualified)
{
    const auto colon = qualified.find(':');
    return colon == std::string_view::npos ? qualified : qualified.substr(colon + 1);
}

/** Decodes the five predefined XML entities plus numeric character references. */
std::string DecodeEntities(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] != '&') {
            out.push_back(s[i++]);
            continue;
        }
        const auto semi = s.find(';', i + 1);
        if (semi == std::string_view::npos) {
            out.push_back(s[i++]);
            continue;
        }
        const std::string_view ent = s.substr(i + 1, semi - i - 1);
        if (ent == "lt") {
            out.push_back('<');
        } else if (ent == "gt") {
            out.push_back('>');
        } else if (ent == "amp") {
            out.push_back('&');
        } else if (ent == "quot") {
            out.push_back('"');
        } else if (ent == "apos") {
            out.push_back('\'');
        } else if (ent.size() > 1 && ent[0] == '#') {
            unsigned long code = 0;
            if (ent.size() > 2 && (ent[1] == 'x' || ent[1] == 'X')) {
                code = std::strtoul(std::string(ent.substr(2)).c_str(), nullptr, 16);
            } else {
                code = std::strtoul(std::string(ent.substr(1)).c_str(), nullptr, 10);
            }
            if (code < 0x80) {
                out.push_back(static_cast<char>(code));
            } else if (code < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
        } else {
            out.push_back('&'); // unknown entity, keep literal
        }
        i = semi + 1;
    }
    return out;
}

/**
 * Recursive-descent XML parser. Tracks a position in the source text and builds
 * an XmlNode tree one element at a time. Robust to the constructs Collada
 * files actually contain; anything it cannot recognize is skipped so a
 * partially unusual file still yields its geometry.
 */
class XmlParser {
public:
    explicit XmlParser(std::string_view text) : text_(text) {}

    XmlNode ParseDocument()
    {
        SkipBom();
        SkipWhitespace();
        while (pos_ < text_.size()) {
            if (text_[pos_] != '<') {
                ++pos_;
                continue;
            }
            if (StartsWith("<?")) {
                SkipProcessingInstruction();
            } else if (StartsWith("<!--")) {
                SkipComment();
            } else if (StartsWith("<!DOCTYPE")) {
                SkipDoctype();
            } else {
                return ParseElement();
            }
            SkipWhitespace();
        }
        return {};
    }

private:
    XmlNode ParseElement()
    {
        ++pos_; // consume '<'
        const std::string name = std::string(LocalName(ReadName()));

        XmlNode node;
        node.name = name;

        // Attributes (may be empty).
        SkipWhitespace();
        while (pos_ < text_.size() && text_[pos_] != '>' && text_[pos_] != '/') {
            const std::string attrName = std::string(LocalName(ReadName()));
            SkipWhitespace();
            if (pos_ < text_.size() && text_[pos_] == '=') {
                ++pos_;
                SkipWhitespace();
                node.attributes.emplace_back(attrName, ReadQuotedValue());
            }
            SkipWhitespace();
        }

        // Self-closing tag: <name .../>
        if (pos_ < text_.size() && text_[pos_] == '/') {
            pos_ += 2; // consume '/>'
            return node;
        }
        if (pos_ < text_.size() && text_[pos_] == '>') {
            ++pos_; // consume '>'
        }

        ParseContent(node);

        // Consume the closing </name>; ReadName skips the '/' already handled.
        // We don't verify the name matches — a mismatched close is tolerated.
        if (pos_ < text_.size() && text_[pos_] == '<' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
            pos_ += 2; // consume '</'
            ReadName(); // discard the closing tag name
            SkipWhitespace();
            if (pos_ < text_.size() && text_[pos_] == '>') {
                ++pos_;
            }
        }
        return node;
    }

    void ParseContent(XmlNode& parent)
    {
        while (pos_ < text_.size()) {
            if (text_[pos_] == '<') {
                if (StartsWith("<!--")) {
                    SkipComment();
                } else if (StartsWith("<![CDATA[")) {
                    parent.text += ReadCdata();
                } else if (StartsWith("<?")) {
                    SkipProcessingInstruction();
                } else if (StartsWith("</")) {
                    return; // end of this element's content
                } else {
                    parent.children.push_back(ParseElement());
                }
            } else {
                // Text run up to the next '<'.
                const auto start = pos_;
                while (pos_ < text_.size() && text_[pos_] != '<') {
                    ++pos_;
                }
                parent.text += DecodeEntities(text_.substr(start, pos_ - start));
            }
        }
    }

    std::string ReadName()
    {
        const auto start = pos_;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == ':' || c == '_' || c == '-' || c == '.') {
                ++pos_;
            } else {
                break;
            }
        }
        return std::string(text_.substr(start, pos_ - start));
    }

    std::string ReadQuotedValue()
    {
        if (pos_ >= text_.size() || (text_[pos_] != '"' && text_[pos_] != '\'')) {
            return {};
        }
        const char quote = text_[pos_++];
        const auto start = pos_;
        while (pos_ < text_.size() && text_[pos_] != quote) {
            ++pos_;
        }
        std::string_view raw = text_.substr(start, pos_ - start);
        if (pos_ < text_.size()) {
            ++pos_; // closing quote
        }
        return DecodeEntities(raw);
    }

    std::string ReadCdata()
    {
        pos_ += 9; // consume "<![CDATA["
        const auto start = pos_;
        while (pos_ + 2 < text_.size() &&
               !(text_[pos_] == ']' && text_[pos_ + 1] == ']' && text_[pos_ + 2] == '>')) {
            ++pos_;
        }
        std::string_view raw = text_.substr(start, pos_ - start);
        if (pos_ + 2 < text_.size()) {
            pos_ += 3; // consume "]]>"
        }
        return std::string(raw);
    }

    void SkipComment()
    {
        pos_ += 4; // consume "<!--"
        while (pos_ + 2 < text_.size() &&
               !(text_[pos_] == '-' && text_[pos_ + 1] == '-' && text_[pos_ + 2] == '>')) {
            ++pos_;
        }
        if (pos_ + 2 < text_.size()) {
            pos_ += 3;
        } else {
            pos_ = text_.size();
        }
    }

    void SkipProcessingInstruction()
    {
        pos_ += 2; // consume "<?"
        while (pos_ + 1 < text_.size() && !(text_[pos_] == '?' && text_[pos_ + 1] == '>')) {
            ++pos_;
        }
        if (pos_ + 1 < text_.size()) {
            pos_ += 2;
        } else {
            pos_ = text_.size();
        }
    }

    void SkipDoctype()
    {
        // Skip until the matching '>' — DOCTYPE may contain nested [...] blocks.
        int depth = 0;
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '[') {
                ++depth;
            } else if (c == ']') {
                --depth;
            } else if (c == '>' && depth <= 0) {
                break;
            }
        }
    }

    void SkipBom()
    {
        if (text_.size() >= 3 && static_cast<unsigned char>(text_[0]) == 0xEF &&
            static_cast<unsigned char>(text_[1]) == 0xBB && static_cast<unsigned char>(text_[2]) == 0xBF) {
            pos_ = 3;
        }
    }

    void SkipWhitespace()
    {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool StartsWith(std::string_view prefix) const noexcept
    {
        return text_.size() - pos_ >= prefix.size() && text_.substr(pos_, prefix.size()) == prefix;
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

} // namespace

XmlNode ParseXml(std::string_view text)
{
    return XmlParser(text).ParseDocument();
}

} // namespace Concord::Asset::Dae
