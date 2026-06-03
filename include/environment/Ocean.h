#pragma once
#include "core/Vec3.h"
#include <vector>
#include <random>

struct Complex {
    float real, imag;

    Complex() : real(0.0f), imag(0.0f) {}
    Complex(float r, float i) : real(r), imag(i) {}

    Complex operator+(const Complex& other) const {
        return Complex(real + other.real, imag + other.imag);
    }

    Complex operator-(const Complex& other) const {
        return Complex(real - other.real, imag - other.imag);
    }

    Complex operator*(const Complex& other) const {
        return Complex(real * other.real - imag * other.imag,
                       real * other.imag + imag * other.real);
    }

    Complex operator*(float scalar) const {
        return Complex(real * scalar, imag * scalar);
    }

    Complex& operator+=(const Complex& other) {
        real += other.real;
        imag += other.imag;
        return *this;
    }

    Complex conjugate() const {
        return Complex(real, -imag);
    }
};

class Ocean {
public:
    Ocean(int N = 256, float L = 64.0f, float windSpeed = 10.0f,
          Vec3f windDir = Vec3f(1.0f, 0.0f, 0.0f), float amplitude = 0.0003f,
          float time = 5.0f);

    void generate();
    Vec3f getNormal(float worldX, float worldZ) const;

private:
    float phillips(float kx, float kz) const;
    Complex gaussianRandom();
    void generateSpectrum();
    void evaluateWavesAtTime(float t);
    void fft1D(Complex* data, int size, bool inverse);
    void fft2D(std::vector<Complex>& data, bool inverse);
    void computeNormals();
    int index(int row, int col) const;

    std::vector<Complex> h0Tilde;
    std::vector<Complex> h0TildeConj;
    std::vector<Complex> hTilde;
    std::vector<float> heightMap;
    std::vector<Vec3f> normalMap;
    int N;
    float L;
    float windSpeed;
    Vec3f windDir;
    float amplitude;
    float time;
    std::mt19937 gen;
    std::uniform_real_distribution<float> dist;
};
