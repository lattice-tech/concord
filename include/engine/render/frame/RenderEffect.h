#ifndef CONCORD_RENDEREFFECT_H
#define CONCORD_RENDEREFFECT_H

namespace Concord {

/** Selects the shader path used to render an instance. */
enum class RenderEffect {
    Mesh,
    ParticleBillboard,
};

} // namespace Concord

#endif // CONCORD_RENDEREFFECT_H
