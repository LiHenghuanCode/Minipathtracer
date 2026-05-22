#pragma once
#include "Vec3.h"
#include "Texture.h"
#include <cmath>
#include <random>
#include <memory>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Thread-local RNG
inline float random_float() {
    static thread_local std::mt19937 gen(std::random_device{}());
    static thread_local std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(gen);
}

enum class MaterialType {
    DIFFUSE,
    METAL,
    DIELECTRIC,
    EMISSIVE
};

struct Material {
    MaterialType type = MaterialType::DIFFUSE;
    Vec3f color = Vec3f(0.8f);      // base color / albedo
    Vec3f emission = Vec3f(0);       // emissive color
    float roughness = 1.0f;          // 0 = mirror, 1 = rough
    float metallic = 0.0f;
    float ior = 1.5f;                // index of refraction
    float opacity = 1.0f;            // 1 = opaque
    Vec3f absorptionColor = Vec3f(0.2f, 0.05f, 0.01f);
    Texture* texture = nullptr;      // diffuse texture (not owned)

    bool hasEmission() const {
        return emission.x > 0.01f || emission.y > 0.01f || emission.z > 0.01f;
    }

    Vec3f getColor(float u, float v) const {
        if (texture && texture->isLoaded()) {
            return texture->sample(u, v) * color;
        }
        return color;
    }

    // Build local coordinate frame from normal
    static void buildFrame(const Vec3f& N, Vec3f& T, Vec3f& B) {
        if (std::fabs(N.x) > std::fabs(N.y)) {
            float invLen = 1.0f / std::sqrt(N.x * N.x + N.z * N.z);
            T = Vec3f(N.z * invLen, 0.0f, -N.x * invLen);
        } else {
            float invLen = 1.0f / std::sqrt(N.y * N.y + N.z * N.z);
            T = Vec3f(0.0f, N.z * invLen, -N.y * invLen);
        }
        B = cross(N, T);
    }

    static Vec3f toWorld(const Vec3f& local, const Vec3f& N) {
        Vec3f T, B;
        buildFrame(N, T, B);
        return local.x * T + local.y * B + local.z * N;
    }

    // Cosine-weighted hemisphere sample
    static Vec3f sampleCosineHemisphere(const Vec3f& N) {
        float r1 = random_float();
        float r2 = random_float();
        float r = std::sqrt(r1);
        float phi = 2.0f * (float)M_PI * r2;
        Vec3f local(r * std::cos(phi), r * std::sin(phi),
                    std::sqrt(std::max(0.0f, 1.0f - r1)));
        return toWorld(local, N);
    }

    // Sample a scattered ray direction
    Vec3f sample(const Vec3f& wi, const Vec3f& N) const {
        switch (type) {
            case MaterialType::DIFFUSE:
                return sampleCosineHemisphere(N);

            case MaterialType::METAL: {
                Vec3f reflected = reflect(wi, N);
                // Perturb by roughness
                if (roughness > 0.001f) {
                    Vec3f rand_dir = sampleCosineHemisphere(reflected.normalized());
                    reflected = (reflected.normalized() + rand_dir * roughness).normalized();
                    if (dot(reflected, N) < 0) reflected = reflect(wi, N).normalized();
                }
                return reflected.normalized();
            }

            case MaterialType::DIELECTRIC: {
                float cosi = dot(-wi, N);
                bool entering = cosi > 0;
                Vec3f n = entering ? N : -N;
                float etai = 1.0f, etat = ior;
                if (!entering) std::swap(etai, etat);
                cosi = std::fabs(cosi);

                float sint = etai / etat * std::sqrt(std::max(0.0f, 1.0f - cosi * cosi));
                float kr;
                if (sint >= 1.0f) {
                    kr = 1.0f;
                } else {
                    float cost = std::sqrt(std::max(0.0f, 1.0f - sint * sint));
                    float Rs = ((etat * cosi) - (etai * cost)) / ((etat * cosi) + (etai * cost));
                    float Rp = ((etai * cosi) - (etat * cost)) / ((etai * cosi) + (etat * cost));
                    kr = (Rs * Rs + Rp * Rp) / 2.0f;
                }

                if (random_float() < kr) {
                    // Reflect
                    return reflect(wi, n).normalized();
                } else {
                    float eta = etai / etat;
                    float k = 1.0f - eta * eta * (1.0f - cosi * cosi);
                    if (k < 0) return reflect(wi, n).normalized();
                    Vec3f refracted = eta * wi + (eta * cosi - std::sqrt(k)) * n;
                    return refracted.normalized();
                }
            }

            case MaterialType::EMISSIVE:
                return Vec3f(0); // emissive surfaces don't scatter
        }
        return Vec3f(0);
    }

    // PDF of the sampled direction
    float pdf(const Vec3f& wi, const Vec3f& wo, const Vec3f& N) const {
        switch (type) {
            case MaterialType::DIFFUSE: {
                float cosTheta = dot(wo, N);
                return cosTheta > 0 ? cosTheta / (float)M_PI : 0.0f;
            }
            case MaterialType::METAL:
                return 1.0f; // delta distribution approximation
            case MaterialType::DIELECTRIC:
                return 1.0f; // delta distribution
            case MaterialType::EMISSIVE:
                return 0.0f;
        }
        return 0.0f;
    }

    // BRDF evaluation
    Vec3f eval(const Vec3f& wi, const Vec3f& wo, const Vec3f& N,
               float texU = 0, float texV = 0) const {
        switch (type) {
            case MaterialType::DIFFUSE: {
                float cosTheta = dot(wo, N);
                if (cosTheta > 0) {
                    return getColor(texU, texV) / (float)M_PI;
                }
                return Vec3f(0);
            }
            case MaterialType::METAL: {
                return getColor(texU, texV);
            }
            case MaterialType::DIELECTRIC: {
                return Vec3f(1.0f); // perfect specular
            }
            case MaterialType::EMISSIVE:
                return Vec3f(0);
        }
        return Vec3f(0);
    }
};
