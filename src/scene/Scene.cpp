#include "scene/Scene.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
struct ShadingConstants {
    const Vec3f glossyAmbientTint = Vec3f(0.018f, 0.016f, 0.014f);
    const Vec3f diffuseAmbientTint = Vec3f(0.04f, 0.035f, 0.03f);
    float upperSkyFillScale = 0.025f;
    float glossySkyFillScale = 0.015f;
    float diffuseSkyFillScale = 0.025f;
    float backLightScale = 0.15f;
    float fogColorScale = 0.35f;
    float clearcoatF0 = 0.10f;
    float clearcoatStrengthScale = 0.35f;
    float clearcoatEnvReflectionScale = 1.5f;
    float finalBaseBlend = 0.35f;
    float finalMirrorBlend = 0.65f;
};

const ShadingConstants kShading;

Vec3f sanitizeRadiance(const Vec3f& v) {
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) {
        return Vec3f(0.0f);
    }
    constexpr float maxRadiance = 100.0f;
    return Vec3f(std::clamp(v.x, 0.0f, maxRadiance),
                 std::clamp(v.y, 0.0f, maxRadiance),
                 std::clamp(v.z, 0.0f, maxRadiance));
}

Vec3f safeNormalizeScene(const Vec3f& v, const Vec3f& fallback = Vec3f(0, 1, 0)) {
    const float len2 = v.length2();
    if (len2 < 1e-12f || !std::isfinite(len2)) {
        return fallback;
    }
    return v / std::sqrt(len2);
}

Vec3f applyMaterialNormalMap(const Material& mat, const Intersection& isect, const Vec3f& normal) {
    if (!mat.bumpTexture || !mat.bumpTexture->isLoaded() || !isect.hasTangent) {
        return normal;
    }

    Vec3f n = safeNormalizeScene(normal);
    Vec3f t = isect.tangent - n * dot(isect.tangent, n);
    t = safeNormalizeScene(t, Vec3f(0.0f));
    if (t.length2() < 1e-8f) {
        return n;
    }

    Vec3f b = isect.bitangent - n * dot(isect.bitangent, n) - t * dot(isect.bitangent, t);
    b = safeNormalizeScene(b, cross(n, t));
    if (b.length2() < 1e-8f) {
        return n;
    }
    if (dot(cross(t, b), n) < 0.0f) {
        b = -b;
    }

    const Vec3f sampleNormal = mat.bumpTexture->sample(isect.texU, isect.texV);
    Vec3f tangentNormal = sampleNormal * 2.0f - Vec3f(1.0f);
    const float detailStrength = 1.0f;
    tangentNormal.x *= detailStrength;
    tangentNormal.y *= detailStrength;
    tangentNormal = safeNormalizeScene(tangentNormal, Vec3f(0.0f, 0.0f, 1.0f));

    Vec3f mappedNormal = safeNormalizeScene(
        t * tangentNormal.x + b * tangentNormal.y + n * tangentNormal.z,
        n
    );

    const float requestedStrength = mat.normalStrength >= 0.0f ? mat.normalStrength : 1.0f;
    const float normalStrength = std::clamp(requestedStrength, 0.0f, 1.0f);
    return safeNormalizeScene(n * (1.0f - normalStrength) + mappedNormal * normalStrength, n);
}
}

Vec3f Scene::computeWaterNormal(const Vec3f& hitPoint, const Vec3f& geometricNormal) const {
    if (!ocean) {
        return geometricNormal;
    }

    Vec3f largeNormal = ocean->getNormal(hitPoint.x, hitPoint.z);
    Vec3f waterNormal = largeNormal;
    if (oceanRipple) {
        Vec3f rippleNormal = oceanRipple->getNormal(hitPoint.x, hitPoint.z);
        float ls = std::max(0.0f, config.water.largeWaveScale);
        float ss = std::max(0.0f, config.water.smallWaveScale);
        waterNormal = normalize(Vec3f(
            largeNormal.x * ls + rippleNormal.x * ss,
            1.0f,
            largeNormal.z * ls + rippleNormal.z * ss
        ));
    }

    float normalStrength = std::clamp(config.water.normalStrength, 0.0f, 1.0f);
    return normalize(geometricNormal * (1.0f - normalStrength) + waterNormal * normalStrength);
}

void Scene::createAreaLight() {
    auto& cfg = config.areaLightConfig;

    Vec3f dir = cfg.direction.normalized();
    Vec3f up = (std::fabs(dir.y) < 0.99f) ? Vec3f(0, 1, 0) : Vec3f(1, 0, 0);
    Vec3f right = cross(up, dir).normalized();
    up = cross(dir, right).normalized();

    areaLight.center = cfg.position;
    areaLight.normal = -dir;
    areaLight.u = right;
    areaLight.v = up;
    areaLight.halfWidth = cfg.width * 0.5f;
    areaLight.halfHeight = cfg.height * 0.5f;
    areaLight.emission = cfg.color * cfg.intensity;

    Material emitMat;
    emitMat.type = MaterialType::EMISSIVE;
    emitMat.emission = areaLight.emission;
    emitMat.color = cfg.color;
    areaLight.materialId = (int)materials.size();
    materials.push_back(emitMat);

    constexpr int segments = 48;

    for (int i = 0; i < segments; ++i) {
        float a0 = 2.0f * 3.14159265358979323846f * (float)i / (float)segments;
        float a1 = 2.0f * 3.14159265358979323846f * (float)(i + 1) / (float)segments;

        Vec3f rim0 = areaLight.center
            + right * (std::cos(a0) * areaLight.halfWidth)
            + up * (std::sin(a0) * areaLight.halfHeight);
        Vec3f rim1 = areaLight.center
            + right * (std::cos(a1) * areaLight.halfWidth)
            + up * (std::sin(a1) * areaLight.halfHeight);

        Triangle tri;
        tri.v0 = areaLight.center;
        tri.v1 = rim0;
        tri.v2 = rim1;
        tri.n0 = tri.n1 = tri.n2 = areaLight.normal;
        tri.u0 = 0.5f; tri.v0t = 0.5f;
        tri.u1 = 0.5f + 0.5f * std::cos(a0); tri.v1t = 0.5f + 0.5f * std::sin(a0);
        tri.u2 = 0.5f + 0.5f * std::cos(a1); tri.v2t = 0.5f + 0.5f * std::sin(a1);
        tri.materialId = areaLight.materialId;
        triangles.push_back(tri);
    }

    hasAreaLight = true;
    std::cout << "Area light created at (" << areaLight.center.x << ", "
              << areaLight.center.y << ", " << areaLight.center.z
              << ") size=" << cfg.width << "x" << cfg.height
              << " emission=(" << areaLight.emission.x << ", "
              << areaLight.emission.y << ", " << areaLight.emission.z << ")" << std::endl;
}

Vec3f Scene::sampleAreaLight(const Vec3f& hitPoint, const Vec3f& wi, const Vec3f& N, const Material& mat,
                             float texU, float texV) const {
    Vec3f lightPoint = areaLight.samplePoint();
    Vec3f toLight = lightPoint - hitPoint;
    float dist2 = toLight.length2();
    float dist = std::sqrt(dist2);
    Vec3f lightDir = toLight / dist;

    float lightCos = dot(-lightDir, areaLight.normal);
    if (lightCos <= 0.0f) return Vec3f(0);

    float surfaceCos = dot(lightDir, N);
    if (surfaceCos <= 0.0f) return Vec3f(0);

    Vec3f shadowOrigin = hitPoint + N * 1e-3f;
    Ray shadowRay(shadowOrigin, lightDir);
    Intersection shadowIsect = bvh.intersect(shadowRay);
    if (shadowIsect.hit && shadowIsect.t < dist - 1e-2f) {
        return Vec3f(0);
    }

    Vec3f brdf = mat.eval(wi, lightDir, N, texU, texV);
    float geometryTerm = surfaceCos * lightCos / dist2;
    return areaLight.emission * brdf * geometryTerm * areaLight.area();
}

Vec3f Scene::castRay(const Ray& ray, int depth) const {
    if (depth >= config.render.maxDepth) return Vec3f(0);

    Intersection isect = bvh.intersect(ray);

    if (!isect.hit) {
        return sky.evaluate(ray.direction);
    }

    const Material& mat = materials[isect.materialId];

    // Emissive surface
    if (mat.hasEmission()) {
        return mat.emission;
    }

    // Russian Roulette (after depth 3)
    float survivalProb = 1.0f;
    if (depth > 3) {
        survivalProb = std::min(0.95f, mat.color.max_component());
        if (random_float() > survivalProb) {
            return Vec3f(0);
        }
    }

    Vec3f hitPoint = isect.position;
    Vec3f N = isect.normal;

    if (mat.type == MaterialType::DIELECTRIC && ocean) {
        N = computeWaterNormal(hitPoint, N);
    } else {
        N = applyMaterialNormalMap(mat, isect, N);
    }

    // Ensure normal faces the ray
    if (dot(N, ray.direction) > 0 && mat.type != MaterialType::DIELECTRIC) {
        N = -N;
    }

    const bool skyEnabled = config.sky.enabled;

    // Direct lighting (NEE)
    Vec3f directLight(0);
    if (mat.type == MaterialType::DIFFUSE || mat.type == MaterialType::METAL) {
        if (hasAreaLight) {
            directLight = sampleAreaLight(hitPoint, ray.direction, N, mat, isect.texU, isect.texV);
        }
    }

    if (mat.type == MaterialType::DIELECTRIC) {
        Vec3f R = reflect(ray.direction.normalized(), N).normalized();
        Vec3f reflectionOrigin = hitPoint + N * 1e-3f;
        Vec3f mirrorReflection = sky.evaluate(R);
        if (depth + 1 < config.render.maxDepth) {
            mirrorReflection = castRay(Ray(reflectionOrigin, R), depth + 1);
        }

        Vec3f waterReflection = mirrorReflection * std::max(0.0f, config.water.reflectionStrength);
        Vec3f result = directLight + waterReflection;

        {
            float fogDensity = std::max(0.0f, config.water.fogDensity);
            float fogAmount = 1.0f - std::exp(-isect.t * fogDensity);
            Vec3f fogColor = skyEnabled ? config.sky.horizonColor * kShading.fogColorScale : Vec3f(0.0f);
            result = result * (1.0f - fogAmount) + fogColor * fogAmount;
        }

        return sanitizeRadiance(result / survivalProb);
    }

    // Indirect lighting
    Vec3f wo = mat.sample(ray.direction, N);
    if (wo.length2() < 1e-8f) {
        Vec3f r = directLight / survivalProb;
        return r;
    }

    float pdf_val = mat.pdf(ray.direction, wo, N);
    if (pdf_val < 1e-6f) {
        Vec3f r = directLight / survivalProb;
        return r;
    }

    Vec3f brdf = mat.eval(ray.direction, wo, N, isect.texU, isect.texV);

    // Offset origin to avoid self-intersection
    Vec3f offset = dot(wo, N) > 0 ? N * 1e-3f : -N * 1e-3f;
    Ray bounceRay(hitPoint + offset, wo);
    Intersection bounceIsect = bvh.intersect(bounceRay);
    Vec3f indirect = castRay(bounceRay, depth + 1);
    if (hasAreaLight && mat.type == MaterialType::DIFFUSE && bounceIsect.hit) {
        if (bounceIsect.materialId == areaLight.materialId) {
            indirect = Vec3f(0);
        }
    }

    Vec3f result;
    if (mat.type == MaterialType::DIFFUSE) {
        bool isGlossyDiffuse = mat.glossyWeight > 0.0f;
        Vec3f baseColor = mat.getColor(isect.texU, isect.texV);
        Vec3f ambient = isGlossyDiffuse
            ? baseColor * kShading.glossyAmbientTint
            : baseColor * kShading.diffuseAmbientTint;
        ambient *= std::max(0.0f, config.water.ambientStrength);
        float envDiffuse = std::max(0.0f, config.water.environmentDiffuseStrength);
        float upperSkyFacing = std::max(N.y, 0.0f);
        Vec3f upperSkyFill(0.0f);
        Vec3f skyAmbient(0.0f);
        Vec3f shadowLift(0.0f);
        float backLight = 0.0f;
        if (skyEnabled) {
            upperSkyFill = baseColor * config.water.upperSkyFillColor
                * (upperSkyFacing * std::max(0.0f, config.water.skyFillStrength) * kShading.upperSkyFillScale * envDiffuse);
        }
        float skyFactor = std::max(N.y, 0.0f) * (isGlossyDiffuse ? kShading.glossySkyFillScale : kShading.diffuseSkyFillScale)
            * std::max(0.0f, config.water.skyFillStrength);
        if (skyEnabled) {
            skyAmbient = baseColor * config.sky.horizonColor
                * (skyFactor * std::max(0.0f, config.water.horizonFillStrength) * envDiffuse);
        }
        Vec3f bounceDir = config.water.bounceDirection.normalized();
        if (bounceDir.length2() < 1e-8f) bounceDir = Vec3f(0.0f, -1.0f, 0.0f);
        float bounceFacing = std::max(0.0f, dot(N, bounceDir));
        float waterBounce = std::pow(bounceFacing, std::max(0.1f, config.water.bounceFalloff))
            * std::max(0.0f, config.water.bounceStrength);
        waterBounce = std::min(waterBounce, std::max(0.0f, config.water.bounceMaxContribution));
        Vec3f bounceAmbient = baseColor * config.water.bounceColor * waterBounce;

        if (skyEnabled) {
            Vec3f sunDir = config.sky.sunDirection.normalized();
            backLight = std::max(-dot(N, sunDir), 0.0f) * kShading.backLightScale;
            shadowLift = baseColor * config.sky.horizonColor
                * (std::max(0.0f, config.water.shadowLift) * (directLight.max_component() <= 0.0f ? 1.0f : 0.0f));
        }
        float cosTheta = std::max(0.0f, dot(wo, N));

        // directLight already contains BRDF * surfaceCos * geometryTerm — do not reweight by nDotL
        Vec3f glossy(0.0f);
        if (cosTheta > 1e-6f)
            glossy = brdf * indirect * cosTheta / pdf_val;

        result = ambient + upperSkyFill + skyAmbient + bounceAmbient + shadowLift + baseColor * backLight + directLight + glossy;

        // Clearcoat environment reflection for glossy paint. This adds a thin
        // Fresnel layer over the path-traced diffuse/glossy BRDF above instead
        // of replacing it with a local Blinn-Phong style highlight.
        if (isGlossyDiffuse) {
            Vec3f V = (-ray.direction).normalized();
            Vec3f R = reflect(ray.direction.normalized(), N).normalized();

            float NoV = std::max(0.0f, dot(N, V));
            float smoothness = 1.0f - std::clamp(mat.roughness, 0.0f, 1.0f);
            float fresnel = kShading.clearcoatF0
                + (1.0f - kShading.clearcoatF0) * std::pow(1.0f - NoV, 5.0f) * smoothness;

            float clearcoatStrength = mat.glossyWeight * smoothness * mat.specularBoost
                * kShading.clearcoatStrengthScale;

            Vec3f envReflection = sky.evaluate(R) * kShading.clearcoatEnvReflectionScale;
            result += envReflection * fresnel * clearcoatStrength;

            Vec3f mirrorStyledReflection = sky.evaluate(R) * baseColor;
            result = result * kShading.finalBaseBlend + mirrorStyledReflection * kShading.finalMirrorBlend;
        }
    } else if (mat.type == MaterialType::METAL) {
        result = directLight + brdf * indirect;
    }

    // Distance fog — blends distant objects toward the warm horizon color
    // for atmospheric haze / golden hour feel.
    {
        float fogDensity = std::max(0.0f, config.water.fogDensity);
        float fogAmount = 1.0f - std::exp(-isect.t * fogDensity);
        Vec3f fogColor = skyEnabled ? config.sky.horizonColor * kShading.fogColorScale : Vec3f(0.0f);
        result = result * (1.0f - fogAmount) + fogColor * fogAmount;
    }

    Vec3f r = sanitizeRadiance(result / survivalProb);
    return r;
}
