#pragma once

#include "core/Vec3.h"
#include "environment/Sky.h"

#include <string>
#include <vector>

struct RenderConfig {
    int width;
    int height;
    int spp;
    int maxDepth;
    std::string toneMapping;
    float displayExposure;
    float highlightCompression;
    float whitePoint;
    std::string outputFile;
};

struct CameraConfig {
    bool enabled;
    Vec3f position;
    Vec3f lookAt;
    Vec3f up;
    float fov;
};

struct WaterConfig {
    struct OceanLayerConfig {
        int resolution = 0;
        float patchLength = 0.0f;
        float windSpeed = 0.0f;
        Vec3f windDirection = Vec3f(0.0f);
        float waveHeight = 0.0f;
        float time = 0.0f;
    };

    float reflectionStrength;
    float normalStrength;
    bool fftEnabled;
    float fogDensity;
    float largeWaveScale;
    float smallWaveScale;
    OceanLayerConfig swell;
    OceanLayerConfig ripple;
};

struct SceneConfig {
    struct ArtTricksConfig {
        bool enabled = true;
        float ambientStrength = 1.3f;
        float environmentDiffuseStrength = 1.28f;
        float upperSkyFillStrength = 1.65f;
        Vec3f upperSkyFillColor = Vec3f(0.48f, 0.58f, 0.78f);
        float horizonFillStrength = 1.45f;
        float bounceStrength = 0.055f;
        Vec3f bounceColor = Vec3f(1.0f, 0.58f, 0.24f);
        Vec3f bounceDirection = Vec3f(0.0f, -1.0f, 0.0f);
        float bounceFalloff = 1.4f;
        float bounceMaxContribution = 0.07f;
        float shadowLift = 0.065f;
        float backLightScale = 0.15f;
        float fogColorScale = 0.35f;
        bool mirrorBlendEnabled = true;
        float mirrorBlendWeight = 0.65f;
        float clearcoatEnvReflectionScale = 1.5f;
        Vec3f blendAmbientTint = Vec3f(0.018f, 0.016f, 0.014f);
        Vec3f diffuseAmbientTint = Vec3f(0.04f, 0.035f, 0.03f);
        float upperSkyFillScale = 0.025f;
        float blendSkyFillScale = 0.015f;
        float diffuseSkyFillScale = 0.025f;
        float clearcoatF0 = 0.10f;
        float clearcoatStrengthScale = 0.35f;
    };

    struct AreaLightConfig {
        Vec3f position;
        Vec3f direction;
        float width;
        float height;
        Vec3f color;
        float intensity;
    };

    struct ObjectEntry {
        std::string type;
        std::string file;
        Vec3f position;
        float scale;
        std::string materialType;
        std::string normalSource;
        Vec3f materialColor;
        float roughness;
        float blendWeight;
        float reflectionScale;
        bool convertFromBlender = false;
    };

    RenderConfig render;
    CameraConfig camera;
    WaterConfig water;
    SkyConfig sky;
    ArtTricksConfig artTricks;
    AreaLightConfig areaLightConfig;
    bool hasAreaLight;
    std::vector<ObjectEntry> objects;
};
