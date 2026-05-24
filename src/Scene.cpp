#include "Scene.h"
#include "Noise.h"
#include "OBJ_Loader.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>

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
              << "diffuseTexture=" << ((mat.texture && mat.texture->isLoaded()) ? "loaded" : "none")
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
    triangles.clear();
    materials.clear();
    materialMap.clear();
    textures.clear();
    hasAreaLight = false;
    sceneBounds = AABB{};

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

    // Load materials from MTL
    for (auto& mtl : loader.LoadedMaterials) {
        Material mat;
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

        // Load diffuse texture
        if (!mtl.map_Kd.empty()) {
            auto tex = std::make_unique<Texture>();
            std::string texPath = basePath + mtl.map_Kd;
            if (!std::filesystem::exists(texPath)) {
                texPath = basePath + "textures/" + mtl.map_Kd;
            }
            if (tex->load(texPath)) {
                mat.texture = tex.get();
                textures.push_back(std::move(tex));
            }
        }

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

    Ray shadowRay(hitPoint + N * 1e-3f, lightDir);
    Intersection shadowIsect = bvh.intersect(shadowRay);
    if (shadowIsect.hit && shadowIsect.t < dist - 1e-2f) {
        return Vec3f(0);
    }

    Vec3f brdf = mat.eval(wi, lightDir, N, texU, texV);
    float geometryTerm = surfaceCos * lightCos / dist2;
    return areaLight.emission * brdf * geometryTerm * areaLight.area();
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
            config.renderMode == "waterRefractionOnly" || config.renderMode == "waterFresnelOnly") {
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
    }

    // Ensure normal faces the ray
    if (dot(N, ray.direction) > 0 && mat.type != MaterialType::DIELECTRIC) {
        N = -N;
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
