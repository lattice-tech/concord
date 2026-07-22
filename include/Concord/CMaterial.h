#ifndef CONCORD_CMATERIAL_H
#define CONCORD_CMATERIAL_H

/**
 * Public entry point for the material system.
 *
 * This header only re-exports the real declarations from the engine module's
 * private headers under `engine/material/` (facade re-export pattern, see
 * AGENTS.md §3); application code includes this file and builds a
 * Material::MaterialDesc to hand to an object's SetMaterial, never reaching
 * into the `engine/material/` headers directly.
 */

#include "engine/material/DrawOptions.h"
#include "engine/material/Gradient.h"
#include "engine/material/GradientAxis.h"
#include "engine/material/MaterialDesc.h"
#include "engine/material/MaterialModel.h"
#include "engine/material/MaterialTextures.h"
#include "engine/material/Surface.h"
#include "engine/material/Texture.h"
#include "engine/render/material/CullMode.h"
#include "engine/render/material/DepthTest.h"

#endif // CONCORD_CMATERIAL_H
