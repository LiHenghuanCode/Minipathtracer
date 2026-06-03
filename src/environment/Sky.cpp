#include "environment/Sky.h"

#include <algorithm>
#include <cmath>

namespace {
float smoothstep01(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / std::max(edge1 - edge0, 1e-6f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float hash21(float x, float y) {
    float h = std::sin(x * 127.1f + y * 311.7f) * 43758.5453123f;
    return h - std::floor(h);
}

float valueNoise2D(float x, float y) {
    float ix = std::floor(x), iy = std::floor(y);
    float fx = x - ix, fy = y - iy;
    float ux = fx * fx * (3.0f - 2.0f * fx);
    float uy = fy * fy * (3.0f - 2.0f * fy);
    float v00 = hash21(ix, iy);
    float v10 = hash21(ix + 1.0f, iy);
    float v01 = hash21(ix, iy + 1.0f);
    float v11 = hash21(ix + 1.0f, iy + 1.0f);
    float lo = v00 * (1.0f - ux) + v10 * ux;
    float hi = v01 * (1.0f - ux) + v11 * ux;
    return lo * (1.0f - uy) + hi * uy;
}

float fbm2D(float x, float y) {
    float value = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    for (int i = 0; i < 5; ++i) {
        value += valueNoise2D(x * frequency, y * frequency) * amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return value;
}

float smoothstepCloud(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / std::max(edge1 - edge0, 1e-6f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Vec3f expVec(const Vec3f& v) {
    return Vec3f(std::exp(v.x), std::exp(v.y), std::exp(v.z));
}

Vec3f safeDiv(const Vec3f& numerator, const Vec3f& denominator) {
    return Vec3f(
        numerator.x / std::max(denominator.x, 1e-6f),
        numerator.y / std::max(denominator.y, 1e-6f),
        numerator.z / std::max(denominator.z, 1e-6f)
    );
}
}

void Sky::setConfig(const SkyConfig& config) {
    config_ = config;
}

Vec3f Sky::evaluate(const Vec3f& direction) const {
    if (!config_.enabled) {
        return Vec3f(0.0f);
    }

    Vec3f dir = direction.normalized();
    Vec3f sunDir = config_.sunDirection.normalized();
    float sunDot = std::max(0.0f, dot(dir, sunDir));
    float cosTheta = dot(dir, sunDir);

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

    float phaseR = 0.0596831f * (1.0f + cosTheta * cosTheta);
    Vec3f skyR = betaRayleigh * phaseR;

    constexpr float g = 0.76f;
    constexpr float g2 = g * g;
    float denomM = 1.0f + g2 - 2.0f * g * cosTheta;
    float phaseM = (denomM > 1e-6f)
        ? 0.1193662f * (1.0f - g2) * (1.0f + cosTheta * cosTheta)
            / ((2.0f + g2) * std::pow(denomM, 1.5f))
        : 0.0f;
    Vec3f skyM = betaMie * phaseM;

    Vec3f scattering = skyR + skyM;
    Vec3f inscatter = safeDiv(scattering * sunTransmittance, extinction)
        * (Vec3f(1.0f) - viewTransmittance);
    Vec3f skyRGB = inscatter * (16.0f * std::max(0.0f, config_.skyIntensity));

    if (dir.y < 0.0f) {
        float underHorizon = std::clamp(-dir.y * 6.0f, 0.0f, 1.0f);
        float warmDepth = 1.0f / std::max(0.12f + dir.y + 0.2f, 0.03f);
        Vec3f warmExtinction = expVec(Vec3f(-0.18f, -0.10f, -0.04f) * warmDepth);
        skyRGB = skyRGB * (1.0f - underHorizon)
            + warmExtinction * (0.28f * underHorizon * std::max(0.0f, config_.horizonWarmth));
    }

    {
        Vec3f dirH(dir.x, 0.0f, dir.z);
        float dirHL = dirH.length();
        Vec3f sunH(sunDir.x, 0.0f, sunDir.z);
        float sunHL = sunH.length();
        if (dirHL > 1e-6f && sunHL > 1e-6f) {
            dirH = dirH / dirHL;
            sunH = sunH / sunHL;
            float hDot = std::max(0.0f, dot(dirH, sunH));
            float vDiff = dir.y - sunDir.y;
            float aniso = std::pow(hDot, 6.0f) * std::exp(-vDiff * vDiff * 20.0f);
            float horizonBoost = std::exp(-dir.y * dir.y * 8.0f);
            skyRGB += config_.sunGlowColor
                * (aniso * horizonBoost * 0.16f * std::max(0.0f, config_.sunsetGradientStrength));
        }
    }

    skyRGB += sunDiskColor(dir);

    if (config_.cloudsEnabled && dir.y > -0.05f) {
        float denom = std::max(dir.y + 0.3f, 0.05f);
        float sunDenom = std::max(sunDir.y + 0.3f, 0.05f);
        float projU = dir.x / denom - sunDir.x / sunDenom;
        float projV = dir.z / denom - sunDir.z / sunDenom;

        float horizLen = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        float distApprox = horizLen / std::max(dir.y + 0.1f, 0.01f);
        float adaptiveScale = config_.cloudScale;
        if (config_.cloudAdaptiveScaleEnabled) {
            adaptiveScale = config_.cloudScale * 0.3f / (distApprox * 0.01f + 0.2f);
            adaptiveScale = std::clamp(
                adaptiveScale,
                config_.cloudAdaptiveMinScale,
                config_.cloudAdaptiveMaxScale
            );
        }

        float cloudRaw = fbm2D(projU * adaptiveScale, projV * adaptiveScale);
        float cloudMask = smoothstepCloud(
            config_.cloudThreshold,
            config_.cloudThreshold + config_.cloudSoftness,
            cloudRaw
        );
        float zenithFade = 1.0f - std::clamp((dir.y - 0.6f) / 0.4f, 0.0f, 1.0f);
        cloudMask *= zenithFade;

        float sunInfluence = std::pow(sunDot, 3.0f);
        Vec3f cloudColor = config_.cloudDarkColor * skyRGB * 1.25f * (1.0f - sunInfluence)
            + config_.cloudWarmColor * (sunInfluence * 0.75f);

        float rawSat = std::clamp(cloudRaw, 0.0f, 1.0f);
        float maskSat = std::clamp(cloudMask, 0.0f, 1.0f);
        float sunEdge = std::pow(rawSat, config_.cloudSunEdgePower)
            * std::pow(maskSat, config_.cloudSunEdgePower)
            * std::pow(sunDot, config_.cloudSunFocusPower);
        cloudColor += config_.sunGlowColor * (sunEdge * config_.cloudSunEdgeIntensity * 0.35f);

        float alpha = cloudMask * config_.cloudOpacity;
        skyRGB = skyRGB * (1.0f - alpha) + cloudColor * alpha;

        float cloudCore = std::pow(std::clamp(cloudRaw, 0.0f, 1.0f), 3.0f)
            * std::pow(std::clamp(cloudMask, 0.0f, 1.0f), 3.0f);
        skyRGB += Vec3f(1.2f, 0.38f, 0.06f) * (cloudCore * std::pow(sunDot, 4.0f) * 0.16f);
    }

    skyRGB += Vec3f(0.8f, 0.32f, 0.16f) * (std::pow(sunDot, 12.0f) * 0.08f);
    return skyRGB;
}

Vec3f Sky::sunDiskColor(const Vec3f& direction) const {
    if (!config_.enabled) {
        return Vec3f(0.0f);
    }

    Vec3f dir = direction.normalized();
    Vec3f sunDir = config_.sunDirection.normalized();
    float cosTheta = dot(dir, sunDir);
    float radius = std::clamp(config_.sunAngularRadius, 0.0005f, 0.08f);
    float softness = std::clamp(config_.sunEdgeSoftness, 0.0001f, 0.08f);
    float inner = std::cos(radius);
    float outer = std::cos(radius + softness);
    float disk = smoothstep01(outer, inner, cosTheta);
    return config_.sunDiskColor
        * (disk * std::max(0.0f, config_.sunIntensity) * std::max(0.0f, config_.sunDiskIntensity));
}
