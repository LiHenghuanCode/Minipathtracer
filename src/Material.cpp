#include "Material.h"

#include "Random.h"

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

Vec3f sampleCosinePowerLobe(const Vec3f& axis, float exponent) {
    const float r1 = random_float();
    const float r2 = random_float();
    const float cosTheta = std::pow(r1, 1.0f / (exponent + 1.0f));
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const float phi = 2.0f * kPi * r2;

    const Vec3f local(
        sinTheta * std::cos(phi),
        sinTheta * std::sin(phi),
        cosTheta
    );
    return safeNormalize(toWorld(local, safeNormalize(axis)));
}

float fresnelReflectance(const Vec3f& wi, const Vec3f& normal, float ior) {
    float cosi = dot(-wi, normal);
    const bool entering = cosi > 0.0f;
    float etai = 1.0f;
    float etat = ior;
    if (!entering) {
        std::swap(etai, etat);
    }
    cosi = std::fabs(cosi);

    const float sint = etai / etat * std::sqrt(std::max(0.0f, 1.0f - cosi * cosi));
    if (sint >= 1.0f) {
        return 1.0f;
    }

    const float cost = std::sqrt(std::max(0.0f, 1.0f - sint * sint));
    const float rs = ((etat * cosi) - (etai * cost)) / ((etat * cosi) + (etai * cost));
    const float rp = ((etai * cosi) - (etat * cost)) / ((etai * cosi) + (etat * cost));
    return (rs * rs + rp * rp) * 0.5f;
}
}  // namespace

bool Material::hasEmission() const {
    return emission.x > 0.01f || emission.y > 0.01f || emission.z > 0.01f;
}

Vec3f Material::getColor(float u, float v) const {
    if (texture && texture->isLoaded()) {
        return texture->sample(u, v) * color;
    }
    return color;
}

float Material::clampedRoughness() const {
    return std::clamp(roughness, 0.02f, 1.0f);
}

float Material::clampedGlossyWeight() const {
    return std::clamp(glossyWeight, 0.0f, 0.8f);
}

float Material::clampedSpecularBoost() const {
    return std::clamp(specularBoost, 0.0f, 4.0f);
}

float Material::glossyExponent() const {
    const float r = clampedRoughness();
    return std::max(1.0f, 2.0f / (r * r) - 2.0f);
}

float Material::diffusePdf(const Vec3f& wo, const Vec3f& normal) const {
    const float cosTheta = dot(wo, normal);
    return cosTheta > 0.0f ? cosTheta / kPi : 0.0f;
}

float Material::glossyPdf(const Vec3f& wi, const Vec3f& wo, const Vec3f& normal) const {
    const Vec3f reflected = safeNormalize(reflect(wi, normal), normal);
    if (dot(reflected, normal) <= 0.0f) {
        return 0.0f;
    }

    const float cosAlpha = std::max(0.0f, dot(safeNormalize(wo), reflected));
    if (cosAlpha <= 0.0f) {
        return 0.0f;
    }

    const float exponent = glossyExponent();
    return (exponent + 1.0f) / (2.0f * kPi) * std::pow(cosAlpha, exponent);
}

Vec3f Material::sampleDiffuse(const Vec3f& wi, const Vec3f& normal) const {
    const float glossy = clampedGlossyWeight();
    if (glossy <= 0.0f || random_float() >= glossy) {
        return sampleCosineHemisphere(normal);
    }

    const Vec3f reflected = safeNormalize(reflect(wi, normal), normal);
    if (dot(reflected, normal) <= 0.0f) {
        return sampleCosineHemisphere(normal);
    }

    const Vec3f glossyDir = sampleCosinePowerLobe(reflected, glossyExponent());
    if (dot(glossyDir, normal) > 1e-4f) {
        return glossyDir;
    }
    return sampleCosineHemisphere(normal);
}

Vec3f Material::sampleMetal(const Vec3f& wi, const Vec3f& normal) const {
    const Vec3f perfectReflection = safeNormalize(reflect(wi, normal), normal);
    if (roughness <= 0.001f) {
        return perfectReflection;
    }

    const Vec3f randomDir = sampleCosineHemisphere(perfectReflection);
    Vec3f reflected = safeNormalize(perfectReflection + randomDir * roughness, perfectReflection);
    if (dot(reflected, normal) < 0.0f) {
        reflected = perfectReflection;
    }
    return reflected;
}

Vec3f Material::sampleDielectric(const Vec3f& wi, const Vec3f& normal) const {
    float cosi = dot(-wi, normal);
    const bool entering = cosi > 0.0f;
    const Vec3f orientedNormal = entering ? normal : -normal;

    float etai = 1.0f;
    float etat = ior;
    if (!entering) {
        std::swap(etai, etat);
    }
    cosi = std::fabs(cosi);

    const float reflectance = fresnelReflectance(wi, normal, ior);
    if (random_float() < reflectance) {
        return safeNormalize(reflect(wi, orientedNormal), orientedNormal);
    }

    const float eta = etai / etat;
    const float k = 1.0f - eta * eta * (1.0f - cosi * cosi);
    if (k < 0.0f) {
        return safeNormalize(reflect(wi, orientedNormal), orientedNormal);
    }

    const Vec3f refracted = eta * wi + (eta * cosi - std::sqrt(k)) * orientedNormal;
    return safeNormalize(refracted);
}

Vec3f Material::sample(const Vec3f& wi, const Vec3f& normal) const {
    switch (type) {
        case MaterialType::DIFFUSE:
            return sampleDiffuse(wi, normal);
        case MaterialType::METAL:
            return sampleMetal(wi, normal);
        case MaterialType::DIELECTRIC:
            return sampleDielectric(wi, normal);
        case MaterialType::EMISSIVE:
            return Vec3f(0.0f);
    }
    return Vec3f(0.0f);
}

float Material::pdf(const Vec3f& wi, const Vec3f& wo, const Vec3f& normal) const {
    switch (type) {
        case MaterialType::DIFFUSE: {
            const float glossy = clampedGlossyWeight();
            return (1.0f - glossy) * diffusePdf(wo, normal) + glossy * glossyPdf(wi, wo, normal);
        }
        case MaterialType::METAL:
        case MaterialType::DIELECTRIC:
            return 1.0f;
        case MaterialType::EMISSIVE:
            return 0.0f;
    }
    return 0.0f;
}

Vec3f Material::eval(const Vec3f& wi, const Vec3f& wo, const Vec3f& normal, float texU, float texV) const {
    switch (type) {
        case MaterialType::DIFFUSE: {
            if (dot(wo, normal) <= 0.0f) {
                return Vec3f(0.0f);
            }

            const Vec3f diffuseBrdf = getColor(texU, texV) / kPi;
            const float glossy = clampedGlossyWeight();
            if (glossy <= 0.0f) {
                return diffuseBrdf;
            }

            const Vec3f reflected = safeNormalize(reflect(wi, normal), normal);
            if (dot(reflected, normal) <= 0.0f) {
                return diffuseBrdf;
            }

            const float cosAlpha = std::max(0.0f, dot(safeNormalize(wo), reflected));
            if (cosAlpha <= 0.0f) {
                return diffuseBrdf;
            }

            const float exponent = glossyExponent();
            const Vec3f glossyBrdf =
                specularColor * clampedSpecularBoost() *
                ((exponent + 2.0f) / (2.0f * kPi)) *
                std::pow(cosAlpha, exponent);
            return diffuseBrdf + glossyBrdf * glossy;
        }
        case MaterialType::METAL:
            return getColor(texU, texV);
        case MaterialType::DIELECTRIC:
            return Vec3f(1.0f);
        case MaterialType::EMISSIVE:
            return Vec3f(0.0f);
    }
    return Vec3f(0.0f);
}
