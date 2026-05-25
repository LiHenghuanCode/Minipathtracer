#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "Texture.h"
#include <iostream>
#include <algorithm>

Texture::~Texture() {
    if (data) stbi_image_free(data);
}

bool Texture::load(const std::string& path) {
    data = stbi_load(path.c_str(), &width, &height, &channels, 3);
    if (!data) {
        std::cerr << "Failed to load texture: " << path << std::endl;
        return false;
    }
    std::cout << "Loaded texture: " << path << " (" << width << "x" << height << ")" << std::endl;
    return true;
}

Vec3f Texture::sample(float u, float v) const {
    if (!data) return Vec3f(1, 0, 1); // magenta = missing texture

    // Wrap UV
    u = u - std::floor(u);
    v = 1.0f - (v - std::floor(v)); // flip V (OBJ convention)

    float fx = u * (width - 1);
    float fy = v * (height - 1);

    int x0 = std::clamp((int)fx, 0, width - 1);
    int y0 = std::clamp((int)fy, 0, height - 1);
    int x1 = std::clamp(x0 + 1, 0, width - 1);
    int y1 = std::clamp(y0 + 1, 0, height - 1);

    float sx = fx - x0;
    float sy = fy - y0;

    auto getPixel = [&](int x, int y) -> Vec3f {
        int idx = (y * width + x) * 3;
        return Vec3f(data[idx] / 255.0f, data[idx + 1] / 255.0f, data[idx + 2] / 255.0f);
    };

    // Bilinear interpolation
    Vec3f c00 = getPixel(x0, y0);
    Vec3f c10 = getPixel(x1, y0);
    Vec3f c01 = getPixel(x0, y1);
    Vec3f c11 = getPixel(x1, y1);

    Vec3f top = c00 * (1 - sx) + c10 * sx;
    Vec3f bot = c01 * (1 - sx) + c11 * sx;
    return top * (1 - sy) + bot * sy;
}

Vec3f Texture::sampleNearest(float u, float v) const {
    if (!data) return Vec3f(1, 0, 1); // magenta = missing texture

    u = u - std::floor(u);
    v = 1.0f - (v - std::floor(v)); // flip V (OBJ convention)

    int x = std::clamp((int)std::round(u * (width - 1)), 0, width - 1);
    int y = std::clamp((int)std::round(v * (height - 1)), 0, height - 1);
    int idx = (y * width + x) * 3;
    return Vec3f(data[idx] / 255.0f, data[idx + 1] / 255.0f, data[idx + 2] / 255.0f);
}
