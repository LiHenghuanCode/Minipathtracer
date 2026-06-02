#include "Scene.h"
#include "ScenePrivateUtils.h"
#include <algorithm>
#include <cmath>

namespace {
// Generates a dark procedural cockpit pattern from canopy UV coordinates.
Vec3f fakeCockpitPattern(float texU, float texV) {
    const float u = texU;
    const float v = texV;

    // Base: nearly black blue cockpit cavity.
    Vec3f color(0.001f, 0.003f, 0.008f);

    // Dark dashboard band — horizontal stripe in lower-centre UV region
    const float dashboard =
        smoothstep01(0.25f, 0.35f, v) *
        (1.0f - smoothstep01(0.55f, 0.70f, v)) *
        smoothstep01(0.10f, 0.25f, u) *
        (1.0f - smoothstep01(0.75f, 0.90f, u));
    color = mixVec(color, Vec3f(0.004f, 0.006f, 0.010f), dashboard * 1.0f);

    // Pilot / seat silhouette — dark ellipse slightly above centre UV
    const float pu = (u - 0.52f) / 0.18f;
    const float pv = (v - 0.58f) / 0.28f;
    const float pDot = pu * pu + pv * pv;
    const float pilot = 1.0f - smoothstep01(0.75f, 1.0f, pDot);
    color = mixVec(color, Vec3f(0.0002f, 0.0003f, 0.0005f), pilot * 0.98f);

    // Subtle instrument / structure lines (horizontal + vertical crossing dashboard)
    const float line1 = 1.0f - smoothstep01(0.005f, 0.015f, std::fabs(v - 0.38f));
    const float line2 = 1.0f - smoothstep01(0.005f, 0.015f, std::fabs(u - 0.42f));
    const float lineMask = (line1 + line2) * dashboard;
    color += Vec3f(0.030f, 0.048f, 0.070f) * (lineMask * 0.55f);

    // Slight blue side-wall tint (falls off from extreme UV edges toward centre)
    const float sideTint =
        smoothstep01(0.0f, 0.25f, u) *
        (1.0f - smoothstep01(0.75f, 1.0f, u));
    color += Vec3f(0.004f, 0.012f, 0.024f) * sideTint;

    return color * 1.8f;
}
}

// Shades the canopy glass using a physical Fresnel-based model.
Vec3f Scene::shadeCanopyGlassPhysical(const Ray& ray, const Intersection& isect,
                                      const Material& mat, const Vec3f& N, int depth) const {
    if (config.debugCanopyExtreme) {
        Vec3f r(0.0f, 0.0f, 5.0f);
        return r;
    }
    if (config.debugFakeCockpitPattern) {
        Vec3f r = sanitizeRadiance(fakeCockpitPattern(isect.texU, isect.texV) * 8.0f);
        return r;
    }

    const Vec3f P = isect.position;
    const Vec3f I = safeNormalizeScene(ray.direction, Vec3f(0.0f, 0.0f, 1.0f));
    const Vec3f V = -I;
    const float NoV = std::max(0.0f, dot(N, V));
    const float edgeSparkle = std::pow(std::max(0.0f, 1.0f - NoV), 3.0f);
    const float kr = std::clamp(std::max(fresnelExact(I, N, mat.ior),
                                         fresnelSchlick(NoV, mat.fresnelF0)),
                                0.0f, 1.0f);

    const Vec3f reflDir = safeNormalizeScene(reflectDir(I, N), N);
    const Vec3f reflOrigin = dot(reflDir, N) < 0.0f ? P - N * 1e-3f : P + N * 1e-3f;
    Vec3f reflectionColor = skyColor(reflDir);
    if (depth + 1 < config.maxDepth) {
        reflectionColor = castRay(Ray(reflOrigin, reflDir, ray.hasPassedPropellerAfterimage), depth + 1);
    }
    reflectionColor = reflectionColor * config.canopyGlassCoolReflectionFactor *
                      std::max(0.0f, config.canopyGlassReflectionBoost);

    Vec3f refractionColor(0.01f, 0.025f, 0.04f);
    if (kr < 0.999f) {
        Vec3f refrDir = refractDir(I, N, mat.ior);
        if (refrDir.length2() > 1e-8f) {
            refrDir = safeNormalizeScene(refrDir, I);
            const Vec3f refrOrigin = dot(refrDir, N) < 0.0f ? P - N * 1e-3f : P + N * 1e-3f;
            if (depth + 1 < config.maxDepth) {
                refractionColor = castRay(Ray(refrOrigin, refrDir, ray.hasPassedPropellerAfterimage), depth + 1);
            } else {
                refractionColor = skyColor(refrDir);
            }
            refractionColor = refractionColor * config.canopyGlassTransmissionTint *
                              std::clamp(config.canopyGlassCenterDarkness, 0.0f, 1.0f);
        }
    }

    const float reflectionWeight = std::clamp(kr * config.canopyGlassReflectionBoost + edgeSparkle * 0.18f,
                                              0.0f, 0.95f);
    const Vec3f smokedCenter(0.01f, 0.025f, 0.04f);
    Vec3f color = reflectionColor * reflectionWeight + refractionColor * (1.0f - reflectionWeight);
    color = mixVec(smokedCenter, color, 0.88f);
    color += reflectionColor * (edgeSparkle * 0.10f);
    Vec3f r = sanitizeRadiance(color);
    return r;
}

// Shades the canopy glass using a cinematic fake interior and reflection model.
Vec3f Scene::shadeCanopyGlassFake(const Ray& ray, const Intersection& isect,
                                  const Material& mat, const Vec3f& N, int depth) const {
    (void)mat;

    // ----- Debug overrides -----
    if (config.debugCanopyExtreme) {
        Vec3f r(0.0f, 1.0f, 1.0f);
        return r;
    }
    // Visualise the raw cockpit pattern (8× brightened so dark values are visible).
    if (config.debugFakeCockpitPattern) {
        Vec3f r = sanitizeRadiance(fakeCockpitPattern(isect.texU, isect.texV) * 8.0f);
        return r;
    }

    // ----- View geometry -----
    const Vec3f V = safeNormalizeScene(-ray.direction, Vec3f(0.0f, 0.0f, 1.0f));
    const float NoV = std::max(dot(N, V), 0.0f);

    // Fresnel: ~canopyFresnelBase at centre, approaches 0.90 at grazing edges.
    // Using a fixed 0.85 scale keeps the centre dark even when canopyFresnelBase
    // is small, matching the "0.12 + fresnel*0.85" from the design spec.
    float fresnel = std::pow(std::max(0.0f, 1.0f - NoV),
                             std::max(0.1f, config.canopyFresnelPower));
    fresnel = std::clamp(config.canopyFresnelBase + fresnel * 0.85f,
                         config.canopyFresnelBase, 0.90f);

    // ----- Reflected environment (cast or sky fallback) -----
    const Vec3f reflDir = safeNormalizeScene(reflectDir(safeNormalizeScene(ray.direction), N), N);
    Vec3f env = skyColor(reflDir);
    if (depth + 1 < config.maxDepth) {
        env = castRay(Ray(isect.position + N * 1e-3f, reflDir, ray.hasPassedPropellerAfterimage), depth + 1);
    }
    // Cool the reflected sunset heavily so the canopy stops reading as gold plastic.
    const Vec3f cooledEnv = env * config.canopyCoolReflection;

    // ----- Procedural cockpit interior -----
    // fakeCockpitPattern returns a very dark (< 0.05) blue-black pattern with
    // subtle dashboard / pilot-silhouette structure baked in.
    const Vec3f interior = fakeCockpitPattern(isect.texU, isect.texV);
    // Apply a blue-grey glass tint so the cockpit reads through tinted glass.
    const Vec3f glassTint(0.30f, 0.50f, 0.85f);
    const Vec3f cockpitBase(0.001f, 0.003f, 0.008f);
    const Vec3f interiorDark(0.0f, 0.002f, 0.006f);
    Vec3f tintedInterior = (cockpitBase + interior) * glassTint;
    tintedInterior = mixVec(tintedInterior, interiorDark,
                            std::clamp(config.canopyInteriorDarkness, 0.0f, 1.0f) * 0.55f);

    // ----- Layered glass composite -----
    // At centre (low Fresnel) → dark cockpit pattern dominates.
    // At edges (high Fresnel) → cool environment reflection dominates.
    // Reflection ramps in nonlinearly so the canopy centre stays visibly dark.
    const float edgeFresnel = std::clamp(
        (fresnel - config.canopyFresnelBase) /
        std::max(1e-3f, 1.0f - config.canopyFresnelBase),
        0.0f, 1.0f);
    const float reflectionWeight =
        std::clamp(std::pow(edgeFresnel, 1.65f) * config.canopyReflectionMix, 0.0f, 0.82f);
    Vec3f reflection = mixVec(Vec3f(0.045f, 0.085f, 0.16f), cooledEnv, 0.34f);
    Vec3f color = mixVec(tintedInterior, reflection, reflectionWeight);

    // ----- Sharp sun glint -----
    const Vec3f sunDir = safeNormalizeScene(config.sky.sunDirection, Vec3f(0.0f, 1.0f, 0.0f));
    const float glint = std::pow(std::max(dot(reflDir, sunDir), 0.0f), 80.0f);
    color += Vec3f(1.0f, 0.90f, 0.65f) * glint * std::max(0.0f, config.canopyGlintStrength);

    color *= std::max(0.0f, config.canopyBrightnessBoost);
    Vec3f r = sanitizeRadiance(color);
    return r;
}

// Shades the propeller afterimage as a transparent motion-blur surface.
Vec3f Scene::shadePropellerAfterimage(const Ray& ray, const Intersection& isect,
                                      const Material& mat, const Vec3f& N, int depth) const {
    // -----------------------------------------------------------------------
    // Debug mode: force obvious green/orange tint to confirm this path fires.
    // Enable via "debugPropellerAfterimageExtreme": true in scene.json.
    // Expected: the afterimage area becomes clearly green; if it does NOT,
    // the transparent faces are either occluded by solid blades in front, or
    // the afterimage geometry faces away from the camera.
    // -----------------------------------------------------------------------
    if (config.debugPropellerAfterimageExtreme) {
        Vec3f behindColor = skyColor(ray.direction);
        if (depth + 1 < config.maxDepth) {
            // Larger offset to avoid re-hitting the same surface
            behindColor = castRay(Ray(isect.position + ray.direction * 5e-2f, ray.direction, true), depth + 1);
        }
        // behindColor * 0.4 + strong green * 0.6 — unmistakably green
        Vec3f r = sanitizeRadiance(behindColor * 0.4f + Vec3f(0.0f, 1.0f, 0.0f) * 0.6f);
        return r;
    }

    // -----------------------------------------------------------------------
    // Grey semi-transparent afterimage shader.
    //
    // Design intent:
    //   The afterimage surface must contribute its own GREY tint visibly.
    //   It must NOT look like clear transparent glass.
    //   It must NOT look like a solid black blade.
    //   It should read as a soft grey residual motion trail.
    //
    // Key properties:
    //   afterimageColor = grey (set in scene.json propellerAfterimageColor)
    //   alpha = ~0.40 (set in scene.json propellerAfterimageAlpha)
    //   additive term (+0.15 * grey) prevents it from vanishing into dark bg
    //
    // FIX HISTORY:
    //   - Previous bug: color was warm brown (0.30,0.23,0.16) → blended
    //     invisibly with dark solid blade or warm sky behind it.
    //   - Previous bug: NoV*softness term clamped alpha too aggressively
    //     (effectiveAlpha as low as 0.13), making grey contribution negligible.
    //   - Previous bug: additive term was alpha*0.15 instead of constant 0.08,
    //     which at alpha=0.13 gave only 0.02 additive → nearly invisible.
    //   - Fix: grey color, higher alpha (0.40), constant additive 0.15*grey.
    // -----------------------------------------------------------------------

    // Grey semi-transparent tint. Configured via propellerAfterimageColor in JSON.
    // Should be set to e.g. [0.12, 0.12, 0.13] (neutral dark grey).
    const Vec3f afterimageColor = config.propellerAfterimageColor;

    if (depth + 1 >= config.maxDepth) {
        // At max depth: return the grey at partial visibility — visible grey fallback,
        // not pure black (which was the old behaviour).
        Vec3f r = sanitizeRadiance(afterimageColor * 0.5f);
        return r;
    }

    // Continue the ray through the transparent surface.
    // 5e-2f offset clears the current flat face without skipping nearby geometry.
    const Vec3f origin = isect.position + ray.direction * 5e-2f;
    Ray continueRay(origin, ray.direction);
    continueRay.hasPassedPropellerAfterimage = true;

    Vec3f behindColor = castRay(continueRay, depth + 1);

    // Effective alpha: use mat.alpha directly (no NoV softness reduction).
    // NoV softness was making already-low alpha even smaller at grazing angles,
    // which caused near-miss hits to become nearly invisible.
    float alpha = std::clamp(mat.alpha, 0.0f, 0.5f);
    if (ray.hasPassedPropellerAfterimage) {
        // Stacked hit: reduce alpha to prevent multiple overlapping transparent
        // faces from adding up to full opacity.
        alpha *= std::clamp(config.propellerAfterimageStackReduction, 0.0f, 1.0f);
    }

    // Standard over-composite: afterimageColor tints whatever is behind.
    Vec3f baseBlend = behindColor * (1.0f - alpha) + afterimageColor * alpha;

    // Additive grey contribution:
    // This is the critical fix — it ensures the grey is VISIBLE even when
    // behindColor is very dark (e.g. the solid blade immediately behind).
    // Without this term, a dark blade at behind + 0.28 alpha grey gives
    // a result only barely brighter than the blade itself.
    // With this term, the grey reads clearly as a semi-transparent overlay.
    Vec3f result = baseBlend + afterimageColor * 0.15f;

    Vec3f r = sanitizeRadiance(result);
    return r;
}

// Shades aircraft metal with a custom environment reflection response.
Vec3f Scene::shadeAircraftMetal(const Ray& ray, const Intersection& isect,
                                const Material& mat, const Vec3f& N, int depth) const {
    const Vec3f I = safeNormalizeScene(ray.direction, Vec3f(0.0f, 0.0f, 1.0f));
    const Vec3f reflDir = safeNormalizeScene(reflectDir(I, N), N);
    Vec3f reflectionColor = skyColor(reflDir);
    if (depth + 1 < config.maxDepth) {
        reflectionColor = castRay(Ray(isect.position + N * 1e-3f, reflDir,
                                      ray.hasPassedPropellerAfterimage), depth + 1);
    }

    const Vec3f base = mat.getColor(isect.texU, isect.texV);
    Vec3f r = sanitizeRadiance(mixVec(base, reflectionColor * mat.aluminumReflectance, 0.60f));
    return r;
}
