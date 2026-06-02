#include "Diagnostics.h"
#include "Scene.h"
#include <algorithm>
#include <atomic>
#include <iostream>

// Converts a material type enum to a readable name.
const char* materialTypeName(MaterialType type) {
    switch (type) {
        case MaterialType::DIFFUSE: return "DIFFUSE";
        case MaterialType::METAL: return "METAL";
        case MaterialType::DIELECTRIC: return "DIELECTRIC";
        case MaterialType::EMISSIVE: return "EMISSIVE";
    }
    return "UNKNOWN";
}

// Converts a material role enum to a readable name.
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

// Maps a material role to the diagnostics counter index.
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

// Returns a debug color for a material role.
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

// Resets primary material role hit counters.
void Scene::resetMaterialRoleDiagnostics() const {
    for (auto& counter : primaryRoleHits) {
        counter.store(0, std::memory_order_relaxed);
    }
}

// Prints primary material role hit counters.
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

// Traces a primary ray against the scene BVH.
bool Scene::tracePrimary(const Ray& ray, Intersection& isect) const {
    isect = bvh.intersect(ray);
    return isect.hit;
}

// Returns a debug color for material property visualization.
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
