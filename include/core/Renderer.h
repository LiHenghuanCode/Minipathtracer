#pragma once
#include "scene/Scene.h"
#include "scene/SceneConfig.h"
#include <string>

class Renderer {
public:
    void render(const Scene& scene);

private:
    void renderView(const Scene& scene);
    void writePPM(const std::string& filename, const std::vector<Vec3f>& framebuffer,
                  int width, int height, const SceneConfig& config);
};
