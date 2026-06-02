#pragma once

#include "Vec3.h"
#include <algorithm>
#include <cmath>

// Clamps HDR radiance to a safe finite range.
inline Vec3f sanitizeRadiance(const Vec3f& v) {
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) {
        return Vec3f(0.0f);
    }
    constexpr float maxRadiance = 100.0f;
    return Vec3f(std::clamp(v.x, 0.0f, maxRadiance),
                 std::clamp(v.y, 0.0f, maxRadiance),
                 std::clamp(v.z, 0.0f, maxRadiance));
}

// Computes a clamped smoothstep value between two edges.
inline float smoothstep01(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / std::max(edge1 - edge0, 1e-6f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Normalizes a vector with a fallback for invalid lengths.
inline Vec3f safeNormalizeScene(const Vec3f& v, const Vec3f& fallback = Vec3f(0, 1, 0)) {
    const float len2 = v.length2();
    if (len2 < 1e-12f || !std::isfinite(len2)) {
        return fallback;
    }
    return v / std::sqrt(len2);
}

// Linearly blends two vectors by a scalar weight.
inline Vec3f mixVec(const Vec3f& a, const Vec3f& b, float t) {
    return a * (1.0f - t) + b * t;
}

// Reflects an incident direction around a surface normal.
inline Vec3f reflectDir(const Vec3f& incident, const Vec3f& normal) {
    return incident - 2.0f * dot(incident, normal) * normal;
}

// Refracts an incident direction through a surface with the given IOR.
inline Vec3f refractDir(const Vec3f& incident, const Vec3f& normal, float ior) {
    float cosi = std::clamp(dot(incident, normal), -1.0f, 1.0f);
    float etai = 1.0f;
    float etat = ior;
    Vec3f n = normal;

    if (cosi < 0.0f) {
        cosi = -cosi;
    } else {
        std::swap(etai, etat);
        n = -normal;
    }

    const float eta = etai / etat;
    const float k = 1.0f - eta * eta * (1.0f - cosi * cosi);
    if (k < 0.0f) {
        return Vec3f(0.0f);
    }

    return safeNormalizeScene(eta * incident + (eta * cosi - std::sqrt(k)) * n, Vec3f(0.0f));
}

// Computes exact Fresnel reflectance for a dielectric interface.
inline float fresnelExact(const Vec3f& incident, const Vec3f& normal, float ior) {
    float cosi = std::clamp(dot(incident, normal), -1.0f, 1.0f);
    float etai = 1.0f;
    float etat = ior;

    if (cosi > 0.0f) {
        std::swap(etai, etat);
    }

    const float sint = etai / etat * std::sqrt(std::max(0.0f, 1.0f - cosi * cosi));
    if (sint >= 1.0f) {
        return 1.0f;
    }

    const float cost = std::sqrt(std::max(0.0f, 1.0f - sint * sint));
    cosi = std::fabs(cosi);

    const float rs = ((etat * cosi) - (etai * cost)) / ((etat * cosi) + (etai * cost));
    const float rp = ((etai * cosi) - (etat * cost)) / ((etai * cosi) + (etat * cost));
    return (rs * rs + rp * rp) * 0.5f;
}

// Approximates Fresnel reflectance using Schlick's formula.
inline float fresnelSchlick(float cosTheta, float f0) {
    return f0 + (1.0f - f0) * std::pow(1.0f - cosTheta, 5.0f);
}
