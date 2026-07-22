#include "engine/asset/import/dae/DaeMaterialResolver.h"

#include "engine/asset/import/ImportPaths.h"
#include "engine/debug/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_map>

namespace Concord::Asset::Dae {

namespace {

/** Parses a space-separated `<color>` text into r/g/b/a floats (defaulting alpha to 1). */
void ParseColor(const std::string& text, float& r, float& g, float& b, float& a)
{
    r = 0.0f; g = 0.0f; b = 0.0f; a = 1.0f;
    std::istringstream ss(text);
    if (!(ss >> r >> g >> b)) {
        return;
    }
    ss >> a;
}

/** Packs four 0..1 linear floats into a 0xRRGGBBAA color (clamped, opaque alpha when omitted). */
std::uint32_t ToPackedColor(float r, float g, float b, float a)
{
    const auto clamp = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    const auto to8 = [&clamp](float v) {
        return static_cast<std::uint32_t>(clamp(v) * 255.0f + 0.5f);
    };
    return (to8(r) << 24) | (to8(g) << 16) | (to8(b) << 8) | to8(a);
}

/** Reads the single float inside a `<shininess>`/`<transparency>` element. */
float ParseScalar(const XmlNode& node)
{
    if (const XmlNode* f = node.FindChild("float")) {
        try {
            return std::stof(f->text);
        } catch (...) {
            return 0.0f;
        }
    }
    return 0.0f;
}

/**
 * Holds the newparam resolution chain for one effect profile: sampler sid ->
 * surface sid -> image id. Collada textures reference a sampler sid through
 * `<texture texture="SamplerSID">`; this resolves it to an image id, which the
 * image table then maps to a file path.
 */
class ParamTable {
public:
    /** Adds a surface newparam: sid -> image id (from <init_from>). */
    void AddSurface(const std::string& sid, const std::string& imageId)
    {
        m_surfaces[sid] = imageId;
    }

    /** Adds a sampler newparam: sid -> surface sid (from <source>). */
    void AddSampler(const std::string& sid, const std::string& surfaceSid)
    {
        m_samplers[sid] = surfaceSid;
    }

    /** Resolves a sampler sid to its image id, or empty when unresolvable. */
    std::string ResolveImage(const std::string& samplerSid) const
    {
        const auto sit = m_samplers.find(samplerSid);
        if (sit == m_samplers.end()) {
            return samplerSid; // may be a direct image id (no newparam indirection)
        }
        const auto fit = m_surfaces.find(sit->second);
        return fit != m_surfaces.end() ? fit->second : std::string{};
    }

private:
    std::unordered_map<std::string, std::string> m_surfaces;
    std::unordered_map<std::string, std::string> m_samplers;
};

/** Collects every <newparam> in `profile` into the param table. */
void CollectParams(const XmlNode& profile, ParamTable& out)
{
    for (const XmlNode* np : profile.FindChildren("newparam")) {
        const std::string sid = np->Attr("sid");
        if (sid.empty()) {
            continue;
        }
        if (const XmlNode* surface = np->FindChild("surface")) {
            if (const XmlNode* init = surface->FindChild("init_from")) {
                out.AddSurface(sid, init->text);
            }
        } else if (const XmlNode* sampler = np->FindChild("sampler2D")) {
            if (const XmlNode* src = sampler->FindChild("source")) {
                out.AddSampler(sid, src->text);
            }
        }
    }
}

/**
 * Resolves a `<texture texture="sid">` reference to a file path, using the
 * param table and the image map. Returns empty when the texture cannot be
 * resolved.
 */
std::string ResolveTextureFile(const XmlNode& textureNode,
                               const ParamTable& params,
                               const std::unordered_map<std::string, std::string>& images,
                               const std::string& dir)
{
    const std::string sid = textureNode.Attr("texture");
    if (sid.empty()) {
        return {};
    }
    const std::string imageId = params.ResolveImage(sid);
    const auto it = images.find(imageId);
    if (it == images.end() || it->second.empty()) {
        return {};
    }
    return Paths::Join(dir, it->second);
}

/**
 * Builds a Concord material from a shading element (<phong>/<lambert>/<blinn>).
 * Diffuse maps to albedo, shininess to roughness, emission to emissive. The
 * engine has no blend pass yet, so every alpha is forced opaque — a Collada
 * diffuse color with alpha=0 (common in Lightwave exports) would otherwise
 * make the surface fully transparent and invisible. Collada predates PBR, so
 * metallic stays 0 (dielectric) unless the specular color strongly suggests
 * metal (bright specular, dark diffuse).
 */
Material::MaterialDesc BuildFromShading(const XmlNode& shading,
                                        const ParamTable& params,
                                        const std::unordered_map<std::string, std::string>& images,
                                        const std::string& dir)
{
    Material::MaterialDesc desc;
    // Imported models often have inconsistent winding across exporter quirks;
    // rendering both sides avoids the "model is invisible from one angle"
    // failure mode until a proper winding-analysis pass exists.
    desc.draw.cull = CullMode::None;

    bool hasDiffuseTexture = false;
    if (const XmlNode* diff = shading.FindChild("diffuse")) {
        if (const XmlNode* color = diff->FindChild("color")) {
            float r, g, b, a;
            ParseColor(color->text, r, g, b, a);
            // Force opaque: the engine cannot blend, and many Collada exporters
            // write alpha=0 in the diffuse color even for solid surfaces.
            desc.surface.albedo = ToPackedColor(r, g, b, 1.0f);
        } else if (const XmlNode* tex = diff->FindChild("texture")) {
            desc.textures.albedo.path = ResolveTextureFile(*tex, params, images, dir);
            hasDiffuseTexture = !desc.textures.albedo.path.empty();
        }
    }

    // Specular color: estimate metallic. A surface with a bright specular and
    // dark diffuse reads as metal in a Blinn-Phong workflow; map that to a
    // non-zero metallic so the PBR path gives it a reflective highlight.
    if (const XmlNode* spec = shading.FindChild("specular")) {
        if (const XmlNode* color = spec->FindChild("color")) {
            float r, g, b, a;
            ParseColor(color->text, r, g, b, a);
            const float specLum = 0.299f * r + 0.587f * g + 0.114f * b;
            // If specular is strong (bright) relative to diffuse, treat as metal.
            const auto unpack = [](std::uint32_t packed, int channel) {
                return static_cast<float>((packed >> (24 - channel * 8)) & 0xFF) / 255.0f;
            };
            const float diffLum = 0.299f * unpack(desc.surface.albedo, 0)
                                + 0.587f * unpack(desc.surface.albedo, 1)
                                + 0.114f * unpack(desc.surface.albedo, 2);
            if (specLum > 0.5f && diffLum < 0.3f) {
                desc.surface.metallic = std::clamp(specLum, 0.0f, 1.0f);
            }
        }
    }

    if (const XmlNode* emis = shading.FindChild("emission")) {
        if (const XmlNode* color = emis->FindChild("color")) {
            float r, g, b, a;
            ParseColor(color->text, r, g, b, a);
            if (r != 0.0f || g != 0.0f || b != 0.0f) {
                desc.surface.emissive = ToPackedColor(r, g, b, 1.0f);
                desc.surface.emissiveStrength = 1.0f;
            }
        }
    }
    if (const XmlNode* shiny = shading.FindChild("shininess")) {
        const float s = std::max(ParseScalar(*shiny), 0.0f);
        // Map Blinn-Phong exponent to GGX roughness. The relationship is
        // approximately alpha = 2 / (n + 2), then roughness = sqrt(alpha).
        // This gives: s=0 -> rough (1), s=128 -> smooth (~0.12), s=512 -> mirror.
        if (s < 1.0f) {
            desc.surface.roughness = 1.0f;
        } else {
            desc.surface.roughness = std::clamp(std::sqrt(2.0f / (s + 2.0f)), 0.05f, 1.0f);
        }
    }
    // <transparency> is parsed but the alpha is forced opaque (see above).
    return desc;
}

/** Parses one <effect> into an EffectEntry, handling its <profile_COMMON>. */
EffectEntry ParseEffect(const XmlNode& effect,
                        const std::unordered_map<std::string, std::string>& images,
                        const std::string& dir)
{
    EffectEntry entry;
    entry.id = effect.Attr("id");

    const XmlNode* profile = effect.FindChild("profile_COMMON");
    if (profile == nullptr) {
        return entry;
    }

    ParamTable params;
    CollectParams(*profile, params);

    if (const XmlNode* technique = profile->FindChild("technique")) {
        // Try each shading model in order of preference; Collada files use one.
        for (const char* tag : {"phong", "blinn", "lambert"}) {
            if (const XmlNode* shading = technique->FindChild(tag)) {
                entry.desc = BuildFromShading(*shading, params, images, dir);
                break;
            }
        }
    }
    return entry;
}

} // namespace

void DaeMaterialResolver::Load(const XmlNode& root, const std::string& dir)
{
    // Collect image id -> file path from <library_images>.
    std::unordered_map<std::string, std::string> images;
    if (const XmlNode* lib = root.FindChild("library_images")) {
        for (const XmlNode* image : lib->FindChildren("image")) {
            const std::string id = image->Attr("id");
            if (const XmlNode* init = image->FindChild("init_from")) {
                images[id] = init->text;
            }
        }
    }
    // Some files place <library_images> children under <asset> siblings; also
    // scan top-level for any stray image libraries in case of nesting quirks.
    for (const XmlNode* lib : root.FindChildren("library_images")) {
        for (const XmlNode* image : lib->FindChildren("image")) {
            const std::string id = image->Attr("id");
            if (const XmlNode* init = image->FindChild("init_from")) {
                images[id] = init->text;
            }
        }
    }

    // Parse every <effect> in <library_effects>.
    for (const XmlNode* lib : root.FindChildren("library_effects")) {
        for (const XmlNode* effect : lib->FindChildren("effect")) {
            m_effects.push_back(ParseEffect(*effect, images, dir));
        }
    }

    // Parse every <material> in <library_materials> (maps material id -> effect id).
    for (const XmlNode* lib : root.FindChildren("library_materials")) {
        for (const XmlNode* mat : lib->FindChildren("material")) {
            MaterialEntry me;
            me.id = mat->Attr("id");
            std::string eff = mat->Attr("instance_effect");
            if (!eff.empty() && eff[0] == '#') {
                eff.erase(0, 1);
            }
            me.effectId = eff;
            m_materials.push_back(std::move(me));
        }
    }
}

Material::MaterialDesc DaeMaterialResolver::Resolve(std::string_view materialId) const noexcept
{
    // A material id may carry a leading '#'; strip it.
    std::string_view id = materialId;
    if (!id.empty() && id[0] == '#') {
        id.remove_prefix(1);
    }

    // Find the material entry, then its effect.
    std::string_view effectId = id; // fallback: treat the id as an effect id directly
    for (const MaterialEntry& me : m_materials) {
        if (me.id == id) {
            effectId = me.effectId;
            break;
        }
    }

    for (const EffectEntry& ee : m_effects) {
        if (ee.id == effectId) {
            return ee.desc;
        }
    }
    return {};
}

} // namespace Concord::Asset::Dae
