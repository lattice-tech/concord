#include "engine/asset/import/PlyImporter.h"

#include "engine/asset/import/ply/PlyHeader.h"
#include "engine/asset/import/ply/PlyMeshBuilder.h"
#include "engine/debug/Logger.h"

#include <exception>
#include <fstream>
#include <ios>
#include <new>

namespace Concord::Asset {

bool PlyImporter::SupportsExtension(std::string_view ext) const
{
    return ext == "ply";
}

ImportedModel PlyImporter::Import(const std::string& path, ImportContext& context)
{
    (void)context;
    ImportedModel model;
    try {
        model.name = path;

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            Debug::Logger::Error("Asset", "PLY: could not open '%s'", path.c_str());
            return model;
        }

        Ply::PlyHeader header;
        if (!Ply::ParseHeader(file, path, header)) {
            return model;
        }

        if (!Ply::BuildMesh(file, header, path, model)) {
            return ImportedModel{};
        }
        return model;
    } catch (const std::bad_alloc&) {
        Debug::Logger::Error("Asset", "PLY: allocation failed while importing '%s'", path.c_str());
    } catch (const std::exception& exception) {
        Debug::Logger::Error("Asset", "PLY: import failed for '%s': %s",
                             path.c_str(), exception.what());
    } catch (...) {
        Debug::Logger::Error("Asset", "PLY: import failed for '%s' with an unknown error",
                             path.c_str());
    }
    return ImportedModel{};
}

} // namespace Concord::Asset
