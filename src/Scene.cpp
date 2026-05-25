#include "Scene.h"
#include "Noise.h"
#include "OBJ_Loader.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <cctype>

namespace {
void printVec3(const char* label, const Vec3f& v) {
    std::cout << label << " = (" << v.x << ", " << v.y << ", " << v.z << ")" << std::endl;
}

const char* materialTypeName(MaterialType type) {
    switch (type) {
        case MaterialType::DIFFUSE: return "DIFFUSE";
        case MaterialType::METAL: return "METAL";
        case MaterialType::DIELECTRIC: return "DIELECTRIC";
        case MaterialType::EMISSIVE: return "EMISSIVE";
    }
    return "UNKNOWN";
}

const char* materialRoleName(MaterialRole role) {
    switch (role) {
        case MaterialRole::DEFAULT: return "DEFAULT";
        case MaterialRole::AIRCRAFT_BODY: return "AIRCRAFT_BODY";
        case MaterialRole::AIRCRAFT_PARTS: return "AIRCRAFT_PARTS";
        case MaterialRole::CANOPY_GLASS: return "CANOPY_GLASS";
        case MaterialRole::PROPELLER_AFTERIMAGE: return "PROPELLER_AFTERIMAGE";
        case MaterialRole::AIRCRAFT_METAL: return "AIRCRAFT_METAL";
    }
    return "UNKNOWN";
}

int materialRoleIndex(MaterialRole role) {
    switch (role) {
        case MaterialRole::CANOPY_GLASS: return 0;
        case MaterialRole::PROPELLER_AFTERIMAGE: return 1;
        case MaterialRole::AIRCRAFT_METAL: return 2;
        case MaterialRole::AIRCRAFT_BODY: return 3;
        case MaterialRole::AIRCRAFT_PARTS: return 4;
        case MaterialRole::DEFAULT: return 5;
    }
    return 5;
}

Vec3f materialRoleDebugColor(MaterialRole role) {
    switch (role) {
        case MaterialRole::CANOPY_GLASS: return Vec3f(0.0f, 0.8f, 1.0f);
        case MaterialRole::PROPELLER_AFTERIMAGE: return Vec3f(0.0f, 1.0f, 0.0f);
        case MaterialRole::AIRCRAFT_METAL: return Vec3f(1.0f, 0.0f, 1.0f);
        case MaterialRole::AIRCRAFT_BODY: return Vec3f(1.0f, 1.0f, 1.0f);
        case MaterialRole::AIRCRAFT_PARTS: return Vec3f(1.0f, 0.8f, 0.0f);
        case MaterialRole::DEFAULT: return Vec3f(1.0f, 0.0f, 0.0f);
    }
    return Vec3f(1.0f, 0.0f, 0.0f);
}

std::string lowercase(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

const std::string& mapNameOrNone(const std::string& name) {
    static const std::string none = "<none>";
    return name.empty() ? none : name;
}

float roughnessFromNs(float ns) {
    float roughness = std::sqrt(2.0f / (ns + 2.0f));
    return std::clamp(roughness, 0.02f, 1.0f);
}

float glossyWeightFromKs(const Vec3f& ks) {
    return std::clamp(ks.max_component() * 0.35f, 0.0f, 0.35f);
}

Vec3f sanitizeRadiance(const Vec3f& v) {
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) {
        return Vec3f(0.0f);
    }
    constexpr float maxRadiance = 100.0f;
    return Vec3f(std::clamp(v.x, 0.0f, maxRadiance),
                 std::clamp(v.y, 0.0f, maxRadiance),
                 std::clamp(v.z, 0.0f, maxRadiance));
}

float smoothstep01(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / std::max(edge1 - edge0, 1e-6f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Vec3f safeNormalizeScene(const Vec3f& v, const Vec3f& fallback = Vec3f(0, 1, 0)) {
    const float len2 = v.length2();
    if (len2 < 1e-12f || !std::isfinite(len2)) {
        return fallback;
    }
    return v / std::sqrt(len2);
}

Vec3f mixVec(const Vec3f& a, const Vec3f& b, float t) {
    return a * (1.0f - t) + b * t;
}

// ---------------------------------------------------------------------------
// Procedural fake cockpit interior pattern.
//
// Samples a dark instrument-panel / pilot-silhouette texture from the canopy
// hit UV coordinates.  All values are intentionally very dark (HDR < 0.05)
// so the glass tint and Fresnel reflection layer sit on top convincingly.
//
// texU / texV are the triangle-interpolated OBJ UV coordinates at the hit
// point (Intersection::texU, Intersection::texV).
// ---------------------------------------------------------------------------
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

Vec3f reflectDir(const Vec3f& incident, const Vec3f& normal) {
    return incident - 2.0f * dot(incident, normal) * normal;
}

[[maybe_unused]] Vec3f refractDir(const Vec3f& incident, const Vec3f& normal, float ior) {
    float cosi = std::clamp(dot(incident, normal), -1.0f, 1.0f);
    float etai = 1.0f;
    float etat = ior;
    Vec3f n = normal;

    if (cosi < 0.0f) {
        cosi = -cosi;
    } else {
        std::swap(etai, etat);
        n = -normal;
    }

    const float eta = etai / etat;
    const float k = 1.0f - eta * eta * (1.0f - cosi * cosi);
    if (k < 0.0f) {
        return Vec3f(0.0f);
    }

    return safeNormalizeScene(eta * incident + (eta * cosi - std::sqrt(k)) * n, Vec3f(0.0f));
}

float fresnelExact(const Vec3f& incident, const Vec3f& normal, float ior) {
    float cosi = std::clamp(dot(incident, normal), -1.0f, 1.0f);
    float etai = 1.0f;
    float etat = ior;

    if (cosi > 0.0f) {
        std::swap(etai, etat);
    }

    const float sint = etai / etat * std::sqrt(std::max(0.0f, 1.0f - cosi * cosi));
    if (sint >= 1.0f) {
        return 1.0f;
    }

    const float cost = std::sqrt(std::max(0.0f, 1.0f - sint * sint));
    cosi = std::fabs(cosi);

    const float rs = ((etat * cosi) - (etai * cost)) / ((etat * cosi) + (etai * cost));
    const float rp = ((etai * cosi) - (etat * cost)) / ((etai * cosi) + (etat * cost));
    return (rs * rs + rp * rp) * 0.5f;
}

[[maybe_unused]] float fresnelSchlick(float cosTheta, float f0) {
    return f0 + (1.0f - f0) * std::pow(1.0f - cosTheta, 5.0f);
}

std::string textureFileFromMtlMap(const std::string& mapName) {
    std::istringstream stream(mapName);
    std::string token;
    std::string lastToken;
    while (stream >> token) {
        lastToken = token;
    }
    return lastToken;
}

std::string resolveTexturePath(const std::string& basePath, const std::string& mapName) {
    const std::string fileName = textureFileFromMtlMap(mapName);
    std::string texPath = basePath + fileName;
    if (!std::filesystem::exists(texPath)) {
        texPath = basePath + "textures/" + fileName;
    }
    return texPath;
}

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

void applyMaterialNameOverride(Material& mat) {
    const std::string name = lowercase(mat.name);
    if (name == "canopy_glass") {
        mat.role = MaterialRole::CANOPY_GLASS;
        mat.type = MaterialType::DIFFUSE;
        mat.color = Vec3f(0.02f, 0.04f, 0.06f);
        mat.metallicBase = 0.0f;
        mat.metallic = 0.0f;
        mat.roughness = 0.015f;
        mat.ior = 1.52f;
        mat.fresnelF0 = 0.043f;
        mat.alpha = 1.0f;
        mat.glossyWeight = 0.0f;
        mat.specularBoost = 1.0f;
        mat.normalStrength = 0.0f;
        mat.texture = nullptr;
        mat.bumpTexture = nullptr;
        return;
    }

    if (name == "transparent") {
        mat.role = MaterialRole::PROPELLER_AFTERIMAGE;
        mat.type = MaterialType::DIFFUSE;
        mat.color = Vec3f(0.20f, 0.16f, 0.12f);
        mat.metallicBase = 0.0f;
        mat.metallic = 0.0f;
        mat.roughness = 0.60f;
        mat.alpha = 0.08f;
        mat.glossyWeight = 0.0f;
        mat.specularBoost = 0.4f;
        mat.normalStrength = 0.0f;
        mat.texture = nullptr;
        mat.bumpTexture = nullptr;
        return;
    }

    if (name == "metal") {
        mat.role = MaterialRole::AIRCRAFT_METAL;
        mat.type = MaterialType::METAL;
        mat.color = Vec3f(0.55f, 0.55f, 0.52f);
        mat.metallicBase = 1.0f;
        mat.metallic = 1.0f;
        mat.roughness = 0.32f;
        mat.aluminumReflectance = Vec3f(0.91f, 0.92f, 0.92f);
        mat.glossyWeight = 0.0f;
        mat.specularBoost = 1.0f;
        mat.normalStrength = 0.20f;
        return;
    }

    if (name == "01_-_default" || name == "main" || name == "main.001") {
        mat.role = MaterialRole::AIRCRAFT_BODY;
        mat.normalStrength = 0.45f;
        mat.metallicBase = 0.75f;
        mat.metallic = std::max(mat.metallic, mat.metallicBase);
        mat.roughness = std::clamp(mat.roughness, 0.28f, 0.45f);
        return;
    }

    if (name == "parts" || name == "parts.001") {
        mat.role = MaterialRole::AIRCRAFT_PARTS;
        mat.normalStrength = 0.45f;
        mat.metallicBase = 0.35f;
        mat.metallic = std::max(mat.metallic, mat.metallicBase);
        mat.roughness = std::clamp(mat.roughness, 0.35f, 0.60f);
    }
}

void printObjMaterialLog(const objl::Material& mtl, const Material& mat) {
    std::cout << "OBJ material '" << mtl.name << "': "
              << "Kd=(" << mtl.Kd.X << ", " << mtl.Kd.Y << ", " << mtl.Kd.Z << "), "
              << "Ks=(" << mtl.Ks.X << ", " << mtl.Ks.Y << ", " << mtl.Ks.Z << "), "
              << "originalNs=" << mtl.Ns << ", "
              << "roughnessFromNs=";
    if (mat.roughnessFromNs >= 0.0f) {
        std::cout << mat.roughnessFromNs;
    } else {
        std::cout << "<none>";
    }
    std::cout << ", "
              << "finalRoughness=" << mat.roughness << ", "
              << "jsonRoughnessOverride=" << (mat.usedJsonRoughnessOverride ? "yes" : "no") << ", "
              << "metallicBase=" << mat.metallicBase << ", "
              << "finalMetallic=" << mat.metallic << ", "
              << "jsonMetallicOverride=" << (mat.usedJsonMetallicOverride ? "yes" : "no") << ", "
              << "specularColor=(" << mat.specularColor.x << ", " << mat.specularColor.y << ", " << mat.specularColor.z << "), "
              << "glossyWeightBase=" << mat.glossyWeightBase << ", "
              << "finalGlossyWeight=" << mat.glossyWeight << ", "
              << "finalSpecularBoost=" << mat.specularBoost << ", "
              << "glossyWeightSource=" << (mat.usedJsonGlossyWeightOverride ? "JSON" : "Ks") << ", "
              << "illum=" << mtl.illum << ", "
              << "map_Kd=" << mapNameOrNone(mtl.map_Kd) << ", "
              << "map_Ks=" << mapNameOrNone(mtl.map_Ks) << ", "
              << "map_Ns=" << mapNameOrNone(mtl.map_Ns) << ", "
              << "map_bump=" << mapNameOrNone(mtl.map_bump) << std::endl;

    std::cout << "  converted Material: "
              << "type=" << materialTypeName(mat.type) << ", "
              << "color=(" << mat.color.x << ", " << mat.color.y << ", " << mat.color.z << "), "
              << "roughness=" << mat.roughness << ", "
              << "mtlNs=" << mat.mtlNs << ", "
              << "roughnessFromNs=" << mat.roughnessFromNs << ", "
              << "jsonRoughnessOverride=" << (mat.usedJsonRoughnessOverride ? "yes" : "no") << ", "
              << "metallicBase=" << mat.metallicBase << ", "
              << "metallic=" << mat.metallic << ", "
              << "jsonMetallicOverride=" << (mat.usedJsonMetallicOverride ? "yes" : "no") << ", "
              << "specularColor=(" << mat.specularColor.x << ", " << mat.specularColor.y << ", " << mat.specularColor.z << "), "
              << "glossyWeight=" << mat.glossyWeight << ", "
              << "specularBoost=" << mat.specularBoost << ", "
              << "glossyWeightSource=" << (mat.usedJsonGlossyWeightOverride ? "JSON" : "Ks") << ", "
              << "ior=" << mat.ior << ", "
              << "emission=(" << mat.emission.x << ", " << mat.emission.y << ", " << mat.emission.z << "), "
              << "diffuseTexture=" << ((mat.texture && mat.texture->isLoaded()) ? "loaded" : "none") << ", "
              << "bumpTexture=" << ((mat.bumpTexture && mat.bumpTexture->isLoaded()) ? "loaded" : "none")
              << std::endl;
}

void printObjectMaterialLog(const char* label, const Material& mat) {
    std::cout << label << " material: "
              << "type=" << materialTypeName(mat.type) << ", "
              << "specularColor=(" << mat.specularColor.x << ", " << mat.specularColor.y << ", " << mat.specularColor.z << "), "
              << "finalGlossyWeight=" << mat.glossyWeight << ", "
              << "finalSpecularBoost=" << mat.specularBoost << ", "
              << "glossyWeightSource=" << (mat.usedJsonGlossyWeightOverride ? "JSON" : "default/Ks") << ", "
              << "roughness=" << mat.roughness << ", "
              << "metallic=" << mat.metallic
              << std::endl;
}

Vec3f blenderToRendererPoint(const Vec3f& v) {
    return Vec3f(v.x, v.z, -v.y);
}

Vec3f blenderToRendererVector(const Vec3f& v) {
    return blenderToRendererPoint(v).normalized();
}

bool mistDiagnosticsEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("MINIPATH_MIST_DIAGNOSTICS");
        return value && std::string(value) != "0";
    }();
    return enabled;
}

// ---- Procedural cloud noise (Stage 4) ----

// Maps (x, y) to a pseudo-random float in [0, 1] via sin-fract hash.
float hash21(float x, float y) {
    float h = std::sin(x * 127.1f + y * 311.7f) * 43758.5453123f;
    return h - std::floor(h);
}

// Bilinear value noise with cubic (smoothstep) interpolation.
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

// Fractal Brownian Motion: 5 octaves, amplitude halved and frequency doubled each step.
// Result is roughly in [0, 1] and has multi-scale cloud structure.
float fbm2D(float x, float y) {
    float value = 0.f, amp = 0.5f, freq = 1.f;
    for (int i = 0; i < 5; ++i) {
        value += valueNoise2D(x * freq, y * freq) * amp;
        amp   *= 0.5f;
        freq  *= 2.0f;
    }
    return value;
}

// Cubic smoothstep remapping of x from [edge0, edge1] into [0, 1].
float smoothstepCloud(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ---- Volumetric mist helpers ----

// Ray/AABB slab intersection. Returns true if the ray intersects [boxMin, boxMax].
// tNear and tFar are the entry/exit parameters along the ray.
bool intersectAABBMist(const Ray& ray, const Vec3f& boxMin, const Vec3f& boxMax,
                       float& tNear, float& tFar) {
    float tmin = 0.0f, tmax = 1e30f;
    const float* ro = &ray.origin.x;
    const float* rd = &ray.direction.x;
    const float* blo = &boxMin.x;
    const float* bhi = &boxMax.x;
    for (int i = 0; i < 3; ++i) {
        if (std::abs(rd[i]) < 1e-8f) {
            if (ro[i] < blo[i] || ro[i] > bhi[i]) return false;
        } else {
            float t0 = (blo[i] - ro[i]) / rd[i];
            float t1 = (bhi[i] - ro[i]) / rd[i];
            if (t0 > t1) std::swap(t0, t1);
            tmin = std::max(tmin, t0);
            tmax = std::min(tmax, t1);
            if (tmin > tmax) return false;
        }
    }
    tNear = tmin;
    tFar  = tmax;
    return tFar > 0.0f;
}

float smoothMaxDensity(float a, float b, float k) {
    float h = std::clamp(0.5f + 0.5f * (a - b) / std::max(k, 1e-4f), 0.0f, 1.0f);
    return a * h + b * (1.0f - h) + k * h * (1.0f - h);
}

float mistBlob(const Vec3f& q, const Vec3f& center, const Vec3f& radius) {
    Vec3f d((q.x - center.x) / radius.x,
            (q.y - center.y) / radius.y,
            (q.z - center.z) / radius.z);
    float r = d.length();
    return 1.0f - smoothstepCloud(0.58f, 1.12f, r);
}

// Mist density at world position p for the given volume.
// Returns a non-negative density value (0 = transparent, >0 = participating media).
float mistDensity(const Vec3f& p, const SceneConfig::MistVolumeConfig& vol) {
    // Normalised position in [-1, 1] within the half-extents box.
    Vec3f q((p.x - vol.center.x) / vol.size.x,
            (p.y - vol.center.y) / vol.size.y,
            (p.z - vol.center.z) / vol.size.z);

    if (std::abs(q.x) > 1.0f || std::abs(q.y) > 1.0f || std::abs(q.z) > 1.0f) {
        return 0.0f;
    }

    float y01 = std::clamp((q.y + 1.0f) * 0.5f, 0.0f, 1.0f);

    Vec3f warpP = p * (vol.noiseScale * 0.70f) + vol.noiseOffset;
    float warpX = Noise::fbm(warpP.x + 13.2f, warpP.y * 0.20f, warpP.z - 4.7f, 4, 2.0f, 0.5f);
    float warpZ = Noise::fbm(warpP.x - 8.4f,  warpP.y * 0.20f, warpP.z + 17.9f, 4, 2.0f, 0.5f);
    Vec3f wq(q.x + warpX * 0.09f, q.y, q.z + warpZ * 0.16f);

    float b1 = mistBlob(wq, Vec3f(-0.38f, -0.58f, -0.08f), Vec3f(0.54f, 0.48f, 0.86f));
    float b2 = mistBlob(wq, Vec3f( 0.10f, -0.44f,  0.14f), Vec3f(0.62f, 0.42f, 0.72f));
    float b3 = mistBlob(wq, Vec3f( 0.42f, -0.62f, -0.22f), Vec3f(0.44f, 0.36f, 0.62f));
    float base = smoothMaxDensity(b1, b2, 0.20f);
    base = smoothMaxDensity(base, b3, 0.16f);
    base = std::pow(std::clamp(base, 0.0f, 1.0f), std::max(0.55f, vol.softness));

    float bottomHeavy = std::pow(std::max(0.0f, 1.0f - y01), std::max(0.3f, vol.heightFalloff));
    float topFade = 1.0f - smoothstepCloud(0.42f, 0.86f, y01);
    float bottomFade = smoothstepCloud(0.01f, 0.12f, y01);
    float heightMask = bottomHeavy * topFade * bottomFade;

    float sideFadeX = 1.0f - smoothstepCloud(0.68f, 1.02f, std::abs(q.x));
    float sideFadeZ = 1.0f - smoothstepCloud(0.66f, 1.02f, std::abs(q.z));
    float sideFade = sideFadeX * sideFadeZ;

    Vec3f largeP = p * (vol.noiseScale * 0.48f) + vol.noiseOffset;
    largeP.x += 0.11f * std::sin(p.z * 0.14f + vol.noiseOffset.x);
    largeP.z += 0.09f * std::sin(p.x * 0.11f + vol.noiseOffset.z);
    Vec3f medP  = p * (vol.noiseScale * 1.25f) + vol.noiseOffset * 1.73f + Vec3f(5.3f, 0.0f, -2.1f);
    Vec3f fineP = p * (vol.noiseScale * 2.70f) + vol.noiseOffset * 2.41f + Vec3f(-8.0f, 3.5f, 11.0f);

    float largeN = 0.5f + 0.5f * Noise::fbm(largeP.x, largeP.y * 0.35f, largeP.z, 5, 2.0f, 0.5f);
    float medN   = 0.5f + 0.5f * Noise::fbm(medP.x,   medP.y * 0.45f,   medP.z,   4, 2.0f, 0.5f);
    float fineN  = 0.5f + 0.5f * Noise::fbm(fineP.x,  fineP.y,          fineP.z,  3, 2.0f, 0.5f);

    float edge = 1.0f - std::clamp(base * sideFade, 0.0f, 1.0f);
    float breakNoise = largeN * 0.58f + medN * 0.32f + fineN * 0.10f - edge * 0.18f;
    float brokenMask = smoothstepCloud(vol.coverage,
                                       vol.coverage + std::max(0.04f, vol.edgeSoftness),
                                       breakNoise);
    float additiveDetail = (medN - 0.50f) * 0.16f + (fineN - 0.50f) * 0.07f;

    float density = base * sideFade * heightMask * brokenMask + additiveDetail * base * heightMask;
    return vol.density * std::clamp(density, 0.0f, 1.0f);
}
}

void Scene::loadFromConfig(const SceneConfig& cfg) {
    config = cfg;
    resetMistDiagnostics();
    resetMaterialRoleDiagnostics();
    triangles.clear();
    materials.clear();
    materialMap.clear();
    textures.clear();
    hasAreaLight = false;
    sceneBounds = AABB{};

    std::cout << "Active material debug/render config:" << std::endl;
    std::cout << "  canopyGlassUsePhysical=" << (config.canopyGlassUsePhysical ? "true" : "false")
              << ", canopyGlassIOR=" << config.canopyGlassIOR
              << ", canopyGlassF0=" << config.canopyGlassF0
              << ", canopyGlassReflectionBoost=" << config.canopyGlassReflectionBoost
              << ", canopyGlassCenterDarkness=" << config.canopyGlassCenterDarkness
              << std::endl;
    std::cout << "  canopyFresnelBase=" << config.canopyFresnelBase
              << ", canopyFresnelPower=" << config.canopyFresnelPower
              << ", canopyGlintStrength=" << config.canopyGlintStrength
              << ", canopyBrightnessBoost=" << config.canopyBrightnessBoost
              << ", canopyInteriorDarkness=" << config.canopyInteriorDarkness
              << std::endl;
    std::cout << "  canopyGlassCoolReflectionFactor=("
              << config.canopyGlassCoolReflectionFactor.x << ", "
              << config.canopyGlassCoolReflectionFactor.y << ", "
              << config.canopyGlassCoolReflectionFactor.z << "), "
              << "canopyGlassTransmissionTint=("
              << config.canopyGlassTransmissionTint.x << ", "
              << config.canopyGlassTransmissionTint.y << ", "
              << config.canopyGlassTransmissionTint.z << ")" << std::endl;
    std::cout << "  canopyCoolReflection=("
              << config.canopyCoolReflection.x << ", "
              << config.canopyCoolReflection.y << ", "
              << config.canopyCoolReflection.z << "), "
              << "canopySmokedCenter=("
              << config.canopySmokedCenter.x << ", "
              << config.canopySmokedCenter.y << ", "
              << config.canopySmokedCenter.z << ")" << std::endl;
    std::cout << "  propellerAfterimageAlpha=" << config.propellerAfterimageAlpha
              << ", propellerAfterimageColor=("
              << config.propellerAfterimageColor.x << ", "
              << config.propellerAfterimageColor.y << ", "
              << config.propellerAfterimageColor.z << "), "
              << "propellerAfterimageStackReduction="
              << config.propellerAfterimageStackReduction << std::endl;
    std::cout << "  debugMaterialRoles=" << (config.debugMaterialRoles ? "true" : "false")
              << ", debugCanopyExtreme=" << (config.debugCanopyExtreme ? "true" : "false")
              << ", debugPropellerAfterimageExtreme="
              << (config.debugPropellerAfterimageExtreme ? "true" : "false")
              << ", debugFakeCockpitPattern="
              << (config.debugFakeCockpitPattern ? "true" : "false")
              << std::endl;

    for (auto& entry : config.objects) {
        if (entry.type == "obj") {
            loadOBJ(entry);
        } else if (entry.type == "plane") {
            addPlane(entry);
        }
    }

    for (const auto& tri : triangles) {
        sceneBounds.expand(tri);
    }

    if (!triangles.empty()) {
        printVec3("OBJ bounding box min", sceneBounds.min_p);
        printVec3("OBJ bounding box max", sceneBounds.max_p);
        printVec3("OBJ bounding box center", sceneBounds.centroid());
        printVec3("OBJ bounding box size", sceneBounds.extent());
    }

    bool hasGlossyDiffuse = false;
    for (const auto& mat : materials) {
        if (mat.type == MaterialType::DIFFUSE && mat.glossyWeight > 0.0f) {
            hasGlossyDiffuse = true;
            break;
        }
    }
    if (hasGlossyDiffuse) {
        std::cout << "clearcoat enabled for glossy DIFFUSE materials" << std::endl;
    }

    if (config.hasAreaLight) {
        createAreaLight();
    }

    buildBVH();
    ocean = std::make_unique<Ocean>(256, 150.0f, 12.0f, Vec3f(1, 0, 0.5f).normalized(), 1.5f, 5.0f);
    ocean->generate();
    oceanRipple = std::make_unique<Ocean>(128, 12.0f, 4.5f, Vec3f(0.8f, 0, 0.6f).normalized(), 0.14f, 8.0f);
    oceanRipple->generate();
    std::cout << "Ocean FFT generated (large swell L=150m + ripple L=12m)." << std::endl;
    std::cout << "Scene loaded: " << triangles.size() << " triangles, "
              << materials.size() << " materials" << std::endl;
}

void Scene::loadOBJ(const SceneConfig::ObjectEntry& entry) {
    objl::Loader loader;
    if (!loader.LoadFile(entry.file)) {
        std::cerr << "Failed to load OBJ: " << entry.file << std::endl;
        return;
    }

    std::string basePath = "";
    auto lastSlash = entry.file.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        basePath = entry.file.substr(0, lastSlash + 1);
    }

    std::set<std::string> usedMaterialNames;
    for (const auto& mesh : loader.LoadedMeshes) {
        if (!mesh.MeshMaterial.name.empty()) {
            usedMaterialNames.insert(mesh.MeshMaterial.name);
        }
    }
        std::cout << "OBJ uses material names:";
    if (usedMaterialNames.empty()) {
        std::cout << " <none>";
    } else {
        for (const auto& name : usedMaterialNames) {
            std::cout << " " << name;
        }
    }
    std::cout << std::endl;

    std::map<std::string, int> faceCountByMaterial;
    std::cout << "OBJ mesh/material face mapping:" << std::endl;
    for (const auto& mesh : loader.LoadedMeshes) {
        const std::string matName = mesh.MeshMaterial.name.empty() ? "<none>" : mesh.MeshMaterial.name;
        const int faces = static_cast<int>(mesh.Vertices.size() / 3);
        faceCountByMaterial[matName] += faces;
        std::cout << "  mesh='" << mesh.MeshName << "', material='" << matName
                  << "', faces=" << faces << std::endl;
    }
    std::cout << "OBJ face counts by material:" << std::endl;
    for (const auto& [matName, faces] : faceCountByMaterial) {
        std::cout << "  material='" << matName << "', faces=" << faces << std::endl;
    }

    // Load materials from MTL
    for (auto& mtl : loader.LoadedMaterials) {
        Material mat;
        mat.name = mtl.name;
        mat.color = Vec3f(mtl.Kd.X, mtl.Kd.Y, mtl.Kd.Z);
        mat.mtlNs = mtl.Ns;
        mat.specularColor = Vec3f(mtl.Ks.X, mtl.Ks.Y, mtl.Ks.Z);
        mat.glossyWeightBase = glossyWeightFromKs(mat.specularColor);
        mat.glossyWeight = mat.glossyWeightBase;

        // Auto-detect material type from MTL fields
        if (entry.materialType == "metal" ||
            (entry.materialType.empty() && mtl.illum >= 3 && mtl.Ks.X + mtl.Ks.Y + mtl.Ks.Z > 0.5f)) {
            mat.type = MaterialType::METAL;
            mat.metallicBase = 1.0f;
            mat.metallic = mat.metallicBase;
        } else if (entry.materialType == "glass" ||
                   (entry.materialType.empty() && mtl.d < 0.9f && mtl.illum >= 4)) {
            mat.type = MaterialType::DIELECTRIC;
            mat.ior = mtl.Ni > 0.1f ? mtl.Ni : 1.5f;
        } else if (entry.materialType == "emissive") {
            mat.type = MaterialType::EMISSIVE;
            mat.emission = mat.color * 10.0f;
        } else {
            mat.type = MaterialType::DIFFUSE;
        }

        if (std::isfinite(mtl.Ns) && mtl.Ns > 0.0f &&
            (mat.type == MaterialType::DIFFUSE || mat.type == MaterialType::METAL)) {
            mat.roughnessFromNs = roughnessFromNs(mtl.Ns);
            mat.roughness = mat.roughnessFromNs;
        }

        // JSON overrides
        if (entry.materialColor.x >= 0) mat.color = entry.materialColor;
        if (entry.roughness >= 0) {
            mat.roughness = entry.roughness;
            mat.usedJsonRoughnessOverride = true;
        }
        if (entry.metallic >= 0) {
            mat.metallic = std::clamp(entry.metallic, 0.0f, 1.0f);
            mat.usedJsonMetallicOverride = true;
        }
        if (entry.glossyWeight >= 0) {
            mat.glossyWeight = std::clamp(entry.glossyWeight, 0.0f, 0.8f);
            mat.usedJsonGlossyWeightOverride = true;
        }
        mat.specularBoost = std::clamp(entry.specularBoost, 0.0f, 4.0f);
        if (entry.ior >= 0) mat.ior = entry.ior;

        std::string diffuseTexturePath = "<none>";
        std::string bumpTexturePath = "<none>";
        bool diffuseTextureLoaded = false;
        bool bumpTextureLoaded = false;

        // Load diffuse texture
        if (!mtl.map_Kd.empty()) {
            auto tex = std::make_unique<Texture>();
            std::string texPath = resolveTexturePath(basePath, mtl.map_Kd);
            diffuseTexturePath = texPath;
            if (tex->load(texPath)) {
                std::cout << "Loaded diffuse texture: " << texPath << std::endl;
                mat.texture = tex.get();
                textures.push_back(std::move(tex));
                diffuseTextureLoaded = true;
            } else {
                std::cerr << "Failed to load diffuse texture for material "
                          << mtl.name << ": " << texPath << std::endl;
            }
        }

        // Load tangent-space bump/normal texture. The local OBJ loader stores map_Bump,
        // map_bump, bump, norm, and normal declarations in map_bump.
        if (!mtl.map_bump.empty()) {
            auto tex = std::make_unique<Texture>();
            std::string texPath = resolveTexturePath(basePath, mtl.map_bump);
            bumpTexturePath = texPath;
            if (tex->load(texPath)) {
                std::cout << "Loaded bump/normal texture: " << texPath << std::endl;
                mat.bumpTexture = tex.get();
                textures.push_back(std::move(tex));
                bumpTextureLoaded = true;
            } else {
                std::cerr << "Failed to load bump/normal texture for material "
                          << mtl.name << ": " << texPath << std::endl;
            }
        }

        applyMaterialNameOverride(mat);
        if (mat.role == MaterialRole::CANOPY_GLASS) {
            mat.ior = config.canopyGlassIOR;
            mat.fresnelF0 = config.canopyGlassF0;
        } else if (mat.role == MaterialRole::PROPELLER_AFTERIMAGE) {
            mat.alpha = std::clamp(config.propellerAfterimageAlpha, 0.0f, 0.5f);
        }

        std::cout << "Material diagnostics: "
                  << "name='" << mtl.name << "', "
                  << "diffusePath=" << diffuseTexturePath << ", "
                  << "diffuseLoaded=" << (diffuseTextureLoaded ? "yes" : "no") << ", "
                  << "bumpNormalPath=" << bumpTexturePath << ", "
                  << "bumpNormalLoaded=" << (bumpTextureLoaded ? "yes" : "no") << ", "
                  << "finalType=" << materialTypeName(mat.type) << ", "
                  << "role=" << materialRoleName(mat.role) << ", "
                  << "baseColor=(" << mat.color.x << ", " << mat.color.y << ", " << mat.color.z << "), "
                  << "alpha=" << mat.alpha << ", "
                  << "F0=" << mat.fresnelF0 << ", "
                  << "IOR=" << mat.ior << ", "
                  << "metallic=" << mat.metallic << ", "
                  << "roughness=" << mat.roughness << ", "
                  << "normalStrength="
                  << (mat.normalStrength >= 0.0f ? std::to_string(mat.normalStrength) : std::string("scene"))
                  << ", aluminumReflectance=(" << mat.aluminumReflectance.x << ", "
                  << mat.aluminumReflectance.y << ", " << mat.aluminumReflectance.z << ")";
        if (mat.role == MaterialRole::CANOPY_GLASS) {
            std::cout << ", physicalGlass=" << (config.canopyGlassUsePhysical ? "true" : "false")
                      << ", shader=" << (config.canopyGlassUsePhysical
                          ? "physical polished canopy glass"
                          : "cinematic fake smoked glass");
        } else if (mat.role == MaterialRole::PROPELLER_AFTERIMAGE) {
            std::cout << ", shader=propeller afterimage alpha ray continuation";
        } else if (mat.role == MaterialRole::AIRCRAFT_METAL) {
            std::cout << ", shader=aluminium reflection";
        }
        std::cout
                  << std::endl;

        printObjMaterialLog(mtl, mat);

        materialMap[mtl.name] = (int)materials.size();
        materials.push_back(mat);
    }

    // If no materials loaded, add a default
    if (materials.empty() || loader.LoadedMaterials.empty()) {
        Material defaultMat;
        defaultMat.color = Vec3f(0.8f);

        if (entry.materialType == "metal") defaultMat.type = MaterialType::METAL;
        else if (entry.materialType == "glass") defaultMat.type = MaterialType::DIELECTRIC;
        else if (entry.materialType == "emissive") defaultMat.type = MaterialType::EMISSIVE;
        if (defaultMat.type == MaterialType::METAL) {
            defaultMat.metallicBase = 1.0f;
            defaultMat.metallic = defaultMat.metallicBase;
        }

        if (entry.materialColor.x >= 0) defaultMat.color = entry.materialColor;
        if (entry.roughness >= 0) defaultMat.roughness = entry.roughness;
        if (entry.metallic >= 0) {
            defaultMat.metallic = std::clamp(entry.metallic, 0.0f, 1.0f);
            defaultMat.usedJsonMetallicOverride = true;
        }
        if (entry.glossyWeight >= 0) {
            defaultMat.glossyWeight = std::clamp(entry.glossyWeight, 0.0f, 0.8f);
            defaultMat.usedJsonGlossyWeightOverride = true;
        }
        defaultMat.specularBoost = std::clamp(entry.specularBoost, 0.0f, 4.0f);
        if (entry.ior >= 0) defaultMat.ior = entry.ior;

        materialMap["_default"] = (int)materials.size();
        materials.push_back(defaultMat);
    }

    // Convert meshes to triangles
    for (auto& mesh : loader.LoadedMeshes) {
        int matId = 0;
        if (!mesh.MeshMaterial.name.empty()) {
            auto it = materialMap.find(mesh.MeshMaterial.name);
            if (it != materialMap.end()) matId = it->second;
        }

        for (size_t i = 0; i + 2 < mesh.Vertices.size(); i += 3) {
            Triangle tri;
            for (int j = 0; j < 3; ++j) {
                auto& v = mesh.Vertices[i + j];
                Vec3f pos(v.Position.X, v.Position.Y, v.Position.Z);
                if (entry.convertFromBlender) {
                    pos = blenderToRendererPoint(pos);
                }
                // Apply transform: scale -> rotate (TODO) -> translate
                pos = pos * entry.scale + entry.position;

                Vec3f nor(v.Normal.X, v.Normal.Y, v.Normal.Z);
                if (entry.convertFromBlender) {
                    nor = blenderToRendererVector(nor);
                }

                if (j == 0) {
                    tri.v0 = pos; tri.n0 = nor;
                    tri.u0 = v.TextureCoordinate.X; tri.v0t = v.TextureCoordinate.Y;
                } else if (j == 1) {
                    tri.v1 = pos; tri.n1 = nor;
                    tri.u1 = v.TextureCoordinate.X; tri.v1t = v.TextureCoordinate.Y;
                } else {
                    tri.v2 = pos; tri.n2 = nor;
                    tri.u2 = v.TextureCoordinate.X; tri.v2t = v.TextureCoordinate.Y;
                }
            }
            tri.materialId = matId;
            computeTriangleTangents(tri);
            triangles.push_back(tri);
        }
    }
}

void Scene::addPlane(const SceneConfig::ObjectEntry& entry) {
    // Create material for plane
    Material mat;
    if (entry.materialType == "glass" || entry.materialType == "dielectric") {
        mat.type = MaterialType::DIELECTRIC;
        mat.ior = entry.ior >= 0 ? entry.ior : 1.33f; // water default
        mat.color = entry.materialColor.x >= 0 ? entry.materialColor : Vec3f(0.1f, 0.2f, 0.4f);
    } else if (entry.materialType == "metal") {
        mat.type = MaterialType::METAL;
        mat.metallicBase = 1.0f;
        mat.metallic = mat.metallicBase;
        mat.roughness = entry.roughness >= 0 ? entry.roughness : 0.1f;
        mat.color = entry.materialColor.x >= 0 ? entry.materialColor : Vec3f(0.8f);
    } else {
        mat.type = MaterialType::DIFFUSE;
        mat.color = entry.materialColor.x >= 0 ? entry.materialColor : Vec3f(0.5f);
    }
    if (entry.metallic >= 0) {
        mat.metallic = std::clamp(entry.metallic, 0.0f, 1.0f);
        mat.usedJsonMetallicOverride = true;
    }
    if (entry.glossyWeight >= 0) {
        mat.glossyWeight = std::clamp(entry.glossyWeight, 0.0f, 0.8f);
        mat.usedJsonGlossyWeightOverride = true;
    }
    mat.specularBoost = std::clamp(entry.specularBoost, 0.0f, 4.0f);

    printObjectMaterialLog("Plane", mat);

    int matId = (int)materials.size();
    materials.push_back(mat);

    float size = entry.scale > 0 ? entry.scale : 10.0f;
    float y = entry.position.y;
    Vec3f p = entry.position;
    Vec3f normal(0, 1, 0);

    // Two triangles forming a quad
    Triangle t1;
    t1.v0 = Vec3f(p.x - size, y, p.z - size);
    t1.v1 = Vec3f(p.x + size, y, p.z - size);
    t1.v2 = Vec3f(p.x + size, y, p.z + size);
    t1.n0 = t1.n1 = t1.n2 = normal;
    t1.u0 = 0; t1.v0t = 0; t1.u1 = 1; t1.v1t = 0; t1.u2 = 1; t1.v2t = 1;
    t1.materialId = matId;

    Triangle t2;
    t2.v0 = Vec3f(p.x - size, y, p.z - size);
    t2.v1 = Vec3f(p.x + size, y, p.z + size);
    t2.v2 = Vec3f(p.x - size, y, p.z + size);
    t2.n0 = t2.n1 = t2.n2 = normal;
    t2.u0 = 0; t2.v0t = 0; t2.u1 = 1; t2.v1t = 1; t2.u2 = 0; t2.v2t = 1;
    t2.materialId = matId;

    triangles.push_back(t1);
    triangles.push_back(t2);
}

void Scene::buildBVH() {
    if (triangles.empty()) return;
    bvh.build(triangles);
}

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

void Scene::resetMistDiagnostics() const {
    std::lock_guard<std::mutex> lock(mistDiagnosticsMutex);
    mistDiagnostics = MistDiagnostics{};
}

void Scene::resetMaterialRoleDiagnostics() const {
    for (auto& counter : primaryRoleHits) {
        counter.store(0, std::memory_order_relaxed);
    }
}

void Scene::printMaterialRoleDiagnostics() const {
    std::cout << "Primary hit counts:" << std::endl;
    std::cout << "  CANOPY_GLASS: "
              << primaryRoleHits[materialRoleIndex(MaterialRole::CANOPY_GLASS)].load(std::memory_order_relaxed)
              << std::endl;
    std::cout << "  PROPELLER_AFTERIMAGE: "
              << primaryRoleHits[materialRoleIndex(MaterialRole::PROPELLER_AFTERIMAGE)].load(std::memory_order_relaxed)
              << std::endl;
    std::cout << "  AIRCRAFT_METAL: "
              << primaryRoleHits[materialRoleIndex(MaterialRole::AIRCRAFT_METAL)].load(std::memory_order_relaxed)
              << std::endl;
    std::cout << "  AIRCRAFT_BODY: "
              << primaryRoleHits[materialRoleIndex(MaterialRole::AIRCRAFT_BODY)].load(std::memory_order_relaxed)
              << std::endl;
    std::cout << "  AIRCRAFT_PARTS: "
              << primaryRoleHits[materialRoleIndex(MaterialRole::AIRCRAFT_PARTS)].load(std::memory_order_relaxed)
              << std::endl;
    std::cout << "  DEFAULT: "
              << primaryRoleHits[materialRoleIndex(MaterialRole::DEFAULT)].load(std::memory_order_relaxed)
              << std::endl;
}

void Scene::recordMistDiagnostics(bool leftAabbHit, bool rightAabbHit,
                                  float alpha, const Vec3f& mistColor,
                                  const Vec3f& before, const Vec3f& after) const {
    if (!mistDiagnosticsEnabled()) return;

    float delta = std::max({std::fabs(after.x - before.x),
                            std::fabs(after.y - before.y),
                            std::fabs(after.z - before.z)});

    std::lock_guard<std::mutex> lock(mistDiagnosticsMutex);
    mistDiagnostics.compositeCalls++;
    if (leftAabbHit) mistDiagnostics.leftAabbHits++;
    if (rightAabbHit) mistDiagnostics.rightAabbHits++;
    if (delta > 1.0f / 255.0f) mistDiagnostics.changedPixels++;
    mistDiagnostics.alphaSum += alpha;
    mistDiagnostics.maxAlpha = std::max(mistDiagnostics.maxAlpha, (double)alpha);
    mistDiagnostics.mistColorSum += mistColor;
}

void Scene::printMistDiagnostics() const {
    if (!mistDiagnosticsEnabled()) return;

    MistDiagnostics stats;
    {
        std::lock_guard<std::mutex> lock(mistDiagnosticsMutex);
        stats = mistDiagnostics;
    }

    double calls = std::max<uint64_t>(stats.compositeCalls, 1);
    Vec3f avgMistColor = stats.mistColorSum / (float)calls;
    double changedPct = 100.0 * (double)stats.changedPixels / calls;

    std::cout << "Mist diagnostics:" << std::endl;
    std::cout << "  compositeMist primary calls = " << stats.compositeCalls << std::endl;
    std::cout << "  leftMist AABB hits = " << stats.leftAabbHits << std::endl;
    std::cout << "  rightMist AABB hits = " << stats.rightAabbHits << std::endl;
    std::cout << "  average accumulated alpha = " << (stats.alphaSum / calls) << std::endl;
    std::cout << "  max accumulated alpha = " << stats.maxAlpha << std::endl;
    std::cout << "  average mist RGB contribution = ("
              << avgMistColor.x << ", " << avgMistColor.y << ", " << avgMistColor.z << ")" << std::endl;
    std::cout << "  pixels changed by > 1/255 HDR = "
              << stats.changedPixels << " (" << changedPct << "%)" << std::endl;
}

bool Scene::tracePrimary(const Ray& ray, Intersection& isect) const {
    isect = bvh.intersect(ray);
    return isect.hit;
}

float Scene::mistAlphaToHit(const Ray& ray, float hitT) const {
    const SceneConfig::MistVolumeConfig* vols[2] = {&config.leftMist, &config.rightMist};
    float totalTrans = 1.0f;
    for (auto* vol : vols) {
        if (!vol->enabled) continue;
        MistSample ms = renderMistVolume(ray, *vol, hitT);
        totalTrans *= ms.transmittance;
    }
    return 1.0f - totalTrans;
}

Vec3f Scene::materialDebugColor(const Material& mat, const Vec3f& normal,
                                float texU, float texV) const {
    const std::string& mode = config.materialDebug;

    if (mode == "debugBaseColor") {
        return mat.getColor(texU, texV);
    }

    if (mode == "debugRoughness") {
        float r = std::clamp(mat.roughness, 0.0f, 1.0f);
        return Vec3f(r);
    }

    if (mode == "debugMetallic") {
        float metallic = std::clamp(mat.metallic, 0.0f, 1.0f);
        return Vec3f(metallic);
    }

    if (mode == "debugGlossyWeight") {
        float glossy = std::clamp(mat.glossyWeight, 0.0f, 1.0f);
        return Vec3f(glossy);
    }

    if (mode == "debugNormal") {
        Vec3f n = normal.normalized();
        return Vec3f(n.x * 0.5f + 0.5f, n.y * 0.5f + 0.5f, n.z * 0.5f + 0.5f);
    }

    if (mode == "debugMaterialType") {
        switch (mat.type) {
            case MaterialType::DIFFUSE: return Vec3f(0.45f, 0.8f, 0.45f);
            case MaterialType::METAL: return Vec3f(0.35f, 0.55f, 1.0f);
            case MaterialType::DIELECTRIC: return Vec3f(0.35f, 0.95f, 1.0f);
            case MaterialType::EMISSIVE: return Vec3f(1.0f, 0.85f, 0.2f);
        }
    }

    return Vec3f(1.0f, 0.0f, 1.0f);
}

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

// ---------------------------------------------------------------------------
// Volumetric mist rendering
// ---------------------------------------------------------------------------

Scene::MistSample Scene::renderMistVolume(const Ray& ray,
                                          const SceneConfig::MistVolumeConfig& vol,
                                          float maxT) const
{
    Vec3f boxMin = vol.center - vol.size;
    Vec3f boxMax = vol.center + vol.size;

    float tNear, tFar;
    if (!intersectAABBMist(ray, boxMin, boxMax, tNear, tFar))
        return {Vec3f(0), 1.0f};

    tNear = std::max(tNear, 0.001f);
    tFar  = std::min(tFar,  maxT);
    if (tNear >= tFar) return {Vec3f(0), 1.0f};

    int steps     = std::max(2, vol.marchSteps);
    float stepSize = (tFar - tNear) / steps;

    // Cheap blue-noise-style jitter to break up banding
    float jitter = hash21(ray.direction.x * 1731.9f, ray.direction.z * 4319.7f);

    Vec3f sunDir = config.sky.sunDirection.normalized();

    Vec3f accColor(0);
    float transmittance = 1.0f;

    for (int i = 0; i < steps; ++i) {
        float t = tNear + (i + jitter) * stepSize;
        if (t >= tFar || transmittance < 0.002f) break;

        Vec3f p = ray.at(t);
        float d = mistDensity(p, vol);
        if (d <= 0.0f) continue;

        float extinction    = d * vol.absorption * stepSize;
        float sampleTrans   = std::exp(-extinction);

        // Shadow march toward the sun — accumulate density to approximate self-shadow
        float shadowD = 0.0f;
        {
            int sSteps = std::max(2, vol.shadowSteps);
            Ray sunRay(p + sunDir * 0.02f, sunDir);
            float sTNear, sTFar;
            if (intersectAABBMist(sunRay, boxMin, boxMax, sTNear, sTFar) && sTFar > 0.0f) {
                float sStep = sTFar / sSteps;
                for (int j = 0; j < sSteps; ++j)
                    shadowD += mistDensity(p + sunDir * ((j + 0.5f) * sStep), vol)
                               * vol.absorption * sStep;
            }
        }
        float sunTrans = std::exp(-shadowD);

        // Single-scattering: warm sun rim + cool ambient shadow interior
        // Simple cosine-weighted phase (slightly forward-biased)
        float cosTheta = std::max(0.0f, dot(-ray.direction.normalized(), sunDir));
        float phase    = 0.25f + 0.75f * cosTheta * cosTheta;

        Vec3f warmContrib = vol.warmSunColor * (sunTrans * vol.sunRimStrength * phase);
        Vec3f coolContrib = vol.coolAmbientColor * (1.0f - sunTrans * 0.7f);
        Vec3f scatter     = (warmContrib + coolContrib) * vol.scatteringStrength;

        // Beer-Lambert front-to-back accumulation
        float dT  = transmittance * (1.0f - sampleTrans);
        accColor      += scatter * dT;
        transmittance *= sampleTrans;
    }

    // Enforce maxAlpha ceiling — prevents the mist from being too opaque
    float alpha = 1.0f - transmittance;
    if (alpha > vol.maxAlpha && alpha > 1e-6f) {
        float scale   = vol.maxAlpha / alpha;
        accColor      *= scale;
        transmittance  = 1.0f - vol.maxAlpha;
    }

    return {accColor, transmittance};
}

// Composite both mist volumes over the given sceneColor.
// hitT is the nearest scene surface distance (1e30 for sky misses).
// Debug render modes are handled here so castRay stays clean.
Vec3f Scene::compositeMist(const Ray& ray, float hitT, Vec3f sceneColor) const {
    const SceneConfig::MistVolumeConfig* vols[2] = {&config.leftMist, &config.rightMist};
    bool aabbHits[2] = {false, false};
    for (int idx = 0; idx < 2; ++idx) {
        const auto* vol = vols[idx];
        if (!vol->enabled) continue;
        Vec3f boxMin = vol->center - vol->size;
        Vec3f boxMax = vol->center + vol->size;
        float tN, tF;
        if (intersectAABBMist(ray, boxMin, boxMax, tN, tF)) {
            tN = std::max(tN, 0.001f);
            tF = std::min(tF, hitT);
            aabbHits[idx] = tN < tF;
        }
    }

    if (config.renderMode == "mistBoundsOnly") {
        Vec3f c(0.0f);
        if (aabbHits[0]) c += Vec3f(0.15f, 0.75f, 1.0f);
        if (aabbHits[1]) c += Vec3f(1.0f, 0.35f, 0.95f);
        return c;
    }

    // --- Debug: raw density integrated along ray ---
    if (config.renderMode == "mistDensityOnly") {
        float total = 0.0f;
        for (auto* vol : vols) {
            if (!vol->enabled) continue;
            Vec3f boxMin = vol->center - vol->size;
            Vec3f boxMax = vol->center + vol->size;
            float tN, tF;
            if (!intersectAABBMist(ray, boxMin, boxMax, tN, tF)) continue;
            tN = std::max(tN, 0.001f); tF = std::min(tF, hitT);
            if (tN >= tF) continue;
            int steps = std::max(4, vol->marchSteps);
            float ss  = (tF - tN) / steps;
            for (int i = 0; i < steps; ++i)
                total += mistDensity(ray.at(tN + (i + 0.5f) * ss), *vol) * ss;
        }
        return Vec3f(std::min(total * 2.5f, 1.0f));
    }

    // --- Normal composite (also handles alpha/lighting debug modes) ---
    Vec3f color = sceneColor;
    float totalTrans = 1.0f;
    Vec3f totalMistColor(0);

    for (auto* vol : vols) {
        if (!vol->enabled) continue;
        MistSample ms = renderMistVolume(ray, *vol, hitT);
        totalMistColor += ms.color;
        totalTrans     *= ms.transmittance;
        color = color * ms.transmittance + ms.color;
    }

    if (config.renderMode == "mistAlphaOnly")   return Vec3f(1.0f - totalTrans);
    if (config.renderMode == "mistLightingOnly") return totalMistColor;

    recordMistDiagnostics(aabbHits[0], aabbHits[1], 1.0f - totalTrans,
                          totalMistColor, sceneColor, color);
    return color;
}

Vec3f Scene::shadeCanopyGlassPhysical(const Ray& ray, const Intersection& isect,
                                      const Material& mat, const Vec3f& N, int depth) const {
    if (config.debugCanopyExtreme) {
        Vec3f r(0.0f, 0.0f, 5.0f);
        return depth == 0 ? compositeMist(ray, isect.t, r) : r;
    }
    if (config.debugFakeCockpitPattern) {
        Vec3f r = sanitizeRadiance(fakeCockpitPattern(isect.texU, isect.texV) * 8.0f);
        return depth == 0 ? compositeMist(ray, isect.t, r) : r;
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
    return depth == 0 ? compositeMist(ray, isect.t, r) : r;
}

Vec3f Scene::shadeCanopyGlassFake(const Ray& ray, const Intersection& isect,
                                  const Material& mat, const Vec3f& N, int depth) const {
    (void)mat;

    // ----- Debug overrides -----
    if (config.debugCanopyExtreme) {
        Vec3f r(0.0f, 1.0f, 1.0f);
        return depth == 0 ? compositeMist(ray, isect.t, r) : r;
    }
    // Visualise the raw cockpit pattern (8× brightened so dark values are visible).
    if (config.debugFakeCockpitPattern) {
        Vec3f r = sanitizeRadiance(fakeCockpitPattern(isect.texU, isect.texV) * 8.0f);
        return depth == 0 ? compositeMist(ray, isect.t, r) : r;
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
    return depth == 0 ? compositeMist(ray, isect.t, r) : r;
}

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
        return depth == 0 ? compositeMist(ray, isect.t, r) : r;
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
        return depth == 0 ? compositeMist(ray, isect.t, r) : r;
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
    return depth == 0 ? compositeMist(ray, isect.t, r) : r;
}

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
    return depth == 0 ? compositeMist(ray, isect.t, r) : r;
}

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
        Vec3f skyC = skyColor(ray.direction);
        if (depth == 0) {
            if (config.renderMode == "mistOnly" || config.renderMode == "mistBoundsOnly") skyC = Vec3f(0);
            skyC = compositeMist(ray, 1e30f, skyC);
        }
        return skyC;
    }

    // At depth 0, short-circuit for pure mist debug modes before expensive shading
    if (depth == 0 && (config.renderMode == "mistDensityOnly" ||
                       config.renderMode == "mistAlphaOnly"   ||
                       config.renderMode == "mistLightingOnly" ||
                       config.renderMode == "mistBoundsOnly" ||
                       config.renderMode == "mistOnly")) {
        return compositeMist(ray, isect.t, Vec3f(0));
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
        return depth == 0 ? compositeMist(ray, isect.t, mat.emission) : mat.emission;
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
        return depth == 0 ? compositeMist(ray, isect.t, r) : r;
    }

    float pdf_val = mat.pdf(ray.direction, wo, N);
    if (pdf_val < 1e-6f) {
        Vec3f r = directLight / survivalProb;
        return depth == 0 ? compositeMist(ray, isect.t, r) : r;
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
    if (depth == 0) r = compositeMist(ray, isect.t, r);
    return r;
}
