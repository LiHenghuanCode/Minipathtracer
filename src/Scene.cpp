#include "Scene.h"
#include "Diagnostics.h"
#include "MeshUtils.h"
#include "ScenePrivateUtils.h"
#include <algorithm>
#include <cmath>

Vec3f Scene::castRay(const Ray& ray, int depth) const {
    if (depth >= config.maxDepth) return Vec3f(0);

    if (depth == 0 && config.renderMode == "skyOnly") {
        return skyColor(ray.direction);
    }
    if (depth == 0 && config.renderMode == "sunDiskOnly") {
        return sunDiskColor(ray.direction);
    }

    Intersection isect = bvh.intersect(ray);

    if (!isect.hit) {
        if (config.renderMode == "sunDiskOnly") {
            return sunDiskColor(ray.direction);
        }
        if (config.renderMode == "albedoOnly" || config.renderMode == "baseColorOnly" ||
            config.renderMode == "textureOnly" || config.renderMode == "directOnly" ||
            config.renderMode == "ambientOnly" || config.renderMode == "specularOnly" ||
            config.renderMode == "shadowFactorOnly" || config.renderMode == "waterReflectionOnly" ||
            config.renderMode == "waterRefractionOnly" || config.renderMode == "waterFresnelOnly" ||
            config.renderMode == "propellerMaterialDebug") {
            return Vec3f(0.0f);
        }
        return skyColor(ray.direction);
    }

    const Material& mat = materials[isect.materialId];
    if (depth == 0) {
        primaryRoleHits[materialRoleIndex(mat.role)].fetch_add(1, std::memory_order_relaxed);
    }

    if (depth == 0 && config.debugMaterialRoles) {
        return materialRoleDebugColor(mat.role);
    }

    // Propeller-specific material debug render.
    // renderMode = "propellerMaterialDebug" shows:
    //   PROPELLER_AFTERIMAGE (transparent blades) = bright green
    //   AIRCRAFT_PARTS  (solid propeller blades)  = bright red
    //   AIRCRAFT_BODY / AIRCRAFT_METAL (hub/nose) = blue
    //   everything else                           = dark grey
    // Use this to confirm which blades the camera sees first and whether
    // transparent faces are occluded by solid geometry in front of them.
    if (depth == 0 && config.renderMode == "propellerMaterialDebug") {
        switch (mat.role) {
            case MaterialRole::PROPELLER_AFTERIMAGE: return Vec3f(0.0f, 0.9f, 0.0f);
            case MaterialRole::AIRCRAFT_PARTS:       return Vec3f(0.9f, 0.1f, 0.1f);
            case MaterialRole::AIRCRAFT_BODY:        return Vec3f(0.1f, 0.1f, 0.9f);
            case MaterialRole::AIRCRAFT_METAL:       return Vec3f(0.3f, 0.0f, 0.9f);
            case MaterialRole::CANOPY_GLASS:         return Vec3f(0.0f, 0.8f, 1.0f);
            default:                                 return Vec3f(0.10f, 0.10f, 0.10f);
        }
    }

    if (depth == 0 && (config.renderMode == "albedoOnly" || config.renderMode == "baseColorOnly")) {
        return mat.getColor(isect.texU, isect.texV);
    }
    if (depth == 0 && config.renderMode == "textureOnly") {
        return mat.getTextureColor(isect.texU, isect.texV);
    }

    if (config.materialDebug != "none") {
        Vec3f N = isect.normal;
        if (mat.type == MaterialType::DIELECTRIC && ocean) {
            Vec3f largeN = ocean->getNormal(isect.position.x, isect.position.z);
            Vec3f waterN = largeN;
            if (oceanRipple) {
                Vec3f rippleN = oceanRipple->getNormal(isect.position.x, isect.position.z);
                float ls = std::max(0.0f, config.waterLargeWaveScale);
                float ss = std::max(0.0f, config.waterSmallWaveScale);
                waterN = normalize(Vec3f(largeN.x * ls + rippleN.x * ss, 1.0f, largeN.z * ls + rippleN.z * ss));
            }
            N = waterN;
        } else {
            N = applyMaterialNormalMap(mat, isect, N, config);
        }
        if (dot(N, ray.direction) > 0 && mat.type != MaterialType::DIELECTRIC) {
            N = -N;
        }
        return materialDebugColor(mat, N, isect.texU, isect.texV);
    }

    // Emissive surface
    if (mat.hasEmission()) {
        return mat.emission;
    }

    // Russian Roulette (after depth 3)
    float survivalProb = 1.0f;
    if (depth > 3) {
        survivalProb = std::min(0.95f, mat.color.max_component());
        if (random_float() > survivalProb) {
            return Vec3f(0);
        }
    }

    Vec3f hitPoint = isect.position;
    Vec3f N = isect.normal;

    if (mat.type == MaterialType::DIELECTRIC && ocean) {
        Vec3f largeNormal = ocean->getNormal(hitPoint.x, hitPoint.z);
        Vec3f waterNormal = largeNormal;
        if (oceanRipple) {
            Vec3f rippleNormal = oceanRipple->getNormal(hitPoint.x, hitPoint.z);
            float ls = std::max(0.0f, config.waterLargeWaveScale);
            float ss = std::max(0.0f, config.waterSmallWaveScale);
            waterNormal = normalize(Vec3f(
                largeNormal.x * ls + rippleNormal.x * ss,
                1.0f,
                largeNormal.z * ls + rippleNormal.z * ss
            ));
        }
        float normalStrength = std::clamp(config.waterNormalStrength, 0.0f, 1.0f);
        N = normalize(Vec3f(0.0f, 1.0f, 0.0f) * (1.0f - normalStrength) + waterNormal * normalStrength);
    } else {
        N = applyMaterialNormalMap(mat, isect, N, config);
    }

    // Ensure normal faces the ray
    if (dot(N, ray.direction) > 0 && mat.type != MaterialType::DIELECTRIC) {
        N = -N;
    }

    if (depth == 0 && config.debugNormalMap) {
        return (mat.bumpTexture && mat.bumpTexture->isLoaded()) ? N * 0.5f + Vec3f(0.5f) : Vec3f(0.0f);
    }

    if (mat.role == MaterialRole::CANOPY_GLASS) {
        return config.canopyGlassUsePhysical
            ? shadeCanopyGlassPhysical(ray, isect, mat, N, depth)
            : shadeCanopyGlassFake(ray, isect, mat, N, depth);
    }

    if (mat.role == MaterialRole::PROPELLER_AFTERIMAGE) {
        return shadePropellerAfterimage(ray, isect, mat, N, depth);
    }

    if (mat.role == MaterialRole::AIRCRAFT_METAL) {
        return shadeAircraftMetal(ray, isect, mat, N, depth);
    }

    // Direct lighting (NEE)
    Vec3f directLight(0);
    Vec3f shadowFactor(1.0f);
    if (mat.type == MaterialType::DIFFUSE || mat.type == MaterialType::METAL) {
        if (hasAreaLight) {
            directLight = sampleAreaLight(hitPoint, ray.direction, N, mat, isect.texU, isect.texV);
            shadowFactor = directLight.max_component() > 0.0f ? Vec3f(1.0f) : Vec3f(0.0f);
        }
    }

    // Indirect lighting
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

    // Offset origin to avoid self-intersection
    Vec3f offset = dot(wo, N) > 0 ? N * 1e-3f : -N * 1e-3f;
    Ray bounceRay(hitPoint + offset, wo);
    Intersection bounceIsect = bvh.intersect(bounceRay);
    Vec3f indirect = castRay(bounceRay, depth + 1);
    if (hasAreaLight && mat.type == MaterialType::DIFFUSE && bounceIsect.hit) {
        if (bounceIsect.materialId == areaLight.materialId) {
            indirect = Vec3f(0);
        }
    }

    bool enteringWater = mat.type == MaterialType::DIELECTRIC &&
                         dot(ray.direction, isect.normal) < 0.0f &&
                         dot(wo, isect.normal) < 0.0f;
    if (enteringWater && bounceIsect.hit) {
        float distance = bounceIsect.t;
        float absScale = std::max(0.0f, config.waterBaseAbsorption);
        Vec3f attenuation(
            std::exp(-mat.absorptionColor.x * absScale * distance),
            std::exp(-mat.absorptionColor.y * absScale * distance),
            std::exp(-mat.absorptionColor.z * absScale * distance)
        );
        indirect = indirect * attenuation;
    }

    Vec3f result;
    Vec3f ambientDebug(0.0f);
    Vec3f specularDebug(0.0f);
    if (mat.type == MaterialType::DIFFUSE) {
        bool isGlossyDiffuse = mat.glossyWeight > 0.0f;
        Vec3f baseColor = mat.getColor(isect.texU, isect.texV);
        Vec3f ambient = isGlossyDiffuse
            ? baseColor * Vec3f(0.018f, 0.016f, 0.014f)
            : baseColor * Vec3f(0.04f, 0.035f, 0.03f);
        ambient *= std::max(0.0f, config.ambientStrength);
        float envDiffuse = std::max(0.0f, config.environmentDiffuseStrength);
        float upperSkyFacing = std::max(N.y, 0.0f);
        Vec3f upperSkyFill = baseColor * config.upperSkyFillColor
            * (upperSkyFacing * std::max(0.0f, config.skyFillStrength) * 0.025f * envDiffuse);
        float skyFactor = std::max(N.y, 0.0f) * (isGlossyDiffuse ? 0.015f : 0.025f)
            * std::max(0.0f, config.skyFillStrength);
        Vec3f skyAmbient = baseColor * config.sky.horizonColor
            * (skyFactor * std::max(0.0f, config.horizonFillStrength) * envDiffuse);
        Vec3f bounceDir = config.waterBounceDirection.normalized();
        if (bounceDir.length2() < 1e-8f) bounceDir = Vec3f(0.0f, -1.0f, 0.0f);
        float bounceFacing = std::max(0.0f, dot(N, bounceDir));
        float waterBounce = std::pow(bounceFacing, std::max(0.1f, config.waterBounceFalloff))
            * std::max(0.0f, config.waterBounceStrength);
        waterBounce = std::min(waterBounce, std::max(0.0f, config.waterBounceMaxContribution));
        Vec3f bounceAmbient = baseColor * config.waterBounceColor * waterBounce;

        Vec3f sunDir = config.sky.sunDirection.normalized();
        float backLight = std::max(-dot(N, sunDir), 0.0f) * 0.15f;
        Vec3f shadowLift = baseColor * config.sky.horizonColor
            * (std::max(0.0f, config.shadowLift) * (directLight.max_component() <= 0.0f ? 1.0f : 0.0f));
        float cosTheta = std::max(0.0f, dot(wo, N));

        // directLight already contains BRDF * surfaceCos * geometryTerm — do not reweight by nDotL
        Vec3f glossy(0.0f);
        if (cosTheta > 1e-6f)
            glossy = brdf * indirect * cosTheta / pdf_val;

        ambientDebug = ambient + upperSkyFill + skyAmbient + bounceAmbient + shadowLift + baseColor * backLight;
        result = ambient + upperSkyFill + skyAmbient + bounceAmbient + shadowLift + baseColor * backLight + directLight + glossy;

        // Clearcoat environment reflection for glossy paint. This adds a thin
        // Fresnel layer over the path-traced diffuse/glossy BRDF above instead
        // of replacing it with a local Blinn-Phong style highlight.
        if (isGlossyDiffuse) {
            Vec3f V = (-ray.direction).normalized();
            Vec3f R = reflect(ray.direction.normalized(), N).normalized();

            float NoV = std::max(0.0f, dot(N, V));
            float smoothness = 1.0f - std::clamp(mat.roughness, 0.0f, 1.0f);
            float fR0 = std::clamp(config.aircraftClearcoatF0, 0.0f, 1.0f);
            float fresnel = fR0 + (1.0f - fR0) * std::pow(1.0f - NoV, 5.0f) * smoothness;

            float clearcoatStrength = mat.glossyWeight * smoothness * mat.specularBoost
                * std::max(0.0f, config.aircraftClearcoatStrength);

            Vec3f envReflection = skyColor(R) * std::max(0.0f, config.aircraftClearcoatEnvBoost);
            result += envReflection * fresnel * clearcoatStrength;

            if (config.aircraftMirrorDebug) {
                Vec3f mirrorDebug = skyColor(R) * baseColor;
                result = result * 0.35f + mirrorDebug * 0.65f;
            }
        }
        if (mat.role == MaterialRole::AIRCRAFT_PARTS) {
            result += baseColor * 0.08f + Vec3f(0.025f, 0.025f, 0.028f);
        }
    } else if (mat.type == MaterialType::METAL) {
        result = directLight + brdf * indirect;
        specularDebug = brdf * indirect;
    } else {
        // Dielectric: path-traced result + direct sky/sun reflection fallback
        float foreground = std::clamp((60.0f - isect.t) / 60.0f, 0.0f, 1.0f);
        float refractionWeight = std::max(0.0f, config.waterRefractionWeight)
            * (1.0f - std::clamp(config.waterForegroundDarkening, 0.0f, 1.0f) * foreground);
        result = directLight + brdf * indirect * refractionWeight;

        // Add Fresnel-weighted environment reflection so the water surface
        // always picks up sky color, even when recursive paths lose energy.
        Vec3f R = reflect(ray.direction, N).normalized();
        float NoV = std::max(0.0f, dot(N, (-ray.direction).normalized()));
        // Schlick Fresnel with F0 = 0.02 (water at ior 1.33)
        float fresnel = std::clamp(0.02f + 0.98f * std::pow(1.0f - NoV, 5.0f)
            + std::max(0.0f, config.waterFresnelBias), 0.0f, 1.0f);
        Vec3f envColor = skyColor(R) * std::max(0.0f, config.environmentReflectionStrength);
        Vec3f warmEnv = envColor * (1.0f - std::clamp(config.waterWarmth, 0.0f, 1.0f))
            + (envColor * config.sky.horizonColor) * std::clamp(config.waterWarmth, 0.0f, 1.0f);

        Vec3f sunDir = config.sky.sunDirection.normalized();
        Vec3f rH = Vec3f(R.x, 0.0f, R.z);
        Vec3f sH = Vec3f(sunDir.x, 0.0f, sunDir.z);
        float horizontal = 0.0f;
        if (rH.length2() > 1e-8f && sH.length2() > 1e-8f) {
            horizontal = std::max(0.0f, dot(rH.normalized(), sH.normalized()));
        }
        float vertical = std::exp(-std::pow((R.y - sunDir.y) / std::max(0.04f, config.waterRoughness), 2.0f));
        float pathPower = 10.0f / std::max(0.25f, config.waterRoughness);
        float sunPath = std::pow(horizontal, pathPower) * vertical;
        Vec3f sunPathColor = config.sky.sunDiskColor * (sunPath * config.waterSunReflectionStrength);

        // Minimal floor — only waterReflectionFloor, no longer compounding shadowLift or
        // horizonFillStrength here. Those were inflating the floor and killing Fresnel contrast.
        float reflectionFloor = std::max(0.0f, config.waterReflectionFloor);
        Vec3f horizonFloor = config.sky.horizonColor * (reflectionFloor * std::max(0.0f, config.waterReflectionStrength));
        Vec3f waterReflection = (warmEnv * (fresnel + reflectionFloor) + horizonFloor)
                * std::max(0.0f, config.waterReflectionStrength)
            + sunPathColor * fresnel;
        result += waterReflection;
        specularDebug = waterReflection;

        if (config.renderMode == "waterReflectionOnly") {
            return sanitizeRadiance(waterReflection / survivalProb);
        }
        if (config.renderMode == "waterRefractionOnly") {
            return sanitizeRadiance((directLight + brdf * indirect * refractionWeight) / survivalProb);
        }
        if (config.renderMode == "waterFresnelOnly") {
            return Vec3f(fresnel, fresnel, fresnel);
        }
    }

    if (depth == 0) {
        if (config.renderMode == "directOnly") return sanitizeRadiance(directLight / survivalProb);
        if (config.renderMode == "ambientOnly") return sanitizeRadiance(ambientDebug / survivalProb);
        if (config.renderMode == "specularOnly") return sanitizeRadiance(specularDebug / survivalProb);
        if (config.renderMode == "shadowFactorOnly") return shadowFactor;
        if (config.renderMode == "waterReflectionOnly") return Vec3f(0.0f);
    }

    // Distance fog — blends distant objects toward the warm horizon color
    // for atmospheric haze / golden hour feel.
    {
        float fogDensity = 0.003f;
        float fogAmount = 1.0f - std::exp(-isect.t * fogDensity);
        Vec3f fogColor = config.sky.horizonColor * 0.35f;
        result = result * (1.0f - fogAmount) + fogColor * fogAmount;
    }

    Vec3f r = sanitizeRadiance(result / survivalProb);
    return r;
}
