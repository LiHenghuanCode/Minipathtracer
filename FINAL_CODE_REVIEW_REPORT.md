# Final Code Review Report

> 审查日期：2026-06-03
> 审查范围：完整项目代码，经多轮重构后的最终状态
> 审查原则：只读分析，不修改代码

---

## 1. 总体结论

经过多轮重构，项目的整体架构已经显著改善：文件职责边界大体清晰，`castRay()` 已从一个混乱的单体函数拆分为由多个 shader 子函数协作的结构，`JsonParser` 具备了完整的词法分析器，mist 相关代码已彻底清除。

然而仍然存在若干需要修复的问题：

- **有 3 个 config 字段被解析入 SceneConfig 但从未被任何 C++ 代码读取**（死配置参数），会让调参者困惑；
- **存在 1 个 P0 级 parser 逻辑缺陷**（嵌套数组 skipValue 提前退出），目前不影响当前 JSON 格式，但脆弱；
- **BVH 在每次 bounce 中被调用两次**（性能 P1）；
- **距离雾被应用到所有 bounce 深度**，不仅是主光线，物理上不正确；
- `safeNormalize`、`fresnelReflectance` 等工具函数被重复定义；
- `createAreaLight()` 和 `sampleAreaLight()` 放在 Sky.cpp 中，职责不匹配。

---

## 2. 优先级最高的问题

| 优先级 | 问题 | 文件 | 影响 | 建议 |
|--------|------|------|------|------|
| P0 | `skipValue()` 嵌套数组提前退出 | `JsonParser.cpp:185-196` | 如果 scene.json 中有 Vec3 数组作为对象第一个字段（如 `"objects": [[...]]`），解析会提前结束，导致静默丢失数据 | 修正数组跳过逻辑，消费第一个元素前先 push-back 或重构为深度计数器 |
| P1 | BVH 对 bounceRay 被调用两次 | `Scene.cpp:178-179` | 每次 bounce 做两次 BVH 遍历；`bounceIsect` 和 `castRay` 内部各调用一次，性能浪费 100% | 将 `bounceIsect` 传入 castRay，或重构让 castRay 返回已有的 intersection 信息 |
| P1 | `canopySmokedCenter` 配置字段从未被读取 | `Scene.h:72`, `AircraftShaders.cpp:94` | `locked_settings.json` 里设置的 `canopySmokedCenter` 完全无效；shader 里硬编码了 `Vec3f smokedCenter(0.01f, 0.025f, 0.04f)` | 在 `shadeCanopyGlassPhysical()` 中把硬编码的 smokedCenter 替换为 `config.canopySmokedCenter` |
| P1 | `SkyConfig.topColor`、`bottomColor`、`sunDiskPower` 被解析但从未读取 | `Scene.h:101-107`, `Sky.cpp` | 这三个字段是旧梯度天空模型的遗留；当前物理大气散射模型完全不读它们；改这些值没有任何效果 | 从 SkyConfig struct 和 parseSky 中删除这三个字段 |
| P1 | 距离雾被应用到所有 bounce 深度 | `Scene.cpp:329-335` | `fogAmount` 计算在每次 `castRay(depth+n)` 中独立运行，导致间接光被叠加雾化。`depth>0` 的 bounce 实际上返回的是已被雾化的间接光，再在 `depth=0` 时再次雾化，导致深度方向变暗过重 | 添加 `if (depth == 0)` 守卫，仅对主光线应用雾 |
| P1 | `Material::safeNormalize` 与 `ScenePrivateUtils::safeNormalizeScene` 重复 | `Material.cpp:11-17`, `ScenePrivateUtils.h:25-31` | 完全相同的逻辑维护了两份；`fresnelReflectance`（Material.cpp）与 `fresnelExact`（ScenePrivateUtils.h）同理 | Material.cpp 包含 ScenePrivateUtils.h，删除自己的 anonymous namespace 副本 |
| P2 | `createAreaLight()` 和 `sampleAreaLight()` 在 Sky.cpp 中 | `Sky.cpp:48-143` | 面光源创建与采样和天空渲染无关，给维护者造成误导 | 移至 SceneLoading.cpp（`createAreaLight`）和 Scene.cpp（`sampleAreaLight`）或新建 Lighting.cpp |
| P2 | `parseRender()` 含 80+ 字段，职责混杂 | `JsonParser.cpp:202-280` | 水体参数、飞机参数、canopy 参数、debug 参数全部平铺在一个 `parseRender()` 函数里，极难维护 | 拆出 `parseWater()`、`parseAircraft()`、`parseDebug()` 等子解析函数 |
| P2 | 水面法线计算重复 | `Scene.cpp:71-84` 和 `Scene.cpp:108-125` | `materialDebug` 路径和主路径各自重新计算一次 `ocean/oceanRipple` 的法线混合，逻辑完全相同 | 提取为 `computeWaterNormal(hitPoint)` private 函数 |
| P2 | `printVec3` 函数重复定义 | `SceneLoading.cpp:15` 和 `Renderer.cpp:18` | 两个 anonymous namespace 中各自有一份实现 | 移至 ScenePrivateUtils.h 或新建 DebugUtils.h |
| P2 | `entry.rotation` 被解析但从不应用 | `SceneLoading.cpp:244` | 有 `// Apply transform: scale -> rotate (TODO) -> translate` 注释，rotation 始终被忽略，造成用户配置无效 | 在报告中明确告知用户当前旋转无效，或实现基本旋转 |
| P3 | `SkyConfig.sky.enabled` 被解析但从不检查 | `Scene.h:118`, `Sky.cpp` | 设为 false 也不会关闭天空渲染 | 在 `skyColor()` 入口检查并早返回 |
| P3 | 距离雾密度硬编码 | `Scene.cpp:331` | `fogDensity = 0.003f` 和 `fogColor = horizonColor * 0.35f` 均不可配置 | 添加 `fogEnabled`、`fogDensity` 字段到 SceneConfig |
| P3 | AIRCRAFT_PARTS 的环境光加成硬编码 | `Scene.cpp:264` | `result += baseColor * 0.08f + Vec3f(0.025f, 0.025f, 0.028f)` 无注释无配置 | 移至 AircraftShaders.cpp 并添加说明注释 |

---

## 3. 按文件审查

---

### `include/Scene.h`

**当前职责：** SceneConfig（渲染参数大结构体）+ Scene 类声明

**主要问题：**

1. **SceneConfig 过度扁平化。** SkyConfig 已经成为嵌套子结构，但 20+ 个水体参数、12 个 canopy 参数、5 个 aircraft 参数、3 个 propeller 参数、6 个 debug 标志全部平铺在顶层。与 `SkyConfig` 的分层方式不一致。

2. **死字段（无效配置）：**
   - `SkyConfig::topColor`（第 101 行）：解析后从未被 Sky.cpp 读取
   - `SkyConfig::bottomColor`（第 103 行）：同上
   - `SkyConfig::sunDiskPower`（第 107 行）：解析后从未读取；`sunDiskColor()` 使用的是 `smoothstep01`，不用 `sunDiskPower`
   - `canopySmokedCenter`（第 72 行）：`shadeCanopyGlassPhysical()` 里有硬编码的 `smokedCenter(0.01f, 0.025f, 0.04f)` 替代了这个字段
   - `SkyConfig::enabled`（第 118 行）：`skyColor()` 不检查此标志

3. **`AreaLightSource::samplePoint()`** 定义在头文件里（第 25-32 行），调用了全局 `random_float()`，这是副作用函数，不应内联在 header struct 里。

4. **`Scene` private 函数 `skyColor`、`sunDiskColor`** 虽是成员函数，但实现在 Sky.cpp。实际上 skyColor/sunDiskColor 不使用任何 Scene 成员变量，只依赖 `config`。理论上可以变成自由函数，减少 Sky.cpp 对 Scene 内部的依赖。

**建议：**
- 添加 `WaterConfig`、`AircraftConfig`、`CanopyConfig` 子结构，与 `SkyConfig` 对等
- 删除三个死字段（topColor/bottomColor/sunDiskPower）
- 修复 canopySmokedCenter 未被读取的问题

---

### `src/Scene.cpp`

**当前职责：** `Scene::castRay()`——主路径追踪入口

**主要问题：**

1. **debug renderMode 分支过多（第 11-33 行）。** `skyOnly`、`sunDiskOnly`、`albedoOnly`、`baseColorOnly`、`textureOnly`、`directOnly`、`ambientOnly`、`specularOnly`、`shadowFactorOnly`、`waterReflectionOnly`、`waterRefractionOnly`、`waterFresnelOnly`、`propellerMaterialDebug` 共 13 种 renderMode。有些是 `depth==0` 检查，有些是 miss-ray 检查，有些是 hit-ray 检查，顺序混乱，易出错。

2. **双重 BVH 遍历（P1）：** 第 178 行 `bvh.intersect(bounceRay)` 和第 179 行 `castRay(bounceRay, depth+1)` 各自对同一条 bounceRay 调用一次 BVH。`bounceIsect` 只用于两处：NEE 去重（第 180-184 行）和水体吸收距离（第 189-197 行）。可以通过把 intersection 结果传入 castRay 来消除冗余遍历。

3. **距离雾应用到所有 depth（P1）：** 第 329-335 行的雾块没有 `depth == 0` 守卫，导致间接光在每个 bounce 都被独立雾化。

4. **水面法线重复计算（P2）：** 第 71-84 行（materialDebug 路径）和第 108-125 行（主路径）计算逻辑完全相同，应提取为 `computeWaterNormal()` 辅助函数。

5. **`AIRCRAFT_PARTS` 的神秘环境光（P3）：** 第 264 行 `result += baseColor * 0.08f + Vec3f(0.025f, 0.025f, 0.028f)` 无注释，与 AircraftShaders.cpp 的隔离原则矛盾。

6. **`materialDebug != "none"` 路径（第 70-89 行）提前返回**，但这段代码会在 `depth > 0` 时运行，此时 Russian Roulette 还没有执行，使得 materialDebug 的路径追踪消耗比预期更多。（P3 级别的效率问题）

7. **`propellerMaterialDebug` renderMode 的完整注释（第 44-61 行）**：这段 switch-case debug 可视化的代码和注释体量较大，建议移至 Diagnostics.cpp，通过 Diagnostics 函数暴露，和 `materialRoleDebugColor()` 合并。

**总评：** `castRay` 的核心逻辑（DIFFUSE → METAL → DIELECTRIC → 结果合成）已经较清晰，但外层的 debug 分支散布全函数，增加了认知负担。

---

### `src/SceneLoading.cpp`

**当前职责：** loadFromConfig / loadOBJ / addPlane / buildBVH / loadOcean

**主要问题：**

1. **`entry.rotation` 从未应用。** `position` 和 `scale` 在几何体构建时正确使用，但第 244 行注释写明 rotation 是 TODO，`entry.rotation` 从 JSON 解析后被完全忽略。用户若在 scene.json 设置 rotation 会无效。

2. **Ocean 参数全部硬编码（P2）：** 第 87-90 行：
   ```cpp
   ocean = std::make_unique<Ocean>(256, 150.0f, 12.0f, Vec3f(1,0,0.5f), 1.5f, 5.0f);
   oceanRipple = std::make_unique<Ocean>(128, 12.0f, 4.5f, ...);
   ```
   这些参数（分辨率、patch size、风速、风向、振幅、相位）完全无法从配置文件调整。对水体渲染效果影响很大。

3. **`loadOcean()` 无条件运行（P2）：** 无论场景中是否有 DIELECTRIC 平面，Ocean 总是被生成。可以先检查是否有 DIELECTRIC 材质再生成。

4. **`printVec3` 重复（P2）：** 第 15-17 行定义了一份，Renderer.cpp 也有一份。

5. **`createAreaLight()` 和 `sampleAreaLight()` 在 Sky.cpp 而非此文件中（P2 架构问题）。** `createAreaLight()` 从逻辑上属于场景加载（创建几何体、创建材质），应在 SceneLoading.cpp。

**总评：** 文件职责清晰，问题主要是硬编码参数和一个遗留的 TODO。

---

### `src/Sky.cpp`

**当前职责：** `skyColor()`（大气散射 + 云层）、`sunDiskColor()`、**以及** `createAreaLight()` 和 `sampleAreaLight()`

**主要问题：**

1. **`createAreaLight()` 和 `sampleAreaLight()` 不属于此文件（P2）。** Sky.cpp 的职责应是天空渲染，而这两个函数是面光源管理，在概念上与天空无关。建议拆出。

2. **`SkyConfig.topColor`、`bottomColor`、`sunDiskPower` 完全不被使用（P1）。** 整个 `skyColor()` 实现是物理大气散射，这三个字段是旧梯度天空模型的遗留。

3. **`sky.enabled` 不被检查（P3）。** 在 `skyColor()` 入口无任何 `if (!sky.enabled)` 判断。

4. **大量硬编码的物理常量：** `betaRayleighBase`、`betaMieBase`、散射系数、相函数参数等均无法从配置调整。这些是算法常量，目前位置合理，不建议移入配置。

5. **云层相关代码中有硬编码 FBM 参数（`fbm2D` 的 octave 数固定为 5）。** 不可配置，P3 级别。

6. **`safeDiv` lambda 在 `skyColor()` 局部定义（第 158-164 行）。** 可以放入 ScenePrivateUtils.h，但当前规模较小，P3 优先级。

**总评：** 天空渲染本身代码完整清晰。主要问题是 createAreaLight/sampleAreaLight 错误放置和死字段。

---

### `src/AircraftShaders.cpp`

**当前职责：** `shadeCanopyGlassPhysical`、`shadeCanopyGlassFake`、`shadePropellerAfterimage`、`shadeAircraftMetal`

**主要问题：**

1. **`canopySmokedCenter` 配置字段被无视（P1）：** `shadeCanopyGlassPhysical()` 第 94 行硬编码 `const Vec3f smokedCenter(0.01f, 0.025f, 0.04f)`。`config.canopySmokedCenter` 在 `locked_settings.json` 里有值（`[0.001, 0.003, 0.008]`），但从未被读取。这是一个 bug——修改 JSON 参数无效。

2. **`shadeCanopyGlassPhysical` 中的 `smokedCenter`（第 96 行）** 硬编码值与 `shadeCanopyGlassFake` 的 `canopySmokedCenter` 默认值（`Vec3f(0.005f, 0.018f, 0.035f)`）不一致，且与 `locked_settings.json` 中的 `[0.001, 0.003, 0.008]` 也不一致。三处不同值令人困惑。

3. **Debug 守卫 `debugCanopyExtreme`** 在两个 canopy 函数（physical 和 fake）都有，逻辑一致，但返回值不同（一个返回 `Vec3f(0,0,5)` 蓝色，另一个返回 `Vec3f(0,1,1)` 青色）。可能是有意区分，但值得加注释说明。

4. **`shadePropellerAfterimage` 的长注释块（第 196-213 行）** 详细记录了 fix history。这些内容更适合放在 git commit message 或 PR 描述中，不是运行时代码注释。（P3）

5. **`shadeCanopyGlassFake` 有 `(void)mat;`（第 105 行）：** 表明 mat 参数在这个路径中没被用到。这里只用了 config 参数和几何信息。可能是接口设计不够精准。（P3）

**总评：** 文件职责清晰，shader 拆分合理。主要问题是 `canopySmokedCenter` 未被接入。

---

### `src/Diagnostics.cpp`

**当前职责：** materialTypeName/materialRoleName/materialRoleIndex/materialRoleDebugColor（纯函数）+ Scene::resetMaterialRoleDiagnostics/printMaterialRoleDiagnostics/tracePrimary/materialDebugColor（Scene 成员函数实现）

**主要问题：**

1. **`Scene::tracePrimary()` 不是诊断功能。** 这是一个通用光线追踪函数（`bvh.intersect` 包装），放在 Diagnostics.cpp 纯粹是历史遗留。但它的实现只有两行，当前危害有限。（P3）

2. **`Scene::materialDebugColor()` 实现在此文件，声明在 Scene.h（第 215 行）。** 这没有问题，但使 Scene 类的实现分散在多个 .cpp 中（Scene.cpp、AircraftShaders.cpp、Sky.cpp、SceneLoading.cpp、Diagnostics.cpp）。这是整体架构的核心模式，不算 bug，但增加了导航难度。

3. **`propellerMaterialDebug` renderMode 的 switch-case（Scene.cpp:52-61）** 与 `materialRoleDebugColor()` 功能高度相似但单独存在，建议统一通过 Diagnostics 的函数处理。（P2）

**总评：** 文件内容清晰，问题较小。

---

### `src/MtlConverter.cpp` / `include/MtlConverter.h`

**当前职责：** `roughnessFromNs`、`glossyWeightFromKs`、`applyMaterialNameOverride`

**主要问题：**

1. **`applyMaterialNameOverride()` 里有大量硬编码材质参数（P2/P3）：**
   - `canopy_glass` 的 `roughness = 0.015f`
   - `transparent` (propeller) 的 `color = Vec3f(0.20f, 0.16f, 0.12f)`、`roughness = 0.60f`、`alpha = 0.08f`
   - `metal` 的 `color = Vec3f(0.55f, 0.55f, 0.52f)`、`aluminumReflectance`
   - `01_-_default/main` 的 `metallicBase = 0.75f`、roughness 范围
   - `parts` 的 `metallicBase = 0.35f`

   这些参数与飞机特定材质绑定，无法从 JSON 调整。对于"最终固定效果"而言这是可以接受的——**但应明确标注为"艺术固定参数"而非算法常量**，以避免后续维护者误以为可以从 JSON 覆盖。

2. **`transparent` 的默认 `alpha = 0.08f`（第 57 行）** 会立即被 `SceneLoading.cpp:191` 用 `config.propellerAfterimageAlpha` 覆盖（`alpha = clamp(config.propellerAfterimageAlpha, 0, 0.5f)`）。所以这个默认值实际上不生效。

3. **材质名匹配是精确字符串匹配（lowercase 后）。** 如果 MTL 文件中的名字有空格或大小写变化（如 `01_-_Default` 变为 `01_-_default`），可能意外匹配或不匹配。当前 lowercase 转换已处理大小写，但非精确匹配（无前缀/后缀容忍）可能是脆弱点。

**总评：** 文件职责明确，代码简洁。主要风险是硬编码参数的可维护性。

---

### `src/MeshUtils.cpp` / `include/MeshUtils.h`

**当前职责：** `computeTriangleTangents`、`applyMaterialNormalMap`

**主要问题：**

1. **`applyMaterialNormalMap` 依赖 `SceneConfig` 参数（第 29 行）。** 函数签名包含 `const SceneConfig& config`，这使 MeshUtils.cpp 间接依赖整个 `Scene.h`。考虑到 `config.normalDetailStrength`、`config.flipNormalGreen`、`config.sharpNormalSampling` 这三个参数都是从 config 取的，更干净的设计是把这三个参数拆出来作为独立参数传入，而不是整个 SceneConfig。（P3）

2. **代码质量整体高，无冗余。** Gram-Schmidt 切空间正交化（第 35-47 行）正确处理了 handedness 翻转，边界条件健壮。

**总评：** 职责清晰，代码质量良好。

---

### `src/JsonParser.cpp` / `include/JsonParser.h`

**当前职责：** 手写 JSON 词法+语法分析器，解析 scene.json 和 locked_settings.json

**主要问题：**

#### 架构问题

1. **`parseRender()` 过大（第 202-280 行，约 80 个字段）。** 水体参数、canopy 参数、aircraft 参数、debug 参数、环境参数全部平铺在一个函数里。建议分拆为 `parseWater()`、`parseAircraft()`、`parseDebug()` 等，每个只处理 10-15 个字段。（P2）

2. **`parseObjects()` 有死代码块（第 333-339 行）：**
   ```cpp
   if (t.type == Token::LBRACE || t.type == Token::COMMA) {
       if (t.type == Token::COMMA) {
           // next should be LBRACE — 什么也没做
       }
   }
   // Simpler: re-structure
   // We already consumed first token
   if (t.type != Token::RBRACKET) {  // 这个条件总是 true（while 已保证）
   ```
   这是不完整重构留下的残骸注释和空 if-block，令人困惑，但不影响运行（P2）。

#### 潜在 Bug

3. **`skipValue()` 嵌套数组提前退出（P0）：** 第 185-196 行的数组跳过逻辑在进入 `depth` 循环前先读取并丢弃了数组的第一个元素 token，然后才开始计数。如果第一个元素本身是数组或对象（如 `[[1,2],[3,4]]`），第一个 `[` 会被丢弃，导致深度计数少一层，过早退出。当前 config 文件中没有嵌套数组，所以暂未触发，但属于脆弱设计。

4. **`readNumber()` 对 `-e5` 等非法数字的处理（P3）：** 当存在前置 `-` 时，循环内的 `pos == start` 守卫失效（此时 `pos = start+1`），导致 `-e5` 被接受为一个 NUMBER token，随后 `stof("-e5")` 会抛出 `std::invalid_argument`，产生 runtime_error。影响局限于格式错误的 JSON。

#### 合并顺序

5. **`locked_settings.json` 先加载，`scene.json` 后加载，这是正确的**（第 457-464 行）。scene.json 会覆盖 locked_settings.json 的同名字段。当前逻辑正确，但文档/注释中未说明这一覆盖语义，调参者可能误以为两个文件不能出现相同字段。

6. **`locked_settings.json` 不存在时安全跳过（第 460-462 行）：** 正确。但主 scene.json 不存在时会抛异常（第 471-473 行），行为有意区分，合理。

7. **不支持 JSON 尾随逗号**（`[1, 2, 3,]`）——这是标准 JSON 的限制，但许多手写 JSON 会意外产生尾随逗号，建议在报告中明确标注"这是简易 JSON parser，不兼容尾随逗号"。

---

### `src/Material.cpp` / `include/Material.h`

**当前职责：** BSDF sample/eval/pdf 实现

**主要问题：**

1. **`safeNormalize` 和 `fresnelReflectance` 与 ScenePrivateUtils.h 重复（P1）：**
   - `Material.cpp` 的 `safeNormalize`（匿名命名空间）与 `ScenePrivateUtils.h::safeNormalizeScene` 完全等价
   - `Material.cpp` 的 `fresnelReflectance` 与 `ScenePrivateUtils.h::fresnelExact` 算法等价（sign convention 不同但物理等价）
   
   解决方案：`Material.cpp` 包含 `ScenePrivateUtils.h`，删除自己的重复实现。

2. **`clampedRoughness()`、`clampedGlossyWeight()`、`clampedSpecularBoost()` 三个 clamp 包装（P3）：** 这些函数的值范围（roughness 0.02-1.0、glossyWeight 0-0.8、specularBoost 0-4.0）与 SceneLoading.cpp 中的 `std::clamp(entry.roughness, ...)` 调用重叠，但范围不完全一致。如果 JSON 赋值时已经 clamp，Material 内再 clamp 是双重保险，问题不大；但如果范围不一致（例如 roughness 在加载时 clamp 到 [0,1]，在 BSDF 内 clamp 到 [0.02, 1.0]），会导致行为依赖调用路径，需要搜索确认统一化范围。

3. **`hasEmission()` 阈值 0.01f（第 88 行）：** 极低发光度的材质（emission < 0.01）不会被视为光源，在路径追踪中不被 NEE 采样。这是艺术参数，行为合理，但无法从 JSON 调整。

4. **`pdf()` 对 METAL 和 DIELECTRIC 返回 1.0f（第 222-223 行）：** 这是简化处理（delta 分布的 pdf），在 castRay 里 `brdf * indirect / pdf = brdf * indirect`，对 METAL 而言 `eval()` 返回 `getColor()`，所以这段逻辑合理。但 API 使用者可能对 pdf=1.0 的语义感到困惑。（P3 文档问题）

5. **算法常量 vs 艺术参数区分清晰：** `kPi`、Fresnel 相关计算是算法常量，保留在代码里合理。roughness clamp 范围、glossyWeight 范围则更接近艺术约束，可以考虑配置化。

**总评：** BSDF 实现正确，接口清晰。主要问题是工具函数重复。

---

### `src/Renderer.cpp`

**当前职责：** 主渲染循环、tone mapping、PPM 输出

**主要问题：**

1. **`printVec3` 重复（P2）：** 与 SceneLoading.cpp 重复定义。

2. **Vignette 效果硬编码（P3）：** 第 61 行 `vign = 1.0f - 0.35f * dist2`，无配置项。softWhiteClamp tone mapping 始终应用 vignette。

3. **`timeWarp` 和 `filmic` tone mapping 均含硬编码颜色系数（P3）：** `filmic` 里的饱和度 1.20f、绿通道衰减 0.94f 等无法配置。这些是艺术参数。

4. **Renderer 不含 `#include "Scene.h"` 中 ScenePrivateUtils.h 的能力：** tone mapping 里多处用了 `std::pow`、`std::sqrt` 等，本身没问题，但有些逻辑（如 vignette）可以提取为工具函数。（P3）

---

### `config/scene.json`

**审查结论：**

- 文件精简，只保留可手调的核心参数：分辨率、spp、maxDepth、相机、物体列表、天空、面光源。
- `renderMode` 保留在 scene.json 里（第 8 行），这意味着用户可以方便切换到 debug 模式，这是合理的。
- 有两个 `camera2` 相关字段（enabled=false），保留在文件中占空间但无害。
- `sky` 块只覆盖了少量关键参数（颜色、sunDirection、cloudsEnabled），与 locked_settings.json 分工明确。

**待确认问题：** scene.json 里的 `"roughness": 0.25` 和 `locked_settings.json` 里的 water/aircraft 参数互不重叠，覆盖逻辑目前正确。

---

### `config/locked_settings.json`

**审查结论：**

- 包含了所有"最终调好不该随意改"的参数：tone mapping、曝光、所有 canopy 参数、propeller 参数、所有 water 参数、sky 精调参数。
- **Debug 参数也在此文件中**（`debugMaterialRoles: false`、`debugCanopyExtreme: false` 等，共 4 个）。这些 debug=false 参数占空间且容易被改成 true 后忘记改回来。建议从 locked_settings.json 中删除，因为默认值已是 false，不需要显式存储。（P3）
- 无参数与 scene.json 重叠——分工清晰。

---

### `CMakeLists.txt`

**审查结论：**

- 所有源文件均在 CMakeLists.txt 中列出，无遗漏，无重复。
- `ScenePrivateUtils.cpp` 不在列表中，这是正确的（header-only）。
- 无旧文件名残留（Water.cpp、MaterialUtils.cpp、SceneLoader.cpp 均不存在也不在列表中）。
- OpenMP 使用 REQUIRED，若环境无 OpenMP 会直接报错，不是 silently disabled，可以考虑改为 OPTIONAL。（P3）

---

## 4. 配置系统审查

### scene.json vs locked_settings.json 分工

| 职责 | scene.json | locked_settings.json |
|------|-----------|---------------------|
| 分辨率/spp/maxDepth | ✅ 可调 | ❌ 不覆盖 |
| 相机 | ✅ 可调 | ❌ 不覆盖 |
| 物体列表 | ✅ 可调 | ❌ 不覆盖 |
| 天空颜色/方向 | ✅ 粗调 | ✅ 精调参数 |
| Water 参数 | ❌ 不覆盖 | ✅ 最终参数 |
| Canopy 参数 | ❌ 不覆盖 | ✅ 最终参数 |
| Aircraft 参数 | ❌ 不覆盖 | ✅ 最终参数 |
| Debug 参数 | ❌ 不覆盖 | ✅（均为 false，建议删除） |
| renderMode | ✅ 可调 | ❌ 不覆盖 |
| tone mapping 参数 | ❌ 不覆盖 | ✅ 最终参数 |

**问题：**

1. **`canopySmokedCenter` 在 locked_settings.json 里有值（`[0.001, 0.003, 0.008]`）但代码中永远不读取**，这是最严重的配置系统问题。

2. **`SkyConfig.topColor`、`bottomColor`、`sunDiskPower`** 虽然在 scene.json 里被指定（topColor 等），但代码不使用，导致改这些值无任何效果。（scene.json 里有 `topColor`，locked_settings.json 里没有——但它们都无效）

3. **没有任何参数可以控制距离雾**，这是纯硬编码行为，scene.json/locked_settings.json 均无相关字段。

---

## 5. `castRay` 专项审查

**函数总行数：** 339 行（整个 Scene.cpp）

**可以继续抽出的逻辑：**

| 逻辑块 | 建议处理 |
|--------|---------|
| debug renderMode early-returns（第 11-33 行） | 提取为 `handleDebugEarlyReturn(ray, depth)` 或 `Diagnostics::castDebugRay()` |
| `propellerMaterialDebug` switch（第 52-61 行） | 合并至 `materialRoleDebugColor()`，在 Diagnostics.cpp 统一 |
| 水面法线计算（第 71-84 行和第 108-125 行的重复） | 提取为 `computeWaterNormal(hitPoint)` private 方法 |
| diffuse 环境光合成（第 200-238 行，~40 行）| 提取为 `computeDiffuseAmbient(mat, N, ...)` |
| 水面 Fresnel 反射合成（第 270-306 行，~40 行）| 提取为 `computeWaterReflection(R, N, isect)` |
| clearcoat 环境反射（第 244-262 行）| 移至 AircraftShaders.cpp 或 Scene.cpp 内的独立函数 |
| 距离雾（第 329-335 行）| 添加 `if (depth == 0)` 守卫并提取为 `applyFog(result, isect.t)` |

**保留不动的部分：**

- Russian Roulette 位置（depth>3）：合理，不动
- NEE（`sampleAreaLight`）调用位置：合理
- 光源 hit 去重（第 180-184 行）：逻辑正确，不动
- `survivalProb` 分母修正（第 337 行）：正确

**潜在 NaN/除零风险：**

- `pdf_val < 1e-6f` 守卫（第 168-171 行）：已正确处理
- `wo.length2() < 1e-8f` 守卫（第 162-165 行）：已正确处理
- `sanitizeRadiance()` 兜底：已覆盖 NaN 和 Inf
- 水体 `horizontal` 水平分量除零（第 291-293 行）：`rH.length2() > 1e-8f && sH.length2() > 1e-8f` 守卫已处理
- **未发现明显会触发的 NaN 路径**

---

## 6. JsonParser 专项审查

**总体评价：** 这是一个简易的手写 JSON parser，**不是完整标准 JSON parser**。不支持：
- 尾随逗号（`[1, 2, 3,]`）
- Unicode 转义（`\uXXXX`）
- 注释（JSON 本身也不支持，但一些工具允许）
- 深度嵌套数组中的对象（`skipValue` 有 bug）

**结构问题：**
- `parseSky()` 已独立，结构良好
- `parseCamera()` 简洁
- `parseObjects()` 功能正确，但头部有 dead code
- `parseLighting()` 简洁，但只支持一个 areaLight
- `parseRender()` **过大**，需要拆分

**可靠性：**
- `resolveInputPath()` 有向上查找逻辑，较健壮
- `resolveAssetPath()` 先尝试相对于 config 目录，再 fallback 到向上查找，合理
- `locked_settings.json` 不存在时安全跳过
- 主 JSON 文件不存在时抛异常，行为正确
- `skipValue()` 有 P0 bug（嵌套数组）

---

## 7. Material / BSDF 专项审查

**BSDF 设计评价：**

| 方面 | 评价 |
|------|------|
| `sample()` / `eval()` / `pdf()` 分离 | ✅ 职责清晰，符合路径追踪标准接口 |
| Cosine hemisphere sampling | ✅ 正确实现（Malley's method） |
| Phong lobe sampling | ✅ 余弦幂叶采样正确 |
| METAL pdf = 1.0 | ✅ delta 分布简化处理，合理 |
| DIELECTRIC pdf = 1.0 | ✅ 同上 |
| safeNormalize 重复 | ❌ 应使用 ScenePrivateUtils.h 的版本 |
| fresnelReflectance 重复 | ❌ 应使用 fresnelExact |
| roughness clamp 范围 [0.02, 1.0] | ✅ 防止 roughness=0 产生 Inf 指数 |
| glossyWeight clamp [0, 0.8] | ✅ 艺术约束，合理 |
| specularBoost clamp [0, 4] | ✅ 防止过爆 |

**算法常量（应留在代码里）：**
- `kPi` = 3.14159...
- roughness clamp 最小值 0.02（防止 Phong 指数为 Inf）
- glossyWeight 上限 0.8
- BSDF 归一化因子 `(exponent+2)/(2π)`

**艺术参数（应来自 JSON 的）：**
- `hasEmission()` 的 0.01 阈值（目前硬编码）
- `clampedSpecularBoost()` 的上限 4.0（与 SceneLoading.cpp 一致，可以统一）

---

## 8. 建议的下一步重构顺序

按安全性和影响从高到低排列：

1. **修复 P0/P1 bugs（先做这些）：**
   - 修复 `skipValue()` 嵌套数组 bug（JsonParser.cpp:185-196）
   - 修复 `canopySmokedCenter` 未被读取（AircraftShaders.cpp:94 替换为 `config.canopySmokedCenter`）
   - 为距离雾添加 `if (depth == 0)` 守卫（Scene.cpp:329）

2. **清除死字段（低风险，高收益）：**
   - 从 SkyConfig 删除 `topColor`、`bottomColor`、`sunDiskPower`
   - 从 parseSky 删除对应的三行解析
   - 从 locked_settings.json 删除 4 个 debug=false 字段

3. **消除工具函数重复：**
   - Material.cpp 包含 ScenePrivateUtils.h，移除 anonymous namespace 中的 `safeNormalize` 和 `fresnelReflectance`
   - SceneLoading.cpp 的 `printVec3` 移至 ScenePrivateUtils.h 或 Renderer.cpp 共享

4. **重构 castRay 的水面法线重复：**
   - 提取 `computeWaterNormal()` private 方法
   - 替换 Scene.cpp 71-84 和 108-125 两处相同代码

5. **移动 createAreaLight / sampleAreaLight：**
   - 从 Sky.cpp 移至 SceneLoading.cpp（创建）和 Scene.cpp（采样）或新建 Lighting.cpp

6. **拆分 parseRender()：**
   - 提取 `parseWater()`、`parseAircraft()`、`parseCanopy()`、`parseDebug()` 子函数

7. **BVH 双重遍历（最后，重构量最大）：**
   - 修改 `castRay` 接受已有 intersection 或缓存结果的方案较复杂，需要较大接口改动

---

## 9. 不建议现在改的部分

以下内容虽然不够"理想"，但改动风险高于收益：

1. **SceneConfig 子结构化**（WaterConfig / AircraftConfig 等）：需要修改 JsonParser、所有 `.cpp` 中的 `config.waterXxx` 引用、`SceneConfig` 本身。改动量大，容易引入错误。当前扁平结构已工作，优先级 P2。

2. **Ocean 参数配置化**：需要新增 SceneConfig 字段、解析逻辑、调参验证。改动范围明确但需要调参时间，建议放在独立任务中。

3. **`skyColor()`/`sunDiskColor()` 改为自由函数**：会破坏 Scene 类的封装，需要把 config 以 const ref 传入，涉及调用链。当前架构虽不够纯，但可用。

4. **Scene 类实现从多个 .cpp 中整合**：AircraftShaders.cpp、Sky.cpp 等实现 Scene 成员函数的方式是刻意的拆分策略，重新整合会破坏文件粒度。不建议合并，这是当前架构的合理取舍。

5. **`applyMaterialNameOverride` 里的硬编码材质参数**：这些是"艺术锁定"参数，来自具体飞机模型的调参历史。贸然移入 JSON 可能破坏材质识别语义。建议加注释标注为"艺术固定参数"但不移动。

---

## 10. 总结

**报告文件已生成：`FINAL_CODE_REVIEW_REPORT.md`**

**问题数量统计：**

| 优先级 | 数量 | 说明 |
|--------|------|------|
| P0 | 1 | `skipValue()` 嵌套数组 bug（当前 JSON 格式不触发，但脆弱） |
| P1 | 5 | BVH 双重遍历、canopySmokedCenter 未接入、3 个死 SkyConfig 字段、雾应用到所有 depth、工具函数重复 |
| P2 | 9 | createAreaLight 位置、parseRender 过大、水面法线重复、printVec3 重复、parseObjects dead code、rotation 未实现、AIRCRAFT_PARTS 硬编码、debug 参数在 locked_settings、ocean 参数硬编码 |
| P3 | 9 | 雾参数硬编码、canopySmokedCenter 值不一致、sky.enabled 不检查、kPi 局部定义、vignette 硬编码、hasEmission 阈值、Material PDF API 歧义、readNumber 对 `-e` 的脆弱处理、TODO rotation |

**最推荐下一步先修：**

1. **`canopySmokedCenter` 未接入（P1，改一行代码）** — AircraftShaders.cpp:94 把 `const Vec3f smokedCenter(0.01f, 0.025f, 0.04f)` 替换为 `config.canopySmokedCenter`，立即让 locked_settings.json 中的参数生效。
2. **删除三个死 SkyConfig 字段（P1，删改 ~6 行）** — 移除 topColor/bottomColor/sunDiskPower，消除对调参者的误导。
3. **距离雾的 `depth==0` 守卫（P1，改一行）** — 防止间接光被叠加雾化。
