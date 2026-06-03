#pragma once
#include "core/Vec3.h"
#include "scene/Texture.h"
#include <string>

enum class MaterialType {
    DIFFUSE,
    EMISSIVE,
    MIRROR,
    BLEND
};

struct Material {
    std::string name;
    MaterialType type = MaterialType::DIFFUSE;
    Vec3f color = Vec3f(0.8f);       // Base albedo used by diffuse shading and texture modulation.
    Vec3f emission = Vec3f(0);       // Self-emitted radiance for light-emitting surfaces.
    Vec3f mirrorColor = Vec3f(1.0f); // Tint applied to mirror-like reflections.
    float reflectionScale = 1.0f;
    float blendWeight = 0.0f;        // Reflection share in the simplified diffuse-plus-mirror blend.
    float roughness = 1.0f;
    bool useOceanNormals = false;
    Texture* texture = nullptr;
    Texture* bumpTexture = nullptr;

    bool hasEmission() const;
    Vec3f getColor(float u, float v) const;
    Vec3f getMirrorColor() const;
    Vec3f sample(const Vec3f& wi, const Vec3f& normal) const;
    float pdf(const Vec3f& wi, const Vec3f& wo, const Vec3f& normal) const;
    Vec3f eval(const Vec3f& wi, const Vec3f& wo, const Vec3f& normal,
               float texU = 0.0f, float texV = 0.0f) const;

private:
    float clampedReflectionScale() const;
    float diffusePdf(const Vec3f& wo, const Vec3f& normal) const;
    Vec3f sampleDiffuse(const Vec3f& wi, const Vec3f& normal) const;
    Vec3f sampleMirror(const Vec3f& wi, const Vec3f& normal) const;
};
