#ifndef CONCORD_DAE_XMLNODE_H
#define CONCORD_DAE_XMLNODE_H

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Concord::Asset::Dae {

/**
 * One element in a parsed XML document tree.
 *
 * A minimal DOM: element name, text content, child elements and attributes.
 * Namespace prefixes are stripped during parsing so lookups use the local name
 * only (Collada's default namespace never appears as a prefix in tag names, so
 * this keeps matching simple). This is purpose-built for Collada, not a
 * general XML library — it ignores DTDs, entity declarations and processing
 * instructions, all of which Collada files never rely on.
 */
struct XmlNode {
    /** Element tag name with any namespace prefix removed. */
    std::string name;

    /** Concatenated text between this element's open and close tags, entities decoded. */
    std::string text;

    /** Child elements, in document order. */
    std::vector<XmlNode> children;

    /** Attribute name/value pairs, as written (entities decoded). */
    std::vector<std::pair<std::string, std::string>> attributes;

    /** First child named `childName`, or nullptr when there is none. */
    const XmlNode* FindChild(std::string_view childName) const noexcept
    {
        for (const XmlNode& c : children) {
            if (c.name == childName) {
                return &c;
            }
        }
        return nullptr;
    }

    /** Every child named `childName`, in document order. */
    std::vector<const XmlNode*> FindChildren(std::string_view childName) const
    {
        std::vector<const XmlNode*> out;
        for (const XmlNode& c : children) {
            if (c.name == childName) {
                out.push_back(&c);
            }
        }
        return out;
    }

    /** Attribute value by name, or `fallback` when the attribute is absent. */
    std::string Attr(std::string_view attrName, std::string_view fallback = {}) const
    {
        for (const auto& [k, v] : attributes) {
            if (k == attrName) {
                return v;
            }
        }
        return std::string(fallback);
    }
};

} // namespace Concord::Asset::Dae

#endif // CONCORD_DAE_XMLNODE_H
