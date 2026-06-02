# Minipathtracer 最终收尾修复任务提示词

你是一个 C++ 代码重构专家。以下是针对 Minipathtracer 项目的**最终收尾修复清单**，只包含低风险、高收益的任务。

**重要原则：**
- 严格按照顺序执行，每完成一项后确认编译通过再继续
- 不要修改任何核心路径追踪逻辑（castRay、BSDF、BVH 遍历）
- 不要改变现有 JSON 格式结构（字段仍平铺，不新增嵌套对象）
- 不要移动 createAreaLight / sampleAreaLight
- 遇到"sign convention 可能不同"的函数替换，跳过，不要做

---

## 项目背景

这是一个 C++ Monte Carlo 路径追踪渲染器，渲染海面日落场景。主要文件结构：

```
src/
  Scene.cpp           ← castRay() 主路径追踪（不要动核心逻辑）
  SceneLoading.cpp    ← loadFromConfig / loadOBJ / addPlane
  Sky.cpp             ← skyColor() / sunDiskColor() / createAreaLight / sampleAreaLight
  AircraftShaders.cpp ← 飞机材质 shader
  Diagnostics.cpp     ← 诊断和 debug 工具
  MtlConverter.cpp    ← MTL 参数转换和材质名识别
  MeshUtils.cpp       ← 切线计算和法线贴图应用
  Material.cpp        ← BSDF sample/eval/pdf
  Renderer.cpp        ← 渲染循环和 tone mapping
  JsonParser.cpp      ← JSON 解析器
include/
  Scene.h             ← SceneConfig + Scene 类声明
  ScenePrivateUtils.h ← 工具函数
config/
  scene.json
  locked_settings.json
```

---

## P0 级修复（最先执行）

### 任务 P0-1：修复 `skipValue()` 嵌套数组提前退出 bug

**文件：** `src/JsonParser.cpp`，约第 185-196 行

**问题：**
数组跳过逻辑在进入深度计数循环之前，先读取并丢弃了数组的第一个元素 token。如果第一个元素是数组或对象（如 `[[1,2],[3,4]]`），第一个 `[` 被丢弃，深度计数少一层，过早退出，静默丢失数据。当前 config 文件不触发此 bug，但逻辑脆弱。

**修复：**
将数组跳过逻辑改为纯深度计数器，不提前消费第一个元素：

```cpp
// 修复前（问题逻辑）：
} else if (t.type == Token::LBRACKET) {
    t = lex.next();
    if (t.type == Token::RBRACKET) return;
    int depth = 1;
    while (depth > 0) {
        t = lex.next();
        if (t.type == Token::LBRACKET || t.type == Token::LBRACE) depth++;
        else if (t.type == Token::RBRACKET || t.type == Token::RBRACE) depth--;
    }
}

// 修复后（正确的深度计数器）：
} else if (t.type == Token::LBRACKET) {
    int depth = 1;
    while (depth > 0) {
        t = lex.next();
        if (t.type == Token::LBRACKET || t.type == Token::LBRACE) depth++;
        else if (t.type == Token::RBRACKET || t.type == Token::RBRACE) depth--;
        else if (t.type == Token::END) return; // 防止无限循环
    }
}
```

**验证：** 编译通过，现有 `scene.json` 和 `locked_settings.json` 仍能正常解析，渲染结果不变。

---

## P1 级修复（高优先级）

### 任务 P1-1：修复 `canopySmokedCenter` 未被读取

**文件：** `src/AircraftShaders.cpp`，约第 94 行

**问题：**
`shadeCanopyGlassPhysical()` 中硬编码了 `smokedCenter` 的值，`locked_settings.json` 里配置的 `canopySmokedCenter` 参数完全未被读取，修改 JSON 无效。

**修复：**
找到：
```cpp
const Vec3f smokedCenter(0.01f, 0.025f, 0.04f);
```
替换为：
```cpp
const Vec3f smokedCenter = config.canopySmokedCenter;
```

`shadeCanopyGlassPhysical()` 已经可以访问 `config`，无需额外传参。

**顺带修复：** 将 `include/Scene.h` 中 `SceneConfig` 的 `canopySmokedCenter` 默认值改为与 `locked_settings.json` 一致：
```cpp
Vec3f canopySmokedCenter = Vec3f(0.001f, 0.003f, 0.008f);
```

**验证：** 将 `locked_settings.json` 中 `canopySmokedCenter` 改为 `[1.0, 0.0, 0.0]`，渲染后 canopy 玻璃暗部应呈红色。改回原值后结果恢复正常。

---

### 任务 P1-2：删除三个死 SkyConfig 字段

**涉及文件：**
- `include/Scene.h`（SkyConfig struct）
- `src/JsonParser.cpp`（parseSky 函数）
- `config/scene.json` 和 `config/locked_settings.json`（如有相关字段）

**问题：**
以下三个字段被解析进 `SkyConfig` 但从未被 `Sky.cpp` 读取。当前 `skyColor()` 使用物理大气散射模型，这三个是旧梯度天空模型的遗留，修改它们没有任何效果：
- `SkyConfig::topColor`
- `SkyConfig::bottomColor`
- `SkyConfig::sunDiskPower`

**修复步骤：**

1. 在 `include/Scene.h` 的 `SkyConfig` struct 中删除这三行：
```cpp
Vec3f topColor     = Vec3f(0.08f, 0.10f, 0.23f);
Vec3f bottomColor  = Vec3f(0.95f, 0.62f, 0.25f);
float sunDiskPower = 500.0f;
```

2. 在 `src/JsonParser.cpp` 的 `parseSky()` 中删除对应的三行解析：
```cpp
else if (key == "topColor")     cfg.sky.topColor     = parseVec3(lex);
else if (key == "bottomColor")  cfg.sky.bottomColor  = parseVec3(lex);
else if (key == "sunDiskPower") cfg.sky.sunDiskPower = expectNumber(lex);
```

3. 在 `config/scene.json` 和 `config/locked_settings.json` 中搜索并删除 `topColor`、`bottomColor`、`sunDiskPower` 字段（如有）。

**验证：** 编译通过，渲染结果不变（因为这些字段本就没有被使用）。

---

### 任务 P1-3：为距离雾添加 `depth == 0` 守卫

**文件：** `src/Scene.cpp`，约第 329-335 行

**问题：**
距离雾计算块没有深度守卫，间接光（bounce rays）在每次递归中都被独立雾化，导致深度方向过暗。

**修复：**
找到距离雾代码块：
```cpp
// Distance fog
{
    float fogDensity = 0.003f;
    float fogAmount = 1.0f - std::exp(-isect.t * fogDensity);
    Vec3f fogColor = config.sky.horizonColor * 0.35f;
    result = result * (1.0f - fogAmount) + fogColor * fogAmount;
}
```

在整个代码块外层添加深度守卫：
```cpp
// Distance fog — only apply to primary rays
if (depth == 0) {
    float fogDensity = 0.003f;
    float fogAmount = 1.0f - std::exp(-isect.t * fogDensity);
    Vec3f fogColor = config.sky.horizonColor * 0.35f;
    result = result * (1.0f - fogAmount) + fogColor * fogAmount;
}
```

**验证：** 渲染结果中远处物体仍有雾化效果，整体画面间接光不再偏暗。

---

## P2 级修复（中优先级）

### 任务 P2-1：提取水面法线重复计算

**文件：** `src/Scene.cpp`，第 71-84 行 和 第 108-125 行

**问题：**
`castRay()` 中有两处完全相同的水面法线混合计算逻辑。

**修复步骤：**

1. 在 `include/Scene.h` 的 `Scene` class private 区域添加声明：
```cpp
Vec3f computeWaterNormal(const Vec3f& hitPoint) const;
```

2. 在 `src/Scene.cpp` 中实现（从重复代码提取）：
```cpp
Vec3f Scene::computeWaterNormal(const Vec3f& hitPoint) const {
    Vec3f largeNormal = ocean->getNormal(hitPoint.x, hitPoint.z);
    Vec3f waterNormal = largeNormal;
    if (oceanRipple) {
        Vec3f rippleNormal = oceanRipple->getNormal(hitPoint.x, hitPoint.z);
        float ls = std::max(0.0f, config.waterLargeWaveScale);
        float ss = std::max(0.0f, config.waterSmallWaveScale);
        waterNormal = normalize(Vec3f(
            largeNormal.x * ls + rippleNormal.x * ss,
            1.0f,
            largeNormal.z * ls + rippleNormal.z * ss
        ));
    }
    return waterNormal;
}
```

3. 将 `castRay()` 中两处重复的水面法线计算替换为 `computeWaterNormal(hitPoint)` 调用。

**验证：** 编译通过，水面渲染结果不变。

---

### 任务 P2-2：清理 `parseObjects()` 死代码

**文件：** `src/JsonParser.cpp`，约第 333-339 行

**问题：**
`parseObjects()` 头部有不完整重构留下的死代码块：一个空的 `if (t.type == Token::COMMA)` 分支和残骸注释，不影响运行但令人困惑。

**修复：**
找到以下死代码块：
```cpp
if (t.type == Token::LBRACE || t.type == Token::COMMA) {
    if (t.type == Token::COMMA) {
        // next should be LBRACE
    }
}
// Simpler: re-structure
// We already consumed first token
if (t.type != Token::RBRACKET) {
```

删除最外层空 if 块和残骸注释，只保留 `if (t.type != Token::RBRACKET)` 判断。

**验证：** 编译通过，JSON 解析结果不变。

---

### 任务 P2-3：从 `locked_settings.json` 删除 debug=false 字段

**文件：** `config/locked_settings.json`

**问题：**
4 个 debug 字段均设为 false，这些字段的默认值本就是 false，显式存储无意义，且容易被误改为 true 后忘记改回来。

**修复：**
从 `locked_settings.json` 中删除以下行：
```json
"debugMaterialRoles": false,
"debugCanopyExtreme": false,
"debugPropellerAfterimageExtreme": false,
"debugFakeCockpitPattern": false,
```

**验证：** 渲染结果不变，因为默认值本就是 false。

---

### 任务 P2-4：为 `parseRender()` 添加分区注释

**文件：** `src/JsonParser.cpp`，`parseRender()` 函数内

**问题：**
`parseRender()` 包含约 80 个字段，水体参数、canopy 参数、aircraft 参数、debug 参数全部平铺，没有任何分隔，极难维护。

**注意：不要改变 JSON 格式，不要新增嵌套对象，不要拆出新的 parse 子函数。** 只在现有 `else if` 链中添加注释分区。

**修复：**
在 `parseRender()` 的 `else if` 链中，在对应字段前插入分区注释：

```cpp
// ===== Render Core =====
if (key == "width") ...
else if (key == "height") ...
// ...

// ===== Tone Mapping =====
else if (key == "toneMapping") ...
// ...

// ===== Normal Map =====
else if (key == "normalStrength") ...
// ...

// ===== Canopy Glass =====
else if (key == "canopyGlassUsePhysical") ...
// ...

// ===== Propeller Afterimage =====
else if (key == "propellerAfterimageColor") ...
// ...

// ===== Water =====
else if (key == "waterReflectionStrength") ...
// ...

// ===== Environment =====
else if (key == "skyFillStrength") ...
// ...

// ===== Debug =====
else if (key == "debugMaterialRoles") ...
// ...
```

**验证：** 编译通过（纯注释改动），功能不变。

---

### 任务 P2-5：添加 `rotation` 未实现警告

**文件：** `src/SceneLoading.cpp`，约第 244 行

**问题：**
`entry.rotation` 从 JSON 解析后被完全忽略，用户在 scene.json 设置 rotation 后无任何反馈。

**修复：**
在变换应用处，添加运行时警告（只在 rotation 非零时输出）：

```cpp
// Apply transform: scale -> translate (rotation not yet implemented)
if (std::fabs(entry.rotation.x) > 1e-4f ||
    std::fabs(entry.rotation.y) > 1e-4f ||
    std::fabs(entry.rotation.z) > 1e-4f) {
    std::cout << "WARNING: rotation field is not yet implemented and will be "
              << "ignored for: " << entry.file << std::endl;
}
pos = pos * entry.scale + entry.position;
```

**验证：** 在 scene.json 的某个 object 中设置 `"rotation": [0, 45, 0]`，确认控制台输出警告。改回 `[0, 0, 0]` 后无警告。

---

## P3 级修复（低优先级，可选）

### 任务 P3-1：为 `sky.enabled` 添加检查

**文件：** `src/Sky.cpp`，`skyColor()` 函数入口

**修复：**
在 `skyColor()` 函数第一行添加：
```cpp
Vec3f Scene::skyColor(const Vec3f& direction) const {
    if (!config.sky.enabled) {
        return Vec3f(0.0f);
    }
    // ... 原有代码
}
```

**验证：** 在 `scene.json` 中设置 `"enabled": false`（在 sky 块内），渲染结果变为黑色背景。

---

### 任务 P3-2：为 AircraftShaders 硬编码参数添加注释

**文件：** `src/AircraftShaders.cpp`

**修复：**
在 `shadeCanopyGlassFake()` 和 `shadePropellerAfterimage()` 中，在硬编码的艺术参数旁添加 `// ART CONSTANT` 注释，明确标明这些不来自 JSON 配置：

```cpp
// ART CONSTANT: glass tint, not configurable via JSON
const Vec3f glassTint(0.30f, 0.50f, 0.85f);

// ART CONSTANT: cockpit interior base color
const Vec3f cockpitBase(0.001f, 0.003f, 0.008f);
```

同时将 `shadePropellerAfterimage()` 中过长的 fix history 注释块（约 20 行）缩短为简洁的行为说明：
```cpp
// Semi-transparent grey overlay. Alpha and color from scene.json:
// propellerAfterimageAlpha, propellerAfterimageColor, propellerAfterimageStackReduction.
// Additive term (+0.15 * grey) ensures visibility against dark blade backgrounds.
```

**验证：** 纯注释改动，渲染结果不变。

---

## 明确不在本轮执行的内容

以下任务**不要执行**，已移至 Future Work：

| 任务 | 原因 |
|------|------|
| `fresnelReflectance` 替换为 `fresnelExact` | sign convention 可能不同，有改变玻璃/水面采样的风险 |
| BVH 双重遍历修复（castRay 改签名） | 改动递归接口，风险大于收益，不在最终阶段执行 |
| 移动 `createAreaLight` / `sampleAreaLight` | 架构整理，非 bug，最终阶段不开刀 |
| `printVec3` 合并到 `ScenePrivateUtils.h` | 会让数学工具头依赖 `<iostream>`，不值得 |
| `parseRender()` 拆子函数 | 需要同步改 JSON 格式或保留平铺，本轮只加注释 |
| `applyMaterialNormalMap` 函数签名修改 | 改动 MeshUtils 接口，影响调用链，不在本轮 |
| SceneConfig 子结构化（WaterConfig 等） | 改动量太大，影响 JsonParser + 所有调用处 |
| Ocean 参数配置化 | 需要新增字段+调参验证，单独任务 |

---

## 验证清单

完成所有修复后，按顺序验证：

1. **编译验证：** `cmake --build . --config Release` 无错误、无新增警告
2. **渲染基线对比：** 使用默认配置渲染，P0/P2/P3 修复不应改变画面，P1-1 会改变 canopy 玻璃暗部颜色（因为 smokedCenter 现在从 config 读取）
3. **canopySmokedCenter 生效验证：** 将 `locked_settings.json` 中 `canopySmokedCenter` 临时改为 `[1.0, 0.0, 0.0]`，确认 canopy 暗部变红
4. **雾深度验证：** 对比修复前后，间接光区域（阴影内）不再偏暗
5. **JSON 解析验证（P0-1）：** 在 scene.json 中临时添加一个嵌套数组字段，确认解析不报错也不丢失后续字段
6. **rotation 警告验证：** 设置 `"rotation": [0, 45, 0]`，确认控制台有 WARNING 输出

---

## Future Work（记录但本轮不执行）

以下是审查中发现的、值得未来处理的改进项，供后续参考：

- **BVH 双重遍历优化：** 修改 `castRay` 签名接受 `precomputedIsect`，可提升约 15-25% 渲染性能
- **`parseRender()` 拆子函数：** 先把 JSON 格式整理为嵌套对象，再拆 parse 函数
- **SceneConfig 子结构化：** 添加 `WaterConfig`、`AircraftConfig`、`CanopyConfig`，与 `SkyConfig` 对等
- **Ocean 参数配置化：** 把硬编码的分辨率、风速、振幅等移入 SceneConfig
- **距离雾参数化：** 添加 `fogEnabled`、`fogDensity`、`fogStrength` 配置字段
- **工具函数统一：** `fresnelReflectance` 与 `fresnelExact` 统一，需先仔细对比 sign convention
- **`createAreaLight` / `sampleAreaLight` 移至 Lighting.cpp：** 架构整理，非紧急

---

*本提示词基于 Final Code Review Report (2026-06-03) 及后续审查建议生成，为最终收尾版本。*
