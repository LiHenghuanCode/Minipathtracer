#pragma once

#include "Vec3.h"
#include "Material.h"
#include <string>

// Converts a string to lowercase for material name matching.
std::string lowercase(std::string value);

// Converts an MTL specular exponent into a roughness value.
float roughnessFromNs(float ns);

// Derives a glossy weight from an MTL specular color.
float glossyWeightFromKs(const Vec3f& ks);

// Applies project-specific overrides based on material names.
void applyMaterialNameOverride(Material& mat);
