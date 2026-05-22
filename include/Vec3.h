#pragma once
#include <cmath>
#include <iostream>
#include <algorithm>

struct Vec3f {
    float x, y, z;

    Vec3f() : x(0), y(0), z(0) {}
    Vec3f(float v) : x(v), y(v), z(v) {}
    Vec3f(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3f operator+(const Vec3f& v) const { return {x + v.x, y + v.y, z + v.z}; }
    Vec3f operator-(const Vec3f& v) const { return {x - v.x, y - v.y, z - v.z}; }
    Vec3f operator*(const Vec3f& v) const { return {x * v.x, y * v.y, z * v.z}; }
    Vec3f operator*(float t) const { return {x * t, y * t, z * t}; }
    Vec3f operator/(float t) const { float inv = 1.0f / t; return {x * inv, y * inv, z * inv}; }
    Vec3f operator-() const { return {-x, -y, -z}; }

    Vec3f& operator+=(const Vec3f& v) { x += v.x; y += v.y; z += v.z; return *this; }
    Vec3f& operator*=(float t) { x *= t; y *= t; z *= t; return *this; }

    float dot(const Vec3f& v) const { return x * v.x + y * v.y + z * v.z; }
    Vec3f cross(const Vec3f& v) const {
        return {y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x};
    }

    float length() const { return std::sqrt(x * x + y * y + z * z); }
    float length2() const { return x * x + y * y + z * z; }

    Vec3f normalized() const {
        float len = length();
        if (len < 1e-8f) return {0, 0, 0};
        return *this / len;
    }

    float max_component() const { return std::max({x, y, z}); }

    float& operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }
};

inline Vec3f operator*(float t, const Vec3f& v) { return v * t; }

inline float dot(const Vec3f& a, const Vec3f& b) { return a.dot(b); }
inline Vec3f cross(const Vec3f& a, const Vec3f& b) { return a.cross(b); }
inline Vec3f normalize(const Vec3f& v) { return v.normalized(); }

inline Vec3f vec_min(const Vec3f& a, const Vec3f& b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}
inline Vec3f vec_max(const Vec3f& a, const Vec3f& b) {
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

inline Vec3f reflect(const Vec3f& I, const Vec3f& N) {
    return I - 2.0f * dot(I, N) * N;
}

struct Ray {
    Vec3f origin, direction;
    Ray() {}
    Ray(const Vec3f& o, const Vec3f& d) : origin(o), direction(d) {}
    Vec3f at(float t) const { return origin + direction * t; }
};
