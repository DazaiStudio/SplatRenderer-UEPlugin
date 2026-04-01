# CSGaussianRenderer - Compute Shader Gaussian Splatting Plugin

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace Niagara-based 3DGS rendering with a Compute Shader pipeline that reads existing texture formats directly, supporting both static PLY and .bin sequence playback.

**Architecture:** Adapted from PICOSplat's proven rendering pipeline (ComputeDistance → GPU Sort → ComputeTransform → VS/PS quad splatting), but modified to read from Texture2D inputs (RGBA32F/16F) instead of packed buffers. Covariance matrices are computed on-the-fly from quaternion+scale textures rather than being pre-computed. A single plugin handles both static and sequence modes.

**Tech Stack:** UE 5.6, HLSL Compute Shaders (SM5), FSceneViewExtension, GPU Radix Sort (UE built-in `SortGPUBuffers`), RDG (Render Dependency Graph)

**Project:** `D:\Unreal Projects\FourDGS - V2\FourDGS.uproject`
**Plugin Location:** `D:\Unreal Projects\FourDGS - V2\Plugins\CSGaussianRenderer\`

---

## Texture Data Format Reference

The existing plugins store gaussian data in textures with a specific swizzle:

| Texture | R | G | B | A | Precision |
|---------|---|---|---|---|-----------|
| Position | ue.Z | ue.X | -ue.Y | 0 | RGBA32F |
| Rotation | quat.Z | quat.X | -quat.Y | quat.W | RGBA32F/16F |
| ScaleOpacity | exp(scale.Z) | exp(scale.X) | exp(scale.Y) | sigmoid(opacity) | RGBA32F/16F |
| SH0 | sh0_R | sh0_G | sh0_B | (unused) | RGBA32F/16F |

**Unswizzle in shader:**
```hlsl
// Position: texel → UE local space
float3 pos = float3(texel.g, -texel.b, texel.r);

// Rotation quaternion: texel → (x, y, z, w)
float4 quat = float4(texel.g, -texel.b, texel.r, texel.a);

// Scale: texel → (X, Y, Z) already activated (exp'd)
float3 scale = float3(texel.g, texel.b, texel.r);

// Opacity: already activated (sigmoid'd)
float opacity = scaleTex.a;

// Color from SH0 DC term
float3 color = saturate(sh0.rgb * 0.28209479177 + 0.5);
```

**Texture coordinate from linear index:**
```hlsl
uint2 tc = uint2(idx % texture_width, idx / texture_width);
float4 value = MyTexture.Load(int3(tc, 0));
```

---

## File Structure

```
CSGaussianRenderer/
├── CSGaussianRenderer.uplugin
├── Shaders/Private/
│   ├── CSGaussianCommon.ush
│   ├── CSComputeDistance.usf
│   ├── CSComputeTransform.usf
│   ├── CSRenderSplatVS.usf
│   └── CSRenderSplatPS.usf
└── Source/CSGaussianRenderer/
    ├── CSGaussianRenderer.Build.cs
    ├── Public/
    │   ├── CSGaussianRendererModule.h
    │   ├── CSGaussianComponent.h
    │   ├── CSGaussianActor.h
    │   └── CSGaussianSequencePlayer.h
    └── Private/
        ├── CSGaussianRendererModule.cpp
        ├── CSGaussianComponent.cpp
        ├── CSGaussianActor.cpp
        ├── CSGaussianSequencePlayer.cpp
        ├── CSGaussianSubsystem.h
        └── Rendering/
            ├── CSGaussianShaders.h
            ├── CSGaussianShaders.cpp
            ├── CSGaussianBuffers.h
            ├── CSGaussianSceneProxy.h
            ├── CSGaussianSceneProxy.cpp
            ├── CSGaussianViewExtension.h
            ├── CSGaussianViewExtension.cpp
            ├── CSGaussianRendering.h
            ├── CSGaussianRendering.cpp
            └── CSGaussianRenderingUtilities.h
```

---

## Task 1: Plugin Skeleton

### Files to Create

**1.1: `CSGaussianRenderer.uplugin`**

```json
{
  "FileVersion": 3,
  "Version": 1,
  "VersionName": "1.0",
  "FriendlyName": "CS Gaussian Renderer",
  "Description": "Compute Shader based 3D Gaussian Splatting renderer",
  "Category": "Rendering",
  "CreatedBy": "",
  "CanContainContent": false,
  "Modules": [
    {
      "Name": "CSGaussianRenderer",
      "Type": "Runtime",
      "LoadingPhase": "PostConfigInit"
    }
  ]
}
```

**1.2: `Source/CSGaussianRenderer/CSGaussianRenderer.Build.cs`**

```csharp
using System.IO;
using UnrealBuildTool;

public class CSGaussianRenderer : ModuleRules
{
    public CSGaussianRenderer(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Projects",
            "RenderCore",
            "Renderer",
            "RHI",
        });

        PrivateIncludePaths.AddRange(new string[]
        {
            Path.Combine(GetModuleDirectory("Renderer"), "Private"),
            Path.Combine(GetModuleDirectory("Renderer"), "Internal"),
        });
    }
}
```

**1.3: `Source/CSGaussianRenderer/Public/CSGaussianRendererModule.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FCSGaussianRendererModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
```

**1.4: `Source/CSGaussianRenderer/Private/CSGaussianRendererModule.cpp`**

```cpp
#include "CSGaussianRendererModule.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FCSGaussianRendererModule"

void FCSGaussianRendererModule::StartupModule()
{
    FString PluginShaderDir = FPaths::Combine(
        IPluginManager::Get().FindPlugin(TEXT("CSGaussianRenderer"))->GetBaseDir(),
        TEXT("Shaders"));
    AddShaderSourceDirectoryMapping(TEXT("/Plugin/CSGaussianRenderer"), PluginShaderDir);
}

void FCSGaussianRendererModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FCSGaussianRendererModule, CSGaussianRenderer)
```

### Step 1: Create all directories
```bash
mkdir -p "D:/Unreal Projects/FourDGS - V2/Plugins/CSGaussianRenderer/Shaders/Private"
mkdir -p "D:/Unreal Projects/FourDGS - V2/Plugins/CSGaussianRenderer/Source/CSGaussianRenderer/Public"
mkdir -p "D:/Unreal Projects/FourDGS - V2/Plugins/CSGaussianRenderer/Source/CSGaussianRenderer/Private/Rendering"
```

### Step 2: Write all 4 files above

### Step 3: Add plugin to .uproject
Add to the Plugins array in `FourDGS.uproject`:
```json
{
    "Name": "CSGaussianRenderer",
    "Enabled": true
}
```

### Step 4: Verify compilation
```bash
cd "C:/Program Files/Epic Games/UE_5.6/Engine/Build/BatchFiles"
./RunUBT.sh FourDGS Win64 Development -Project="D:/Unreal Projects/FourDGS - V2/FourDGS.uproject" -TargetType=Editor
```

---

## Task 2: Shader Files

### 2.1: `Shaders/Private/CSGaussianCommon.ush`

```hlsl
// Constants
#define RADIUS_SIGMA sqrt(8.0)
#define RADIUS_SIGMA_OVER_SQRT_2 (RADIUS_SIGMA / sqrt(2.0))
#define CUTOFF_RADIUS_SIGMA_SQUARED_OVER_2 (RADIUS_SIGMA_OVER_SQRT_2 * RADIUS_SIGMA_OVER_SQRT_2)

#define DISTANCE_PRECISION 16
#define DISTANCE_SCALE ((1 << DISTANCE_PRECISION) - 2)
#define DISTANCE_NOT_VISIBLE ((1 << DISTANCE_PRECISION) - 1)

// SH0 DC coefficient: 1 / (2 * sqrt(pi))
#define SH_C0 0.28209479177387814

// Read position from texture and unswizzle to UE local space
// Texture stores: R=ue.Z, G=ue.X, B=-ue.Y
float3 ReadPosition(Texture2D<float4> tex, uint idx, uint tex_width)
{
    uint2 tc = uint2(idx % tex_width, idx / tex_width);
    float4 t = tex.Load(int3(tc, 0));
    return float3(t.g, -t.b, t.r);
}

// Read rotation quaternion from texture and unswizzle
// Texture stores: R=q.Z, G=q.X, B=-q.Y, A=q.W
// Returns: (x, y, z, w)
float4 ReadRotation(Texture2D<float4> tex, uint idx, uint tex_width)
{
    uint2 tc = uint2(idx % tex_width, idx / tex_width);
    float4 t = tex.Load(int3(tc, 0));
    return float4(t.g, -t.b, t.r, t.a);
}

// Read scale from texture and unswizzle
// Texture stores: R=exp(s.Z), G=exp(s.X), B=exp(s.Y), A=sigmoid(opacity)
// Returns scale as (X, Y, Z) - already activated
float3 ReadScale(Texture2D<float4> tex, uint idx, uint tex_width)
{
    uint2 tc = uint2(idx % tex_width, idx / tex_width);
    float4 t = tex.Load(int3(tc, 0));
    return float3(t.g, t.b, t.r);
}

// Read opacity from ScaleOpacity texture (already sigmoid-activated)
float ReadOpacity(Texture2D<float4> tex, uint idx, uint tex_width)
{
    uint2 tc = uint2(idx % tex_width, idx / tex_width);
    return tex.Load(int3(tc, 0)).a;
}

// Read color from SH0 texture
// DC color = SH0 * C0 + 0.5
float3 ReadColorSH0(Texture2D<float4> tex, uint idx, uint tex_width)
{
    uint2 tc = uint2(idx % tex_width, idx / tex_width);
    float3 sh0 = tex.Load(int3(tc, 0)).rgb;
    return saturate(sh0 * SH_C0 + 0.5);
}

// Compute 3x3 covariance matrix from quaternion and scale
// Sigma = R * diag(s^2) * R^T where R is rotation matrix from quat
float3x3 ComputeCovariance(float4 q, float3 s)
{
    float x = q.x, y = q.y, z = q.z, w = q.w;

    // Rotation matrix from quaternion
    float3x3 R = float3x3(
        1.0 - 2.0*(y*y + z*z), 2.0*(x*y - w*z),       2.0*(x*z + w*y),
        2.0*(x*y + w*z),       1.0 - 2.0*(x*x + z*z),  2.0*(y*z - w*x),
        2.0*(x*z - w*y),       2.0*(y*z + w*x),         1.0 - 2.0*(x*x + y*y)
    );

    // M = R * diag(s), then Sigma = M * M^T
    float3x3 M = float3x3(
        R[0] * s,
        R[1] * s,
        R[2] * s
    );

    return mul(M, transpose(M));
}
```

### 2.2: `Shaders/Private/CSComputeDistance.usf`

```hlsl
#include "/Engine/Public/Platform.ush"
#include "CSGaussianCommon.ush"

Texture2D<float4> PositionTexture;
uint texture_width;
uint num_splats;
float4x4 local_to_clip;

RWBuffer<uint> indices;
RWBuffer<uint> distances;

[numthreads(THREAD_GROUP_SIZE_X, 1, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint idx = dispatch_thread_id.x;
    if (idx >= num_splats)
        return;

    float3 pos_local = ReadPosition(PositionTexture, idx, texture_width);
    float4 pos_clip = mul(float4(pos_local, 1.0), local_to_clip);

    bool inside_frustum = !(pos_clip.x < -pos_clip.w || pos_clip.x > pos_clip.w ||
                            pos_clip.y < -pos_clip.w || pos_clip.y > pos_clip.w ||
                            pos_clip.z > pos_clip.w);

    indices[idx] = idx;
    distances[idx] = inside_frustum
        ? uint(saturate(pos_clip.z / pos_clip.w) * DISTANCE_SCALE)
        : DISTANCE_NOT_VISIBLE;
}
```

### 2.3: `Shaders/Private/CSComputeTransform.usf`

```hlsl
#include "/Engine/Public/Platform.ush"
#include "CSGaussianCommon.ush"

Texture2D<float4> PositionTexture;
Texture2D<float4> RotationTexture;
Texture2D<float4> ScaleOpacityTexture;
uint texture_width;
uint num_splats;
float4x4 local_to_view;
float two_focal_length;

RWBuffer<float4> transforms;

[numthreads(THREAD_GROUP_SIZE_X, 1, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint idx = dispatch_thread_id.x;
    if (idx >= num_splats)
        return;

    float3 pos_local = ReadPosition(PositionTexture, idx, texture_width);
    float4 quat = ReadRotation(RotationTexture, idx, texture_width);
    float3 scale = ReadScale(ScaleOpacityTexture, idx, texture_width);

    // Build 3x3 covariance from quaternion + scale
    float3x3 sig = ComputeCovariance(quat, scale);

    // Transform position to view space
    float3 pos_view = mul(float4(pos_local, 1.0), local_to_view).xyz;

    // View matrix W (upper 3x3 of local_to_view)
    // Note: UE uses row-major / row-vector convention (v * M)
    float3x3 W = float3x3(
        local_to_view[0].xyz,
        local_to_view[1].xyz,
        local_to_view[2].xyz
    );

    // Jacobian of perspective projection combined with W
    // J = [[1/z, 0, -x/z^2], [0, 1/z, -y/z^2]]
    // Simplified: scale_x = -pos_view.x / pos_view.z
    float scale_x = -pos_view.x / pos_view.z;
    float scale_y = -pos_view.y / pos_view.z;

    // (J * W) rows
    float3 jw_0 = float3(
        W[0][0] + scale_x * W[0][2],
        W[1][0] + scale_x * W[1][2],
        W[2][0] + scale_x * W[2][2]
    );
    float3 jw_1 = float3(
        W[0][1] + scale_y * W[0][2],
        W[1][1] + scale_y * W[1][2],
        W[2][1] + scale_y * W[2][2]
    );

    // Sigma' = (J*W) * Sigma * (J*W)^T
    float3 jw_sig_0 = mul(jw_0, sig);
    float3 jw_sig_1 = mul(jw_1, sig);

    float sig_p_00 = dot(jw_sig_0, jw_0);
    float sig_p_01 = dot(jw_sig_0, jw_1);
    float sig_p_11 = dot(jw_sig_1, jw_1);

    // Eigendecomposition of 2x2 Sigma'
    float det = sig_p_00 * sig_p_11 - sig_p_01 * sig_p_01;
    float trace = sig_p_00 + sig_p_11;
    float sqrt_disc = sqrt(max(trace * trace - 4.0 * det, 0.0));

    float2 two_lmb = float2(trace + sqrt_disc, trace - sqrt_disc);
    float2 sqrt_two_sigma = sqrt(max(two_lmb, float2(0.0, 0.0)));

    // First eigenvector
    float2 v_0 = normalize(float2(sig_p_01, two_lmb.x * 0.5 - sig_p_00));

    // Scale: 2*focal_length / z * sqrt(2*lambda)
    float2 s = two_focal_length / pos_view.z * sqrt_two_sigma;

    // 2x2 rotation+scale matrix packed as float4
    transforms[idx] = float4(v_0.x, -v_0.y, v_0.y, v_0.x) * s.xyxy;
}
```

### 2.4: `Shaders/Private/CSRenderSplatVS.usf`

```hlsl
#include "/Engine/Private/Common.ush"
#include "/Engine/Public/Platform.ush"
#include "CSGaussianCommon.ush"

Texture2D<float4> PositionTexture;
Texture2D<float4> SH0Texture;
Texture2D<float4> ScaleOpacityTexture;
uint texture_width;

float4x4 local_to_world;
Buffer<uint> sorted_indices;
Buffer<float4> computed_transforms;

// UE world-to-clip using translated world coordinates (LWC support)
half4 world_to_clip(half3 world)
{
    ResolvedView = ResolveView();
    world += local_to_world[3].xyz + ResolvedView.PreViewTranslationHigh;
    return mul(half4(world, 1), ResolvedView.TranslatedWorldToClip);
}

half2 get_render_resolution()
{
    return View.ViewSizeAndInvSize.xy / View.ViewResolutionFraction;
}

void main(
    in uint in_id : SV_VertexID,
    out half2 out_sig_div_sqrt_2 : DELTA_STD_DEVS,
    out nointerpolation half4 out_color : COLOR,
    out half4 out_position : SV_Position)
{
    uint splat_id = sorted_indices[in_id / 6];

    // Read position and transform to world space
    half3 pos_local = (half3)ReadPosition(PositionTexture, splat_id, texture_width);
    half3 pos_world = mul(pos_local, (half3x3)local_to_world);
    half4 pos_clip = world_to_clip(pos_world);

    // Frustum cull
    bool outside_frustum = pos_clip.x < -pos_clip.w || pos_clip.x > pos_clip.w ||
                           pos_clip.y < -pos_clip.w || pos_clip.y > pos_clip.w ||
                           pos_clip.z > pos_clip.w;

    if (outside_frustum)
    {
        out_position = half4(0, 0, 0, 0);
        out_sig_div_sqrt_2 = half2(0, 0);
        out_color = half4(0, 0, 0, 0);
        return;
    }

    // Get 2x2 transform from compute pass
    half4 t = (half4)computed_transforms[splat_id];

    // 6 vertices per splat (2 triangles = 1 quad)
    half2 corners[6] = {
        half2(-RADIUS_SIGMA_OVER_SQRT_2, -RADIUS_SIGMA_OVER_SQRT_2),
        half2( RADIUS_SIGMA_OVER_SQRT_2, -RADIUS_SIGMA_OVER_SQRT_2),
        half2(-RADIUS_SIGMA_OVER_SQRT_2,  RADIUS_SIGMA_OVER_SQRT_2),
        half2( RADIUS_SIGMA_OVER_SQRT_2, -RADIUS_SIGMA_OVER_SQRT_2),
        half2(-RADIUS_SIGMA_OVER_SQRT_2,  RADIUS_SIGMA_OVER_SQRT_2),
        half2( RADIUS_SIGMA_OVER_SQRT_2,  RADIUS_SIGMA_OVER_SQRT_2)
    };

    // Apply 2x2 transform to corner offset
    half2 offset = mul(half2x2(t.x, t.y, t.z, t.w), corners[in_id % 6]);
    // Pixel to NDC
    offset /= get_render_resolution();

    out_position = half4(pos_clip.xy + offset * pos_clip.w, pos_clip.z, pos_clip.w);
    out_sig_div_sqrt_2 = corners[in_id % 6];

    // Color from SH0 + opacity from ScaleOpacity
    half3 color = (half3)ReadColorSH0(SH0Texture, splat_id, texture_width);
    half opacity = (half)ReadOpacity(ScaleOpacityTexture, splat_id, texture_width);
    out_color = half4(color, opacity);
}
```

### 2.5: `Shaders/Private/CSRenderSplatPS.usf`

```hlsl
#include "/Engine/Public/Platform.ush"
#include "CSGaussianCommon.ush"

void main(
    in half2 sig_div_sqrt_2 : DELTA_STD_DEVS,
    in nointerpolation half4 in_color : COLOR,
    out half4 out_color : SV_Target0)
{
    half sig_sq_div_2 = dot(sig_div_sqrt_2, sig_div_sqrt_2);

    half alpha_gaussian = (sig_sq_div_2 < CUTOFF_RADIUS_SIGMA_SQUARED_OVER_2)
        ? in_color.a / exp(sig_sq_div_2)
        : 0;

    out_color = half4(in_color.rgb, alpha_gaussian);
}
```

### Step: Write all 5 shader files, then compile to verify no syntax errors

---

## Task 3: C++ Shader Bindings + GPU Buffers

### 3.1: `Private/Rendering/CSGaussianShaders.h`

```cpp
#pragma once

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"

namespace CSGaussian
{

constexpr uint32 THREAD_GROUP_SIZE_X = 64;
constexpr uint32 DepthMask = 0x0000FFFF;

// --- Compute Distance ---

class FCSComputeDistanceCS final : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FCSComputeDistanceCS);
    SHADER_USE_PARAMETER_STRUCT(FCSComputeDistanceCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FMatrix44f, local_to_clip)
        SHADER_PARAMETER(uint32, num_splats)
        SHADER_PARAMETER(uint32, texture_width)
        SHADER_PARAMETER_TEXTURE(Texture2D<float4>, PositionTexture)
        SHADER_PARAMETER_UAV(RWBuffer<uint>, indices)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, distances)
    END_SHADER_PARAMETER_STRUCT()

public:
    static void ModifyCompilationEnvironment(
        const FGlobalShaderPermutationParameters& Parameters,
        FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE_X"), THREAD_GROUP_SIZE_X);
    }
};

// --- Compute Transform ---

class FCSComputeTransformCS final : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FCSComputeTransformCS);
    SHADER_USE_PARAMETER_STRUCT(FCSComputeTransformCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FMatrix44f, local_to_view)
        SHADER_PARAMETER(float, two_focal_length)
        SHADER_PARAMETER(uint32, num_splats)
        SHADER_PARAMETER(uint32, texture_width)
        SHADER_PARAMETER_TEXTURE(Texture2D<float4>, PositionTexture)
        SHADER_PARAMETER_TEXTURE(Texture2D<float4>, RotationTexture)
        SHADER_PARAMETER_TEXTURE(Texture2D<float4>, ScaleOpacityTexture)
        SHADER_PARAMETER_UAV(RWBuffer<float4>, transforms)
    END_SHADER_PARAMETER_STRUCT()

public:
    static void ModifyCompilationEnvironment(
        const FGlobalShaderPermutationParameters& Parameters,
        FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE_X"), THREAD_GROUP_SIZE_X);
    }
};

// --- Render VS/PS shared parameters ---

BEGIN_SHADER_PARAMETER_STRUCT(FCSRenderSplatSharedParameters, )
    SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
    SHADER_PARAMETER_STRUCT_REF(FInstancedViewUniformShaderParameters, InstancedView)
    SHADER_PARAMETER(FMatrix44f, local_to_world)
    SHADER_PARAMETER(uint32, texture_width)
    SHADER_PARAMETER_TEXTURE(Texture2D<float4>, PositionTexture)
    SHADER_PARAMETER_TEXTURE(Texture2D<float4>, SH0Texture)
    SHADER_PARAMETER_TEXTURE(Texture2D<float4>, ScaleOpacityTexture)
    SHADER_PARAMETER_SRV(Buffer<float4>, computed_transforms)
    SHADER_PARAMETER_SRV(Buffer<uint>, sorted_indices)
END_SHADER_PARAMETER_STRUCT()

// --- Vertex Shader ---

class FCSRenderSplatVS final : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FCSRenderSplatVS);
    SHADER_USE_PARAMETER_STRUCT(FCSRenderSplatVS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_STRUCT_INCLUDE(FCSRenderSplatSharedParameters, Shared)
    END_SHADER_PARAMETER_STRUCT()
};

// --- Pixel Shader ---

class FCSRenderSplatPS final : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FCSRenderSplatPS);
    SHADER_USE_PARAMETER_STRUCT(FCSRenderSplatPS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
};

} // namespace CSGaussian
```

### 3.2: `Private/Rendering/CSGaussianShaders.cpp`

```cpp
#include "CSGaussianShaders.h"

namespace CSGaussian
{

IMPLEMENT_GLOBAL_SHADER(
    FCSComputeDistanceCS,
    "/Plugin/CSGaussianRenderer/Private/CSComputeDistance.usf",
    "main",
    SF_Compute);

IMPLEMENT_GLOBAL_SHADER(
    FCSComputeTransformCS,
    "/Plugin/CSGaussianRenderer/Private/CSComputeTransform.usf",
    "main",
    SF_Compute);

IMPLEMENT_GLOBAL_SHADER(
    FCSRenderSplatVS,
    "/Plugin/CSGaussianRenderer/Private/CSRenderSplatVS.usf",
    "main",
    SF_Vertex);

IMPLEMENT_GLOBAL_SHADER(
    FCSRenderSplatPS,
    "/Plugin/CSGaussianRenderer/Private/CSRenderSplatPS.usf",
    "main",
    SF_Pixel);

} // namespace CSGaussian
```

### 3.3: `Private/Rendering/CSGaussianBuffers.h`

```cpp
#pragma once

#include "RenderResource.h"
#include "RHI.h"
#include "RHIResources.h"

namespace CSGaussian
{

// GPU-only buffer with both SRV and UAV (for intermediate compute results)
class FCSGPUBuffer : public FVertexBufferWithSRV
{
public:
    FCSGPUBuffer() = default;

    FCSGPUBuffer(uint32 InNumElements, EPixelFormat InFormat)
        : NumElements(InNumElements)
        , Format(InFormat)
    {
    }

    virtual void InitRHI(FRHICommandListBase& RHICmdList) override
    {
        if (NumElements == 0) return;

        uint32 Stride = GPixelFormats[Format].BlockBytes;
        uint32 Size = NumElements * Stride;

        FRHIResourceCreateInfo CreateInfo(TEXT("CSGPUBuffer"));
        Buffer = RHICmdList.CreateVertexBuffer(
            Size,
            BUF_ShaderResource | BUF_UnorderedAccess,
            ERHIAccess::UAVCompute,
            CreateInfo);

        ShaderResourceViewRHI = RHICmdList.CreateShaderResourceView(Buffer, Stride, Format);
        UnorderedAccessViewRHI = RHICmdList.CreateUnorderedAccessView(Buffer, Format);
    }

    virtual void ReleaseRHI() override
    {
        UnorderedAccessViewRHI.SafeRelease();
        FVertexBufferWithSRV::ReleaseRHI();
    }

private:
    uint32 NumElements = 0;
    EPixelFormat Format = PF_Unknown;
};

} // namespace CSGaussian
```

### Step: Write files, compile

---

## Task 4: Scene Proxy

### 4.1: `Private/Rendering/CSGaussianSceneProxy.h`

```cpp
#pragma once

#include "CSGaussianBuffers.h"
#include "PrimitiveSceneProxy.h"

class UCSGaussianComponent;

namespace CSGaussian
{

class FCSGaussianSceneProxy final : public FPrimitiveSceneProxy
{
public:
    FCSGaussianSceneProxy(UCSGaussianComponent& Component);

    //~ Begin FPrimitiveSceneProxy Interface
    virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override;
    virtual void DestroyRenderThreadResources() override;
    virtual uint32 GetMemoryFootprint() const override { return sizeof(*this); }
    virtual SIZE_T GetTypeHash() const override
    {
        static size_t UniquePointer;
        return reinterpret_cast<SIZE_T>(&UniquePointer);
    }
    //~ End FPrimitiveSceneProxy Interface

    uint32 GetNumSplats() const { return NumSplats; }
    uint32 GetTextureWidth() const { return TextureWidth; }
    bool IsVisible(const FSceneView& View) const;

    // Texture accessors (render thread)
    FTextureRHIRef GetPositionTextureRHI() const { return PositionTextureRHI; }
    FTextureRHIRef GetRotationTextureRHI() const { return RotationTextureRHI; }
    FTextureRHIRef GetScaleOpacityTextureRHI() const { return ScaleOpacityTextureRHI; }
    FTextureRHIRef GetSH0TextureRHI() const { return SH0TextureRHI; }

    // GPU buffer accessors
    FShaderResourceViewRHIRef GetIndicesSRV() const { return Indices.ShaderResourceViewRHI; }
    FUnorderedAccessViewRHIRef GetIndicesUAV() const { return Indices.UnorderedAccessViewRHI; }
    FShaderResourceViewRHIRef GetTransformsSRV() const { return Transforms.ShaderResourceViewRHI; }
    FUnorderedAccessViewRHIRef GetTransformsUAV() const { return Transforms.UnorderedAccessViewRHI; }

    FRDGBufferRef& GetIndicesFake() { return IndicesFake; }
    FRDGBufferRef& GetDistancesFake() { return DistancesFake; }

    FString GetName() const { return Name; }

    // Called from game thread via ENQUEUE_RENDER_COMMAND to update textures
    void UpdateTextures_RenderThread(
        FTextureRHIRef InPosition,
        FTextureRHIRef InRotation,
        FTextureRHIRef InScaleOpacity,
        FTextureRHIRef InSH0,
        uint32 InNumSplats,
        uint32 InTextureWidth);

private:
    // Texture RHI refs (updated dynamically for sequence mode)
    FTextureRHIRef PositionTextureRHI;
    FTextureRHIRef RotationTextureRHI;
    FTextureRHIRef ScaleOpacityTextureRHI;
    FTextureRHIRef SH0TextureRHI;

    uint32 NumSplats = 0;
    uint32 TextureWidth = 0;

    // GPU intermediate buffers
    FCSGPUBuffer Indices;
    FCSGPUBuffer Transforms;

    // Fake RDG buffers for resource tracking
    FRDGBufferRef IndicesFake;
    FRDGBufferRef DistancesFake;

    FString Name;

    // Max capacity (for buffer reallocation)
    uint32 MaxSplats = 0;
};

} // namespace CSGaussian
```

### 4.2: `Private/Rendering/CSGaussianSceneProxy.cpp`

```cpp
#include "CSGaussianSceneProxy.h"
#include "CSGaussianComponent.h"
#include "CSGaussianSubsystem.h"

namespace CSGaussian
{

FCSGaussianSceneProxy::FCSGaussianSceneProxy(UCSGaussianComponent& Component)
    : FPrimitiveSceneProxy(&Component)
{
    // Get initial texture data from component
    NumSplats = Component.GetGaussianCount();
    TextureWidth = Component.GetTextureWidth();
    MaxSplats = NumSplats;

    if (NumSplats > 0)
    {
        Indices = FCSGPUBuffer(NumSplats, PF_R32_UINT);
        Transforms = FCSGPUBuffer(NumSplats, PF_FloatRGBA);
    }

    // Capture texture RHI refs
    auto GetTextureRHI = [](UTexture2D* Tex) -> FTextureRHIRef
    {
        if (Tex && Tex->GetResource())
            return Tex->GetResource()->TextureRHI;
        return nullptr;
    };

    PositionTextureRHI = GetTextureRHI(Component.PositionTexture);
    RotationTextureRHI = GetTextureRHI(Component.RotationTexture);
    ScaleOpacityTextureRHI = GetTextureRHI(Component.ScaleOpacityTexture);
    SH0TextureRHI = GetTextureRHI(Component.SH0Texture);

    Name = Component.GetOwner() ? Component.GetOwner()->GetName() : TEXT("Unknown");
}

void FCSGaussianSceneProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
{
    if (NumSplats > 0)
    {
        Indices.InitRHI(RHICmdList);
        Transforms.InitRHI(RHICmdList);
    }

    if (GEngine)
    {
        UCSGaussianSubsystem* Subsystem = GEngine->GetEngineSubsystem<UCSGaussianSubsystem>();
        if (Subsystem)
        {
            Subsystem->RegisterProxy_RenderThread(this);
        }
    }
}

void FCSGaussianSceneProxy::DestroyRenderThreadResources()
{
    if (GEngine)
    {
        UCSGaussianSubsystem* Subsystem = GEngine->GetEngineSubsystem<UCSGaussianSubsystem>();
        if (Subsystem)
        {
            Subsystem->UnregisterProxy_RenderThread(this);
        }
    }

    Indices.ReleaseResource();
    Transforms.ReleaseResource();
}

bool FCSGaussianSceneProxy::IsVisible(const FSceneView& View) const
{
    if (NumSplats == 0 || !PositionTextureRHI)
        return false;

    return IsShown(&View) && &GetScene() == View.Family->Scene;
}

void FCSGaussianSceneProxy::UpdateTextures_RenderThread(
    FTextureRHIRef InPosition,
    FTextureRHIRef InRotation,
    FTextureRHIRef InScaleOpacity,
    FTextureRHIRef InSH0,
    uint32 InNumSplats,
    uint32 InTextureWidth)
{
    check(IsInRenderingThread());

    PositionTextureRHI = InPosition;
    RotationTextureRHI = InRotation;
    ScaleOpacityTextureRHI = InScaleOpacity;
    SH0TextureRHI = InSH0;
    NumSplats = InNumSplats;
    TextureWidth = InTextureWidth;

    // Reallocate GPU buffers if needed
    if (InNumSplats > MaxSplats)
    {
        Indices.ReleaseResource();
        Transforms.ReleaseResource();

        MaxSplats = InNumSplats;
        Indices = FCSGPUBuffer(MaxSplats, PF_R32_UINT);
        Transforms = FCSGPUBuffer(MaxSplats, PF_FloatRGBA);

        FRHICommandListImmediate& RHICmdList = FRHICommandListImmediate::Get();
        Indices.InitRHI(RHICmdList);
        Transforms.InitRHI(RHICmdList);
    }
}

} // namespace CSGaussian
```

### Step: Write files, compile

---

## Task 5: View Extension + Rendering

### 5.1: `Private/Rendering/CSGaussianRenderingUtilities.h`

```cpp
#pragma once

#include "CSGaussianSceneProxy.h"
#include "SceneView.h"

namespace CSGaussian
{

inline float GetFocalLength(const FSceneView& View)
{
    return View.UnconstrainedViewRect.Width() /
           (2.f * tanf(View.ViewMatrices.ComputeHalfFieldOfViewPerAxis().X));
}

inline FMatrix GetView(const FSceneView& View)
{
    return View.ViewMatrices.GetViewMatrix();
}

inline FMatrix GetViewProj(const FSceneView& View)
{
    return View.ViewMatrices.GetViewProjectionMatrix();
}

inline uint32 NumThreadGroups(uint32 NumElements)
{
    return (NumElements + (THREAD_GROUP_SIZE_X - 1)) / THREAD_GROUP_SIZE_X;
}

} // namespace CSGaussian
```

### 5.2: `Private/Rendering/CSGaussianRendering.h`

```cpp
#pragma once

#include "CSGaussianSceneProxy.h"
#include "CSGaussianShaders.h"
#include "RHICommandList.h"
#include "RenderGraphBuilder.h"
#include "SceneView.h"

namespace CSGaussian
{

BEGIN_SHADER_PARAMETER_STRUCT(FCSRenderSplatDeps, )
    SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, Indices)
    SHADER_PARAMETER_STRUCT_INCLUDE(FCSRenderSplatVS::FParameters, VS)
    SHADER_PARAMETER_STRUCT_INCLUDE(FCSRenderSplatPS::FParameters, PS)
END_SHADER_PARAMETER_STRUCT()

FRDGPassRef CalculateDistances(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    FCSGaussianSceneProxy* Proxy,
    FRDGBufferRef Indices,
    FRDGBufferRef Distances);

FRDGPassRef ComputeTransforms(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    FCSGaussianSceneProxy* Proxy);

FRDGPassRef SortSplats(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    FCSGaussianSceneProxy* Proxy,
    FRDGBufferRef Indices,
    FRDGBufferRef Distances);

void RenderSplats(
    FRHICommandList& RHICmdList,
    FCSRenderSplatDeps* SplatParameters,
    uint32 NumSplats,
    const FSceneView& View);

} // namespace CSGaussian
```

### 5.3: `Private/Rendering/CSGaussianRendering.cpp`

```cpp
#include "CSGaussianRendering.h"

#include "GPUSort.h"
#include "RenderGraphUtils.h"
#include "SceneRendering.h"
#include "CSGaussianRenderingUtilities.h"

namespace CSGaussian
{

namespace
{
BEGIN_SHADER_PARAMETER_STRUCT(FGPUSortProducerParameters, )
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, IndicesUAV)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, Indices2UAV)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, Distances2UAV)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FGPUSortParameters, )
    SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, IndicesSRV)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, IndicesUAV)
    SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, Indices2SRV)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, Indices2UAV)
    SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, DistancesSRV)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DistancesUAV)
    SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, Distances2SRV)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, Distances2UAV)
END_SHADER_PARAMETER_STRUCT()
} // namespace

FRDGPassRef CalculateDistances(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    FCSGaussianSceneProxy* Proxy,
    FRDGBufferRef Indices,
    FRDGBufferRef Distances)
{
    check(Proxy);

    const FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
    TShaderRef<FCSComputeDistanceCS> Shader =
        GlobalShaderMap->GetShader<FCSComputeDistanceCS>();

    FCSComputeDistanceCS::FParameters* Params =
        GraphBuilder.AllocParameters<FCSComputeDistanceCS::FParameters>();
    Params->local_to_clip = FMatrix44f(Proxy->GetLocalToWorld() * GetViewProj(View));
    Params->num_splats = Proxy->GetNumSplats();
    Params->texture_width = Proxy->GetTextureWidth();
    Params->PositionTexture = Proxy->GetPositionTextureRHI();
    Params->indices = Proxy->GetIndicesUAV();
    Params->distances = GraphBuilder.CreateUAV(Distances, PF_R16_UINT);

    return FComputeShaderUtils::AddPass(
        GraphBuilder,
        RDG_EVENT_NAME("CSGaussian: Distances %s", *Proxy->GetName()),
        ERDGPassFlags::AsyncCompute,
        Shader,
        Params,
        FIntVector(NumThreadGroups(Proxy->GetNumSplats()), 1, 1));
}

FRDGPassRef ComputeTransforms(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    FCSGaussianSceneProxy* Proxy)
{
    check(Proxy);

    const FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
    TShaderRef<FCSComputeTransformCS> Shader =
        GlobalShaderMap->GetShader<FCSComputeTransformCS>();

    FCSComputeTransformCS::FParameters* Params =
        GraphBuilder.AllocParameters<FCSComputeTransformCS::FParameters>();
    Params->local_to_view = FMatrix44f(Proxy->GetLocalToWorld() * GetView(View));
    Params->two_focal_length = 2.f * GetFocalLength(View);
    Params->num_splats = Proxy->GetNumSplats();
    Params->texture_width = Proxy->GetTextureWidth();
    Params->PositionTexture = Proxy->GetPositionTextureRHI();
    Params->RotationTexture = Proxy->GetRotationTextureRHI();
    Params->ScaleOpacityTexture = Proxy->GetScaleOpacityTextureRHI();
    Params->transforms = Proxy->GetTransformsUAV();

    return FComputeShaderUtils::AddPass(
        GraphBuilder,
        RDG_EVENT_NAME("CSGaussian: Transforms %s", *Proxy->GetName()),
        ERDGPassFlags::AsyncCompute,
        Shader,
        Params,
        FIntVector(NumThreadGroups(Proxy->GetNumSplats()), 1, 1));
}

FRDGPassRef SortSplats(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    FCSGaussianSceneProxy* Proxy,
    FRDGBufferRef Indices,
    FRDGBufferRef Distances)
{
    check(Proxy);

    uint32 NumSplats = Proxy->GetNumSplats();

    FRDGBufferDesc IndexDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumSplats);
    FRDGBuffer* Indices2 = GraphBuilder.CreateBuffer(IndexDesc, TEXT("Indices2"));

    FRDGBufferDesc DistanceDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint16), NumSplats);
    FRDGBuffer* Distances2 = GraphBuilder.CreateBuffer(DistanceDesc, TEXT("Distances2"));

    // RDG producer pass (tells RDG these buffers are used)
    FGPUSortProducerParameters* SetupParams =
        GraphBuilder.AllocParameters<FGPUSortProducerParameters>();
    SetupParams->IndicesUAV = GraphBuilder.CreateUAV(Indices, PF_R32_UINT);
    SetupParams->Indices2UAV = GraphBuilder.CreateUAV(Indices2, PF_R32_UINT);
    SetupParams->Distances2UAV = GraphBuilder.CreateUAV(Distances2, PF_R16_UINT);

    GraphBuilder.AddPass(
        RDG_EVENT_NAME("CSGaussian: RDG Producer"),
        SetupParams,
        ERDGPassFlags::Compute,
        [](FRHIComputeCommandList& RHICmdList) {});

    // Sort pass
    FGPUSortParameters* SortParams =
        GraphBuilder.AllocParameters<FGPUSortParameters>();
    SortParams->IndicesSRV = GraphBuilder.CreateSRV(Indices, PF_R32_UINT);
    SortParams->IndicesUAV = GraphBuilder.CreateUAV(Indices, PF_R32_UINT);
    SortParams->Indices2SRV = GraphBuilder.CreateSRV(Indices2, PF_R32_UINT);
    SortParams->Indices2UAV = GraphBuilder.CreateUAV(Indices2, PF_R32_UINT);
    SortParams->DistancesSRV = GraphBuilder.CreateSRV(Distances, PF_R16_UINT);
    SortParams->DistancesUAV = GraphBuilder.CreateUAV(Distances, PF_R16_UINT);
    SortParams->Distances2SRV = GraphBuilder.CreateSRV(Distances2, PF_R16_UINT);
    SortParams->Distances2UAV = GraphBuilder.CreateUAV(Distances2, PF_R16_UINT);

    return GraphBuilder.AddPass(
        RDG_EVENT_NAME("CSGaussian: Sort %s", *Proxy->GetName()),
        SortParams,
        ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
        [NumSplats, SortParams,
         SRV = Proxy->GetIndicesSRV(),
         UAV = Proxy->GetIndicesUAV()](FRHIComputeCommandList& RHICmdList)
        {
            FGPUSortBuffers SortBuffers;
            SortBuffers.RemoteKeySRVs[0] = SortParams->DistancesSRV->GetRHI();
            SortBuffers.RemoteKeySRVs[1] = SortParams->Distances2SRV->GetRHI();
            SortBuffers.RemoteKeyUAVs[0] = SortParams->DistancesUAV->GetRHI();
            SortBuffers.RemoteKeyUAVs[1] = SortParams->Distances2UAV->GetRHI();
            SortBuffers.RemoteValueSRVs[0] = SRV;
            SortBuffers.RemoteValueSRVs[1] = SortParams->Indices2SRV->GetRHI();
            SortBuffers.RemoteValueUAVs[0] = UAV;
            SortBuffers.RemoteValueUAVs[1] = SortParams->Indices2UAV->GetRHI();

            int32 ResultIndex = SortGPUBuffers(
                static_cast<FRHICommandList&>(RHICmdList),
                SortBuffers,
                0,
                DepthMask,
                NumSplats,
                GMaxRHIFeatureLevel);
            check(ResultIndex == 0);
        });
}

void RenderSplats(
    FRHICommandList& RHICmdList,
    FCSRenderSplatDeps* SplatParameters,
    uint32 NumSplats,
    const FSceneView& View)
{
    check(SplatParameters);

    const FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
    TShaderRef<FCSRenderSplatVS> VertexShader =
        GlobalShaderMap->GetShader<FCSRenderSplatVS>();
    TShaderRef<FCSRenderSplatPS> PixelShader =
        GlobalShaderMap->GetShader<FCSRenderSplatPS>();

    check(View.bIsViewInfo);
    const FIntRect ViewRect = static_cast<const FViewInfo&>(View).ViewRect;
    RHICmdList.SetViewport(
        float(ViewRect.Min.X), float(ViewRect.Min.Y), 0.f,
        float(ViewRect.Max.X), float(ViewRect.Max.Y), 1.f);

    FGraphicsPipelineStateInitializer GraphicsPSOInit;
    GraphicsPSOInit.PrimitiveType = PT_TriangleList;
    GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI =
        PipelineStateCache::GetOrCreateVertexDeclaration({});
    GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
    GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
    GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false>::GetRHI();
    GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
    GraphicsPSOInit.BlendState = TStaticBlendState<
        CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha>::GetRHI();
    RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

    SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
    SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), SplatParameters->VS);
    SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), SplatParameters->PS);

    RHICmdList.DrawPrimitive(0, 2 * NumSplats, 1);
}

} // namespace CSGaussian
```

### 5.4: `Private/Rendering/CSGaussianViewExtension.h`

```cpp
#pragma once

#include "Containers/Set.h"
#include "SceneViewExtension.h"
#include "CSGaussianSceneProxy.h"

namespace CSGaussian
{

class FCSGaussianViewExtension final : public FSceneViewExtensionBase
{
public:
    FCSGaussianViewExtension(const FAutoRegister& AutoRegister);

    virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
    virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
    virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

    virtual void PreRenderView_RenderThread(
        FRDGBuilder& GraphBuilder, FSceneView& InView) override;

    virtual void PrePostProcessPass_RenderThread(
        FRDGBuilder& GraphBuilder,
        const FSceneView& View,
        const FPostProcessingInputs& Inputs) override;

    void RegisterProxy_RenderThread(FCSGaussianSceneProxy* Proxy)
    {
        check(IsInRenderingThread());
        Proxies.Add(Proxy);
    }

    void UnregisterProxy_RenderThread(FCSGaussianSceneProxy* Proxy)
    {
        check(IsInRenderingThread());
        Proxies.Remove(Proxy);
    }

private:
    TSet<FCSGaussianSceneProxy*> Proxies;
};

} // namespace CSGaussian
```

### 5.5: `Private/Rendering/CSGaussianViewExtension.cpp`

```cpp
#include "CSGaussianViewExtension.h"
#include "CSGaussianRendering.h"
#include "CSGaussianRenderingUtilities.h"
#include "PostProcess/PostProcessing.h"
#include "StereoRendering.h"

namespace CSGaussian
{

FCSGaussianViewExtension::FCSGaussianViewExtension(
    const FAutoRegister& AutoRegister)
    : FSceneViewExtensionBase(AutoRegister)
{
    FSceneViewExtensionIsActiveFunctor IsActiveFunctor;
    IsActiveFunctor.IsActiveFunction =
        [](const ISceneViewExtension* Extension,
           const FSceneViewExtensionContext& Context)
    {
        const FCSGaussianViewExtension* Self =
            static_cast<const FCSGaussianViewExtension*>(Extension);
        return TOptional<bool>(Self->Proxies.Num() > 0);
    };
    IsActiveThisFrameFunctions.Add(IsActiveFunctor);
}

void FCSGaussianViewExtension::PreRenderView_RenderThread(
    FRDGBuilder& GraphBuilder, FSceneView& View)
{
    if (IStereoRendering::IsASecondaryView(View))
        return;

    for (auto& Proxy : Proxies)
    {
        if (!Proxy || !Proxy->IsVisible(View))
            continue;

        uint32 NumSplats = Proxy->GetNumSplats();

        // Compute transforms (projection to 2D)
        ComputeTransforms(GraphBuilder, View, Proxy);

        // GPU sort pipeline
        FRDGBufferDesc IndexDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumSplats);
        Proxy->GetIndicesFake() = GraphBuilder.CreateBuffer(IndexDesc, TEXT("Indices"));

        FRDGBufferDesc DistanceDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint16), NumSplats);
        Proxy->GetDistancesFake() = GraphBuilder.CreateBuffer(DistanceDesc, TEXT("Distances"));

        CalculateDistances(GraphBuilder, View, Proxy,
            Proxy->GetIndicesFake(), Proxy->GetDistancesFake());

        SortSplats(GraphBuilder, View, Proxy,
            Proxy->GetIndicesFake(), Proxy->GetDistancesFake());
    }
}

void FCSGaussianViewExtension::PrePostProcessPass_RenderThread(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FPostProcessingInputs& Inputs)
{
    for (auto& Proxy : Proxies)
    {
        if (!Proxy || !Proxy->IsVisible(View))
            continue;

        FCSRenderSplatSharedParameters Shared;
        Shared.View = View.ViewUniformBuffer;
        Shared.InstancedView = View.GetInstancedViewUniformBuffer();
        Shared.local_to_world = FMatrix44f(Proxy->GetLocalToWorld());
        Shared.texture_width = Proxy->GetTextureWidth();
        Shared.PositionTexture = Proxy->GetPositionTextureRHI();
        Shared.SH0Texture = Proxy->GetSH0TextureRHI();
        Shared.ScaleOpacityTexture = Proxy->GetScaleOpacityTextureRHI();
        Shared.computed_transforms = Proxy->GetTransformsSRV();
        Shared.sorted_indices = Proxy->GetIndicesSRV();

        FCSRenderSplatPS::FParameters ParamsPS;
        check(Inputs.SceneTextures);
        ParamsPS.RenderTargets[0] = FRenderTargetBinding(
            (*Inputs.SceneTextures)->SceneColorTexture,
            ERenderTargetLoadAction::ELoad);
        ParamsPS.RenderTargets.DepthStencil = FDepthStencilBinding(
            (*Inputs.SceneTextures)->SceneDepthTexture,
            ERenderTargetLoadAction::ELoad,
            FExclusiveDepthStencil::DepthWrite_StencilNop);

        FCSRenderSplatDeps* PassParams =
            GraphBuilder.AllocParameters<FCSRenderSplatDeps>();
        PassParams->Indices =
            GraphBuilder.CreateSRV(Proxy->GetIndicesFake(), PF_R32_UINT);
        PassParams->VS.Shared = Shared;
        PassParams->PS = ParamsPS;

        GraphBuilder.AddPass(
            RDG_EVENT_NAME("CSGaussian: Render %s", *Proxy->GetName()),
            PassParams,
            ERDGPassFlags::Raster,
            [PassParams, Proxy, &View](FRHICommandList& RHICmdList)
            {
                RenderSplats(RHICmdList, PassParams, Proxy->GetNumSplats(), View);
            });
    }
}

} // namespace CSGaussian
```

### Step: Write files, compile

---

## Task 6: Component + Actor + Subsystem

### 6.1: `Private/CSGaussianSubsystem.h`

```cpp
#pragma once

#include "Rendering/CSGaussianSceneProxy.h"
#include "Rendering/CSGaussianViewExtension.h"
#include "Subsystems/EngineSubsystem.h"

#include "CSGaussianSubsystem.generated.h"

UCLASS()
class UCSGaussianSubsystem final : public UEngineSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override
    {
        Extension = FSceneViewExtensions::NewExtension<
            CSGaussian::FCSGaussianViewExtension>();
    }

    void RegisterProxy_RenderThread(CSGaussian::FCSGaussianSceneProxy* Proxy)
    {
        check(Extension);
        Extension->RegisterProxy_RenderThread(Proxy);
    }

    void UnregisterProxy_RenderThread(CSGaussian::FCSGaussianSceneProxy* Proxy)
    {
        check(Extension);
        Extension->UnregisterProxy_RenderThread(Proxy);
    }

private:
    TSharedPtr<CSGaussian::FCSGaussianViewExtension, ESPMode::ThreadSafe> Extension;
};
```

### 6.2: `Public/CSGaussianComponent.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Texture2D.h"
#include "CSGaussianComponent.generated.h"

UCLASS(ClassGroup=(Rendering), meta=(BlueprintSpawnableComponent, DisplayName="CS Gaussian Renderer"))
class CSGAUSSIANRENDERER_API UCSGaussianComponent : public UPrimitiveComponent
{
    GENERATED_BODY()

public:
    UCSGaussianComponent();

    // --- Textures (set in editor or programmatically) ---

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Data")
    UTexture2D* PositionTexture = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Data")
    UTexture2D* RotationTexture = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Data")
    UTexture2D* ScaleOpacityTexture = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Data")
    UTexture2D* SH0Texture = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Data", meta = (ClampMin = "0"))
    int32 GaussianCount = 0;

    // --- Blueprint Functions ---

    UFUNCTION(BlueprintCallable, Category = "Gaussian Data")
    void SetGaussianTextures(
        UTexture2D* InPosition,
        UTexture2D* InRotation,
        UTexture2D* InScaleOpacity,
        UTexture2D* InSH0,
        int32 InGaussianCount);

    UFUNCTION(BlueprintCallable, Category = "Gaussian Data")
    int32 GetGaussianCount() const { return GaussianCount; }

    uint32 GetTextureWidth() const;

    //~ Begin UPrimitiveComponent Interface
    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
    //~ End UPrimitiveComponent Interface

private:
    void UpdateProxyTextures();
};
```

### 6.3: `Private/CSGaussianComponent.cpp`

```cpp
#include "CSGaussianComponent.h"
#include "Rendering/CSGaussianSceneProxy.h"

UCSGaussianComponent::UCSGaussianComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    bCastDynamicShadow = false;
    bCastStaticShadow = false;
}

FPrimitiveSceneProxy* UCSGaussianComponent::CreateSceneProxy()
{
    if (!PositionTexture || !RotationTexture || !ScaleOpacityTexture || GaussianCount <= 0)
        return nullptr;

    return new CSGaussian::FCSGaussianSceneProxy(*this);
}

FBoxSphereBounds UCSGaussianComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    // Large default bounds (gaussians can be anywhere in the texture)
    return FBoxSphereBounds(FVector::ZeroVector, FVector(100000.f), 100000.f).TransformBy(LocalToWorld);
}

uint32 UCSGaussianComponent::GetTextureWidth() const
{
    if (PositionTexture)
        return PositionTexture->GetSizeX();
    return 0;
}

void UCSGaussianComponent::SetGaussianTextures(
    UTexture2D* InPosition,
    UTexture2D* InRotation,
    UTexture2D* InScaleOpacity,
    UTexture2D* InSH0,
    int32 InGaussianCount)
{
    PositionTexture = InPosition;
    RotationTexture = InRotation;
    ScaleOpacityTexture = InScaleOpacity;
    SH0Texture = InSH0;
    GaussianCount = InGaussianCount;

    UpdateProxyTextures();
}

void UCSGaussianComponent::UpdateProxyTextures()
{
    if (!SceneProxy)
        return;

    auto GetTextureRHI = [](UTexture2D* Tex) -> FTextureRHIRef
    {
        if (Tex && Tex->GetResource())
            return Tex->GetResource()->TextureRHI;
        return nullptr;
    };

    FTextureRHIRef PosRHI = GetTextureRHI(PositionTexture);
    FTextureRHIRef RotRHI = GetTextureRHI(RotationTexture);
    FTextureRHIRef ScaleRHI = GetTextureRHI(ScaleOpacityTexture);
    FTextureRHIRef SH0RHI = GetTextureRHI(SH0Texture);
    uint32 Count = (uint32)FMath::Max(GaussianCount, 0);
    uint32 TexWidth = GetTextureWidth();

    CSGaussian::FCSGaussianSceneProxy* Proxy =
        static_cast<CSGaussian::FCSGaussianSceneProxy*>(SceneProxy);

    ENQUEUE_RENDER_COMMAND(UpdateCSGaussianTextures)(
        [Proxy, PosRHI, RotRHI, ScaleRHI, SH0RHI, Count, TexWidth]
        (FRHICommandListImmediate& RHICmdList)
        {
            Proxy->UpdateTextures_RenderThread(
                PosRHI, RotRHI, ScaleRHI, SH0RHI, Count, TexWidth);
        });
}
```

### 6.4: `Public/CSGaussianActor.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSGaussianComponent.h"
#include "CSGaussianActor.generated.h"

UCLASS(ComponentWrapperClass, meta = (DisplayName = "CS Gaussian Actor"))
class CSGAUSSIANRENDERER_API ACSGaussianActor : public AActor
{
    GENERATED_BODY()

public:
    ACSGaussianActor()
    {
        GaussianComponent = CreateDefaultSubobject<UCSGaussianComponent>(TEXT("GaussianComponent"));
        RootComponent = GaussianComponent;
    }

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UCSGaussianComponent> GaussianComponent;
};
```

### 6.5: `Private/CSGaussianActor.cpp`

```cpp
#include "CSGaussianActor.h"
```

### Step: Write files, compile

---

## Task 7: Sequence Player

### 7.1: `Public/CSGaussianSequencePlayer.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/Texture2D.h"
#include "CSGaussianSequencePlayer.generated.h"

class UCSGaussianComponent;

USTRUCT(BlueprintType)
struct FCSSequenceFrameMeta
{
    GENERATED_BODY()

    int32 FrameIndex = 0;
    int32 TextureWidth = 0;
    int32 TextureHeight = 0;
    int32 GaussianCount = 0;
    FString FrameFolderPath;

    // Precision flags (0 = Full 32-bit, 1 = Half 16-bit)
    int32 PositionPrecision = 0;
    int32 RotationPrecision = 1;
    int32 ScaleOpacityPrecision = 1;
    int32 SHPrecision = 1;
};

USTRUCT(BlueprintType)
struct FCSSequenceMeta
{
    GENERATED_BODY()

    FString SequenceName;
    int32 FrameCount = 0;
    float TargetFPS = 30.0f;
    int32 SHDegree = 0;
    FString SequenceFolderPath;
    TArray<FString> FrameFolders;
};

UENUM(BlueprintType)
enum class ECSPlaybackMode : uint8
{
    Once,
    Loop,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCSSequenceComplete);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCSFrameChanged, int32, NewFrame);

UCLASS(ClassGroup=(Rendering), meta=(BlueprintSpawnableComponent, DisplayName="CS Gaussian Sequence Player"))
class CSGAUSSIANRENDERER_API UCSGaussianSequencePlayer : public UActorComponent
{
    GENERATED_BODY()

public:
    UCSGaussianSequencePlayer();

    // --- Properties ---

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sequence", meta = (FilePathFilter = "json"))
    FFilePath SequencePath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playback")
    float PlaybackSpeed = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playback")
    ECSPlaybackMode PlaybackMode = ECSPlaybackMode::Loop;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playback")
    bool bAutoPlay = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    bool bPreloadToRAM = false;

    // --- Events ---

    UPROPERTY(BlueprintAssignable, Category = "Events")
    FOnCSSequenceComplete OnSequenceComplete;

    UPROPERTY(BlueprintAssignable, Category = "Events")
    FOnCSFrameChanged OnFrameChanged;

    // --- Blueprint Functions ---

    UFUNCTION(BlueprintCallable, Category = "Setup")
    bool LoadSequence(const FString& InSequencePath);

    UFUNCTION(BlueprintCallable, Category = "Playback")
    void Play();

    UFUNCTION(BlueprintCallable, Category = "Playback")
    void Pause();

    UFUNCTION(BlueprintCallable, Category = "Playback")
    void Stop();

    UFUNCTION(BlueprintCallable, Category = "Playback")
    void GoToFrame(int32 Frame);

    UFUNCTION(BlueprintCallable, Category = "Playback")
    bool IsPlaying() const { return bIsPlaying; }

    UFUNCTION(BlueprintCallable, Category = "Playback")
    int32 GetCurrentFrame() const { return CurrentFrameIndex; }

    UFUNCTION(BlueprintCallable, Category = "Info")
    int32 GetFrameCount() const { return SequenceMetadata.FrameCount; }

    UFUNCTION(BlueprintCallable, Category = "Info")
    bool IsSequenceLoaded() const { return bSequenceLoaded; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    UCSGaussianComponent* FindGaussianComponent() const;
    bool LoadFrameMetadata(int32 FrameIndex, FCSSequenceFrameMeta& OutMeta);
    bool LoadFrameTextures(int32 FrameIndex);
    UTexture2D* LoadTextureFromDisk(const FString& FilePath, int32 Width, int32 Height, int32 Precision);
    void ApplyFrameToComponent(int32 FrameIndex);

    // Preloaded frame data (RAM)
    struct FPreloadedFrame
    {
        TArray<uint8> PositionData;
        TArray<uint8> RotationData;
        TArray<uint8> ScaleOpacityData;
        TArray<uint8> SH0Data;
        int32 TextureWidth = 0;
        int32 TextureHeight = 0;
        int32 GaussianCount = 0;
        int32 PositionPrecision = 0;
        int32 RotationPrecision = 1;
        int32 ScaleOpacityPrecision = 1;
        int32 SHPrecision = 1;
        bool bLoaded = false;
    };
    UTexture2D* CreateTextureFromRAM(const TArray<uint8>& Data, int32 Width, int32 Height, int32 Precision);
    bool PreloadAllFramesToRAM();

    FCSSequenceMeta SequenceMetadata;
    TMap<int32, FCSSequenceFrameMeta> FrameMetaCache;
    TArray<FPreloadedFrame> PreloadedFrames;
    bool bSequenceLoaded = false;

    // Current frame textures (kept alive to prevent GC)
    UPROPERTY()
    UTexture2D* CurrentPositionTex = nullptr;
    UPROPERTY()
    UTexture2D* CurrentRotationTex = nullptr;
    UPROPERTY()
    UTexture2D* CurrentScaleOpacityTex = nullptr;
    UPROPERTY()
    UTexture2D* CurrentSH0Tex = nullptr;

    // Playback state
    bool bIsPlaying = false;
    float FrameAccumulator = 0.0f;
    int32 CurrentFrameIndex = 0;
    int32 LastDisplayedFrame = -1;
};
```

### 7.2: `Private/CSGaussianSequencePlayer.cpp`

```cpp
#include "CSGaussianSequencePlayer.h"
#include "CSGaussianComponent.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

UCSGaussianSequencePlayer::UCSGaussianSequencePlayer()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UCSGaussianSequencePlayer::BeginPlay()
{
    Super::BeginPlay();

    if (!SequencePath.FilePath.IsEmpty() && !bSequenceLoaded)
    {
        LoadSequence(SequencePath.FilePath);
    }

    if (bAutoPlay && bSequenceLoaded)
    {
        Play();
    }
}

void UCSGaussianSequencePlayer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Stop();
    PreloadedFrames.Empty();
    Super::EndPlay(EndPlayReason);
}

void UCSGaussianSequencePlayer::TickComponent(
    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bIsPlaying || !bSequenceLoaded || SequenceMetadata.FrameCount <= 0)
        return;

    FrameAccumulator += DeltaTime * PlaybackSpeed;
    float FrameDuration = 1.0f / SequenceMetadata.TargetFPS;

    while (FrameAccumulator >= FrameDuration)
    {
        FrameAccumulator -= FrameDuration;
        CurrentFrameIndex++;

        if (CurrentFrameIndex >= SequenceMetadata.FrameCount)
        {
            if (PlaybackMode == ECSPlaybackMode::Loop)
            {
                CurrentFrameIndex = 0;
            }
            else
            {
                CurrentFrameIndex = SequenceMetadata.FrameCount - 1;
                bIsPlaying = false;
                OnSequenceComplete.Broadcast();
                return;
            }
        }
    }

    if (CurrentFrameIndex != LastDisplayedFrame)
    {
        if (LoadFrameTextures(CurrentFrameIndex))
        {
            ApplyFrameToComponent(CurrentFrameIndex);
            LastDisplayedFrame = CurrentFrameIndex;
            OnFrameChanged.Broadcast(CurrentFrameIndex);
        }
    }
}

bool UCSGaussianSequencePlayer::LoadSequence(const FString& InSequencePath)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *InSequencePath))
    {
        UE_LOG(LogTemp, Error, TEXT("CSGaussian: Failed to load sequence: %s"), *InSequencePath);
        return false;
    }

    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, JsonObject))
    {
        UE_LOG(LogTemp, Error, TEXT("CSGaussian: Failed to parse sequence JSON"));
        return false;
    }

    SequenceMetadata.SequenceName = JsonObject->GetStringField(TEXT("sequenceName"));
    SequenceMetadata.FrameCount = JsonObject->GetIntegerField(TEXT("frameCount"));
    SequenceMetadata.TargetFPS = (float)JsonObject->GetNumberField(TEXT("targetFPS"));
    SequenceMetadata.SHDegree = JsonObject->GetIntegerField(TEXT("shDegree"));
    SequenceMetadata.SequenceFolderPath = FPaths::GetPath(InSequencePath);

    const TArray<TSharedPtr<FJsonValue>>& FrameFoldersArray =
        JsonObject->GetArrayField(TEXT("frameFolders"));
    for (const auto& Value : FrameFoldersArray)
    {
        SequenceMetadata.FrameFolders.Add(Value->AsString());
    }

    bSequenceLoaded = true;

    if (bPreloadToRAM)
    {
        PreloadAllFramesToRAM();
    }

    UE_LOG(LogTemp, Log, TEXT("CSGaussian: Loaded sequence '%s' with %d frames at %g FPS"),
        *SequenceMetadata.SequenceName,
        SequenceMetadata.FrameCount,
        SequenceMetadata.TargetFPS);

    return true;
}

void UCSGaussianSequencePlayer::Play()
{
    if (bSequenceLoaded)
    {
        bIsPlaying = true;
        SetComponentTickEnabled(true);
    }
}

void UCSGaussianSequencePlayer::Pause()
{
    bIsPlaying = false;
}

void UCSGaussianSequencePlayer::Stop()
{
    bIsPlaying = false;
    CurrentFrameIndex = 0;
    LastDisplayedFrame = -1;
    FrameAccumulator = 0.0f;
}

void UCSGaussianSequencePlayer::GoToFrame(int32 Frame)
{
    if (!bSequenceLoaded) return;
    CurrentFrameIndex = FMath::Clamp(Frame, 0, SequenceMetadata.FrameCount - 1);
    if (LoadFrameTextures(CurrentFrameIndex))
    {
        ApplyFrameToComponent(CurrentFrameIndex);
        LastDisplayedFrame = CurrentFrameIndex;
    }
}

UCSGaussianComponent* UCSGaussianSequencePlayer::FindGaussianComponent() const
{
    if (AActor* Owner = GetOwner())
    {
        return Owner->FindComponentByClass<UCSGaussianComponent>();
    }
    return nullptr;
}

bool UCSGaussianSequencePlayer::LoadFrameMetadata(int32 FrameIndex, FCSSequenceFrameMeta& OutMeta)
{
    if (FCSSequenceFrameMeta* Cached = FrameMetaCache.Find(FrameIndex))
    {
        OutMeta = *Cached;
        return true;
    }

    FString FrameFolder = FPaths::Combine(
        SequenceMetadata.SequenceFolderPath,
        SequenceMetadata.FrameFolders[FrameIndex]);

    FString MetaPath = FPaths::Combine(FrameFolder, TEXT("metadata.json"));
    FString MetaJson;
    if (!FFileHelper::LoadFileToString(MetaJson, *MetaPath))
        return false;

    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(MetaJson);
    if (!FJsonSerializer::Deserialize(Reader, Json))
        return false;

    OutMeta.FrameIndex = FrameIndex;
    OutMeta.TextureWidth = Json->GetIntegerField(TEXT("textureWidth"));
    OutMeta.TextureHeight = Json->GetIntegerField(TEXT("textureHeight"));
    OutMeta.GaussianCount = Json->GetIntegerField(TEXT("gaussianCount"));
    OutMeta.FrameFolderPath = FrameFolder;
    OutMeta.PositionPrecision = Json->GetIntegerField(TEXT("positionPrecision"));
    OutMeta.RotationPrecision = Json->GetIntegerField(TEXT("rotationPrecision"));
    OutMeta.ScaleOpacityPrecision = Json->GetIntegerField(TEXT("scaleOpacityPrecision"));
    OutMeta.SHPrecision = Json->GetIntegerField(TEXT("shPrecision"));

    FrameMetaCache.Add(FrameIndex, OutMeta);
    return true;
}

bool UCSGaussianSequencePlayer::LoadFrameTextures(int32 FrameIndex)
{
    if (bPreloadToRAM && PreloadedFrames.IsValidIndex(FrameIndex) && PreloadedFrames[FrameIndex].bLoaded)
    {
        const FPreloadedFrame& Frame = PreloadedFrames[FrameIndex];
        CurrentPositionTex = CreateTextureFromRAM(Frame.PositionData, Frame.TextureWidth, Frame.TextureHeight, Frame.PositionPrecision);
        CurrentRotationTex = CreateTextureFromRAM(Frame.RotationData, Frame.TextureWidth, Frame.TextureHeight, Frame.RotationPrecision);
        CurrentScaleOpacityTex = CreateTextureFromRAM(Frame.ScaleOpacityData, Frame.TextureWidth, Frame.TextureHeight, Frame.ScaleOpacityPrecision);
        CurrentSH0Tex = CreateTextureFromRAM(Frame.SH0Data, Frame.TextureWidth, Frame.TextureHeight, Frame.SHPrecision);
        return CurrentPositionTex && CurrentRotationTex && CurrentScaleOpacityTex;
    }

    FCSSequenceFrameMeta Meta;
    if (!LoadFrameMetadata(FrameIndex, Meta))
        return false;

    CurrentPositionTex = LoadTextureFromDisk(
        FPaths::Combine(Meta.FrameFolderPath, TEXT("position.bin")),
        Meta.TextureWidth, Meta.TextureHeight, Meta.PositionPrecision);
    CurrentRotationTex = LoadTextureFromDisk(
        FPaths::Combine(Meta.FrameFolderPath, TEXT("rotation.bin")),
        Meta.TextureWidth, Meta.TextureHeight, Meta.RotationPrecision);
    CurrentScaleOpacityTex = LoadTextureFromDisk(
        FPaths::Combine(Meta.FrameFolderPath, TEXT("scaleOpacity.bin")),
        Meta.TextureWidth, Meta.TextureHeight, Meta.ScaleOpacityPrecision);
    CurrentSH0Tex = LoadTextureFromDisk(
        FPaths::Combine(Meta.FrameFolderPath, TEXT("sh_0.bin")),
        Meta.TextureWidth, Meta.TextureHeight, Meta.SHPrecision);

    return CurrentPositionTex && CurrentRotationTex && CurrentScaleOpacityTex;
}

UTexture2D* UCSGaussianSequencePlayer::LoadTextureFromDisk(
    const FString& FilePath, int32 Width, int32 Height, int32 Precision)
{
    TArray<uint8> RawData;
    if (!FFileHelper::LoadFileToArray(RawData, *FilePath))
        return nullptr;

    return CreateTextureFromRAM(RawData, Width, Height, Precision);
}

UTexture2D* UCSGaussianSequencePlayer::CreateTextureFromRAM(
    const TArray<uint8>& Data, int32 Width, int32 Height, int32 Precision)
{
    if (Data.Num() == 0 || Width <= 0 || Height <= 0)
        return nullptr;

    EPixelFormat Format = (Precision == 0) ? PF_A32B32G32R32F : PF_FloatRGBA;
    int32 BytesPerPixel = (Precision == 0) ? 16 : 8;
    int32 ExpectedSize = Width * Height * BytesPerPixel;

    if (Data.Num() < ExpectedSize)
        return nullptr;

    UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, Format);
    if (!Texture)
        return nullptr;

    Texture->Filter = TF_Nearest;
    Texture->SRGB = false;
    Texture->NeverStream = true;

    void* MipData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(MipData, Data.GetData(), ExpectedSize);
    Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
    Texture->UpdateResource();

    return Texture;
}

void UCSGaussianSequencePlayer::ApplyFrameToComponent(int32 FrameIndex)
{
    UCSGaussianComponent* Comp = FindGaussianComponent();
    if (!Comp) return;

    FCSSequenceFrameMeta Meta;
    if (bPreloadToRAM && PreloadedFrames.IsValidIndex(FrameIndex))
    {
        const FPreloadedFrame& Frame = PreloadedFrames[FrameIndex];
        Comp->SetGaussianTextures(
            CurrentPositionTex, CurrentRotationTex,
            CurrentScaleOpacityTex, CurrentSH0Tex,
            Frame.GaussianCount);
    }
    else if (LoadFrameMetadata(FrameIndex, Meta))
    {
        Comp->SetGaussianTextures(
            CurrentPositionTex, CurrentRotationTex,
            CurrentScaleOpacityTex, CurrentSH0Tex,
            Meta.GaussianCount);
    }
}

bool UCSGaussianSequencePlayer::PreloadAllFramesToRAM()
{
    PreloadedFrames.SetNum(SequenceMetadata.FrameCount);

    for (int32 i = 0; i < SequenceMetadata.FrameCount; i++)
    {
        FCSSequenceFrameMeta Meta;
        if (!LoadFrameMetadata(i, Meta))
            continue;

        FPreloadedFrame& Frame = PreloadedFrames[i];
        Frame.TextureWidth = Meta.TextureWidth;
        Frame.TextureHeight = Meta.TextureHeight;
        Frame.GaussianCount = Meta.GaussianCount;
        Frame.PositionPrecision = Meta.PositionPrecision;
        Frame.RotationPrecision = Meta.RotationPrecision;
        Frame.ScaleOpacityPrecision = Meta.ScaleOpacityPrecision;
        Frame.SHPrecision = Meta.SHPrecision;

        FFileHelper::LoadFileToArray(Frame.PositionData,
            *FPaths::Combine(Meta.FrameFolderPath, TEXT("position.bin")));
        FFileHelper::LoadFileToArray(Frame.RotationData,
            *FPaths::Combine(Meta.FrameFolderPath, TEXT("rotation.bin")));
        FFileHelper::LoadFileToArray(Frame.ScaleOpacityData,
            *FPaths::Combine(Meta.FrameFolderPath, TEXT("scaleOpacity.bin")));
        FFileHelper::LoadFileToArray(Frame.SH0Data,
            *FPaths::Combine(Meta.FrameFolderPath, TEXT("sh_0.bin")));

        Frame.bLoaded = Frame.PositionData.Num() > 0;
    }

    UE_LOG(LogTemp, Log, TEXT("CSGaussian: Preloaded %d frames to RAM"), SequenceMetadata.FrameCount);
    return true;
}
```

### Step: Write files, compile

---

## Task 8: Build + Test

### Step 1: Update .uproject with plugin entry

### Step 2: Full compilation
```bash
# Windows - use RunUAT or UnrealBuildTool
"C:/Program Files/Epic Games/UE_5.6/Engine/Build/BatchFiles/RunUAT.bat" BuildPlugin -Plugin="D:/Unreal Projects/FourDGS - V2/Plugins/CSGaussianRenderer/CSGaussianRenderer.uplugin" -TargetPlatforms=Win64 -Package="D:/Temp/CSGaussianPlugin"
```

Alternative direct UBT:
```bash
"C:/Program Files/Epic Games/UE_5.6/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" FourDGS Win64 Development -Project="D:/Unreal Projects/FourDGS - V2/FourDGS.uproject" -TargetType=Editor
```

### Step 3: Fix any compilation errors

### Step 4: Verify shader compilation
Open editor, check Output Log for shader compilation errors. Shaders compile on first use.

### Step 5: Test static mode
1. Open editor
2. Place ACSGaussianActor in scene
3. In Details panel, assign textures from existing ThreeDGaussians import
4. Set GaussianCount
5. Verify rendering

### Step 6: Test sequence mode
1. Create actor with both UCSGaussianComponent and UCSGaussianSequencePlayer
2. Set SequencePath to existing sequence.json
3. Play in editor
4. Verify frame playback

---

## Known Risks / Watch Items

1. **SHADER_PARAMETER_TEXTURE in compute shaders**: If UE doesn't support this for compute, switch to SHADER_PARAMETER_SRV with manual SRV creation from FTextureRHI.

2. **Texture2D.Load() in vertex shader**: SM5 supports this, but if issues arise, pre-read all data in compute shader and store in a Buffer.

3. **Covariance computation precision**: Computing covariance from quat+scale is more math than PICOSplat's packed format. If artifacts appear, verify matrix math and consider adding `max()` guards for sqrt inputs.

4. **GPU sort buffer lifetime**: The fake RDG buffers need to persist across PreRenderView → PrePostProcessPass. This follows PICOSplat's proven pattern.

5. **Texture coordinate swizzle**: The exact unswizzle pattern (R=Z, G=X, B=-Y) must match what ThreeDGaussians and GaussianStreamer write. If colors look wrong, double-check the swizzle in CSGaussianCommon.ush.
