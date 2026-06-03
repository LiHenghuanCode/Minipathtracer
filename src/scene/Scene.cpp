#include "scene/Scene.h"
#include "core/ArtTricks.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
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

    const float normalStrength = 1.0f;
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

    // Stop the path immediately when it hits an emitting surface.
    if (mat.hasEmission()) {
        return mat.emission;
    }

    // Probabilistically terminate deep paths to cap recursion cost while keeping the estimator unbiased.
    float survivalProb = 1.0f;
    if (depth > 3) {
        survivalProb = std::min(0.95f, mat.color.max_component());
        if (random_float() > survivalProb) {
            return Vec3f(0);
        }
    }

    Vec3f hitPoint = isect.position;
    Vec3f N = isect.normal;

    if (mat.useOceanNormals && ocean) {
        N = computeWaterNormal(hitPoint, N);
    } else {
        N = applyMaterialNormalMap(mat, isect, N);
    }

    // Flip non-mirror shading normals so cosine terms stay in the visible hemisphere.
    if (dot(N, ray.direction) > 0 && mat.type != MaterialType::MIRROR) {
        N = -N;
    }

    const bool skyEnabled = config.sky.enabled;

    // Estimate one-bounce direct light from the area light with next-event estimation.
    Vec3f directLight(0);
    if (mat.type == MaterialType::DIFFUSE || mat.type == MaterialType::BLEND) {
        if (hasAreaLight) {
            directLight = sampleAreaLight(hitPoint, ray.direction, N, mat, isect.texU, isect.texV);
        }
    }

    if (mat.type == MaterialType::MIRROR) {
        Vec3f R = reflect(ray.direction.normalized(), N).normalized();
        Vec3f reflectionOrigin = hitPoint + N * 1e-3f;
        Vec3f mirrorReflection = sky.evaluate(R);
        if (depth + 1 < config.render.maxDepth) {
            mirrorReflection = castRay(Ray(reflectionOrigin, R), depth + 1);
        }

        Vec3f waterReflection = mirrorReflection * std::max(0.0f, config.water.reflectionStrength);
        Vec3f result = directLight + waterReflection;

        return sanitizeRadiance(ArtTricks::applyDistanceFog(result, isect.t, config, skyEnabled) / survivalProb);
    }

    // Sample the BSDF and continue tracing the indirect bounce.
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

    // Offset the bounce origin away from the surface to avoid acne from self-intersection.
    Vec3f offset = dot(wo, N) > 0 ? N * 1e-3f : -N * 1e-3f;
    Ray bounceRay(hitPoint + offset, wo);
    Intersection bounceIsect = bvh.intersect(bounceRay);
    Vec3f indirect = castRay(bounceRay, depth + 1);
    if (hasAreaLight && (mat.type == MaterialType::DIFFUSE || mat.type == MaterialType::BLEND) && bounceIsect.hit) {
        if (bounceIsect.materialId == areaLight.materialId) {
            indirect = Vec3f(0);
        }
    }

    Vec3f result;
    if (mat.type == MaterialType::DIFFUSE || mat.type == MaterialType::BLEND) {
        const bool isBlendMaterial = mat.type == MaterialType::BLEND;
        Vec3f baseColor = mat.getColor(isect.texU, isect.texV);
        float cosTheta = std::max(0.0f, dot(wo, N));
        Vec3f diffuseIndirect(0.0f);
        if (cosTheta > 1e-6f) {
            diffuseIndirect = brdf * indirect * cosTheta / pdf_val;
        }

        if (isBlendMaterial) {
            Vec3f reflection = reflect(ray.direction.normalized(), N).normalized();
            Vec3f reflectionOrigin = hitPoint + N * 1e-3f;
            Vec3f mirrorBounce = sky.evaluate(reflection);
            if (depth + 1 < config.render.maxDepth) {
                mirrorBounce = castRay(Ray(reflectionOrigin, reflection), depth + 1);
            }
            const float blendWeight = std::clamp(mat.blendWeight, 0.0f, 1.0f);
            diffuseIndirect *= (1.0f - blendWeight);
            diffuseIndirect += mirrorBounce * mat.getMirrorColor() * blendWeight;
        }

        DiffuseArtTricksContribution tricks = ArtTricks::computeDiffuseContribution(
            N,
            baseColor,
            directLight,
            isBlendMaterial,
            config,
            skyEnabled
        );

        result = tricks.ambient
            + tricks.upperSkyFill
            + tricks.horizonFill
            + tricks.bounceFill
            + tricks.shadowLift
            + tricks.backLight
            + directLight
            + diffuseIndirect;

        if (isBlendMaterial) {
            result = ArtTricks::applyBlendFinish(result, ray.direction, N, baseColor, mat, config, sky);
        }
    }

    // Apply distance fog as a final atmospheric blend toward the configured sky colors.
    return sanitizeRadiance(ArtTricks::applyDistanceFog(result, isect.t, config, skyEnabled) / survivalProb);
}
