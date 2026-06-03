#include "environment/Ocean.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float PI = 3.14159265358979323846f;
constexpr float G = 9.81f;
}

Ocean::Ocean(int N, float L, float windSpeed, Vec3f windDir, float amplitude, float time)
    : N(N),
      L(L),
      windSpeed(windSpeed),
      windDir(Vec3f(windDir.x, 0.0f, windDir.z).normalized()),
      amplitude(amplitude),
      time(time),
      gen(42),
      dist(0.0f, 1.0f) {
    const int size = this->N * this->N;
    h0Tilde.resize(size);
    h0TildeConj.resize(size);
    hTilde.resize(size);
    heightMap.resize(size, 0.0f);
    normalMap.resize(size, Vec3f(0.0f, 1.0f, 0.0f));

    if (this->windDir.length2() < 1e-8f) {
        this->windDir = Vec3f(1.0f, 0.0f, 0.0f);
    }
}

void Ocean::generate() {
    generateSpectrum();
    evaluateWavesAtTime(time);
    fft2D(hTilde, true);

    // Shift the FFT result back into a spatial height field.
    for (int m = 0; m < N; ++m) {
        for (int n = 0; n < N; ++n) {
            float sign = ((m + n) % 2 == 0) ? 1.0f : -1.0f;
            heightMap[index(m, n)] = hTilde[index(m, n)].real * sign;
        }
    }

    // Normalize the generated height field so the configured amplitude maps to a visible wave range.
    float minH = heightMap[0], maxH = heightMap[0];
    for (int i = 1; i < N * N; ++i) {
        minH = std::min(minH, heightMap[i]);
        maxH = std::max(maxH, heightMap[i]);
    }
    float range = maxH - minH;
    if (range > 1e-10f) {
        float desiredHeight = amplitude; // Treat amplitude as the target wave-height range in world units.
        float scale = desiredHeight / range;
        for (int i = 0; i < N * N; ++i) {
            heightMap[i] *= scale;
        }
    }

    computeNormals();
}

Vec3f Ocean::getNormal(float worldX, float worldZ) const {
    float u = std::fmod(worldX / L + 0.5f, 1.0f);
    float v = std::fmod(worldZ / L + 0.5f, 1.0f);
    if (u < 0.0f) u += 1.0f;
    if (v < 0.0f) v += 1.0f;

    float x = u * N;
    float z = v * N;

    int x0 = static_cast<int>(std::floor(x)) % N;
    int z0 = static_cast<int>(std::floor(z)) % N;
    int x1 = (x0 + 1) % N;
    int z1 = (z0 + 1) % N;

    float tx = x - std::floor(x);
    float tz = z - std::floor(z);

    const Vec3f& n00 = normalMap[index(z0, x0)];
    const Vec3f& n10 = normalMap[index(z0, x1)];
    const Vec3f& n01 = normalMap[index(z1, x0)];
    const Vec3f& n11 = normalMap[index(z1, x1)];

    Vec3f nx0 = n00 * (1.0f - tx) + n10 * tx;
    Vec3f nx1 = n01 * (1.0f - tx) + n11 * tx;
    return normalize(nx0 * (1.0f - tz) + nx1 * tz);
}

float Ocean::phillips(float kx, float kz) const {
    float k2 = kx * kx + kz * kz;
    if (k2 < 1e-12f) {
        return 0.0f;
    }

    float k = std::sqrt(k2);
    float Lphillips = (windSpeed * windSpeed) / G;
    float smallWave = L / 1000.0f;

    Vec3f kHat(kx / k, 0.0f, kz / k);
    float directional = std::max(0.0f, dot(kHat, windDir));
    directional = directional * directional;

    // Dampen extremely high frequencies so the surface does not turn into noise.
    float dampHigh = std::exp(-k2 * smallWave * smallWave);
    // Dampen the lowest frequencies to avoid the Phillips spectrum over-concentrating energy near zero.
    float kMin = 2.0f * PI / L;  // Smallest useful wavenumber represented by this patch.
    float dampLow = 1.0f - std::exp(-k2 / (kMin * kMin * 4.0f));

    float spectrum = amplitude * std::exp(-1.0f / (k2 * Lphillips * Lphillips)) / (k2 * k2);
    return spectrum * directional * dampHigh * dampLow;
}

Complex Ocean::gaussianRandom() {
    float r1 = std::max(dist(gen), 1e-6f);
    float r2 = dist(gen);
    float mag = std::sqrt(-2.0f * std::log(r1));
    float angle = 2.0f * PI * r2;
    return Complex(mag * std::cos(angle), mag * std::sin(angle));
}

void Ocean::generateSpectrum() {
    for (int m = 0; m < N; ++m) {
        for (int n = 0; n < N; ++n) {
            float kx = (2.0f * PI / L) * (m - N / 2);
            float kz = (2.0f * PI / L) * (n - N / 2);
            float k = std::sqrt(kx * kx + kz * kz);
            int idx = index(m, n);

            if (k < 1e-6f) {
                h0Tilde[idx] = Complex(0.0f, 0.0f);
                h0TildeConj[idx] = Complex(0.0f, 0.0f);
            } else {
                float ph = std::sqrt(phillips(kx, kz)) / std::sqrt(2.0f);
                Complex xi = gaussianRandom();
                h0Tilde[idx] = xi * ph;
                h0TildeConj[idx] = xi.conjugate() * ph;
            }
        }
    }
}

void Ocean::evaluateWavesAtTime(float t) {
    for (int m = 0; m < N; ++m) {
        for (int n = 0; n < N; ++n) {
            float kx = (2.0f * PI / L) * (m - N / 2);
            float kz = (2.0f * PI / L) * (n - N / 2);
            float k = std::sqrt(kx * kx + kz * kz);
            float omega = std::sqrt(G * k);
            Complex exp_iwt(std::cos(omega * t), std::sin(omega * t));
            Complex exp_neg_iwt(std::cos(omega * t), -std::sin(omega * t));

            int idx = index(m, n);
            int mirroredM = N - 1 - m;
            int mirroredN = N - 1 - n;
            int mirroredIdx = index(mirroredM, mirroredN);

            hTilde[idx] = h0Tilde[idx] * exp_iwt + h0TildeConj[mirroredIdx] * exp_neg_iwt;
        }
    }
}

void Ocean::fft1D(Complex* data, int size, bool inverse) {
    for (int i = 1, j = 0; i < size; ++i) {
        int bit = size >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }

    for (int len = 2; len <= size; len <<= 1) {
        float angle = (inverse ? 2.0f : -2.0f) * PI / len;
        Complex wlen(std::cos(angle), std::sin(angle));

        for (int i = 0; i < size; i += len) {
            Complex w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j) {
                Complex u = data[i + j];
                Complex v = data[i + j + len / 2] * w;
                data[i + j] = u + v;
                data[i + j + len / 2] = u - v;
                w = w * wlen;
            }
        }
    }

    if (inverse) {
        for (int i = 0; i < size; ++i) {
            data[i].real /= size;
            data[i].imag /= size;
        }
    }
}

void Ocean::fft2D(std::vector<Complex>& data, bool inverse) {
    std::vector<Complex> scratch(N);

    for (int row = 0; row < N; ++row) {
        fft1D(&data[row * N], N, inverse);
    }

    for (int col = 0; col < N; ++col) {
        for (int row = 0; row < N; ++row) {
            scratch[row] = data[index(row, col)];
        }
        fft1D(scratch.data(), N, inverse);
        for (int row = 0; row < N; ++row) {
            data[index(row, col)] = scratch[row];
        }
    }
}

void Ocean::computeNormals() {
    float cellSize = L / static_cast<float>(N);
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            float hL = heightMap[index(i, (j - 1 + N) % N)];
            float hR = heightMap[index(i, (j + 1) % N)];
            float hD = heightMap[index((i + 1) % N, j)];
            float hU = heightMap[index((i - 1 + N) % N, j)];
            float dx = (hR - hL) / (2.0f * cellSize);
            float dz = (hD - hU) / (2.0f * cellSize);
            normalMap[index(i, j)] = normalize(Vec3f(-dx, 1.0f, -dz));
        }
    }
}

int Ocean::index(int row, int col) const {
    return row * N + col;
}
