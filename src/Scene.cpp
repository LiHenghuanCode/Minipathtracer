#include "Scene.h"
#include "OBJ_Loader.h"
#include <iostream>
#include <algorithm>
#include <cmath>
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
}

void Scene::loadFromConfig(const SceneConfig& cfg) {
    config = cfg;
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
    ocean = std::make_unique<Ocean>(256, 20.0f, 10.0f, Vec3f(1, 0, 0.5f).normalized(), 0.5f, 5.0f);
    ocean->generate();
    std::cout << "Ocean FFT generated." << std::endl;
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
    Vec3f dir = direction.normalized();

    float t = std::clamp(dir.y * 0.5f + 0.5f, 0.0f, 1.0f);
    Vec3f sky = config.skyColorBottom * (1.0f - t) + config.skyColorTop * t;

    Vec3f sunDir = hasAreaLight ? areaLight.normal : Vec3f(0.0f, 0.0f, 1.0f);
    float sunDot = dot(dir, sunDir);

    float horizonGlow = std::exp(-std::abs(dir.y) * 4.0f);
    sky = sky + config.skyColorBottom * horizonGlow * 0.3f;

    if (sunDot > 0.0f) {
        float wideGlow = std::pow(sunDot, 2.0f);
        sky = sky + Vec3f(0.8f, 0.4f, 0.15f) * wideGlow * 0.5f;

        float midGlow = std::pow(sunDot, 8.0f);
        sky = sky + Vec3f(1.0f, 0.6f, 0.2f) * midGlow * 1.5f;

        float narrowGlow = std::pow(sunDot, 32.0f);
        sky = sky + Vec3f(1.0f, 0.8f, 0.4f) * narrowGlow * 3.0f;

        float tightGlow = std::pow(sunDot, 128.0f);
        sky = sky + Vec3f(1.0f, 0.95f, 0.8f) * tightGlow * 5.0f;
    }

    return sky;
}

Vec3f Scene::castRay(const Ray& ray, int depth) const {
    if (depth >= config.maxDepth) return Vec3f(0);

    Intersection isect = bvh.intersect(ray);

    if (!isect.hit) {
        return skyColor(ray.direction);
    }

    const Material& mat = materials[isect.materialId];

    if (config.materialDebug != "none") {
        Vec3f N = isect.normal;
        if (mat.type == MaterialType::DIELECTRIC && ocean) {
            N = ocean->getNormal(isect.position.x, isect.position.z);
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
        N = ocean->getNormal(hitPoint.x, hitPoint.z);
    }

    // Ensure normal faces the ray
    if (dot(N, ray.direction) > 0 && mat.type != MaterialType::DIELECTRIC) {
        N = -N;
    }

    // Direct lighting (NEE)
    Vec3f directLight(0);
    if (mat.type == MaterialType::DIFFUSE || mat.type == MaterialType::METAL) {
        if (hasAreaLight) {
            directLight = sampleAreaLight(hitPoint, ray.direction, N, mat, isect.texU, isect.texV);
        }
    }

    // Indirect lighting
    Vec3f wo = mat.sample(ray.direction, N);
    if (wo.length2() < 1e-8f) {
        return directLight / survivalProb;
    }

    float pdf_val = mat.pdf(ray.direction, wo, N);
    if (pdf_val < 1e-6f) {
        return directLight / survivalProb;
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
        Vec3f attenuation(
            std::exp(-mat.absorptionColor.x * distance),
            std::exp(-mat.absorptionColor.y * distance),
            std::exp(-mat.absorptionColor.z * distance)
        );
        indirect = indirect * attenuation;
    }

    Vec3f result;
    if (mat.type == MaterialType::DIFFUSE) {
        float cosTheta = std::max(0.0f, dot(wo, N));
        result = directLight + brdf * indirect * cosTheta / pdf_val;
    } else {
        // Metal and dielectric: delta-like distributions
        result = directLight + brdf * indirect;
    }

    return sanitizeRadiance(result / survivalProb);
}
