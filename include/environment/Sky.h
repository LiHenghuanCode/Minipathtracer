#pragma once

#include "core/Vec3.h"

struct SkyConfig {
    bool enabled;
    Vec3f horizonColor;
    Vec3f sunDirection;
    Vec3f sunDiskColor;
    Vec3f sunGlowColor;
    float sunAngularRadius;
    float sunEdgeSoftness;
    float sunIntensity;
    float sunDiskIntensity;
    float skyIntensity;
    float horizonWarmth;
    float sunsetGradientStrength;
    bool cloudsEnabled;
    float cloudScale;
    float cloudThreshold;
    float cloudSoftness;
    float cloudOpacity;
    Vec3f cloudDarkColor;
    Vec3f cloudWarmColor;
    bool cloudAdaptiveScaleEnabled;
    float cloudAdaptiveMinScale;
    float cloudAdaptiveMaxScale;
    float cloudSunEdgeIntensity;
    float cloudSunEdgePower;
    float cloudSunFocusPower;
};

class Sky {
public:
    void setConfig(const SkyConfig& config);
    Vec3f evaluate(const Vec3f& direction) const;

private:
    Vec3f sunDiskColor(const Vec3f& direction) const;

    SkyConfig config_;
};
