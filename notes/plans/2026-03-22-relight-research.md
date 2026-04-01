# Gaussian Splat Relighting Research Report (2026-03-22)

Based on hands-on experience with our deferred PBR implementation + comprehensive research.

## TL;DR — The Honest Assessment

**Every academic relighting method requires retraining from scratch.** None work on pre-trained/standard PLY splats. Our current approach (PBR on pre-baked SH0) is fundamentally fighting the format.

**But there is one practical path we missed:** Write to UE's native GBuffer and let UE's deferred pipeline handle lighting. Volinga already does this.

---

## What We Tried & Why It Didn't Work

1. **Deferred PBR on SH0 albedo**: SH0 already contains baked lighting → "double lighting" → weak effect
2. **Shadow map from light view**: Re-renders 1.6M splats = 2x GPU cost → unusable
3. **Cubemap shadow (6 faces)**: 7x GPU cost → worse

### The Fundamental Problem

Gaussian splats store **baked appearance** as SH coefficients. SH0 ≈ average radiance (including lighting), not albedo. You cannot extract clean albedo from baked SH without the original training data. Every paper that does relighting trains separate albedo/normal/roughness per Gaussian — they don't retrofit existing splats.

---

## Academic Methods (All Require Retraining)

### Tier 1: Best Quality + Real-Time

| Method | Venue | Approach | FPS | Key Innovation |
|--------|-------|----------|-----|---------------|
| **R3DG** | ECCV 2024 | BRDF decomposition + point-based ray tracing | 30+ | Per-Gaussian normal/BRDF + visibility baking |
| **GS-IR** | CVPR 2024 | Inverse rendering + baked occlusion | 30+ | Depth-derived normals + occlusion precomputation |
| **GS³** | SIGGRAPH Asia 2024 | Triple splatting (reflectance + shadow + GI) | 90 | Splat toward light for shadow, MLP for GI |
| **PRTGS** | ACM MM 2024 | Precomputed Radiance Transfer per splat | 30+ | PRT coefficients replace SH, ray-traced precomputation |

### Tier 2: High Quality, Slower

| Method | Venue | Approach | Notes |
|--------|-------|----------|-------|
| **DeferredGS** | 2024 | 7-channel GBuffer + SDF supervision | 3-4h training, 30fps@800x800 |
| **GI-GS** | ICLR 2025 | Global illumination decomposition | Path tracing for GI during training |
| **SSD-GS** | 2025 | Scattering + shadow decomposition | Physically-based, handles subsurface |
| **3DGS-DR** | SIGGRAPH 2024 | Deferred reflection | Environment map + deferred specular |
| **GaussianShader** | CVPR 2024 | Shading functions per Gaussian | Best for reflective/glossy surfaces |
| **RelitLRM** | 2024 | Feed-forward large model | 4-8 images → relightable splats |

### Tier 3: Latest (2025 H2 — 2026)

| Method | Venue | Approach | Notes |
|--------|-------|----------|-------|
| **SVG-IR** | CVPR 2025 | Spatially-varying material per Gaussian | More expressive than constant-attribute Gaussians |
| **DiscretizedSDF** | ICCV 2025 | SDF encoded in each Gaussian | No extra memory, surface-aligned via SDF-to-opacity transform |
| **GaRe** | ICCV 2025 | Outdoor relighting: sun/sky/indirect decomposition | Ray-traced shadows for outdoor scenes |
| **SU-RGS** | ICCV 2025 | Sparse views + unconstrained illumination | Joint optimization of materials + env lighting |
| **RTR-GS** | 2025 | Radiance transfer + reflection | Deferred PBR with secondary lighting |
| **TransparentGS** | SIGGRAPH 2025 | Transparent object inverse rendering | Fast pipeline for glass/translucent |
| **GlossyGS** | SIGGRAPH 2025 | Glossy object inverse rendering | Material priors for specular surfaces |
| **Relightable Codec Avatars** | SIGGRAPH 2025 | Full-body avatar relighting | Shadow network on proxy mesh |
| **R3GW** | arXiv 2026.03 | Outdoor wild scene PBR + 3DGS | Latest — separates foreground/background |

**Trend:** Methods are getting lighter (DiscretizedSDF: no extra memory) and more specialized (outdoor: GaRe/R3GW, avatars: Codec Avatars, transparent: TransparentGS). But ALL still require retraining.

### Key Insight from All Papers

Every method stores **per-Gaussian material properties** (albedo, normal, metallic, roughness) as separate attributes trained alongside geometry. They do NOT derive these from standard SH coefficients.

The PLY format for relightable splats typically adds:
- `nx, ny, nz` — surface normal
- `albedo_r, albedo_g, albedo_b` — diffuse albedo (NOT SH0)
- `metallic` — metallic factor
- `roughness` — roughness factor

Mesh2Splat can produce these because it converts from mesh data (which has explicit materials). Standard 3DGS training (from photos) does NOT produce these without a specialized inverse rendering pipeline.

---

## Practical Approaches (What Can Actually Be Implemented)

### Approach A: Write to UE's GBuffer (BEST OPTION)

**Volinga already does this.** Their `VolingaBasePassShader.usf` writes to UE's 8 GBuffer MRTs:

```hlsl
Out_1 = Emissive;        // SceneColor/Emissive
Out_2 = Normal;           // WorldNormal
Out_3 = MetallicSpecularRoughness;  // float4(M, S, R, ShadingModel)
Out_4 = BaseColor;        // Diffuse albedo
Out_5 = SceneVelocity;    // Motion vectors
OutDepth = Depth;         // SV_Depth
```

**How it works:**
- Render splats to off-screen textures (color, depth, normal)
- Fullscreen pass writes these into UE's GBuffer at the Base Pass stage
- UE's deferred pipeline handles: directional lights, point lights, spot lights, shadows, SSR, SSAO, GI — ALL automatically
- When normals are disabled: BaseColor=0, Emissive=full color → unlit appearance
- When normals are enabled: BaseColor=color, Emissive=ambient → UE lights them

**Pros:**
- Zero custom shadow code needed — UE handles everything
- All UE lighting features work (cascaded shadow maps, ray-traced shadows, Lumen GI)
- Integrates naturally with the rest of the scene
- Minimal additional GPU cost (one fullscreen pass)

**How Volinga handles double-lighting (the key trick):**
```hlsl
// When ExperimentalNormals == 0 (no lighting mode):
Emissive = BaseColor * 1.0;   // full emissive = baked appearance
BaseColor *= 0;                // zero = UE won't light it

// When ExperimentalNormals != 0 (lighting mode):
Emissive = BaseColor * AmbientLight;  // partial emissive = baked at reduced intensity
BaseColor *= 1;                        // feed to UE's lighting system
```
They write splat color to BOTH emissive (scaled down by `AmbientLight`) AND BaseColor. UE adds dynamic lighting on top. The `AmbientLight` parameter (0-1) controls the baked/dynamic ratio. This doesn't solve double-lighting — it lets users tune the balance.

**Cons:**
- Need good normals (Volinga calls it "ExperimentalNormals")
- Double-lighting is managed, not solved (user adjusts AmbientLight blend)
- Need to output correct SV_Depth for proper depth testing
- Requires hooking into UE's Base Pass, not PrePostProcess

**Implementation effort:** Medium. We already have GBuffer output. Need to:
1. Move from PrePostProcessPass to BasePass or write to UE's actual GBuffer textures
2. Output proper depth (we already compute world positions)
3. Handle the normal quality issue

### Approach B: Screen-Space Depth → Normals → Basic Lighting

**andrewkchan's lit-splat approach:**
1. Alpha-composite splat depths: `D = Σ d_i α_i Π(1-α_j)`
2. Reconstruct world positions from depth
3. Cross-product neighboring pixels for pseudo-normals
4. Bilateral filter to smooth noisy normals
5. Standard shadow map + shading

**Pros:**
- Works on ANY existing splats (no retraining)
- Screen-space normals are "good enough" for hard surfaces
- Shadow map is standard UE scene depth (not re-rendering splats)

**Cons:**
- Noisy normals on fuzzy/organic surfaces (grass, hair)
- No material properties (metallic/roughness) — just diffuse + specular
- Screen-space depth has holes and artifacts
- Not as integrated as Approach A

**Implementation effort:** Low-Medium. We can add depth output to existing render pass, compute normals in a compute shader.

### Approach C: Proxy Mesh Shadow

**Use a coarse mesh extracted from splat positions for shadow only.**

1. At load time, generate a simplified hull/mesh from Gaussian positions (e.g., marching cubes on density field, or Poisson reconstruction)
2. Use this mesh for UE's standard shadow map rendering
3. Apply the shadow to splats via screen-space comparison

**Pros:**
- Mesh shadow rendering is well-optimized in UE
- Coarse mesh = low poly = fast shadow pass
- Shadows are structurally correct (right shape, right occlusion)

**Cons:**
- Mesh extraction is complex
- Mesh doesn't update with dynamic splats
- Quality depends on mesh accuracy

**Proxy-GS paper:** Generates depth maps at 1000x1000 in <1ms using simplified proxy mesh. This validates the approach is fast enough.

### Approach D: Stochastic Shadow (Subset Rendering)

**Render only 5-10% of splats for shadow map.**

1. During shadow pass, use every Nth splat (or random subset based on hash)
2. Use larger quad size to compensate for gaps
3. Blur the shadow map to hide sampling artifacts

**Pros:**
- Simple to implement (skip in vertex shader based on index)
- GPU cost: 10-20% of full shadow pass instead of 100%
- Works with existing shadow infrastructure

**Cons:**
- Noisy shadows (mitigated by blur)
- Missing fine detail in shadow silhouette
- Still requires separate shadow pass

**Implementation effort:** Low. Add `if (gaussianIdx % 10 != 0) { out_position = 0; return; }` to shadow VS.

### Approach E: Retraining Pipeline (Long-Term)

**Add inverse rendering training to produce proper relightable splats.**

Based on GS-IR / R3DG approach:
1. Train with per-Gaussian albedo, normal, metallic, roughness (separate from SH)
2. Use environment map estimation + BRDF fitting during training
3. Bake occlusion/visibility during or after training
4. Output PLY with clean material properties

**This is the only approach that solves the double-lighting problem properly.**

**Cons:**
- Requires building/integrating a training pipeline
- Users must retrain their data (not just load existing PLY)
- Significant development effort
- Outside scope of a UE plugin (Python/CUDA training code)

---

## Mesh2Splat Reference Details

Mesh2Splat's viewer (OpenGL, C++):
- **PBR pipeline**: Deferred, Cook-Torrance BRDF (GGX NDF + Smith geometry + Schlick Fresnel)
- **Shadow**: 6-face cubemap, each face renders ALL splats with compute prepass
- **Shadow sampling**: 20-sample PCF, bias 0.05
- **Shadow PS**: `gl_FragDepth = length(out_pos - u_lightPos) / u_farPlane`
- **Compute prepass**: `gaussianPointShadowMappingCS.glsl` — per-splat compute assigns cubemap face, projects to 2D, writes indirect draw args. This is their optimization: single compute pass sorts splats into 6 face buckets, then 6 draw calls.
- **GPU cost**: Still renders ALL splats 7 times (1 main + 6 shadow), but the compute prepass avoids redundant vertex processing.
- **Material source**: Because Mesh2Splat converts FROM mesh (which has textures), it has real albedo/normal/MR data. This is fundamentally different from photo-trained 3DGS.

**Conclusion about Mesh2Splat**: Their approach is practical only because they have clean material data from mesh conversion. For photo-trained splats, this approach would have the same double-lighting problem we encountered.

---

## Recommended Strategy

### Phase 0: Depth Buffer Output (PREREQUISITE)

**Critical finding:** Our current PS has `TStaticDepthStencilState<false>` — depth write is DISABLED. UE's scene depth buffer has no knowledge of splat geometry. This blocks ALL screen-space techniques.

**Changes needed:**
1. Add `SV_Depth` output to `CSRenderSplatPS.usf` (compute alpha-composited depth)
2. Change `TStaticDepthStencilState<false>` to `TStaticDepthStencilState<true, CF_GreaterEqual>` in `CSGaussianRendering.cpp` line 254
3. Immediately unlocks: Contact Shadows, SSAO, mesh-splat depth occlusion — ALL for free

**Reference:** Volinga (`VolingaBasePassShader.usf:341`): `out float OutDepth : SV_Depth`
**Reference:** Postshot (`RdncFieldComposite.usf:11`): `out float depth_out : SV_Depth`

**Effort:** 1-2 days. **Impact:** Huge — this alone gives visible shadows via Contact Shadows.

### Phase 1: UE GBuffer Integration (Volinga Free Pattern)

Rewrite the render pipeline to output to UE's GBuffer instead of compositing to SceneColor directly.

1. Render splats → custom GBuffer (as we do now)
2. New fullscreen pass writes to UE's actual GBuffer MRTs (like Volinga 0.8.0)
3. UE's deferred pipeline applies lighting TO splats
4. For normals: use Gaussian covariance shortest axis (GS-IR) or screen-space depth-derived normals
5. For albedo: use SH0 with AmbientLight blend (Volinga pattern)
6. Set metallic=0, roughness=0.8 as defaults
7. Include `/Engine/Private/DeferredShadingCommon.ush` for GBuffer encoding

**What this gives:** Splats RECEIVE dynamic lighting + Contact Shadows + SSAO.
**What this does NOT give:** Splats CASTING shadows onto scene (requires geometry for shadow map).

**Important distinction:**
- GBuffer + SV_Depth = shadow receiving (light affects splats, contact shadows)
- Proxy Mesh = shadow casting (splats cast shadows onto other objects/themselves)
- Volinga Pro (paid) uses proxy mesh for shadow casting — technical details not public

### Phase 2: Better Normals (Approach B hybrid)

Add screen-space normal estimation:
1. Output depth buffer during splat rendering (done in Phase 0)
2. Compute pseudo-normals from depth cross-products in compute shader
3. Bilateral filter for smoothing
4. Use these instead of per-Gaussian normals (which we may not have for photo-trained data)

### Phase 3: Proxy Mesh for Shadow Casting

**Required for splats to cast shadows.** GBuffer alone only gives shadow receiving.

1. At PLY load time, generate simplified mesh from splat positions
   - Option A: Marching cubes on opacity density field (voxelize splat positions → isosurface)
   - Option B: Poisson reconstruction on splat positions + normals
   - Option C: Convex hull / alpha shape (simplest, least accurate)
2. Spawn hidden `UStaticMeshComponent` with `bCastHiddenShadow = true`
3. UE's Virtual Shadow Maps / Cascaded Shadow Maps render this mesh natively
4. Cost: 0.1-0.3ms for shadow pass (10K-50K triangle mesh vs 1.6M splats)
5. Proxy-GS paper validates: <1ms depth rendering at 1000x1000 with simplified proxy

**This is what Volinga Pro does** (commercial, details not public).
**This is also what the research report's "Approach C" describes.**

### Long-term: Retraining Pipeline

Partner with or integrate an inverse rendering pipeline (GS-IR, R3DG) to produce properly decomposed splats with clean material data. This is the only way to truly solve double-lighting.

---

## Key References

- **Volinga BasePassShader**: `ref/VolingaRenderer_0.8.0_UE_5.6_B116/Shaders/Private/VolingaBasePassShader.usf` — THE reference for UE GBuffer integration
- **Mesh2Splat**: github.com/electronicarts/mesh2splat — deferred PBR + cubemap shadow reference
- **andrewkchan lit-splat**: andrewkchan.dev/posts/lit-splat.html — screen-space normal recovery
- **GS-IR**: github.com/lzhnb/GS-IR — inverse rendering with baked occlusion
- **R3DG**: github.com/NJU-3DV/Relightable3DGaussian — BRDF decomposition
- **GS³**: gsrelight.github.io — triple splatting, 90fps
- **PRTGS**: arxiv.org/abs/2408.03538 — PRT for Gaussians
- **DeferredGS**: arxiv.org/abs/2404.09412 — deferred shading with material decomposition
- **Proxy-GS**: arxiv.org/abs/2509.24421 — proxy mesh for fast occlusion
- **StochasticSplats**: github.com/ubc-vision/stochasticsplats — 4x faster rendering
