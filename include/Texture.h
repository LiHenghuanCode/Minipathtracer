#pragma once
#include "Vec3.h"
#include <string>
#include <vector>

class Texture {
public:
    Texture() = default;
    ~Texture();

    bool load(const std::string& path);
    Vec3f sample(float u, float v) const;  // bilinear interpolation
    Vec3f sampleNearest(float u, float v) const;
    bool isLoaded() const { return data != nullptr; }

private:
    unsigned char* data = nullptr;
    int width = 0, height = 0, channels = 0;
};
