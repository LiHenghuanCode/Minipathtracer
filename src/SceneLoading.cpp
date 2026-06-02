#include "Scene.h"
#include "MeshUtils.h"
#include "MtlConverter.h"
#include "OBJ_Loader.h"
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
}

void Scene::loadFromConfig(const SceneConfig& cfg) {
    config = cfg;
    resetMaterialRoleDiagnostics();
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
    ocean = std::make_unique<Ocean>(256, 150.0f, 12.0f, Vec3f(1, 0, 0.5f).normalized(), 1.5f, 5.0f);
    ocean->generate();
    oceanRipple = std::make_unique<Ocean>(128, 12.0f, 4.5f, Vec3f(0.8f, 0, 0.6f).normalized(), 0.14f, 8.0f);
    oceanRipple->generate();
    std::cout << "Ocean FFT generated (large swell L=150m + ripple L=12m)." << std::endl;
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

        // Load diffuse texture
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

        // Load tangent-space bump/normal texture. The local OBJ loader stores map_Bump,
        // map_bump, bump, norm, and normal declarations in map_bump.
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

        applyMaterialNameOverride(mat);
        if (mat.role == MaterialRole::CANOPY_GLASS) {
            mat.ior = config.canopyGlassIOR;
            mat.fresnelF0 = config.canopyGlassF0;
        } else if (mat.role == MaterialRole::PROPELLER_AFTERIMAGE) {
            mat.alpha = std::clamp(config.propellerAfterimageAlpha, 0.0f, 0.5f);
        }

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
