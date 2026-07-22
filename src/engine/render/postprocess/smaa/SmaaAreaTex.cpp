/**
 * SPDX-License-Identifier: MIT
 * Reference AreaTex algorithm from smaa-cpp by IRIE Shinsuke (2016-2017),
 * itself based on the original SMAA AreaTex generator.
 */
#include "engine/render/postprocess/smaa/SmaaAreaTex.h"

#include "engine/render/postprocess/smaa/SmaaAreaTexGenerators.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Concord::Smaa {

namespace {

using Detail::Double2;
using Detail::DiagGenerator;
using Detail::OrthoGenerator;
using Detail::Quantize;
using Detail::kSubsamplesDiag;
using Detail::kSubsamplesOrtho;
using Detail::kTextureSize;

constexpr std::array<double, kSubsamplesOrtho> kOrthoOffsets = {
    0.0, -0.25, 0.25, -0.125, 0.125, -0.375, 0.375,
};

constexpr std::array<Double2, kSubsamplesDiag> kDiagOffsets = {
    Double2{0.0, 0.0}, Double2{0.25, -0.25}, Double2{-0.25, 0.25},
    Double2{0.125, -0.125}, Double2{-0.125, 0.125},
};

/** Copies one subsample block's ortho (and optional diag) coverage into the packed RG8 output. */
void WriteBlock(const std::vector<Double2>& ortho, const std::vector<Double2>& diag,
                bool includeDiag, int block, std::vector<std::uint8_t>& output)
{
    for (int y = 0; y < kTextureSize; ++y) {
        for (int x = 0; x < kTextureSize; ++x) {
            const Double2 value = ortho[static_cast<std::size_t>(y * kTextureSize + x)];
            const std::size_t destination = static_cast<std::size_t>(
                (((block * kTextureSize + y) * kAreaTexWidth) + x) * 2);
            output[destination] = Quantize(value.x);
            output[destination + 1] = Quantize(value.y);
        }
        if (!includeDiag) {
            continue;
        }
        for (int x = 0; x < kTextureSize; ++x) {
            const Double2 value = diag[static_cast<std::size_t>(y * kTextureSize + x)];
            const std::size_t destination = static_cast<std::size_t>(
                (((block * kTextureSize + y) * kAreaTexWidth)
                    + kTextureSize + x) * 2);
            output[destination] = Quantize(value.x);
            output[destination + 1] = Quantize(value.y);
        }
    }
}

} // namespace

void BuildAreaTex(std::vector<std::uint8_t>& outRg8)
{
    outRg8.assign(static_cast<std::size_t>(kAreaTexWidth * kAreaTexHeight * 2), 0);
    std::vector<Double2> ortho(static_cast<std::size_t>(kTextureSize * kTextureSize));
    std::vector<Double2> diag(static_cast<std::size_t>(kTextureSize * kTextureSize));
    OrthoGenerator orthoGenerator(ortho);
    DiagGenerator diagGenerator(diag);

    for (int block = 0; block < kSubsamplesOrtho; ++block) {
        orthoGenerator.Generate(kOrthoOffsets[static_cast<std::size_t>(block)]);
        const bool includeDiag = block < kSubsamplesDiag;
        if (includeDiag) {
            diagGenerator.Generate(kDiagOffsets[static_cast<std::size_t>(block)]);
        }
        WriteBlock(ortho, diag, includeDiag, block, outRg8);
    }
}

} // namespace Concord::Smaa
