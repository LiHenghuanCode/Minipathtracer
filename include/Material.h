#pragma once
#include "Vec3.h"
#include "Random.h"
#include "Texture.h"
#include <string>

enum class MaterialType {
    DIFFUSE,
    METAL,
    DIELECTRIC,
    EMISSIVE
};

enum class MaterialRole {
    DEFAULT,
    AIRCRAFT_BODY,
    AIRCRAFT_PARTS,
    CANOPY_GLASS,
    PROPELLER_AFTERIMAGE,
    AIRCRAFT_METAL
};

struct Material {
    std::string name;
    MaterialType type = MaterialType::DIFFUSE;
    MaterialRole role = MaterialRole::DEFAULT;
    Vec3f color = Vec3f(0.8f);      // base color / albedo
    Vec3f emission = Vec3f(0);       // emissive color
    float metallic = 0.0f;           // 0 = non-metal, 1 = metal
    float metallicBase = 0.0f;       // default/inferred metallic before JSON override
    bool usedJsonMetallicOverride = false;
    Vec3f specularColor = Vec3f(0.04f);
    float specularBoost = 1.0f;
    float glossyWeight = 0.0f;
    float glossyWeightBase = 0.0f;
    bool usedJsonGlossyWeightOverride = false;
    float roughness = 1.0f;          // 0 = mirror, 1 = rough
    float mtlNs = -1.0f;             // original MTL specular exponent, if present
    float roughnessFromNs = -1.0f;   // roughness fallback derived from mtlNs
    bool usedJsonRoughnessOverride = false;
    float ior = 1.5f;                // index of refraction
    float fresnelF0 = 0.04f;
    float alpha = 1.0f;
    Vec3f aluminumReflectance = Vec3f(0.91f, 0.92f, 0.92f);
    Vec3f absorptionColor = Vec3f(0.2f, 0.05f, 0.01f);
    float normalStrength = -1.0f;    // < 0 uses scene-wide normalStrength
    Texture* texture = nullptr;      // diffuse texture (not owned)
    Texture* bumpTexture = nullptr;  // tangent-space bump/normal texture (not owned)

    bool hasEmission() const;
    Vec3f getColor(float u, float v) const;
    Vec3f getTextureColor(float u, float v) const;
    Vec3f sample(const Vec3f& wi, const Vec3f& normal) const;
    float pdf(const Vec3f& wi, const Vec3f& wo, const Vec3f& normal) const;
    Vec3f eval(const Vec3f& wi, const Vec3f& wo, const Vec3f& normal,
               float texU = 0.0f, float texV = 0.0f) const;

private:
    float clampedRoughness() const;
    float clampedGlossyWeight() const;
    float clampedSpecularBoost() const;
    float glossyExponent() const;
    float diffusePdf(const Vec3f& wo, const Vec3f& normal) const;
    float glossyPdf(const Vec3f& wi, const Vec3f& wo, const Vec3f& normal) const;
    Vec3f sampleDiffuse(const Vec3f& wi, const Vec3f& normal) const;
    Vec3f sampleMetal(const Vec3f& wi, const Vec3f& normal) const;
    Vec3f sampleDielectric(const Vec3f& wi, const Vec3f& normal) const;
};
