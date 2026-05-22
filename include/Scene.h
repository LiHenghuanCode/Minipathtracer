#pragma once
#include "Vec3.h"
#include "Triangle.h"
#include "BVH.h"
#include "Material.h"
#include "Ocean.h"
#include "Texture.h"
#include <vector>
#include <string>
#include <memory>
#include <map>

struct AreaLightSource {
    Vec3f center;
    Vec3f normal;
    Vec3f u, v;
    float halfWidth;
    float halfHeight;
    Vec3f emission;
    int materialId = -1;

    Vec3f samplePoint() const {
        float r = std::sqrt(random_float());
        float theta = 2.0f * 3.14159265358979323846f * random_float();
        float x = std::cos(theta) * r * halfWidth;
        float y = std::sin(theta) * r * halfHeight;
        return center + u * x + v * y;
    }

    float area() const {
        return 3.14159265358979323846f * halfWidth * halfHeight;
    }
};

struct SceneConfig {
    // Render settings
    int width = 1280;
    int height = 720;
    int spp = 64;
    int maxDepth = 8;

    // Camera
    Vec3f cameraPos = Vec3f(0, 1.5f, 5);
    Vec3f cameraLookAt = Vec3f(0, 0.8f, 0);
    Vec3f cameraRight = Vec3f(1, 0, 0);
    Vec3f cameraUp = Vec3f(0, 1, 0);
    float fov = 45.0f;
    bool debugMode = false;
    std::string materialDebug = "none";

    // Lighting
    struct AreaLightConfig {
        Vec3f position = Vec3f(0, 10, 0);
        Vec3f direction = Vec3f(0, -1, 0);
        float width = 5.0f;
        float height = 5.0f;
        Vec3f color = Vec3f(1.0f, 0.9f, 0.7f);
        float intensity = 100.0f;
    };
    AreaLightConfig areaLightConfig;
    bool hasAreaLight = false;
    Vec3f skyColorTop = Vec3f(0.2f, 0.3f, 0.8f);
    Vec3f skyColorBottom = Vec3f(1.0f, 0.6f, 0.3f);

    // Objects (loaded from JSON)
    struct ObjectEntry {
        std::string type; // "obj" or "plane"
        std::string file;
        Vec3f position = Vec3f(0);
        Vec3f rotation = Vec3f(0);
        float scale = 1.0f;
        // Material override
        std::string materialType = "";
        Vec3f materialColor = Vec3f(-1); // -1 = use MTL
        float roughness = -1;
        float metallic = -1;
        float glossyWeight = -1;
        float specularBoost = 1.0f;
        float ior = -1;
        bool convertFromBlender = false;
    };
    std::vector<ObjectEntry> objects;

    std::string outputFile = "output.ppm";
};

class Scene {
public:
    Scene() = default;

    void loadFromConfig(const SceneConfig& config);
    Vec3f castRay(const Ray& ray, int depth) const;
    const AABB& bounds() const { return sceneBounds; }

    SceneConfig config;

private:
    void loadOBJ(const SceneConfig::ObjectEntry& entry);
    void addPlane(const SceneConfig::ObjectEntry& entry);
    void createAreaLight();
    void buildBVH();
    Vec3f materialDebugColor(const Material& mat, const Vec3f& normal,
                             float texU, float texV) const;
    Vec3f sampleAreaLight(const Vec3f& hitPoint, const Vec3f& wi, const Vec3f& N, const Material& mat,
                          float texU, float texV) const;

    // Sky color based on ray direction
    Vec3f skyColor(const Vec3f& direction) const;

    std::vector<Triangle> triangles;
    std::vector<Material> materials;
    std::map<std::string, int> materialMap; // name -> index
    std::vector<std::unique_ptr<Texture>> textures;
    std::unique_ptr<Ocean> ocean;
    AreaLightSource areaLight;
    bool hasAreaLight = false;
    BVH bvh;
    AABB sceneBounds;
};
