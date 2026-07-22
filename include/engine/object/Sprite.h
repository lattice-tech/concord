#ifndef CONCORD_SPRITE_H
#define CONCORD_SPRITE_H

#include "Concord/CExport.h"
#include "engine/object/Node.h"
#include "engine/object/SpriteDesc.h"
#include "engine/render/frame/RenderInstance.h"
#include "engine/render/mesh/MeshHandle.h"

#include <vector>

namespace Concord::Object {

/**
 * A 2D animated sprite: a flat textured quad that cycles through the cells of
 * a texture atlas at a fixed frame rate.
 *
 * This is the render side of the 2D animation path (SpriteTrack drives frame
 * indices; here they become visible geometry). Each frame is a pre-generated
 * unit quad whose UVs address one atlas cell, uploaded lazily; the node picks
 * the current frame's quad each draw, so switching frames is just choosing a
 * different mesh — no per-frame vertex churn. Drawn unlit by default in the
 * node's local XY plane (place/orient it via the transform).
 */
class CENGINE_API Sprite : public Node {
public:
    explicit Sprite(SpriteDesc desc = {});
    ~Sprite() override;

    Sprite(const Sprite&) = delete;
    Sprite& operator=(const Sprite&) = delete;

    /** The atlas frame currently shown (0-based, row-major). */
    int CurrentFrame() const noexcept { return m_frame; }

    /** Total animation frames. */
    int FrameCount() const noexcept { return m_frameCount; }

private:
    void Advance(float deltaTime);
    void CollectRender(std::vector<RenderInstance>& out) const override;
    MeshHandle EnsureFrameMesh(int frame) const;

    SpriteDesc m_desc;
    int m_frameCount = 1;
    float m_time = 0.0f;
    mutable int m_frame = 0;

    /** One lazily-uploaded quad per frame (UVs address that atlas cell). */
    mutable std::vector<MeshHandle> m_frameMeshes;
};

} // namespace Concord::Object

#endif // CONCORD_SPRITE_H
