#pragma once
#include "core/Vec3.h"

struct Triangle {
    Vec3f v0, v1, v2;       // Vertex positions in world space.
    Vec3f n0, n1, n2;       // Per-vertex shading normals.
    Vec3f tangent, bitangent;
    float u0, v0t, u1, v1t, u2, v2t; // Per-vertex UV coordinates.
    bool hasTangent = false;
    int materialId;

    Triangle() : u0(0), v0t(0), u1(0), v1t(0), u2(0), v2t(0), materialId(-1) {}

    Vec3f centroid() const { return (v0 + v1 + v2) / 3.0f; }
};

struct Intersection {
    bool hit = false;
    float t = 1e30f;
    Vec3f position;
    Vec3f normal;
    Vec3f tangent, bitangent;
    float u = 0, v = 0;  // Barycentric coordinates for the hit point.
    float texU = 0, texV = 0; // Interpolated UV coordinates at the hit point.
    bool hasTangent = false;
    int materialId = -1;

    // Copies interpolated shading data from the source triangle into the hit record.
    void interpolate(const Triangle& tri, float uu, float vv) {
        float w = 1.0f - uu - vv;
        normal = (tri.n0 * w + tri.n1 * uu + tri.n2 * vv).normalized();
        tangent = tri.tangent;
        bitangent = tri.bitangent;
        texU = tri.u0 * w + tri.u1 * uu + tri.u2 * vv;
        texV = tri.v0t * w + tri.v1t * uu + tri.v2t * vv;
        hasTangent = tri.hasTangent;
        materialId = tri.materialId;
        u = uu;
        v = vv;
    }
};

// Standard Moller-Trumbore ray-triangle intersection test.
inline bool rayTriangleIntersect(const Ray& ray, const Triangle& tri,
                                  float& t, float& u, float& v) {
    Vec3f edge1 = tri.v1 - tri.v0;
    Vec3f edge2 = tri.v2 - tri.v0;
    Vec3f pvec = cross(ray.direction, edge2);
    float det = dot(edge1, pvec);

    if (std::fabs(det) < 1e-8f) return false;

    float invDet = 1.0f / det;
    Vec3f tvec = ray.origin - tri.v0;
    u = dot(tvec, pvec) * invDet;
    if (u < 0.0f || u > 1.0f) return false;

    Vec3f qvec = cross(tvec, edge1);
    v = dot(ray.direction, qvec) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;

    t = dot(edge2, qvec) * invDet;
    return t > 1e-4f;
}
