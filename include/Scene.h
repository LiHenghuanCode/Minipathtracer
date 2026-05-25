#pragma once
#include "Vec3.h"
#include "Triangle.h"
#include "BVH.h"
#include "Material.h"
#include "Ocean.h"
#include "Random.h"
#include "Texture.h"
#include <vector>
#include <string>
#include <memory>
#include <map>
#include <mutex>
#include <array>
#include <atomic>

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
    std::string toneMapping = "softWhiteClamp";
    std::string renderMode = "toneMapped";
    float displayExposure = 1.0f;
    float highlightCompression = 0.15f;
    float whitePoint = 1.0f;
    float aircraftClearcoatStrength = 0.35f;
    float aircraftClearcoatEnvBoost = 1.5f;
    float aircraftClearcoatF0 = 0.10f;
    bool aircraftMirrorDebug = false;
    float normalStrength = 0.45f;
    float normalDetailStrength = 1.2f;
    bool flipNormalGreen = false;
    bool debugNormalMap = false;
    bool sharpNormalSampling = false;
    bool canopyGlassUsePhysical = false;
    float canopyGlassIOR = 1.52f;
    float canopyGlassF0 = 0.043f;
    float canopyGlassReflectionBoost = 1.05f;
    Vec3f canopyGlassTransmissionTint = Vec3f(0.55f, 0.72f, 0.90f);
    Vec3f canopyGlassCoolReflectionFactor = Vec3f(0.80f, 0.88f, 1.0f);
    float canopyGlassCenterDarkness = 0.70f;
    float canopyFresnelPower = 2.2f;
    float canopyFresnelBase = 0.25f;
    float canopyReflectionMix = 0.65f;
    float canopyGlintStrength = 5.0f;
    float canopyBrightnessBoost = 1.5f;
    float canopyInteriorDarkness = 0.70f;
    Vec3f canopyCoolReflection = Vec3f(0.45f, 0.60f, 1.05f);
    Vec3f canopySmokedCenter = Vec3f(0.005f, 0.018f, 0.035f);
    Vec3f propellerAfterimageColor = Vec3f(0.12f, 0.12f, 0.13f);
    float propellerAfterimageAlpha = 0.40f;
    float propellerAfterimageStackReduction = 0.45f;
    bool debugMaterialRoles = false;
    bool debugCanopyExtreme = false;
    bool debugPropellerAfterimageExtreme = false;
    bool debugFakeCockpitPattern = false;

    // Camera
    bool cameraEnabled = true;
    Vec3f cameraPos = Vec3f(0, 1.5f, 5);
    Vec3f cameraLookAt = Vec3f(0, 0.8f, 0);
    Vec3f cameraRight = Vec3f(1, 0, 0);
    Vec3f cameraUp = Vec3f(0, 1, 0);
    float fov = 45.0f;
    bool secondaryCameraEnabled = false;
    Vec3f secondaryCameraPos = Vec3f(0, 1.5f, 5);
    Vec3f secondaryCameraLookAt = Vec3f(0, 0.8f, 0);
    Vec3f secondaryCameraRight = Vec3f(1, 0, 0);
    Vec3f secondaryCameraUp = Vec3f(0, 1, 0);
    float secondaryFov = 45.0f;
    std::string secondaryOutputFile = "output_2.ppm";
    bool debugMode = false;
    std::string materialDebug = "none";

    // Sky configuration (procedural sunset gradient + sun disk/glow)
    struct SkyConfig {
        bool enabled = true;
        Vec3f topColor     = Vec3f(0.08f, 0.10f, 0.23f);  // deep blue/purple zenith
        Vec3f horizonColor = Vec3f(1.0f,  0.34f, 0.12f);  // orange-red horizon
        Vec3f bottomColor  = Vec3f(0.95f, 0.62f, 0.25f);  // warm gold below horizon
        Vec3f sunDirection = Vec3f(0.65f, 0.08f, -0.75f); // sun near the horizon
        Vec3f sunDiskColor = Vec3f(8.0f,  4.5f,  1.5f);   // HDR bright disk
        Vec3f sunGlowColor = Vec3f(2.0f,  0.75f, 0.22f);  // warm wide glow
        float sunDiskPower     = 500.0f;  // tightness of sun disk
        float sunGlowPower     = 10.0f;   // spread of sun glow
        float sunAngularRadius = 0.00935f;
        float sunEdgeSoftness = 0.006f;
        float sunIntensity = 1.0f;
        float sunDiskIntensity = 1.2f;
        float sunGlowIntensity = 1.8f;
        float skyIntensity = 1.0f;
        float horizonWarmth = 1.0f;
        float sunsetGradientStrength = 1.0f;
        // Cloud fields — parsed now, used in Stage 4
        bool  cloudsEnabled  = false;
        float cloudScale     = 3.5f;
        float cloudThreshold = 0.48f;
        float cloudSoftness  = 0.22f;
        float cloudOpacity   = 0.55f;
        Vec3f cloudDarkColor = Vec3f(0.22f, 0.12f, 0.20f);
        Vec3f cloudWarmColor = Vec3f(1.0f,  0.42f, 0.16f);
        // Distance-adaptive FBM scale
        bool  cloudAdaptiveScaleEnabled = true;
        float cloudAdaptiveMinScale     = 0.15f;
        float cloudAdaptiveMaxScale     = 8.0f;
        // Sun-lit edge highlight
        float cloudSunEdgeIntensity = 1.2f;
        float cloudSunEdgePower     = 3.0f;
        float cloudSunFocusPower    = 2.0f;
    };
    SkyConfig sky;

    // Local volumetric mist / sea-fog banks
    struct MistVolumeConfig {
        bool    enabled          = false;
        Vec3f   center           = Vec3f(0, 1, 10);
        Vec3f   size             = Vec3f(15, 2, 6);   // half-extents
        float   density          = 0.10f;
        float   absorption       = 0.30f;
        float   scatteringStrength = 0.65f;
        float   coverage         = 0.28f;
        float   softness         = 2.5f;
        float   noiseScale       = 0.08f;
        Vec3f   noiseOffset      = Vec3f(0, 0, 0);
        float   heightFalloff    = 1.6f;
        float   edgeSoftness     = 0.20f;
        Vec3f   coolAmbientColor = Vec3f(0.30f, 0.36f, 0.52f);
        Vec3f   warmSunColor     = Vec3f(0.88f, 0.65f, 0.30f);
        float   sunRimStrength   = 1.0f;
        float   maxAlpha         = 0.50f;
        int     marchSteps       = 20;
        int     shadowSteps      = 4;
    };
    MistVolumeConfig leftMist;
    MistVolumeConfig rightMist;

    // Water-only reflection controls. These do not alter aircraft materials or shading.
    float waterReflectionStrength = 0.75f;
    float waterSunReflectionStrength = 1.0f;
    float waterWarmth = 0.25f;
    float waterRoughness = 0.45f;
    float waterNormalStrength = 1.0f;
    float skyFillStrength = 1.0f;
    float horizonFillStrength = 1.0f;
    float waterBounceStrength = 0.0f;
    Vec3f waterBounceColor = Vec3f(1.0f, 0.56f, 0.22f);
    Vec3f waterBounceDirection = Vec3f(0.0f, -1.0f, 0.0f);
    float waterBounceFalloff = 1.0f;
    float waterBounceMaxContribution = 0.08f;
    Vec3f upperSkyFillColor = Vec3f(0.45f, 0.55f, 0.80f);
    float environmentDiffuseStrength = 1.0f;
    float environmentReflectionStrength = 1.0f;
    float ambientStrength = 1.0f;
    float shadowLift = 0.0f;
    float waterReflectionFloor = 0.04f;
    float waterFresnelBias = 0.0f;
    float waterRefractionWeight = 1.0f;
    float waterBaseAbsorption = 0.33f;
    float waterForegroundDarkening = 0.0f;
    float waterLargeWaveScale = 1.0f;
    float waterSmallWaveScale = 0.35f;

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
    void resetMistDiagnostics() const;
    void printMistDiagnostics() const;
    void resetMaterialRoleDiagnostics() const;
    void printMaterialRoleDiagnostics() const;
    bool tracePrimary(const Ray& ray, Intersection& isect) const;
    float mistAlphaToHit(const Ray& ray, float hitT) const;

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
    Vec3f shadeCanopyGlassPhysical(const Ray& ray, const Intersection& isect,
                                   const Material& mat, const Vec3f& N, int depth) const;
    Vec3f shadeCanopyGlassFake(const Ray& ray, const Intersection& isect,
                               const Material& mat, const Vec3f& N, int depth) const;
    Vec3f shadePropellerAfterimage(const Ray& ray, const Intersection& isect,
                                   const Material& mat, const Vec3f& N, int depth) const;
    Vec3f shadeAircraftMetal(const Ray& ray, const Intersection& isect,
                             const Material& mat, const Vec3f& N, int depth) const;

    // Sky color based on ray direction
    Vec3f skyColor(const Vec3f& direction) const;
    Vec3f sunDiskColor(const Vec3f& direction) const;

    // Volumetric mist helpers
    struct MistSample { Vec3f color; float transmittance; };
    MistSample renderMistVolume(const Ray& ray,
                                const SceneConfig::MistVolumeConfig& vol,
                                float maxT) const;
    Vec3f compositeMist(const Ray& ray, float hitT, Vec3f sceneColor) const;
    void recordMistDiagnostics(bool leftAabbHit, bool rightAabbHit,
                               float alpha, const Vec3f& mistColor,
                               const Vec3f& before, const Vec3f& after) const;

    struct MistDiagnostics {
        uint64_t compositeCalls = 0;
        uint64_t leftAabbHits = 0;
        uint64_t rightAabbHits = 0;
        uint64_t changedPixels = 0;
        double alphaSum = 0.0;
        double maxAlpha = 0.0;
        Vec3f mistColorSum = Vec3f(0.0f);
    };
    mutable std::mutex mistDiagnosticsMutex;
    mutable MistDiagnostics mistDiagnostics;
    mutable std::array<std::atomic<uint64_t>, 6> primaryRoleHits{};

    std::vector<Triangle> triangles;
    std::vector<Material> materials;
    std::map<std::string, int> materialMap; // name -> index
    std::vector<std::unique_ptr<Texture>> textures;
    std::unique_ptr<Ocean> ocean;
    std::unique_ptr<Ocean> oceanRipple;
    AreaLightSource areaLight;
    bool hasAreaLight = false;
    BVH bvh;
    AABB sceneBounds;
};
