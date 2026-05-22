#include "Renderer.h"
#include <iostream>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <atomic>
#ifdef _OPENMP
#include <omp.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
    Vec3f worldUp(0, 1, 0);
    Vec3f right = cross(forward, worldUp).normalized();
    Vec3f up = cross(right, forward).normalized();

    float scale = std::tan(fov * 0.5f * (float)M_PI / 180.0f);
    float aspectRatio = (float)width / (float)height;

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
