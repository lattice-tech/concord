#include "engine/asset/import/ply/PlyDataReader.h"

#include "engine/asset/import/ply/PlyLimits.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>

namespace Concord::Asset::Ply {

namespace {

/** True when `name` matches any of the common aliases for a coordinate. */
bool NameIs(std::string_view name, std::initializer_list<std::string_view> aliases)
{
    for (std::string_view a : aliases) {
        if (name == a) {
            return true;
        }
    }
    return false;
}

bool IsIntegerType(PlyType type) noexcept
{
    return type != PlyType::Float && type != PlyType::Double;
}

bool ReadValue(std::istream& file, std::istringstream* asciiValues,
               PlyType type, double& value)
{
    if (asciiValues != nullptr) {
        return static_cast<bool>(*asciiValues >> value);
    }

    std::array<std::uint8_t, 8> bytes{};
    file.read(reinterpret_cast<char*>(bytes.data()), TypeSize(type));
    if (!file) {
        return false;
    }
    value = ReadScalar(bytes.data(), type);
    return true;
}

bool ConvertCount(double value, std::uint32_t limit, std::uint32_t& count) noexcept
{
    if (!std::isfinite(value) || value < 0.0 || value > static_cast<double>(limit) ||
        std::trunc(value) != value) {
        return false;
    }
    count = static_cast<std::uint32_t>(value);
    return true;
}

bool ConvertIndex(double value, std::uint32_t vertexCount, std::uint32_t& index) noexcept
{
    if (!std::isfinite(value) || value < 0.0 || value >= static_cast<double>(vertexCount) ||
        std::trunc(value) != value) {
        return false;
    }
    index = static_cast<std::uint32_t>(value);
    return true;
}

bool AppendTriangulated(const std::vector<std::uint32_t>& polygon,
                        std::vector<std::uint32_t>& indices)
{
    if (polygon.size() < 3) {
        return true;
    }
    if (indices.size() > Limits::MaxIndexCount) {
        return false;
    }
    const std::size_t triangleCount = polygon.size() - 2;
    if (triangleCount > (Limits::MaxIndexCount - indices.size()) / 3) {
        return false;
    }
    for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
        indices.push_back(polygon[0]);
        indices.push_back(polygon[i]);
        indices.push_back(polygon[i + 1]);
    }
    return true;
}

} // namespace

VertexLayout ResolveVertexLayout(const PlyElement& vertEl)
{
    VertexLayout layout;
    for (std::size_t i = 0; i < vertEl.properties.size(); ++i) {
        const std::string& n = vertEl.properties[i].name;
        if (NameIs(n, {"x", "pos.X", "X"})) layout.px = static_cast<int>(i);
        else if (NameIs(n, {"y", "pos.Y", "Y"})) layout.py = static_cast<int>(i);
        else if (NameIs(n, {"z", "pos.Z", "Z"})) layout.pz = static_cast<int>(i);
        else if (NameIs(n, {"nx", "normal.X", "normal_x"})) layout.nx = static_cast<int>(i);
        else if (NameIs(n, {"ny", "normal.Y", "normal_y"})) layout.ny = static_cast<int>(i);
        else if (NameIs(n, {"nz", "normal.Z", "normal_z"})) layout.nz = static_cast<int>(i);
        else if (NameIs(n, {"s", "u", "texture_u", "tu"})) layout.tu = static_cast<int>(i);
        else if (NameIs(n, {"t", "v", "texture_v", "tv"})) layout.tv = static_cast<int>(i);
    }
    return layout;
}

bool ReadVertices(std::istream& file, bool binary, const PlyElement& vertEl, const VertexLayout& layout,
                  std::vector<Vector3>& positions, std::vector<Vector3>& normals, std::vector<Vector2>& uvs)
{
    if (vertEl.count > Limits::MaxVertexCount ||
        vertEl.properties.size() > Limits::MaxPropertyCount) {
        return false;
    }
    for (const PlyProperty& property : vertEl.properties) {
        if (property.isList) {
            return false;
        }
    }

    const int px = layout.px, py = layout.py, pz = layout.pz;
    const int nx = layout.nx, ny = layout.ny, nz = layout.nz;
    const int tu = layout.tu, tv = layout.tv;
    const bool wantNormals = layout.HasNormals();
    const bool wantUvs = layout.HasUvs();

    positions.clear();
    normals.clear();
    uvs.clear();

    std::string line;
    std::vector<double> values(vertEl.properties.size());
    for (std::uint32_t vertex = 0; vertex < vertEl.count; ++vertex) {
        std::istringstream asciiValues;
        std::istringstream* asciiValuesPtr = nullptr;
        if (!binary) {
            if (!std::getline(file, line)) {
                return false;
            }
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            asciiValues.str(line);
            asciiValuesPtr = &asciiValues;
        }

        for (std::size_t i = 0; i < vertEl.properties.size(); ++i) {
            if (!ReadValue(file, asciiValuesPtr, vertEl.properties[i].type, values[i])) {
                return false;
            }
        }

        positions.push_back(Vector3{static_cast<float>(values[px]),
                                    static_cast<float>(values[py]),
                                    static_cast<float>(values[pz])});
        if (wantNormals) {
            normals.push_back(Vector3{static_cast<float>(values[nx]),
                                      static_cast<float>(values[ny]),
                                      static_cast<float>(values[nz])});
        }
        if (wantUvs) {
            uvs.push_back(Vector2{static_cast<float>(values[tu]),
                                  static_cast<float>(values[tv])});
        }
    }
    return true;
}

bool ReadFaces(std::istream& file, bool binary, const PlyElement& faceEl,
               std::uint32_t vertexCount, std::vector<std::uint32_t>& indices)
{
    if (faceEl.count > Limits::MaxFaceCount ||
        faceEl.properties.size() > Limits::MaxPropertyCount ||
        vertexCount > Limits::MaxVertexCount || indices.size() > Limits::MaxIndexCount) {
        return false;
    }

    const PlyProperty* listProp = nullptr;
    for (const PlyProperty& p : faceEl.properties) {
        if (p.isList) {
            listProp = &p;
            break;
        }
    }
    if (listProp == nullptr) {
        return true;
    }
    if (!IsIntegerType(listProp->type)) {
        return false;
    }

    std::string line;
    std::vector<std::uint32_t> polygon;
    for (std::uint32_t f = 0; f < faceEl.count; ++f) {
        std::istringstream asciiValues;
        std::istringstream* asciiValuesPtr = nullptr;
        if (!binary) {
            if (!std::getline(file, line)) {
                return false;
            }
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            asciiValues.str(line);
            asciiValuesPtr = &asciiValues;
        }

        polygon.clear();
        for (const PlyProperty& property : faceEl.properties) {
            if (!property.isList) {
                double ignored = 0.0;
                if (!ReadValue(file, asciiValuesPtr, property.type, ignored)) {
                    return false;
                }
                continue;
            }
            if (!IsIntegerType(property.countType)) {
                return false;
            }

            double countValue = 0.0;
            if (!ReadValue(file, asciiValuesPtr, property.countType, countValue)) {
                return false;
            }
            std::uint32_t itemCount = 0;
            if (!ConvertCount(countValue, Limits::MaxFaceListCount, itemCount)) {
                return false;
            }
            const bool collectIndices = &property == listProp;
            if (collectIndices) {
                polygon.reserve(itemCount);
            }
            for (std::uint32_t item = 0; item < itemCount; ++item) {
                double itemValue = 0.0;
                if (!ReadValue(file, asciiValuesPtr, property.type, itemValue)) {
                    return false;
                }
                if (collectIndices) {
                    std::uint32_t index = 0;
                    if (!ConvertIndex(itemValue, vertexCount, index)) {
                        return false;
                    }
                    polygon.push_back(index);
                }
            }
        }
        if (!AppendTriangulated(polygon, indices)) {
            return false;
        }
    }
    return true;
}

} // namespace Concord::Asset::Ply
