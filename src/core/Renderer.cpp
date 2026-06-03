#include "core/Renderer.h"
#include "core/Random.h"
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

Vec3f applyDisplayTransform(Vec3f c, const SceneConfig& config, int px, int py, int width, int height) {
    const RenderConfig& render = config.render;
    const bool useTimeWarp = render.toneMapping == "timeWarp";
    const bool useFilmic = render.toneMapping == "filmic";
    const bool useRawLinear = render.toneMapping == "rawLinear";
    const float displayExposure = std::max(0.0f, render.displayExposure);
    const float highlightCompression = std::max(0.0f, render.highlightCompression);
    const float whitePoint = std::max(1e-4f, render.whitePoint);

    c *= displayExposure;

    if (useRawLinear) {
        c.x = std::clamp(c.x / whitePoint, 0.0f, 1.0f);
        c.y = std::clamp(c.y / whitePoint, 0.0f, 1.0f);
        c.z = std::clamp(c.z / whitePoint, 0.0f, 1.0f);
        return c;
    }

    float invW = width > 1 ? 1.0f / (float)(width - 1) : 0.0f;
    float invH = height > 1 ? 1.0f / (float)(height - 1) : 0.0f;
    float vx = (float)px * invW * 2.0f - 1.0f;
    float vy = (float)py * invH * 2.0f - 1.0f;
    float dist2 = vx * vx + vy * vy;

    if (useTimeWarp) {
        c.x = 1.0f - std::exp2(-c.x);
        c.y = 1.0f - std::exp2(-c.y);
        c.z = 1.0f - std::exp2(-c.z);
        c.x = std::sqrt(std::clamp(c.x, 0.0f, 1.0f));
        c.y = std::sqrt(std::clamp(c.y, 0.0f, 1.0f));
        c.z = std::sqrt(std::clamp(c.z, 0.0f, 1.0f));

        float vignette = 1.0f / (dist2 * 2.5f + 1.0f);
        c *= vignette;
        c.x = std::clamp(c.x, 0.0f, 1.0f);
        c.y = std::clamp(c.y, 0.0f, 1.0f);
        c.z = std::clamp(c.z, 0.0f, 1.0f);
        return c;
    }

    float vign = 1.0f - 0.35f * dist2;
    c *= std::max(0.0f, vign);

    if (useFilmic) {
        c.x = c.x / (c.x + whitePoint);
        c.y = c.y / (c.y + whitePoint);
        c.z = c.z / (c.z + whitePoint);

        float luma = 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
        const float sat = 1.20f;
        c.x = luma + (c.x - luma) * sat;
        c.y = luma + (c.y - luma) * sat;
        c.z = luma + (c.z - luma) * sat;
        c.y *= 0.94f;
    } else {
        const float w2 = highlightCompression * highlightCompression;
        auto sqrtV = [](const Vec3f& v) {
            return Vec3f(std::sqrt(v.x), std::sqrt(v.y), std::sqrt(v.z));
        };
        c = c / whitePoint;
        Vec3f t = (Vec3f(1.0f) - (c + Vec3f(w2))) * 0.5f;
        c = Vec3f(1.0f) - (sqrtV(t * t + Vec3f(w2)) + t);
        c.x = std::clamp(c.x, 0.0f, 1.0f);
        c.y = std::clamp(c.y, 0.0f, 1.0f);
        c.z = std::clamp(c.z, 0.0f, 1.0f);

        float luma = 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
        const float sat = 1.15f;
        c.x = luma + (c.x - luma) * sat;
        c.y = luma + (c.y - luma) * sat;
        c.z = luma + (c.z - luma) * sat;
        c.y *= 0.93f;
    }

    c.x = std::pow(std::clamp(c.x, 0.0f, 1.0f), 1.0f / 2.2f);
    c.y = std::pow(std::clamp(c.y, 0.0f, 1.0f), 1.0f / 2.2f);
    c.z = std::pow(std::clamp(c.z, 0.0f, 1.0f), 1.0f / 2.2f);
    return c;
}
}

void Renderer::render(const Scene& scene) {
    if (!scene.config.camera.enabled) {
        std::cerr << "No enabled cameras; nothing rendered." << std::endl;
        return;
    }

    renderView(scene);
}

void Renderer::renderView(const Scene& scene) {
    int width = scene.config.render.width;
    int height = scene.config.render.height;
    int spp = scene.config.render.spp;
    std::vector<Vec3f> framebuffer(width * height);

    // Camera setup
    Vec3f eye = scene.config.camera.position;
    Vec3f target = scene.config.camera.lookAt;
    float fov = scene.config.camera.fov;

    Vec3f forward = (target - eye).normalized();
    Vec3f upHint = scene.config.camera.up.length2() > 1e-8f ? scene.config.camera.up.normalized() : Vec3f(0, 1, 0);
    Vec3f right = scene.config.camera.right.length2() > 1e-8f
        ? scene.config.camera.right.normalized()
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
    writePPM(scene.config.render.outputFile, framebuffer, width, height, scene.config);
}

void Renderer::writePPM(const std::string& filename, const std::vector<Vec3f>& framebuffer,
                         int width, int height, const SceneConfig& config) {
    FILE* fp = fopen(filename.c_str(), "wb");
    if (!fp) {
        std::cerr << "Failed to open output file: " << filename << std::endl;
        return;
    }

    fprintf(fp, "P6\n%d %d\n255\n", width, height);
    for (int i = 0; i < width * height; ++i) {
        Vec3f c = framebuffer[i];

        int px = i % width;
        int py = i / width;
        c = applyDisplayTransform(c, config, px, py, width, height);

        unsigned char color[3];
        color[0] = (unsigned char)(255 * c.x);
        color[1] = (unsigned char)(255 * c.y);
        color[2] = (unsigned char)(255 * c.z);
        fwrite(color, 1, 3, fp);
    }
    fclose(fp);
    std::cout << "Output saved to: " << filename << std::endl;
}
