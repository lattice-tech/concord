#include "engine/scene/Scene.h"

#include "engine/environment/RenderEnvironment.h"
#include "engine/render/mesh/MeshData.h"

namespace Concord {

void Scene::SetSkyEnvironment(const SkyEnvironment& environment)
{
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    m_environmentSettings.sky = SanitizeSkyEnvironment(environment);
}

SkyEnvironment Scene::GetSkyEnvironment() const
{
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    return m_environmentSettings.sky;
}

void Scene::SetEnvironmentSettings(const EnvironmentSettings& settings)
{
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    m_environmentSettings = SanitizeEnvironmentSettings(settings);
}

EnvironmentSettings Scene::GetEnvironmentSettings() const
{
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    return m_environmentSettings;
}

MeshHandle Scene::AcquireMesh(const MeshData& data)
{
    std::shared_ptr<EngineLoop> loop;
    {
        std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
        loop = m_loop.lock();
    }
    return loop ? loop->CreateMesh(data) : MeshHandle::Invalid();
}

std::future<MeshHandle> Scene::AcquireMeshAsync(MeshData data)
{
    std::shared_ptr<EngineLoop> loop;
    {
        std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
        loop = m_loop.lock();
    }
    if (!loop) {
        std::promise<MeshHandle> promise;
        promise.set_value(MeshHandle::Invalid());
        return promise.get_future();
    }
    return loop->CreateMeshAsync(std::move(data));
}

void Scene::ReleaseMesh(MeshHandle mesh)
{
    if (!mesh.IsValid()) {
        return;
    }
    std::shared_ptr<EngineLoop> loop;
    {
        std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
        loop = m_loop.lock();
    }
    if (loop) {
        loop->DestroyMesh(mesh);
    }
}

} // namespace Concord
