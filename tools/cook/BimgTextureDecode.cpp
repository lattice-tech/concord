#include "BimgTextureDecode.h"

#include <bimg/decode.h>
#include <bx/allocator.h>

#include <cstring>

namespace Concord::Asset {

namespace {

bx::AllocatorI& DecodeAllocator()
{
    static bx::DefaultAllocator allocator;
    return allocator;
}

} // namespace

TextureDecodeFn MakeBimgTextureDecode()
{
    return [](std::span<const std::uint8_t> bytes, RgbaImage& out,
              std::string& errorOut) -> bool {
        bimg::ImageContainer* container = bimg::imageParse(
            &DecodeAllocator(), bytes.data(),
            static_cast<std::uint32_t>(bytes.size()),
            bimg::TextureFormat::RGBA8);
        if (container == nullptr) {
            errorOut = "bimg could not parse the image";
            return false;
        }
        out.width = container->m_width;
        out.height = container->m_height;
        const std::size_t size = static_cast<std::size_t>(out.width)
            * static_cast<std::size_t>(out.height) * 4u;
        out.pixels.resize(size);
        std::memcpy(out.pixels.data(), container->m_data, size);
        bimg::imageFree(container);
        return out.IsValid();
    };
}

} // namespace Concord::Asset
