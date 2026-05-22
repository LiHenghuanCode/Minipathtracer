#pragma once
#include "Vec3.h"
#include "Triangle.h"
#include <vector>
#include <memory>

struct AABB {
    Vec3f min_p{1e30f}, max_p{-1e30f};

    void expand(const Vec3f& p) {
        min_p = vec_min(min_p, p);
        max_p = vec_max(max_p, p);
    }

    void expand(const AABB& other) {
        min_p = vec_min(min_p, other.min_p);
        max_p = vec_max(max_p, other.max_p);
    }

    void expand(const Triangle& tri) {
        expand(tri.v0);
        expand(tri.v1);
        expand(tri.v2);
    }

    Vec3f centroid() const { return (min_p + max_p) * 0.5f; }
    Vec3f extent() const { return max_p - min_p; }

    int longestAxis() const {
        Vec3f d = extent();
        if (d.x > d.y && d.x > d.z) return 0;
        if (d.y > d.z) return 1;
        return 2;
    }

    float surfaceArea() const {
        Vec3f d = extent();
        return 2.0f * (d.x * d.y + d.x * d.z + d.y * d.z);
    }

    bool intersect(const Ray& ray, float tMin, float tMax) const {
        for (int i = 0; i < 3; ++i) {
            float invD = 1.0f / ray.direction[i];
            float t0 = (min_p[i] - ray.origin[i]) * invD;
            float t1 = (max_p[i] - ray.origin[i]) * invD;
            if (invD < 0.0f) std::swap(t0, t1);
            tMin = std::max(tMin, t0);
            tMax = std::min(tMax, t1);
            if (tMax < tMin) return false;
        }
        return true;
    }
};

struct BVHNode {
    AABB bounds;
    BVHNode* left = nullptr;
    BVHNode* right = nullptr;
    int startIdx = 0, endIdx = 0; // triangle range for leaf

    bool isLeaf() const { return left == nullptr && right == nullptr; }
};

class BVH {
public:
    BVH() = default;
    ~BVH();

    void build(std::vector<Triangle>& triangles);
    Intersection intersect(const Ray& ray) const;

private:
    BVHNode* buildRecursive(std::vector<Triangle>& tris, int start, int end);
    Intersection intersectNode(const BVHNode* node, const Ray& ray) const;
    void deleteNode(BVHNode* node);

    BVHNode* root = nullptr;
    std::vector<Triangle>* triangles = nullptr;
};
