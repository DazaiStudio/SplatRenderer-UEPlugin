# SplatRenderer — Rendering Improvements Research (2026-03-20)

## Prioritized Recommendations

### High Priority (High impact, moderate effort)

1. **Depth buffer output** — Write splat depth to scene depth buffer. Enables mesh-splat occlusion. Small change to pixel shader.
2. **SPZ format import** — 10x smaller files than PLY, glTF standard. Niantic MIT C++ library available.
3. **SH quantization** — Store SH data as uint8 textures instead of float32. Immediate memory/bandwidth win.
4. **Distance-based LOD** — Skip SH evaluation or reduce splat count based on camera distance.

### Medium Priority (High impact, significant effort)

5. **Sort-free rendering mode** — Weighted-sum rendering eliminates entire sort pass. 4-8x faster.
6. **Shadow casting via depth pass** — Second render pass from light perspective generates shadow map from splats.
7. **StopThePop** (SIGGRAPH 2024) — Per-tile hierarchical re-sorting eliminates popping, 4% overhead, 50% Gaussian reduction.
8. **VR stereo rendering** — Shader already binds InstancedView. Need 72+ FPS stereo + foveated quality scaling.

### Lower Priority (Future-facing, large effort)

9. **Octree-based hierarchical LOD** — Spatial acceleration for large-scale scenes.
10. **Neural occlusion culling (NVGS)** — MLP-based visibility prediction, 18KB per asset, 29-40% cull rate.
11. **Compressed 4DGS sequences** — Delta-encoded frames with GPU decompression.
12. **Triangle Splatting** — Long-term path to full UE pipeline integration (Lumen, Nanite compatibility).

---

## Key Papers (2024-2026)

### Sort-Free Rendering
- **Weighted Sum Rendering** (2025) — Replaces alpha-blending with depth-weighted sums, 8.6x faster on mobile. [arxiv.org/html/2410.18931v1](https://arxiv.org/html/2410.18931v1)
- **StochasticSplats** (2025) — Monte Carlo stochastic rasterization, 4x faster, works with standard Z-buffer + TAA. [arxiv.org/html/2503.24366v1](https://arxiv.org/html/2503.24366v1)

### Performance Optimization
- **Speedy-Splat / SnugBox** — Tight bounding boxes for projected Gaussians, 6.7x speedup. [speedysplat.github.io](https://speedysplat.github.io/)
- **StopThePop** (SIGGRAPH 2024) — Per-tile re-sorting, eliminates popping, 1.6x net speedup. [r4dl.github.io/StopThePop](https://r4dl.github.io/StopThePop/)

### LOD and Culling
- **Octree-GS** (TPAMI 2025) — Octree LOD with screen-space error selection, 219 FPS. [city-super.github.io/octree-gs](https://city-super.github.io/octree-gs/)
- **LODGE** (2025) — Hierarchical LOD for large scenes, 257-280 FPS. [lodge-gs.github.io](https://lodge-gs.github.io/)
- **NVGS** (2025) — Neural occlusion culling, 18KB MLP per asset, culls 29-40%. [arxiv.org/html/2511.19202](https://arxiv.org/html/2511.19202)

### Compression
- **SPZ** (Niantic, MIT) — 10x compression, glTF standard. [github.com/nianticlabs/spz](https://github.com/nianticlabs/spz)
- **C3DGS** — 31x compression via vector clustering. [keksboter.github.io/c3dgs](https://keksboter.github.io/c3dgs/)

### 4DGS
- **4D-Rotor GS** (SIGGRAPH 2024) — 4D XYZT Gaussians, 583 FPS on RTX 4090. [weify627.github.io/4drotorgs](https://weify627.github.io/4drotorgs/)
- **Anchored 4DGS** (SIGGRAPH Asia 2025) — Anchor points + per-frame neural decoding, more memory-efficient.

### Game Engine Integration
- **Triangle Splatting** (2025) — Triangles instead of Gaussians, compatible with shadow/ray tracing. [trianglesplatting.github.io](https://trianglesplatting.github.io/)
- **MeshSplats** (2025) — Opaque meshes from splats, enables shadows/reflections. [arxiv.org/html/2502.07754v1](https://arxiv.org/html/2502.07754v1)

### VR
- **VRSplat** (2025) — 72+ FPS stereo with foveated rasterizer. [arxiv.org/abs/2505.10144](https://arxiv.org/abs/2505.10144)

---

## Competitor Landscape

| Plugin | License | 4DGS | VR | Key Feature |
|--------|---------|------|-----|-------------|
| NanoGS | MIT | No | No | LOD clusters, radix sort |
| XScene | Apache 2.0 | No | No | Niagara-based (weak >500K) |
| Volinga Pro | Commercial | No | Yes | Relighting, shadows, HDR |
| MLSLabs | Proprietary | Yes | No | 7M+ splats @50fps |
| **SplatRenderer** | Beta | **Yes** | No | 4DGS + GSD + Crop + Audio |

### SplatRenderer unique advantages
- Only free plugin with 4DGS sequence playback
- GSD format with LZ4 compression + random frame access
- Crop Volume with draggable editor widget
- Audio sync
- ParticleFX (advanced, not yet released)

---

## Community Most Requested Features
1. Dynamic relighting / shadow casting
2. VR support
3. Smaller file sizes (compression)
4. LOD for large scenes
5. 4DGS playback (we have this)
6. In-editor splat editing tools

---

## Trends
- **Sort-free is the future** — Multiple research groups converging
- **glTF + SPZ becoming standard** — Khronos 2025 ratified
- **Mesh-splat hybrids** — Bridge to traditional rendering pipeline
- **Production adoption** — Superman (2025) used dynamic GS, Nuke 17 has native support

---

## Resources
- [Awesome 3D Gaussian Splatting](https://mrnerf.github.io/awesome-3D-gaussian-splatting/)
- [2025 Arxiv GS Paper List](https://github.com/Lee-JaeWon/2025-Arxiv-Paper-List-Gaussian-Splatting)
- [Radiance Fields Newsletter](https://radiancefields.substack.com/)
- [Epic Forums: GS Options](https://forums.unrealengine.com/t/gaussian-splatting-options/1907745)
- [NVIDIA vk_gaussian_splatting](https://github.com/nvpro-samples/vk_gaussian_splatting)
- [Real-Time Lighting with GS](https://andrewkchan.dev/posts/lit-splat.html)
