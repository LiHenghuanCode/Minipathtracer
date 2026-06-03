#include "core/BVH.h"
#include <algorithm>
#include <iostream>

BVH::~BVH() {
    if (root) deleteNode(root);
}

void BVH::deleteNode(BVHNode* node) {
    if (!node) return;
    deleteNode(node->left);
    deleteNode(node->right);
    delete node;
}

void BVH::build(std::vector<Triangle>& tris) {
    if (tris.empty()) return;
    triangles = &tris;
    std::cout << "Building BVH for " << tris.size() << " triangles..." << std::endl;
    root = buildRecursive(tris, 0, (int)tris.size());
    std::cout << "BVH build complete." << std::endl;
}

BVHNode* BVH::buildRecursive(std::vector<Triangle>& tris, int start, int end) {
    BVHNode* node = new BVHNode();
    node->startIdx = start;
    node->endIdx = end;

    // Compute bounds
    for (int i = start; i < end; ++i) {
        node->bounds.expand(tris[i]);
    }

    int count = end - start;
    if (count <= 4) {
        // Leaf node
        return node;
    }

    // Split on longest axis at midpoint
    int axis = node->bounds.longestAxis();
    float mid = node->bounds.centroid()[axis];

    // Partition triangles
    auto it = std::partition(tris.begin() + start, tris.begin() + end,
        [axis, mid](const Triangle& tri) {
            return tri.centroid()[axis] < mid;
        });

    int splitIdx = (int)(it - tris.begin());

    // If partition failed, split in half
    if (splitIdx == start || splitIdx == end) {
        splitIdx = start + count / 2;
        std::nth_element(tris.begin() + start, tris.begin() + splitIdx, tris.begin() + end,
            [axis](const Triangle& a, const Triangle& b) {
                return a.centroid()[axis] < b.centroid()[axis];
            });
    }

    node->left = buildRecursive(tris, start, splitIdx);
    node->right = buildRecursive(tris, splitIdx, end);
    return node;
}

Intersection BVH::intersect(const Ray& ray) const {
    if (!root) return Intersection{};
    return intersectNode(root, ray);
}

Intersection BVH::intersectNode(const BVHNode* node, const Ray& ray) const {
    Intersection closest;

    if (!node->bounds.intersect(ray, 1e-4f, closest.t)) {
        return closest;
    }

    if (node->isLeaf()) {
        for (int i = node->startIdx; i < node->endIdx; ++i) {
            float t, u, v;
            if (rayTriangleIntersect(ray, (*triangles)[i], t, u, v)) {
                if (t < closest.t) {
                    closest.hit = true;
                    closest.t = t;
                    closest.position = ray.at(t);
                    closest.interpolate((*triangles)[i], u, v);
                }
            }
        }
        return closest;
    }

    Intersection hitLeft = intersectNode(node->left, ray);
    Intersection hitRight = intersectNode(node->right, ray);

    if (hitLeft.hit && hitRight.hit) {
        return hitLeft.t < hitRight.t ? hitLeft : hitRight;
    }
    if (hitLeft.hit) return hitLeft;
    return hitRight;
}
