#include "engine/scene/io/SceneIO.h"

#include "engine/debug/Logger.h"
#include "engine/object/Node.h"
#include "engine/scene/Scene.h"
#include "engine/scene/io/SceneIOCodec.h"
#include "engine/scene/io/SceneIOFormat.h"
#include "engine/scene/io/SceneIOFile.h"
#include "engine/scene/io/SceneIOPayload.h"

#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Concord {

bool SceneIO::Save(const Scene& scene, const std::string& path)
{
    try {
        Detail::SceneIo::SaveSnapshot snapshot;
        std::vector<std::uint8_t> bytes;
        {
            std::lock_guard<std::recursive_mutex> lock(scene.m_graphState->mutex);
            snapshot.environment = scene.m_environmentSettings.sky;
            Object::Node* active = scene.ResolveLiveLocked(scene.m_activeCamera);
            if (scene.m_nodes.size() > Detail::SceneIo::kMaxSceneNodes) {
                throw std::length_error("scene exceeds the node budget");
            }
            std::unordered_set<const Object::Node*> serializable;
            for (const std::unique_ptr<Object::Node>& owner : scene.m_nodes) {
                Detail::SceneIo::NodeKind kind;
                if (scene.ResolveLiveLocked(owner->Handle()) != nullptr
                    && Detail::SceneIo::GetNodeKind(*owner, kind)) {
                    serializable.insert(owner.get());
                }
            }
            // v7 stores parent links, so a node is only savable when its whole
            // ancestor chain is savable too: re-rooting it would silently move
            // the subtree, since its transform is parent-relative. Nodes under a
            // node type the format cannot express (Character, SkinnedModel, ...)
            // are therefore dropped with a diagnostic rather than failing the
            // whole file — one unsupported node must not make a scene unsavable.
            const auto isSavable = [&serializable](const Object::Node* node) {
                for (const Object::Node* walk = node; walk != nullptr; walk = walk->Parent()) {
                    if (!serializable.contains(walk)) return false;
                }
                return true;
            };
            snapshot.nodes.reserve(serializable.size());
            std::size_t skipped = 0;
            for (const std::unique_ptr<Object::Node>& owner : scene.m_nodes) {
                if (!serializable.contains(owner.get())) continue;
                if (!isSavable(owner.get())) {
                    ++skipped;
                    continue;
                }
                const Object::Node* parent = owner->Parent();
                snapshot.nodes.push_back({owner.get(), owner->PersistentId(),
                    parent != nullptr ? parent->PersistentId() : Object::PersistentObjectId{}});
            }
            if (skipped != 0) {
                Debug::Logger::Warn(
                    "Scene",
                    "cscene save: skipped %zu node(s) parented under an unsupported node",
                    skipped);
            }
            if (active != nullptr && isSavable(active)) {
                snapshot.activeCameraId = active->PersistentId();
            } else if (active != nullptr) {
                Debug::Logger::Warn("Scene",
                                    "cscene save: active camera is not serializable, "
                                    "saving without an active camera reference");
            }
            bytes = Detail::SceneIo::EncodeV7(snapshot);
        }
        if (!Detail::SceneIo::WriteSceneFileAtomic(path, bytes)) {
            Debug::Logger::Error("Scene", "cscene save: write failed for '%s'", path.c_str());
            return false;
        }
        Debug::Logger::Info("Scene", "saved '%s' (%zu bytes, CSCN v7)",
                            path.c_str(), bytes.size());
        return true;
    } catch (const std::exception& exception) {
        Debug::Logger::Error("Scene", "cscene save: '%s' failed (%s)",
                             path.c_str(), exception.what());
        return false;
    } catch (...) {
        Debug::Logger::Error("Scene", "cscene save: '%s' failed", path.c_str());
        return false;
    }
}

SceneLoadResult SceneIO::Load(Scene& scene, const std::string& path)
{
    try {
        std::vector<std::uint8_t> bytes;
        if (!Detail::SceneIo::ReadSceneFile(path, bytes)) {
            Debug::Logger::Error("Scene", "cscene load: cannot read '%s'", path.c_str());
            return {};
        }
        Detail::SceneLoadBatch batch;
        std::uint32_t version = 0;
        if (!Detail::SceneIo::Decode(bytes, batch, version)) {
            Debug::Logger::Error("Scene", "cscene load: invalid CSCN in '%s'", path.c_str());
            return {};
        }

        SceneLoadResult result;
        result.nodes.reserve(batch.nodes.size());
        result.handles.reserve(batch.nodes.size());
        for (const std::unique_ptr<Object::Node>& node : batch.nodes) {
            result.nodes.push_back(node.get());
        }
        scene.CommitLoadedNodes(std::move(batch));
        for (Object::Node* node : result.nodes) result.handles.push_back(node->Handle());
        result.ok = true;
        Debug::Logger::Info("Scene", "loaded '%s' (%zu objects, CSCN v%u)",
                            path.c_str(), result.nodes.size(), version);
        return result;
    } catch (const std::exception& exception) {
        Debug::Logger::Error("Scene", "cscene load: '%s' failed (%s)",
                             path.c_str(), exception.what());
        return {};
    } catch (...) {
        Debug::Logger::Error("Scene", "cscene load: '%s' failed", path.c_str());
        return {};
    }
}

} // namespace Concord
