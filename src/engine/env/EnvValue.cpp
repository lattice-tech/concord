#include "engine/env/EnvValue.h"

#include <stdexcept>
#include <utility>

namespace Concord {

EnvValue::EnvValue(std::string value)
    : m_value(std::move(value))
{
}

const std::string& EnvValue::AsString() const noexcept
{
    return m_value;
}

bool EnvValue::IsEmpty() const noexcept
{
    return m_value.empty();
}

int EnvValue::AsInt(int fallback) const
{
    try {
        return std::stoi(m_value);
    } catch (const std::exception&) {
        return fallback;
    }
}

double EnvValue::AsDouble(double fallback) const
{
    try {
        return std::stod(m_value);
    } catch (const std::exception&) {
        return fallback;
    }
}

bool EnvValue::AsBool(bool fallback) const
{
    if (m_value == "true" || m_value == "1" || m_value == "yes") {
        return true;
    }
    if (m_value == "false" || m_value == "0" || m_value == "no") {
        return false;
    }
    return fallback;
}

} // namespace Concord
