#pragma once
#include "core/Vec3.h"
#include "core/Triangle.h"
#include "core/BVH.h"
#include "core/Material.h"
#include "environment/Ocean.h"
#include "environment/Sky.h"
#include "core/Random.h"
#include "scene/SceneConfig.h"
#include "scene/Texture.h"
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
    void loadOcean();
    Vec3f computeWaterNormal(const Vec3f& hitPoint, const Vec3f& geometricNormal) const;
    Vec3f sampleAreaLight(const Vec3f& hitPoint, const Vec3f& wi, const Vec3f& N, const Material& mat,
                          float texU, float texV) const;

    std::vector<Triangle> triangles;
    std::vector<Material> materials;
    std::map<std::string, int> materialMap; // Maps material names to indices in the local material array.
    std::vector<std::unique_ptr<Texture>> textures;
    std::unique_ptr<Ocean> ocean;
    std::unique_ptr<Ocean> oceanRipple;
    Sky sky;
    AreaLightSource areaLight;
    bool hasAreaLight = false;
    BVH bvh;
    AABB sceneBounds;
};
