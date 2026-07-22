#ifndef CONCORD_DAE_XMLREADER_H
#define CONCORD_DAE_XMLREADER_H

#include "engine/asset/import/dae/XmlNode.h"

#include <string_view>

namespace Concord::Asset::Dae {

/**
 * Parses XML document text into a tree of XmlNode elements.
 *
 * Returns the root element (the first real element after any prolog); an empty
 * node on malformed input or when no element is found. Designed for Collada
 * (.dae) files: it handles comments, CDATA sections, entity references,
 * numeric character references and self-closing tags, but skips processing
 * instructions and DOCTYPE declarations that Collada never uses.
 *
 * @param text The full XML document as text (a UTF-8 BOM is tolerated).
 * @return The root XmlNode, or an empty node when no element was found.
 */
XmlNode ParseXml(std::string_view text);

} // namespace Concord::Asset::Dae

#endif // CONCORD_DAE_XMLREADER_H
