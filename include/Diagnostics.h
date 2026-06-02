#pragma once

#include "Vec3.h"
#include "Material.h"

const char* materialTypeName(MaterialType type);
const char* materialRoleName(MaterialRole role);
Vec3f materialRoleDebugColor(MaterialRole role);
int materialRoleIndex(MaterialRole role);
