#pragma once
#include "core/Vec3.h"
#include "core/Random.h"
#include "scene/Texture.h"
#include <string>

enum class MaterialType {
    DIFFUSE,
    METAL,
    DIELECTRIC,
    EMISSIVE
};

struct Material {
    std::string name;
    MaterialType type = MaterialType::DIFFUSE;
    Vec3f color = Vec3f(0.8f);      // base color / albedo
    Vec3f emission = Vec3f(0);       // emissive color
    Vec3f specularColor = Vec3f(0.04f);
    float specularBoost = 1.0f;
    float glossyWeight = 0.0f;
    float roughness = 1.0f;          // 0 = mirror, 1 = rough
    float ior = 1.5f;                // index of refraction
    float alpha = 1.0f;
    Vec3f aluminumReflectance = Vec3f(0.91f, 0.92f, 0.92f);
    float normalStrength = -1.0f;    // < 0 uses full tangent-space normal map strength
    Texture* texture = nullptr;      // diffuse texture (not owned)
    Texture* bumpTexture = nullptr;  // tangent-space bump/normal texture (not owned)

    bool hasEmission() const;
    Vec3f getColor(float u, float v) const;
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
