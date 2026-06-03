#include "core/Material.h"

#include "core/Random.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;

Vec3f safeNormalize(const Vec3f& v, const Vec3f& fallback = Vec3f(0, 1, 0)) {
    const float len2 = v.length2();
    if (len2 < 1e-12f || !std::isfinite(len2)) {
        return fallback;
    }
    return v / std::sqrt(len2);
}

void buildFrame(const Vec3f& normal, Vec3f& tangent, Vec3f& bitangent) {
    if (std::fabs(normal.x) > std::fabs(normal.y)) {
        const float invLen = 1.0f / std::sqrt(normal.x * normal.x + normal.z * normal.z);
        tangent = Vec3f(normal.z * invLen, 0.0f, -normal.x * invLen);
    } else {
        const float invLen = 1.0f / std::sqrt(normal.y * normal.y + normal.z * normal.z);
        tangent = Vec3f(0.0f, normal.z * invLen, -normal.y * invLen);
    }
    bitangent = cross(normal, tangent);
}

Vec3f toWorld(const Vec3f& local, const Vec3f& normal) {
    Vec3f tangent, bitangent;
    buildFrame(normal, tangent, bitangent);
    return local.x * tangent + local.y * bitangent + local.z * normal;
}

Vec3f sampleCosineHemisphere(const Vec3f& normal) {
    // Cosine-weighted hemisphere sampling matches a Lambertian BRDF.
    const float r1 = random_float();
    const float r2 = random_float();
    const float r = std::sqrt(r1);
    const float phi = 2.0f * kPi * r2;

    const Vec3f local(
        r * std::cos(phi),
        r * std::sin(phi),
        std::sqrt(std::max(0.0f, 1.0f - r1))
    );
    return toWorld(local, normal);
}
}  // namespace

bool Material::hasEmission() const {
    return emission.x > 0.01f || emission.y > 0.01f || emission.z > 0.01f;
}

Vec3f Material::getColor(float u, float v) const {
    // Multiply sampled texture color by the base albedo so textures act as modulation.
    if (texture && texture->isLoaded()) {
        return texture->sample(u, v) * color;
    }
    return color;
}

Vec3f Material::getMirrorColor() const {
    return mirrorColor * clampedReflectionScale();
}

float Material::clampedReflectionScale() const {
    return std::clamp(reflectionScale, 0.0f, 4.0f);
}

float Material::diffusePdf(const Vec3f& wo, const Vec3f& normal) const {
    const float cosTheta = dot(wo, normal);
    return cosTheta > 0.0f ? cosTheta / kPi : 0.0f;
}

Vec3f Material::sampleDiffuse(const Vec3f& wi, const Vec3f& normal) const {
    return sampleCosineHemisphere(normal);
}

Vec3f Material::sampleMirror(const Vec3f& wi, const Vec3f& normal) const {
    return safeNormalize(reflect(wi, normal), normal);
}

Vec3f Material::sample(const Vec3f& wi, const Vec3f& normal) const {
    switch (type) {
        case MaterialType::DIFFUSE:
            return sampleDiffuse(wi, normal);
        case MaterialType::EMISSIVE:
            return Vec3f(0.0f);
        case MaterialType::MIRROR:
            return sampleMirror(wi, normal);
        case MaterialType::BLEND:
            // The mixed material traces diffuse bounces and adds mirror styling elsewhere.
            return sampleDiffuse(wi, normal);
    }
    return Vec3f(0.0f);
}

float Material::pdf(const Vec3f& wi, const Vec3f& wo, const Vec3f& normal) const {
    switch (type) {
        case MaterialType::DIFFUSE:
            return diffusePdf(wo, normal);
        case MaterialType::EMISSIVE:
            return 0.0f;
        case MaterialType::MIRROR:
            return 1.0f;
        case MaterialType::BLEND:
            return diffusePdf(wo, normal);
    }
    return 0.0f;
}

Vec3f Material::eval(const Vec3f& wi, const Vec3f& wo, const Vec3f& normal, float texU, float texV) const {
    switch (type) {
        case MaterialType::DIFFUSE: {
            if (dot(wo, normal) <= 0.0f) {
                return Vec3f(0.0f);
            }
            return getColor(texU, texV) / kPi;
        }
        case MaterialType::EMISSIVE:
            return Vec3f(0.0f);
        case MaterialType::MIRROR:
            // Mirror transport is handled as a deterministic reflected ray instead of a finite BRDF value.
            return getMirrorColor();
        case MaterialType::BLEND:
            // The diffuse lobe is evaluated here; the reflective finish is injected in Scene and ArtTricks.
            return getColor(texU, texV) / kPi;
    }
    return Vec3f(0.0f);
}
