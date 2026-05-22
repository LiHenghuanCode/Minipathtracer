#include "Renderer.h"
#include <iostream>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <utility>
#ifdef _OPENMP
#include <omp.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
void printVec3(const char* label, const Vec3f& v) {
    std::cout << label << " = (" << v.x << ", " << v.y << ", " << v.z << ")" << std::endl;
}
}

void Renderer::render(const Scene& scene) {
    int width = scene.config.width;
    int height = scene.config.height;
    int spp = scene.config.spp;
    std::vector<Vec3f> framebuffer(width * height);

    // Camera setup
    Vec3f eye = scene.config.cameraPos;
    Vec3f target = scene.config.cameraLookAt;
    float fov = scene.config.fov;

    Vec3f forward = (target - eye).normalized();
    Vec3f upHint = scene.config.cameraUp.length2() > 1e-8f ? scene.config.cameraUp.normalized() : Vec3f(0, 1, 0);
    Vec3f right = scene.config.cameraRight.length2() > 1e-8f
        ? scene.config.cameraRight.normalized()
        : cross(forward, upHint).normalized();
    if (right.length2() < 1e-8f) {
        right = cross(forward, Vec3f(0, 0, 1)).normalized();
    }
    Vec3f up = cross(right, forward).normalized();
    if (dot(up, upHint) < 0.0f) {
        right = -right;
        up = -up;
    }

    float scale = std::tan(fov * 0.5f * (float)M_PI / 180.0f);
    float aspectRatio = (float)width / (float)height;

    Vec3f sceneCenter = scene.bounds().centroid();
    float cameraDistance = (sceneCenter - eye).length();
    printVec3("Camera position", eye);
    printVec3("Camera forward", forward);
    printVec3("Camera up", up);
    std::cout << "Camera distance to scene center = " << cameraDistance << std::endl;

    if (scene.config.debugMode) {
        std::cout << "Rendering debug axes/bounding-box view..." << std::endl;
        renderDebugView(scene, framebuffer, width, height, eye, right, up, forward, scale, aspectRatio);
        writePPM(scene.config.outputFile, framebuffer, width, height);
        return;
    }

    std::cout << "Rendering " << width << "x" << height << " @ " << spp << " spp..." << std::endl;

    std::atomic<int> rowsDone(0);

    #pragma omp parallel for schedule(dynamic, 1)
    for (int j = 0; j < height; ++j) {
        for (int i = 0; i < width; ++i) {
            Vec3f pixelColor(0);
            for (int s = 0; s < spp; ++s) {
                float rx = (spp == 1) ? 0.5f : random_float();
                float ry = (spp == 1) ? 0.5f : random_float();

                float x = (2.0f * (i + rx) / (float)width - 1.0f) * aspectRatio * scale;
                float y = (1.0f - 2.0f * (j + ry) / (float)height) * scale;

                Vec3f dir = (forward + right * x + up * y).normalized();
                Ray ray(eye, dir);
                pixelColor += scene.castRay(ray, 0);
            }
            framebuffer[j * width + i] = pixelColor / (float)spp;
        }

        int done = ++rowsDone;
        if (done % 50 == 0 || done == height) {
            #pragma omp critical
            {
                std::cout << "\rProgress: " << done << "/" << height
                          << " (" << (int)(100.0f * done / height) << "%)" << std::flush;
            }
        }
    }
    std::cout << "\nRendering complete." << std::endl;

    writePPM(scene.config.outputFile, framebuffer, width, height);
}

void Renderer::renderDebugView(const Scene& scene, std::vector<Vec3f>& framebuffer,
                                int width, int height, const Vec3f& eye,
                                const Vec3f& right, const Vec3f& up,
                                const Vec3f& forward, float scale,
                                float aspectRatio) {
    std::fill(framebuffer.begin(), framebuffer.end(), Vec3f(0.04f, 0.045f, 0.055f));

    auto project = [&](const Vec3f& p, int& sx, int& sy) {
        Vec3f rel = p - eye;
        float z = dot(rel, forward);
        if (z <= 1e-4f) return false;

        float ndcX = dot(rel, right) / (z * aspectRatio * scale);
        float ndcY = dot(rel, up) / (z * scale);
        sx = (int)((ndcX * 0.5f + 0.5f) * (float)(width - 1));
        sy = (int)((0.5f - ndcY * 0.5f) * (float)(height - 1));
        return sx >= -width && sx <= width * 2 && sy >= -height && sy <= height * 2;
    };

    auto putPixel = [&](int x, int y, const Vec3f& color) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        framebuffer[y * width + x] = color;
    };

    auto drawLine2D = [&](int x0, int y0, int x1, int y1, const Vec3f& color) {
        int dx = std::abs(x1 - x0);
        int sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0);
        int sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        while (true) {
            putPixel(x0, y0, color);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    };

    auto drawLine3D = [&](const Vec3f& a, const Vec3f& b, const Vec3f& color) {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        if (!project(a, x0, y0) || !project(b, x1, y1)) return;
        drawLine2D(x0, y0, x1, y1, color);
    };

    AABB bounds = scene.bounds();
    Vec3f center = bounds.centroid();
    Vec3f size = bounds.extent();
    float axisLen = std::max({size.x, size.y, size.z, 1.0f}) * 0.35f;

    drawLine3D(Vec3f(0), Vec3f(axisLen, 0, 0), Vec3f(1, 0.1f, 0.1f));
    drawLine3D(Vec3f(0), Vec3f(0, axisLen, 0), Vec3f(0.1f, 1, 0.1f));
    drawLine3D(Vec3f(0), Vec3f(0, 0, axisLen), Vec3f(0.1f, 0.35f, 1));

    Vec3f mn = bounds.min_p;
    Vec3f mx = bounds.max_p;
    Vec3f corners[8] = {
        {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z},
        {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}
    };
    int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };
    for (auto& edge : edges) {
        drawLine3D(corners[edge[0]], corners[edge[1]], Vec3f(1.0f, 0.9f, 0.2f));
    }

    float crossLen = axisLen * 0.05f;
    drawLine3D(center - Vec3f(crossLen, 0, 0), center + Vec3f(crossLen, 0, 0), Vec3f(1));
    drawLine3D(center - Vec3f(0, crossLen, 0), center + Vec3f(0, crossLen, 0), Vec3f(1));
    drawLine3D(center - Vec3f(0, 0, crossLen), center + Vec3f(0, 0, crossLen), Vec3f(1));
}

void Renderer::writePPM(const std::string& filename, const std::vector<Vec3f>& framebuffer,
                         int width, int height) {
    FILE* fp = fopen(filename.c_str(), "wb");
    if (!fp) {
        std::cerr << "Failed to open output file: " << filename << std::endl;
        return;
    }

    fprintf(fp, "P6\n%d %d\n255\n", width, height);
    for (int i = 0; i < width * height; ++i) {
        Vec3f c = framebuffer[i];

        // Reinhard tone mapping
        c.x = c.x / (c.x + 1.0f);
        c.y = c.y / (c.y + 1.0f);
        c.z = c.z / (c.z + 1.0f);

        // Gamma correction (1/2.2)
        c.x = std::pow(std::clamp(c.x, 0.0f, 1.0f), 1.0f / 2.2f);
        c.y = std::pow(std::clamp(c.y, 0.0f, 1.0f), 1.0f / 2.2f);
        c.z = std::pow(std::clamp(c.z, 0.0f, 1.0f), 1.0f / 2.2f);

        unsigned char color[3];
        color[0] = (unsigned char)(255 * c.x);
        color[1] = (unsigned char)(255 * c.y);
        color[2] = (unsigned char)(255 * c.z);
        fwrite(color, 1, 3, fp);
    }
    fclose(fp);
    std::cout << "Output saved to: " << filename << std::endl;
}
