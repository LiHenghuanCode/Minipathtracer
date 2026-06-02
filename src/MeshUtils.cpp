#include "MeshUtils.h"
#include "ScenePrivateUtils.h"
#include <algorithm>
#include <cmath>

// Computes tangent and bitangent vectors from triangle UV coordinates.
void computeTriangleTangents(Triangle& tri) {
    const Vec3f edge1 = tri.v1 - tri.v0;
    const Vec3f edge2 = tri.v2 - tri.v0;
    const float du1 = tri.u1 - tri.u0;
    const float dv1 = tri.v1t - tri.v0t;
    const float du2 = tri.u2 - tri.u0;
    const float dv2 = tri.v2t - tri.v0t;
    const float det = du1 * dv2 - du2 * dv1;

    if (std::fabs(det) < 1e-8f || !std::isfinite(det)) {
        tri.hasTangent = false;
        return;
    }

    const float invDet = 1.0f / det;
    tri.tangent = safeNormalizeScene((edge1 * dv2 - edge2 * dv1) * invDet, Vec3f(0.0f));
    tri.bitangent = safeNormalizeScene((edge2 * du1 - edge1 * du2) * invDet, Vec3f(0.0f));
    tri.hasTangent = tri.tangent.length2() > 1e-8f && tri.bitangent.length2() > 1e-8f;
}

// Applies a tangent-space normal map to the interpolated surface normal.
Vec3f applyMaterialNormalMap(const Material& mat, const Intersection& isect, const Vec3f& normal,
                             const SceneConfig& config) {
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

    // Normal maps are non-color data: Texture::sample returns raw 0-1 values with no gamma conversion.
    const Vec3f sampleNormal = config.sharpNormalSampling
        ? mat.bumpTexture->sampleNearest(isect.texU, isect.texV)
        : mat.bumpTexture->sample(isect.texU, isect.texV);
    Vec3f tangentNormal = sampleNormal * 2.0f - Vec3f(1.0f);
    if (config.flipNormalGreen) {
        tangentNormal.y = -tangentNormal.y;
    }
    const float detailStrength = std::max(0.0f, config.normalDetailStrength);
    tangentNormal.x *= detailStrength;
    tangentNormal.y *= detailStrength;
    tangentNormal = safeNormalizeScene(tangentNormal, Vec3f(0.0f, 0.0f, 1.0f));

    Vec3f mappedNormal = safeNormalizeScene(
        t * tangentNormal.x + b * tangentNormal.y + n * tangentNormal.z,
        n
    );

    const float requestedStrength = mat.normalStrength >= 0.0f ? mat.normalStrength : config.normalStrength;
    const float normalStrength = std::clamp(requestedStrength, 0.0f, 1.0f);
    return safeNormalizeScene(n * (1.0f - normalStrength) + mappedNormal * normalStrength, n);
}
