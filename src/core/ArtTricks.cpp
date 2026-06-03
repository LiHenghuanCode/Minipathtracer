#include "core/ArtTricks.h"

#include <algorithm>
#include <cmath>

namespace {
const SceneConfig::ArtTricksConfig& shading(const SceneConfig& config) {
    return config.artTricks;
}
}

Vec3f ArtTricks::applyDistanceFog(const Vec3f& litColor, float distance, const SceneConfig& config, bool skyEnabled) {
    if (!config.artTricks.enabled) {
        return litColor;
    }

    // Use the water fog density as a scene-wide atmospheric falloff control.
    const float fogDensity = std::max(0.0f, config.water.fogDensity);
    const float fogAmount = 1.0f - std::exp(-distance * fogDensity);
    const float fogColorScale = std::max(0.0f, config.artTricks.fogColorScale);
    const Vec3f fogColor = skyEnabled ? config.sky.horizonColor * fogColorScale : Vec3f(0.0f);
    return litColor * (1.0f - fogAmount) + fogColor * fogAmount;
}

DiffuseArtTricksContribution ArtTricks::computeDiffuseContribution(
    const Vec3f& normal,
    const Vec3f& baseColor,
    const Vec3f& directLight,
    bool isBlendMaterial,
    const SceneConfig& config,
    bool skyEnabled
) {
    DiffuseArtTricksContribution out;
    if (!config.artTricks.enabled) {
        return out;
    }

    const auto& style = shading(config);
    // Start from a small ambient floor tinted differently for pure diffuse and mixed materials.
    const Vec3f ambientTint = isBlendMaterial ? style.blendAmbientTint : style.diffuseAmbientTint;
    out.ambient = baseColor * ambientTint * std::max(0.0f, config.artTricks.ambientStrength);

    const float envDiffuse = std::max(0.0f, config.artTricks.environmentDiffuseStrength);
    const float upperSkyFacing = std::max(normal.y, 0.0f);
    if (skyEnabled) {
        // Add cool top-down fill to surfaces that face upward.
        out.upperSkyFill = baseColor * config.artTricks.upperSkyFillColor
            * (upperSkyFacing * std::max(0.0f, config.artTricks.upperSkyFillStrength)
                * std::max(0.0f, style.upperSkyFillScale) * envDiffuse);
    }

    const float skyScale = isBlendMaterial
        ? std::max(0.0f, style.blendSkyFillScale)
        : std::max(0.0f, style.diffuseSkyFillScale);
    const float skyFactor = std::max(normal.y, 0.0f) * skyScale
        * std::max(0.0f, config.artTricks.upperSkyFillStrength);
    if (skyEnabled) {
        // Reinforce the horizon tint separately from the upper-sky fill.
        out.horizonFill = baseColor * config.sky.horizonColor
            * (skyFactor * std::max(0.0f, config.artTricks.horizonFillStrength) * envDiffuse);
    }

    Vec3f bounceDir = config.artTricks.bounceDirection.normalized();
    if (bounceDir.length2() < 1e-8f) {
        bounceDir = Vec3f(0.0f, -1.0f, 0.0f);
    }
    float bounceFacing = std::max(0.0f, dot(normal, bounceDir));
    float bounceStrength = std::pow(bounceFacing, std::max(0.1f, config.artTricks.bounceFalloff))
        * std::max(0.0f, config.artTricks.bounceStrength);
    bounceStrength = std::min(bounceStrength, std::max(0.0f, config.artTricks.bounceMaxContribution));
    // Add a warm directional bounce term to simulate art-directed ground kick.
    out.bounceFill = baseColor * config.artTricks.bounceColor * bounceStrength;

    if (skyEnabled) {
        const Vec3f sunDir = config.sky.sunDirection.normalized();
        // Add a rim-like lift on surfaces that face away from the sun.
        const float backLightStrength = std::max(-dot(normal, sunDir), 0.0f)
            * std::max(0.0f, config.artTricks.backLightScale);
        out.backLight = baseColor * backLightStrength;

        // Lift fully shadowed regions so diffuse materials do not collapse to black.
        const float shadowLiftStrength = std::max(0.0f, config.artTricks.shadowLift)
            * (directLight.max_component() <= 0.0f ? 1.0f : 0.0f);
        out.shadowLift = baseColor * config.sky.horizonColor * shadowLiftStrength;
    }

    return out;
}

Vec3f ArtTricks::applyBlendFinish(
    const Vec3f& litColor,
    const Vec3f& rayDirection,
    const Vec3f& normal,
    const Vec3f& baseColor,
    const Material& mat,
    const SceneConfig& config,
    const Sky& sky
) {
    if (!config.artTricks.enabled || mat.type != MaterialType::BLEND) {
        return litColor;
    }

    const auto& style = shading(config);
    Vec3f result = litColor;
    Vec3f view = (-rayDirection).normalized();
    Vec3f reflection = reflect(rayDirection.normalized(), normal).normalized();

    const float noV = std::max(0.0f, dot(normal, view));
    const float smoothness = 1.0f - std::clamp(mat.roughness, 0.0f, 1.0f);
    const float clearcoatF0 = std::clamp(style.clearcoatF0, 0.0f, 1.0f);
    const float fresnel = clearcoatF0
        + (1.0f - clearcoatF0) * std::pow(1.0f - noV, 5.0f) * smoothness;
    const float clearcoatStrength = mat.blendWeight * smoothness * mat.reflectionScale
        * std::max(0.0f, style.clearcoatStrengthScale);
    const float envReflectionScale = std::max(0.0f, config.artTricks.clearcoatEnvReflectionScale);

    // Inject an extra environment-driven clearcoat highlight on top of the traced result.
    result += sky.evaluate(reflection) * envReflectionScale * fresnel * clearcoatStrength;

    if (!config.artTricks.mirrorBlendEnabled) {
        return result;
    }

    // Optionally blend toward a more graphic mirror-styled finish for presentation renders.
    const float mirrorBlend = std::clamp(config.artTricks.mirrorBlendWeight, 0.0f, 1.0f);
    const float baseBlend = 1.0f - mirrorBlend;
    const Vec3f mirrorStyledReflection = sky.evaluate(reflection) * baseColor;
    return result * baseBlend + mirrorStyledReflection * mirrorBlend;
}
