#include "scene/Scene.h"
#include "third_party/OBJ_Loader.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace {
void printVec3(const char* label, const Vec3f& v) {
    std::cout << label << " = (" << v.x << ", " << v.y << ", " << v.z << ")" << std::endl;
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

Vec3f blenderToRendererPoint(const Vec3f& v) {
    return Vec3f(v.x, v.z, -v.y);
}

Vec3f blenderToRendererVector(const Vec3f& v) {
    return blenderToRendererPoint(v).normalized();
}

Vec3f safeNormalizeScene(const Vec3f& v, const Vec3f& fallback = Vec3f(0, 1, 0)) {
    const float len2 = v.length2();
    if (len2 < 1e-12f || !std::isfinite(len2)) {
        return fallback;
    }
    return v / std::sqrt(len2);
}

float roughnessFromNs(float ns) {
    float roughness = std::sqrt(2.0f / (ns + 2.0f));
    return std::clamp(roughness, 0.02f, 1.0f);
}

float blendWeightFromKs(const Vec3f& ks) {
    return std::clamp(ks.max_component() * 0.35f, 0.0f, 0.35f);
}

bool isNamedMirrorMaterial(const std::string& name) {
    return name == "canopy_glass";
}

bool hasStrongSpecular(const objl::Material& mtl) {
    return std::max({mtl.Ks.X, mtl.Ks.Y, mtl.Ks.Z}) > 0.08f;
}

bool hasDiffuseTexture(const objl::Material& mtl) {
    return !mtl.map_Kd.empty();
}

bool hasBumpTexture(const objl::Material& mtl) {
    return !mtl.map_bump.empty();
}

MaterialType inferMaterialTypeFromMtl(const objl::Material& mtl) {
    const float dissolve = std::clamp(mtl.d, 0.0f, 1.0f);
    const float ior = std::max(mtl.Ni, 1.0f);
    const float ns = std::max(mtl.Ns, 0.0f);
    const bool transparent = dissolve < 0.999f;
    const bool strongSpecular = hasStrongSpecular(mtl);
    const bool texturedDiffuse = hasDiffuseTexture(mtl);
    const bool bumped = hasBumpTexture(mtl);
    const bool reflectiveIllum = mtl.illum >= 2;

    // Strong transparency plus specular energy is treated as a mirror-like shell.
    if (transparent && strongSpecular) {
        return MaterialType::MIRROR;
    }

    // High-ior, texture-free highlights are also promoted to the mirror bucket.
    if (strongSpecular && reflectiveIllum && !texturedDiffuse && !bumped && ior >= 1.3f) {
        return MaterialType::MIRROR;
    }

    // Textured or bumped shiny surfaces keep a diffuse base with an added reflective lobe.
    if (strongSpecular && (texturedDiffuse || bumped)) {
        return MaterialType::BLEND;
    }

    // Opaque but polished materials fall back to the mixed model when the MTL advertises strong highlights.
    if (strongSpecular && reflectiveIllum && ns >= 80.0f) {
        return MaterialType::BLEND;
    }

    return MaterialType::DIFFUSE;
}

void applyNameFallbackForAmbiguousMtl(const objl::Material& mtl, Material& mat) {
    // This asset needs a name-based fallback to preserve the reflective canopy material.
    if (isNamedMirrorMaterial(mtl.name)) {
        mat.type = MaterialType::MIRROR;
        return;
    }

    // Strengthen the reflective contribution for the named metal material when the MTL stays ambiguous.
    if (mtl.name == "metal" && mat.type == MaterialType::BLEND) {
        mat.blendWeight = std::max(mat.blendWeight, 0.85f);
    }
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

bool usesOceanNormals(const SceneConfig::ObjectEntry& entry) {
    return entry.normalSource == "ocean";
}
}

void Scene::loadFromConfig(const SceneConfig& cfg) {
    config = cfg;
    sky.setConfig(config.sky);
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

    if (config.hasAreaLight) {
        createAreaLight();
    }

    buildBVH();
    loadOcean();
    std::cout << "Scene loaded: " << triangles.size() << " triangles, "
              << materials.size() << " materials" << std::endl;
}

void Scene::loadOcean() {
    ocean.reset();
    oceanRipple.reset();

    if (!config.water.fftEnabled) {
        std::cout << "Ocean FFT disabled. Water falls back to the mesh geometric normal." << std::endl;
        return;
    }

    const auto& swell = config.water.swell;
    ocean = std::make_unique<Ocean>(
        swell.resolution > 0 ? swell.resolution : 256,
        swell.patchLength > 0.0f ? swell.patchLength : 150.0f,
        swell.windSpeed > 0.0f ? swell.windSpeed : 12.0f,
        swell.windDirection.length2() > 1e-8f ? swell.windDirection.normalized() : Vec3f(1, 0, 0.5f).normalized(),
        swell.waveHeight > 0.0f ? swell.waveHeight : 1.5f,
        swell.time > 0.0f ? swell.time : 5.0f
    );
    ocean->generate();
    const auto& ripple = config.water.ripple;
    oceanRipple = std::make_unique<Ocean>(
        ripple.resolution > 0 ? ripple.resolution : 128,
        ripple.patchLength > 0.0f ? ripple.patchLength : 12.0f,
        ripple.windSpeed > 0.0f ? ripple.windSpeed : 4.5f,
        ripple.windDirection.length2() > 1e-8f ? ripple.windDirection.normalized() : Vec3f(0.8f, 0, 0.6f).normalized(),
        ripple.waveHeight > 0.0f ? ripple.waveHeight : 0.14f,
        ripple.time > 0.0f ? ripple.time : 8.0f
    );
    oceanRipple->generate();
    std::cout << "Ocean FFT generated (swell L="
              << (swell.patchLength > 0.0f ? swell.patchLength : 150.0f)
              << "m + ripple L="
              << (ripple.patchLength > 0.0f ? ripple.patchLength : 12.0f)
              << "m)." << std::endl;
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

    // Build renderer materials from the OBJ loader's MTL data.
    for (auto& mtl : loader.LoadedMaterials) {
        Material mat;
        mat.name = mtl.name;
        mat.color = Vec3f(mtl.Kd.X, mtl.Kd.Y, mtl.Kd.Z);
        mat.mirrorColor = Vec3f(mtl.Ks.X, mtl.Ks.Y, mtl.Ks.Z);
        mat.blendWeight = blendWeightFromKs(mat.mirrorColor);
        mat.type = inferMaterialTypeFromMtl(mtl);

        // Prefer explicit scene overrides, then inferred MTL semantics, then narrow name-based fallbacks.
        if (entry.materialType == "metal" || entry.materialType == "mirror") {
            mat.type = MaterialType::MIRROR;
        } else if (entry.materialType == "emissive") {
            mat.type = MaterialType::EMISSIVE;
            mat.emission = mat.color * 10.0f;
        } else {
            applyNameFallbackForAmbiguousMtl(mtl, mat);
        }
        mat.useOceanNormals = usesOceanNormals(entry);

        if (std::isfinite(mtl.Ns) && mtl.Ns > 0.0f &&
            (mat.type == MaterialType::DIFFUSE || mat.type == MaterialType::BLEND)) {
            mat.roughness = roughnessFromNs(mtl.Ns);
        }

        // Apply per-object overrides from the scene config after the MTL defaults are in place.
        if (entry.materialColor.x >= 0) mat.color = entry.materialColor;
        if (entry.roughness >= 0) {
            mat.roughness = entry.roughness;
        }
        if (entry.blendWeight >= 0) {
            mat.blendWeight = std::clamp(entry.blendWeight, 0.0f, 1.0f);
            if (mat.type == MaterialType::DIFFUSE && mat.blendWeight > 0.0f) {
                mat.type = MaterialType::BLEND;
            }
        }
        mat.reflectionScale = std::clamp(entry.reflectionScale, 0.0f, 4.0f);
        // Attach the diffuse texture when the MTL provides one.
        if (!mtl.map_Kd.empty()) {
            auto tex = std::make_unique<Texture>();
            std::string texPath = resolveTexturePath(basePath, mtl.map_Kd);
            if (tex->load(texPath)) {
                std::cout << "Loaded diffuse texture: " << texPath << std::endl;
                mat.texture = tex.get();
                textures.push_back(std::move(tex));
            } else {
                std::cerr << "Failed to load diffuse texture for material "
                          << mtl.name << ": " << texPath << std::endl;
            }
        }

        // Attach the tangent-space bump or normal map stored by the OBJ loader in map_bump.
        if (!mtl.map_bump.empty()) {
            auto tex = std::make_unique<Texture>();
            std::string texPath = resolveTexturePath(basePath, mtl.map_bump);
            if (tex->load(texPath)) {
                std::cout << "Loaded bump/normal texture: " << texPath << std::endl;
                mat.bumpTexture = tex.get();
                textures.push_back(std::move(tex));
            } else {
                std::cerr << "Failed to load bump/normal texture for material "
                          << mtl.name << ": " << texPath << std::endl;
            }
        }

        materialMap[mtl.name] = (int)materials.size();
        materials.push_back(mat);
    }

    // Fall back to a single default material when the OBJ carries no usable MTL data.
    if (materials.empty() || loader.LoadedMaterials.empty()) {
        Material defaultMat;
        defaultMat.color = Vec3f(0.8f);

        if (entry.materialType == "metal" || entry.materialType == "mirror") {
            defaultMat.type = MaterialType::MIRROR;
        } else if (entry.materialType == "emissive") {
            defaultMat.type = MaterialType::EMISSIVE;
        }
        defaultMat.useOceanNormals = usesOceanNormals(entry);

        if (entry.materialColor.x >= 0) defaultMat.color = entry.materialColor;
        if (entry.roughness >= 0) defaultMat.roughness = entry.roughness;
        if (entry.blendWeight >= 0) {
            defaultMat.blendWeight = std::clamp(entry.blendWeight, 0.0f, 1.0f);
            if (defaultMat.type == MaterialType::DIFFUSE && defaultMat.blendWeight > 0.0f) {
                defaultMat.type = MaterialType::BLEND;
            }
        }
        defaultMat.reflectionScale = std::clamp(entry.reflectionScale, 0.0f, 4.0f);

        materialMap["_default"] = (int)materials.size();
        materials.push_back(defaultMat);
    }

    // Flatten loader meshes into the renderer's triangle list.
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
                // Apply the object-level transform after any coordinate-system conversion.
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
    // Create a synthetic material for the procedural ground or water plane.
    Material mat;
    if (entry.materialType == "mirror") {
        mat.type = MaterialType::MIRROR;
        mat.color = entry.materialColor.x >= 0 ? entry.materialColor : Vec3f(0.1f, 0.2f, 0.4f);
    } else if (entry.materialType == "metal") {
        mat.type = MaterialType::MIRROR;
        mat.roughness = entry.roughness >= 0 ? entry.roughness : 0.1f;
        mat.color = entry.materialColor.x >= 0 ? entry.materialColor : Vec3f(0.8f);
    } else {
        mat.type = MaterialType::DIFFUSE;
        mat.color = entry.materialColor.x >= 0 ? entry.materialColor : Vec3f(0.5f);
    }
    mat.useOceanNormals = usesOceanNormals(entry);
    if (entry.blendWeight >= 0) {
        mat.blendWeight = std::clamp(entry.blendWeight, 0.0f, 1.0f);
        if (mat.type == MaterialType::DIFFUSE && mat.blendWeight > 0.0f) {
            mat.type = MaterialType::BLEND;
        }
    }
    mat.reflectionScale = std::clamp(entry.reflectionScale, 0.0f, 4.0f);

    int matId = (int)materials.size();
    materials.push_back(mat);

    float size = entry.scale > 0 ? entry.scale : 10.0f;
    float y = entry.position.y;
    Vec3f p = entry.position;
    Vec3f normal(0, 1, 0);

    // Represent the plane as a two-triangle quad.
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
