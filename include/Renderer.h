#pragma once
#include "Scene.h"
#include <string>

class Renderer {
public:
    void render(const Scene& scene);

private:
    void writePPM(const std::string& filename, const std::vector<Vec3f>& framebuffer,
                  int width, int height, const SceneConfig& config);
    void renderDebugView(const Scene& scene, std::vector<Vec3f>& framebuffer,
                         int width, int height, const Vec3f& eye,
                         const Vec3f& right, const Vec3f& up,
                         const Vec3f& forward, float scale,
                         float aspectRatio);
};
