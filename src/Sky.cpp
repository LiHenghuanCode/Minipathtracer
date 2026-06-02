#include "Scene.h"
#include "ScenePrivateUtils.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
// Generates a deterministic pseudo-random value from a 2D coordinate.
float hash21(float x, float y) {
    float h = std::sin(x * 127.1f + y * 311.7f) * 43758.5453123f;
    return h - std::floor(h);
}

// Evaluates smooth 2D value noise for procedural clouds.
float valueNoise2D(float x, float y) {
    float ix = std::floor(x), iy = std::floor(y);
    float fx = x - ix,        fy = y - iy;
    // Cubic smoothstep so derivatives are continuous at cell edges
    float ux = fx * fx * (3.0f - 2.0f * fx);
    float uy = fy * fy * (3.0f - 2.0f * fy);
    float v00 = hash21(ix,       iy      );
    float v10 = hash21(ix + 1.f, iy      );
    float v01 = hash21(ix,       iy + 1.f);
    float v11 = hash21(ix + 1.f, iy + 1.f);
    float lo = v00 * (1.f - ux) + v10 * ux;
    float hi = v01 * (1.f - ux) + v11 * ux;
    return lo * (1.f - uy) + hi * uy;
}

// Combines multiple noise octaves for cloud shape detail.
float fbm2D(float x, float y) {
    float value = 0.f, amp = 0.5f, freq = 1.f;
    for (int i = 0; i < 5; ++i) {
        value += valueNoise2D(x * freq, y * freq) * amp;
        amp   *= 0.5f;
        freq  *= 2.0f;
    }
    return value;
}

// Smoothly remaps a value for cloud masks.
float smoothstepCloud(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
}

// Creates the current scene area light and its emissive geometry.
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

// Samples direct lighting from the current scene area light.
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
    float remainingDist = dist;
    float shadowTransmittance = 1.0f;
    for (int i = 0; i < 4; ++i) {
        Ray shadowRay(shadowOrigin, lightDir);
        Intersection shadowIsect = bvh.intersect(shadowRay);
        if (!shadowIsect.hit || shadowIsect.t >= remainingDist - 1e-2f) {
            break;
        }

        const Material& shadowMat = materials[shadowIsect.materialId];
        if (shadowMat.role != MaterialRole::PROPELLER_AFTERIMAGE) {
            return Vec3f(0);
        }

        shadowTransmittance *= (1.0f - std::clamp(shadowMat.alpha, 0.0f, 0.12f) * 0.35f);
        shadowOrigin = shadowIsect.position + lightDir * 2e-2f;
        remainingDist -= shadowIsect.t;
    }

    Vec3f brdf = mat.eval(wi, lightDir, N, texU, texV);
    float geometryTerm = surfaceCos * lightCos / dist2;
    return areaLight.emission * brdf * geometryTerm * areaLight.area() * shadowTransmittance;
}

// Computes the procedural sky, sun glow, sun disk, and cloud layer.
Vec3f Scene::skyColor(const Vec3f& direction) const {
    const SceneConfig::SkyConfig& sky = config.sky;
    Vec3f dir = direction.normalized();

    // 2. Sun direction
    Vec3f sunDir   = sky.sunDirection.normalized();
    float sunDot   = std::max(0.0f, dot(dir, sunDir)); // clamped — for pow terms
    float cosTheta = dot(dir, sunDir);                  // unclamped — for exp2 and phase functions

    auto expVec = [](const Vec3f& v) {
        return Vec3f(std::exp(v.x), std::exp(v.y), std::exp(v.z));
    };
    auto safeDiv = [](const Vec3f& num, const Vec3f& den) {
        return Vec3f(
            num.x / std::max(den.x, 1e-6f),
            num.y / std::max(den.y, 1e-6f),
            num.z / std::max(den.z, 1e-6f)
        );
    };

    // 1. Optical-depth driven single-scattering approximation.
    // The view path grows rapidly toward the horizon, which naturally shifts
    // the sky from blue overhead toward warm tones near sunset.
    const float viewMu = std::max(dir.y, 0.03f);
    const float sunMu = std::max(sunDir.y + 0.08f, 0.03f);
    const float viewDepth = 1.0f / viewMu;
    const float sunDepth = 1.0f / sunMu;

    const Vec3f betaRayleighBase(5.8e-6f, 13.5e-6f, 33.1e-6f);
    const Vec3f betaMieBase(21.0e-6f);
    const Vec3f betaRayleigh = betaRayleighBase * 2200.0f;
    const Vec3f betaMie = betaMieBase * 900.0f;
    const Vec3f extinction = betaRayleigh + betaMie;

    Vec3f sunTransmittance = expVec(extinction * (-sunDepth * 1.15f));
    Vec3f viewTransmittance = expVec(extinction * (-viewDepth * 0.85f));

    // Rayleigh scattering (blue sky): phase = (3/16π)(1+cos²θ)
    float phaseR = 0.0596831f * (1.0f + cosTheta * cosTheta);
    Vec3f skyR = betaRayleigh * phaseR;

    // Mie scattering (achromatic forward peak): Cornette-Shanks phase
    constexpr float g  = 0.76f;
    constexpr float g2 = g * g;
    float denomM = 1.0f + g2 - 2.0f * g * cosTheta;
    float phaseM = (denomM > 1e-6f)
        ? 0.1193662f * (1.0f - g2) * (1.0f + cosTheta * cosTheta)
          / ((2.0f + g2) * std::pow(denomM, 1.5f))
        : 0.0f;
    Vec3f skyM = betaMie * phaseM;

    Vec3f scattering = skyR + skyM;
    Vec3f inscatter = safeDiv(scattering * sunTransmittance, extinction) * (Vec3f(1.0f) - viewTransmittance);
    Vec3f skyRGB = inscatter * (16.0f * std::max(0.0f, sky.skyIntensity));

    // Below the horizon, keep a little warm aerosol glow instead of a direct gradient.
    if (dir.y < 0.0f) {
        float underHorizon = std::clamp(-dir.y * 6.0f, 0.0f, 1.0f);
        float warmDepth = 1.0f / std::max(0.12f + dir.y + 0.2f, 0.03f);
        Vec3f warmExtinction = expVec(Vec3f(-0.18f, -0.10f, -0.04f) * warmDepth);
        skyRGB = skyRGB * (1.0f - underHorizon)
            + warmExtinction * (0.28f * underHorizon * std::max(0.0f, sky.horizonWarmth));
    }

    // 3. Sun layers ordered wide → tight so the disk sits on top of the glow.
    // exp2 layers use raw cosTheta (unclamped) so the corona bleeds below the horizon line.
    // Anisotropic horizon glow — stretches the sun glow horizontally
    // to create the elongated warm band along the horizon line.
    {
        Vec3f dirH  = Vec3f(dir.x, 0.0f, dir.z);
        float dirHL = dirH.length();
        Vec3f sunH  = Vec3f(sunDir.x, 0.0f, sunDir.z);
        float sunHL = sunH.length();
        if (dirHL > 1e-6f && sunHL > 1e-6f) {
            dirH = dirH / dirHL;
            sunH = sunH / sunHL;
            float hDot  = std::max(0.0f, dot(dirH, sunH));
            float vDiff = dir.y - sunDir.y;
            // Horizontal: slow falloff (pow 6), Vertical: fast gaussian falloff
            float aniso = std::pow(hDot, 6.0f) * std::exp(-vDiff * vDiff * 20.0f);
            // Stronger near horizon (low elevation)
            float horizonBoost = std::exp(-dir.y * dir.y * 8.0f);
            skyRGB += sky.sunGlowColor
                * (aniso * horizonBoost * 0.16f * std::max(0.0f, sky.sunsetGradientStrength));
        }
    }

    Vec3f warmColor = sky.sunGlowColor;
    skyRGB += warmColor * (0.07f * sky.sunGlowIntensity * std::pow(sunDot, sky.sunGlowPower * 4.0f));
    skyRGB += warmColor * (0.08f * sky.sunGlowIntensity * std::exp2(cosTheta * 45.0f - 45.0f));
    skyRGB += warmColor * (0.04f * sky.sunGlowIntensity * std::exp2(cosTheta * 95.0f - 95.0f));
    skyRGB += sunDiskColor(dir);

    // ---- Procedural cloud layer ----
    if (sky.cloudsEnabled && dir.y > -0.05f) {
        // Project ray to a sun-centered UV so clouds cluster around the sun direction.
        float denom    = std::max(dir.y    + 0.3f, 0.05f);
        float sunDenom = std::max(sunDir.y + 0.3f, 0.05f);
        float projU = dir.x / denom - sunDir.x / sunDenom;
        float projV = dir.z / denom - sunDir.z / sunDenom;

        // Distance-adaptive FBM scale:
        // Near clouds (high elevation, low distApprox) sample at higher noise frequency → more detail.
        // Far clouds (near horizon, high distApprox) sample at lower frequency → softer, smoother bands.
        float horizLen   = std::sqrt(dir.x*dir.x + dir.z*dir.z);
        float distApprox = horizLen / std::max(dir.y + 0.1f, 0.01f);
        float adaptiveScale;
        if (sky.cloudAdaptiveScaleEnabled) {
            adaptiveScale = sky.cloudScale * 0.3f / (distApprox * 0.01f + 0.2f);
            adaptiveScale = std::clamp(adaptiveScale,
                                       sky.cloudAdaptiveMinScale,
                                       sky.cloudAdaptiveMaxScale);
        } else {
            adaptiveScale = sky.cloudScale;
        }

        float cloudRaw  = fbm2D(projU * adaptiveScale, projV * adaptiveScale);
        float cloudMask = smoothstepCloud(sky.cloudThreshold,
                                          sky.cloudThreshold + sky.cloudSoftness,
                                          cloudRaw);
        float zenithFade = 1.0f - std::clamp((dir.y - 0.6f) / 0.4f, 0.0f, 1.0f);
        cloudMask *= zenithFade;

        float sunInfluence = std::pow(sunDot, 3.0f);
        // Dark side tinted by current skyRGB so it inherits the orange/blue gradient.
        // Bright side uses warm sunlit color.
        Vec3f cloudColor = sky.cloudDarkColor * skyRGB * 1.25f * (1.0f - sunInfluence)
                         + sky.cloudWarmColor * (sunInfluence * 0.75f);

        // Sun-lit edge highlight:
        // Orange is added only where cloud raw density is high (thick regions), mask is strong,
        // and the cloud is angularly close to the sun — all three pow() terms ensure this.
        float rawSat  = std::clamp(cloudRaw,  0.0f, 1.0f);
        float maskSat = std::clamp(cloudMask, 0.0f, 1.0f);
        float sunEdge = std::pow(rawSat,  sky.cloudSunEdgePower)   // thick cloud only
                      * std::pow(maskSat, sky.cloudSunEdgePower)   // dense mask only
                      * std::pow(sunDot,  sky.cloudSunFocusPower); // near sun only
        cloudColor += sky.sunGlowColor * (sunEdge * sky.cloudSunEdgeIntensity * 0.35f);

        // Alpha-composite. Because skyColor() is the miss-ray return value, reflective water
        // naturally captures these cloud colors through path tracing — no extra code needed.
        float alpha = cloudMask * sky.cloudOpacity;
        skyRGB = skyRGB * (1.0f - alpha) + cloudColor * alpha;

        // Additive orange highlight on dense sun-facing cloud cores (reference shader approach).
        // Punches saturated orange through the blended base instead of being washed into it.
        float cloudCore = std::pow(std::clamp(cloudRaw,  0.0f, 1.0f), 3.0f)
                        * std::pow(std::clamp(cloudMask, 0.0f, 1.0f), 3.0f);
        // Removed the +0.4f constant so orange only fires near the sun, not everywhere.
        skyRGB += Vec3f(1.2f, 0.38f, 0.06f) * (cloudCore * std::pow(sunDot, 4.0f) * 0.16f);
    }

    // 5. Post-cloud flare — tightened to pow(8) so it stays near the sun, not a wide orange wash.
    skyRGB += Vec3f(0.8f, 0.32f, 0.16f) * (std::pow(sunDot, 12.0f) * 0.08f);

    // HDR — Reinhard tone mapping in writePPM handles clamping
    return skyRGB;
}

// Computes the procedural sun disk color.
Vec3f Scene::sunDiskColor(const Vec3f& direction) const {
    const SceneConfig::SkyConfig& sky = config.sky;
    Vec3f dir = direction.normalized();
    Vec3f sunDir = sky.sunDirection.normalized();
    float cosTheta = dot(dir, sunDir);
    float radius = std::clamp(sky.sunAngularRadius, 0.0005f, 0.08f);
    float softness = std::clamp(sky.sunEdgeSoftness, 0.0001f, 0.08f);
    float inner = std::cos(radius);
    float outer = std::cos(radius + softness);
    float disk = smoothstep01(outer, inner, cosTheta);
    return sky.sunDiskColor * (disk * std::max(0.0f, sky.sunIntensity) * std::max(0.0f, sky.sunDiskIntensity));
}
