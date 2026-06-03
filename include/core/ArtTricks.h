#pragma once

#include "core/Material.h"
#include "core/Vec3.h"
#include "environment/Sky.h"
#include "scene/SceneConfig.h"

struct DiffuseArtTricksContribution {
    Vec3f ambient = Vec3f(0.0f);
    Vec3f upperSkyFill = Vec3f(0.0f);
    Vec3f horizonFill = Vec3f(0.0f);
    Vec3f bounceFill = Vec3f(0.0f);
    Vec3f shadowLift = Vec3f(0.0f);
    Vec3f backLight = Vec3f(0.0f);
};

class ArtTricks {
public:
    // Blends distant geometry toward the horizon color to create atmospheric haze.
    static Vec3f applyDistanceFog(const Vec3f& litColor, float distance, const SceneConfig& config, bool skyEnabled);

    // Computes the non-physical fill terms that shape the diffuse presentation.
    static DiffuseArtTricksContribution computeDiffuseContribution(
        const Vec3f& normal,
        const Vec3f& baseColor,
        const Vec3f& directLight,
        bool isBlendMaterial,
        const SceneConfig& config,
        bool skyEnabled
    );

    // Adds stylized clearcoat and mirror-finish treatment to blended materials.
    static Vec3f applyBlendFinish(
        const Vec3f& litColor,
        const Vec3f& rayDirection,
        const Vec3f& normal,
        const Vec3f& baseColor,
        const Material& mat,
        const SceneConfig& config,
        const Sky& sky
    );
};
