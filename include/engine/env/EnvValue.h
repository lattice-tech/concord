#ifndef CONCORD_ENVVALUE_H
#define CONCORD_ENVVALUE_H

#include "Concord/CExport.h"

#include <string>

namespace Concord {

/**
 * A single global environment value read back through Concord::Env.
 *
 * The underlying value is always stored as text (exactly as written in the
 * config, minus the surrounding quotes). The `asXxx` helpers reinterpret
 * that text on demand, each falling back to a caller-supplied default when
 * the value is absent or can't be parsed as the requested type. Callers
 * typically hold it with `auto` and convert only where a concrete type is
 * needed.
 */
class CENGINE_API EnvValue {
public:
    EnvValue() = default;
    explicit EnvValue(std::string value);

    /** Raw text of the value; empty string when the variable is unset. */
    const std::string& AsString() const noexcept;

    /** True when no value is stored (unset variable, or empty text). */
    bool IsEmpty() const noexcept;

    /** Value parsed as an integer, or `fallback` if it isn't one. */
    int AsInt(int fallback = 0) const;

    /** Value parsed as a double, or `fallback` if it isn't one. */
    double AsDouble(double fallback = 0.0) const;

    /** Value parsed as a boolean (true/1/yes), or `fallback` otherwise. */
    bool AsBool(bool fallback = false) const;

private:
    std::string m_value;
};

} // namespace Concord

#endif // CONCORD_ENVVALUE_H
