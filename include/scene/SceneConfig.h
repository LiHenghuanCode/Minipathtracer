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
    Vec3f right;
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
    float skyFillStrength;
    float horizonFillStrength;
    float bounceStrength;
    Vec3f bounceColor;
    Vec3f bounceDirection;
    float bounceFalloff;
    float bounceMaxContribution;
    Vec3f upperSkyFillColor;
    float environmentDiffuseStrength;
    float ambientStrength;
    float shadowLift;
    float largeWaveScale;
    float smallWaveScale;
    OceanLayerConfig swell;
    OceanLayerConfig ripple;
};

struct SceneConfig {
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
        Vec3f materialColor;
        float roughness;
        float glossyWeight;
        float specularBoost;
        float ior;
        bool convertFromBlender;
    };

    RenderConfig render;
    CameraConfig camera;
    WaterConfig water;
    SkyConfig sky;
    AreaLightConfig areaLightConfig;
    bool hasAreaLight;
    std::vector<ObjectEntry> objects;
};
