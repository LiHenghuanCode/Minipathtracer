#pragma once

#include "Vec3.h"
#include "Triangle.h"
#include "Material.h"
#include "Scene.h"

void computeTriangleTangents(Triangle& tri);

Vec3f applyMaterialNormalMap(
    const Material& mat,
    const Intersection& isect,
    const Vec3f& normal,
    const SceneConfig& config
);
