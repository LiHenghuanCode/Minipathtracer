# 4-Minute Seminar Presentation Script

---

## Timing Target

This script is designed for approximately **3.5 minutes** of speaking time at a natural presentation pace (roughly 130–140 words per minute), leaving about 30 seconds for transitions and possible Q&A.

---

## Full Speaking Script

---

**[Slide 1 – Title]**

Hi everyone. We are Team Cat. Our project is a Monte Carlo path tracer built from scratch in C++, and the scene we chose to recreate is a cinematic sunset over the ocean — with an aircraft and a large bridge structure set against a warm golden-sky horizon.

---

**[Slide 2 – Motivational Image]**

The motivational image that inspired us shows a dramatic low-angle sunset with vivid orange and red tones reflecting off the water, volumetric haze near the water surface, and layered clouds catching the last light of the day. We chose this image because it covers many technically interesting rendering challenges at once: complex water behaviour, atmospheric scattering, volumetric effects, and a strong lighting mood. We wanted to see how close we could get using our own renderer rather than a real-time engine.

---

**[Slide 3 – Project Goal]**

Our goal was to reproduce the key visual qualities of that reference image using physically-based path tracing. Specifically, we focused on improving water realism — getting realistic reflections, refraction, and wave shape — while also building a believable sunset atmosphere around it. The scene includes an aircraft and bridge model loaded from OBJ, an ocean water plane, and a large sky dome, all rendered through the same path-tracing pipeline.

---

**[Slide 4 – Rendering Pipeline]**

The pipeline is straightforward. For each pixel we fire multiple sample rays. Each ray either hits a surface and bounces, or misses and returns a procedural sky colour. The scene is accelerated by a BVH — a bounding volume hierarchy — so intersection tests stay fast even with thousands of triangles. We also use Next Event Estimation: whenever a diffuse or metallic surface is hit, we directly sample the area light to reduce noise on lit surfaces. Everything runs in parallel using OpenMP.

---

**[Slide 5 – Ocean and Water Material]**

For the water surface we implemented FFT-based wave normals following the Phillips power spectrum. The idea is to generate a random frequency-domain height field, propagate it in time using the deep-water dispersion relation, then inverse-FFT it back to a height map to extract surface normals. We run two scales — a large 150-metre swell and a smaller 12-metre ripple — and blend them at the water surface. The water material itself is a full dielectric: Fresnel equations decide whether each ray reflects or refracts, and Beer-Lambert absorption tints refracted light according to how far it travels through the water. So shallow water looks more transparent and deeper regions take on a deeper blue-green.

---

**[Slide 6 – Sky, Clouds, and Mist]**

The sky uses a simplified analytic scattering approximation. We compute optical depth along the view ray and towards the sun, apply Rayleigh and Mie phase functions, and add a directional sun-glow. This is not a full physical atmosphere simulation — it's an approximation designed to look like a sunset — but it does respond correctly to the sun's elevation, naturally shifting from blue overhead to warm orange near the horizon.

On top of that we layered 2D procedural clouds. These are not volumetric — they are fractal Brownian motion noise projected onto the sky dome. They are alpha-composited over the sky, and because the sky colour function is also used for environment reflections, the clouds appear reflected in the water automatically through path tracing, without any extra code.

Finally, around the base of the bridge towers we placed two local volumetric mist volumes. These are ray-marched through an AABB bounding box. The density comes from a 3D FBM noise field, and we cast short shadow rays towards the sun to approximate self-shadowing. The result is a warm-lit, soft sea fog that adds depth to the foreground.

---

**[Slide 7 – Tone Mapping and Output]**

Raw path-traced values are HDR, so we apply tone mapping before writing to disk. Our default mode is a soft white-point clamp with a slight saturation boost and gamma correction at 2.2. We also have a filmic operator for comparison. Both include a subtle vignette. The renderer outputs standard PPM files.

---

**[Slide 8 – Results: Two Camera Views]**

We render two views from different angles. The first is a wide-angle low shot looking back at the bridge across the water, which shows the reflections and mist at their strongest. The second is a side view of the aircraft and bridge structure, which shows the material response — the aircraft paint has a thin clearcoat Fresnel layer over the diffuse shading, which you can see as a faint environment reflection on the fuselage.

---

**[Slide 9 – Limitations and Future Work]**

There are a few honest limitations. The clouds are 2D projected noise, not real volumetric clouds, so they don't cast proper shadows. The sky scattering is an analytic approximation — it captures the sunset feel but doesn't match ground-truth spectral data. The BVH uses a simple midpoint split, not SAH, so it's not as optimal as it could be. And we only use first-order volume scattering in the mist. With more time we would add 3D cloud volumes, a spectral atmosphere model, and multi-bounce scattering inside the mist.

---

**[Slide 10 – Conclusion]**

Overall, we are happy with where this ended up. Starting from a basic path tracer, we added FFT ocean waves, Fresnel water, a procedural sky and clouds, volumetric mist, and BVH acceleration. The sunset scene we produced captures many of the visual qualities from our reference image. Thank you.

---

## Slide-by-Slide Notes

| # | Slide Title | Main Speaking Points | Est. Time |
|---|-------------|----------------------|-----------|
| 1 | Title | Team name, project name, brief one-line scene description | ~15 s |
| 2 | Motivational Image | Show reference image; explain why it was chosen: ocean, atmosphere, haze, strong light | ~25 s |
| 3 | Project Goal | Reproduce reference scene; focus on water realism; describe scene contents | ~20 s |
| 4 | Rendering Pipeline | Samples per pixel → BVH intersection → bounce or miss → sky; NEE; OpenMP | ~25 s |
| 5 | Ocean & Water Material | Phillips spectrum FFT; dual-scale normals; dielectric Fresnel; Beer-Lambert absorption | ~40 s |
| 6 | Sky, Clouds & Mist | Simplified Rayleigh/Mie analytic sky; 2D FBM clouds projected on dome; two volumetric mist AABB volumes with ray marching + self-shadow | ~50 s |
| 7 | Tone Mapping | HDR → softWhiteClamp or filmic; gamma 2.2; vignette; saturation | ~15 s |
| 8 | Results | Two camera viewpoints; highlight reflections, mist, clearcoat on aircraft | ~20 s |
| 9 | Limitations | 2D clouds (no shadow); approximate sky; midpoint-split BVH; single-scattering mist | ~20 s |
| 10 | Conclusion | Summary of contributions; thank the audience | ~10 s |
| — | **Total** | | **~3 min 20 s** |

---

## Backup Q&A Notes

### Why use path tracing instead of rasterisation?

Path tracing naturally handles global illumination: reflections, refraction, and colour bleeding between surfaces all fall out of the same algorithm without special-casing each effect. Rasterisation would need separate passes for reflections, shadow maps, and screen-space effects. For a scene that's dominated by a specular water surface and environment reflections, path tracing is the cleaner choice. The cost is longer render times.

---

### Why use FFT for ocean normals, not just bump noise?

A simple noise bump map doesn't capture the directional structure of wind-driven ocean waves. The Phillips spectrum encodes a real physical relationship between wave energy, wavelength, and wind direction. The FFT approach generates a wave field where the spatial frequencies, phases, and propagation directions are physically motivated. The result looks convincingly ocean-like rather than like generic procedural noise. It also tiles seamlessly and can be animated by evaluating at different times.

---

### Is the sky physically accurate?

Not fully. We use a simplified analytic model: optical depth is approximated as a function of view elevation, and we apply Rayleigh and Mie phase functions on top. A fully physical sky would integrate scattering along the actual atmospheric column at each wavelength. Our version captures the visual character of a sunset — blue-to-orange gradient, sun glow, warm horizon — but it would not match spectral measurements. We chose it because it's fast to evaluate per ray and gives good artistic control for the scene.

---

### Are the clouds volumetric?

No, the clouds in this project are 2D. We compute fractal Brownian motion noise projected from the sky direction and alpha-composite it over the sky colour. This means clouds don't cast shadows on the ground or on each other, and they don't have internal light scattering. The benefit is that they are cheap — evaluated once per ray that misses geometry — and they appear automatically in water reflections because the sky function is the same path used for environment sampling.

---

### What are the main limitations?

1. **2D clouds**: no proper shadow casting or volumetric self-shading.
2. **Approximate sky**: analytic model, not a numerical solution to the rendering equation for the atmosphere.
3. **Midpoint-split BVH**: not as efficient as SAH (surface area heuristic); acceptable for this scene size but would slow down with a more complex model.
4. **Single-scattering mist**: multiple scattering inside the volume is not computed; only one bounce of light is considered at each sample point.
5. **No denoising**: at 64 samples per pixel there is some residual noise, especially in dark areas.

---

### What would you improve with more time?

- Replace 2D clouds with a proper 3D volumetric cloud model (e.g., signed-distance or density field with full light marching).
- Upgrade the sky to a spectrally accurate atmosphere model such as Hillaire (2020) or Bruneton-Neyret (2008).
- Add multiple-scattering for the mist volumes.
- Implement SAH BVH construction for better acceleration.
- Add a simple denoiser (e.g., bilateral filter or OIDN integration) to allow lower SPP at the same perceptual quality.
- Animate the ocean and render a short sequence.
