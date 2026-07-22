#ifndef CONCORD_CCAMERA_H
#define CONCORD_CCAMERA_H

/**
 * Public entry point for the camera (CEngine.dll).
 *
 * This header only re-exports the real declarations from the engine module's
 * private headers (see AGENTS.md §3, facade re-export pattern); application
 * code includes this file to reach Concord::Object::Camera and its CameraDesc,
 * never the headers under `engine/`.
 */

#include "engine/object/Camera.h"
#include "engine/object/CameraDesc.h"

#endif // CONCORD_CCAMERA_H
