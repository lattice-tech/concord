#ifndef CONCORD_COOKEDSCENECODEC_H
#define CONCORD_COOKEDSCENECODEC_H

#include "engine/asset/cook/CookedSceneData.h"
#include "engine/serialization/BinaryReader.h"
#include "engine/serialization/BinaryWriter.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Concord::Asset::Detail::CookedSceneCodec {

struct GraphNodeRef {
    std::uint64_t id = 0;
    std::uint64_t parentId = 0;
    const CookedNodeData* data = nullptr;
};

struct DecodedGraphNode {
    std::uint64_t id = 0;
    std::uint64_t parentId = 0;
    CookedNodeData data{};
};

struct DecodeBudget {
    std::uint32_t assetReferences = 0;
    std::uint32_t totalParticleCapacity = 0;
};

CookedNodeKind Kind(const CookedNodePayload& payload) noexcept;
bool IsFinite(const Vector3& value) noexcept;
bool IsValidTransform(const Transform& transform) noexcept;
void PutBool(Serialization::BinaryWriter& writer, bool value);
bool GetBool(Serialization::BinaryReader& reader, bool& value);
void PutF64(Serialization::BinaryWriter& writer, double value);
double GetF64(Serialization::BinaryReader& reader);
void PutAssetId(Serialization::BinaryWriter& writer, const AssetId& id);
bool GetAssetId(Serialization::BinaryReader& reader, AssetType expected,
                bool required, std::uint32_t maxKeyBytes, AssetId& id);

bool ValidateEnvironment(const EnvironmentSettings& environment) noexcept;
void WriteEnvironment(Serialization::BinaryWriter& writer,
                      const EnvironmentSettings& environment);
bool ReadEnvironment(Serialization::BinaryReader& reader,
                     EnvironmentSettings& environment);

bool ValidateLightPayload(const CookedLightPayload& light) noexcept;
bool ValidateSunLightPayload(const CookedSunLightPayload& light) noexcept;
bool ValidateCameraPayload(const CookedCameraPayload& camera) noexcept;
bool ValidateColliderPayload(const CookedColliderPayload& collider) noexcept;
void WriteLightPayload(Serialization::BinaryWriter& writer,
                       const CookedLightPayload& light);
CookedLightPayload ReadLightPayload(Serialization::BinaryReader& reader);
void WriteSunLightPayload(Serialization::BinaryWriter& writer,
                          const CookedSunLightPayload& light);
CookedSunLightPayload ReadSunLightPayload(Serialization::BinaryReader& reader);

bool ValidatePayload(const CookedNodeData& data,
                     const CookedSceneGraphLimits& limits,
                     DecodeBudget& budget, std::size_t& encodedBytes);
void WriteNodeData(Serialization::BinaryWriter& writer,
                   const CookedNodeData& data);
bool ReadNodeData(Serialization::BinaryReader& reader,
                  const CookedSceneGraphLimits& limits,
                  DecodeBudget& budget, CookedNodeData& data);

bool ValidateGraph(std::span<const GraphNodeRef> nodes,
                   std::uint64_t activeCameraId, bool requireActiveCameraKind,
                   const CookedSceneGraphLimits& limits,
                   std::size_t* encodedBytes = nullptr);
void WriteGraph(Serialization::BinaryWriter& writer,
                std::span<const GraphNodeRef> nodes);
bool ReadGraph(Serialization::BinaryReader& reader,
               std::vector<DecodedGraphNode>& nodes,
               const CookedSceneGraphLimits& limits);

bool ValidateParticle(const CookedParticlePayload& particle,
                      const CookedSceneGraphLimits& limits,
                      DecodeBudget& budget, std::size_t& encodedBytes);
void WriteParticle(Serialization::BinaryWriter& writer,
                   const CookedParticlePayload& particle);
bool ReadParticle(Serialization::BinaryReader& reader,
                  const CookedSceneGraphLimits& limits,
                  CookedParticlePayload& particle);

} // namespace Concord::Asset::Detail::CookedSceneCodec

#endif // CONCORD_COOKEDSCENECODEC_H
