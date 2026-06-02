#include "MtlConverter.h"
#include <algorithm>
#include <cmath>
#include <cctype>

// Converts a string to lowercase for material name matching.
std::string lowercase(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

// Converts an MTL specular exponent into a roughness value.
float roughnessFromNs(float ns) {
    float roughness = std::sqrt(2.0f / (ns + 2.0f));
    return std::clamp(roughness, 0.02f, 1.0f);
}

// Derives a glossy weight from an MTL specular color.
float glossyWeightFromKs(const Vec3f& ks) {
    return std::clamp(ks.max_component() * 0.35f, 0.0f, 0.35f);
}

// Applies project-specific overrides based on material names.
void applyMaterialNameOverride(Material& mat) {
    const std::string name = lowercase(mat.name);
    if (name == "canopy_glass") {
        mat.role = MaterialRole::CANOPY_GLASS;
        mat.type = MaterialType::DIFFUSE;
        mat.color = Vec3f(0.02f, 0.04f, 0.06f);
        mat.metallicBase = 0.0f;
        mat.metallic = 0.0f;
        mat.roughness = 0.015f;
        mat.ior = 1.52f;
        mat.fresnelF0 = 0.043f;
        mat.alpha = 1.0f;
        mat.glossyWeight = 0.0f;
        mat.specularBoost = 1.0f;
        mat.normalStrength = 0.0f;
        mat.texture = nullptr;
        mat.bumpTexture = nullptr;
        return;
    }

    if (name == "transparent") {
        mat.role = MaterialRole::PROPELLER_AFTERIMAGE;
        mat.type = MaterialType::DIFFUSE;
        mat.color = Vec3f(0.20f, 0.16f, 0.12f);
        mat.metallicBase = 0.0f;
        mat.metallic = 0.0f;
        mat.roughness = 0.60f;
        mat.alpha = 0.08f;
        mat.glossyWeight = 0.0f;
        mat.specularBoost = 0.4f;
        mat.normalStrength = 0.0f;
        mat.texture = nullptr;
        mat.bumpTexture = nullptr;
        return;
    }

    if (name == "metal") {
        mat.role = MaterialRole::AIRCRAFT_METAL;
        mat.type = MaterialType::METAL;
        mat.color = Vec3f(0.55f, 0.55f, 0.52f);
        mat.metallicBase = 1.0f;
        mat.metallic = 1.0f;
        mat.roughness = 0.32f;
        mat.aluminumReflectance = Vec3f(0.91f, 0.92f, 0.92f);
        mat.glossyWeight = 0.0f;
        mat.specularBoost = 1.0f;
        mat.normalStrength = 0.20f;
        return;
    }

    if (name == "01_-_default" || name == "main" || name == "main.001") {
        mat.role = MaterialRole::AIRCRAFT_BODY;
        mat.normalStrength = 0.45f;
        mat.metallicBase = 0.75f;
        mat.metallic = std::max(mat.metallic, mat.metallicBase);
        mat.roughness = std::clamp(mat.roughness, 0.28f, 0.45f);
        return;
    }

    if (name == "parts" || name == "parts.001") {
        mat.role = MaterialRole::AIRCRAFT_PARTS;
        mat.normalStrength = 0.45f;
        mat.metallicBase = 0.35f;
        mat.metallic = std::max(mat.metallic, mat.metallicBase);
        mat.roughness = std::clamp(mat.roughness, 0.35f, 0.60f);
    }
}
