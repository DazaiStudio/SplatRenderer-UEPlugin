# Compute Shader Gaussian Renderer - Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the Niagara-based 3DGS rendering with a Compute Shader pipeline (referencing PICOSplat architecture), supporting both static PLY import and raw .bin sequence playback.

**Architecture:** New UE plugin "GaussianRenderer" with a custom SceneViewExtension rendering pipeline. Compute shaders handle depth calculation + sorting, transform projection, and quad splatting. A SplatComponent serves static scenes; a SplatSequenceComponent serves .bin sequence playback. Both share the same GPU rendering path via buffer swap.

**Tech Stack:** UE 5.6, C++, HLSL (USF shaders), RHI/RDG, FSceneViewExtension, Structured Buffers, GPU RadixSort (UE built-in)

**Reference code:** `ref/PICOSpla97bc03ea1f55V5/` (PICOSplat plugin - architecture reference, NOT copy-paste)

**Scope:** Mode 1 (static PLY) + Mode 2 (raw .bin sequence). No .gsv compressed stream in this phase.

**SH Support:** SH0 only for initial implementation (user's data is SH0). Higher SH degrees can be added later.

---

## Plugin Structure Overview

```
GaussianRenderer/
  GaussianRenderer.uplugin
  Shaders/
    Private/
      GaussianCommon.ush          — shared constants, unpacking helpers
      ComputeDepthCS.usf          — per-splat depth calc + frustum cull
      ComputeTransformCS.usf      — 3D covariance → 2D projected transform
      GaussianSplatVS.usf         — vertex shader (quad generation)
      GaussianSplatPS.usf         — pixel shader (gaussian alpha)
  Source/
    GaussianRendererRuntime/
      GaussianRendererRuntime.Build.cs
      Public/
        GaussianRendererModule.h
        GaussianData.h             — packed data types (position, cov, color)
        GaussianAsset.h            — UGaussianAsset (imported static data)
        GaussianComponent.h        — UGaussianComponent (static rendering)
        GaussianSequenceComponent.h — UGaussianSequenceComponent (.bin playback)
        GaussianBuffers.h          — GPU buffer wrappers (CPU→GPU, GPU→GPU)
        GaussianSettings.h         — project settings (sort method, etc.)
      Private/
        GaussianRendererModule.cpp
        GaussianData.cpp
        GaussianAsset.cpp
        GaussianComponent.cpp
        GaussianSequenceComponent.cpp
        GaussianBuffers.cpp
        Rendering/
          GaussianSceneProxy.h
          GaussianSceneProxy.cpp
          GaussianViewExtension.h
          GaussianViewExtension.cpp
          GaussianShaders.h        — shader parameter structs
          GaussianShaders.cpp      — shader compilation
          GaussianRendering.h      — dispatch helpers
          GaussianRendering.cpp    — compute + draw calls
    GaussianRendererEditor/
      GaussianRendererEditor.Build.cs
      Public/
        GaussianRendererEditorModule.h
        GaussianAssetFactory.h     — PLY import factory
      Private/
        GaussianRendererEditorModule.cpp
        GaussianAssetFactory.cpp
```

---

## Task 1: Plugin Skeleton + Build Files

**Files:**
- Create: `GaussianRenderer/GaussianRenderer.uplugin`
- Create: `GaussianRenderer/Source/GaussianRendererRuntime/GaussianRendererRuntime.Build.cs`
- Create: `GaussianRenderer/Source/GaussianRendererRuntime/Public/GaussianRendererModule.h`
- Create: `GaussianRenderer/Source/GaussianRendererRuntime/Private/GaussianRendererModule.cpp`
- Create: `GaussianRenderer/Source/GaussianRendererEditor/GaussianRendererEditor.Build.cs`
- Create: `GaussianRenderer/Source/GaussianRendererEditor/Public/GaussianRendererEditorModule.h`
- Create: `GaussianRenderer/Source/GaussianRendererEditor/Private/GaussianRendererEditorModule.cpp`

**Step 1: Create .uplugin**

```json
{
  "FileVersion": 3,
  "Version": 1,
  "VersionName": "0.1",
  "FriendlyName": "Gaussian Renderer",
  "Description": "Compute shader based 3D Gaussian Splatting renderer",
  "Category": "Rendering",
  "CanContainContent": false,
  "Installed": true,
  "Modules": [
    {
      "Name": "GaussianRendererRuntime",
      "Type": "Runtime",
      "LoadingPhase": "PostConfigInit",
      "PlatformAllowList": ["Win64", "Linux"]
    },
    {
      "Name": "GaussianRendererEditor",
      "Type": "Editor",
      "LoadingPhase": "Default",
      "PlatformAllowList": ["Win64"]
    }
  ]
}
```

**Step 2: Create Runtime Build.cs**

Key dependencies: `Core`, `CoreUObject`, `Engine`, `RenderCore`, `RHI`, `Renderer`, `Projects`.
Must add shader source directory mapping in module StartupModule:
```csharp
// In Build.cs
PrivateDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine", "RenderCore", "RHI", "Renderer", "Projects"
});
// Access Renderer Private for FSceneViewExtension
string RendererPath = Path.Combine(ModuleDirectory, "..", "..", "..");
PrivateIncludePaths.Add(Path.Combine(GetModuleDirectory("Renderer"), "Private"));
```

**Step 3: Create Runtime Module**

StartupModule must register shader directory:
```cpp
void FGaussianRendererModule::StartupModule()
{
    FString ShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("GaussianRenderer"))->GetBaseDir(), TEXT("Shaders"));
    AddShaderSourceDirectoryMapping(TEXT("/GaussianRenderer"), ShaderDir);
}
```

**Step 4: Create Editor Build.cs**

Dependencies: `Core`, `CoreUObject`, `Engine`, `UnrealEd`, `AssetTools`, `GaussianRendererRuntime`.

**Step 5: Create Editor Module (empty shell)**

Basic IModuleInterface with empty StartupModule/ShutdownModule.

**Step 6: Verify compilation**

Run: Build the plugin in UE 5.6. Expected: compiles with zero errors.

**Step 7: Commit**

```
feat: add GaussianRenderer plugin skeleton with runtime + editor modules
```

---

## Task 2: Packed Data Types + GPU Buffer Wrappers

**Files:**
- Create: `GaussianRenderer/Source/GaussianRendererRuntime/Public/GaussianData.h`
- Create: `GaussianRenderer/Source/GaussianRendererRuntime/Private/GaussianData.cpp`
- Create: `GaussianRenderer/Source/GaussianRendererRuntime/Public/GaussianBuffers.h`
- Create: `GaussianRenderer/Source/GaussianRendererRuntime/Private/GaussianBuffers.cpp`

**Step 1: Define packed data types in GaussianData.h**

For SH0, we need per-gaussian:
- Position: `FVector3f` (12 bytes) — full precision, quantized later on GPU
- Covariance: computed from rotation (`FQuat4f`) + scale (`FVector3f`) — store as rotation(16B) + scale(12B) = 28 bytes, OR precompute 3x3 upper-triangle covariance (6 floats = 24 bytes)
- Color: `FColor` RGBA8 (4 bytes) — SH0 DC baked to RGB + opacity
- Total per splat: ~40-48 bytes (vs current 240 bytes)

Simplified approach for SH0 (store raw, let GPU do the math):
```cpp
// CPU-side data per gaussian (used during import, then uploaded to GPU)
struct FGaussianSplat
{
    FVector3f Position;    // world-space position (meters, UE convention)
    FVector3f Scale;       // activated scale (exp applied)
    FQuat4f Rotation;      // normalized quaternion
    FColor Color;          // SH0 baked to sRGB + opacity
};

// Sort helper
struct FGaussianSortEntry
{
    uint32 Index;
    uint32 Depth;  // quantized depth for sorting
};
```

**Step 2: Define GPU buffer wrappers in GaussianBuffers.h**

Reference: PICOSplat's `SplatBuffers.h` pattern. Three buffer types:
```cpp
// CPU writes, GPU reads (positions, colors — static data)
class FGaussianStaticBuffer
{
public:
    void Init(uint32 NumElements, uint32 Stride, EPixelFormat Format);
    void Upload(const void* Data, uint32 DataSize);
    void Release();
    FBufferRHIRef GetBuffer() const;
    FShaderResourceViewRHIRef GetSRV() const;
private:
    FBufferRHIRef Buffer;
    FShaderResourceViewRHIRef SRV;
};

// GPU writes + reads (transforms, sorted indices — per-frame intermediates)
class FGaussianDynamicBuffer
{
public:
    void Init(uint32 NumElements, uint32 Stride, EPixelFormat Format);
    void Release();
    FBufferRHIRef GetBuffer() const;
    FShaderResourceViewRHIRef GetSRV() const;
    FUnorderedAccessViewRHIRef GetUAV() const;
private:
    FBufferRHIRef Buffer;
    FShaderResourceViewRHIRef SRV;
    FUnorderedAccessViewRHIRef UAV;
};
```

**Step 3: Implement buffer Init/Upload/Release**

Use `FRHICommandListBase::CreateBuffer` with appropriate usage flags:
- Static: `BUF_ShaderResource | BUF_Static`
- Dynamic: `BUF_ShaderResource | BUF_UnorderedAccess | BUF_Static`

**Step 4: Commit**

```
feat: add packed gaussian data types and GPU buffer wrappers
```

---

## Task 3: GaussianAsset + PLY Import

**Files:**
- Create: `GaussianRenderer/Source/GaussianRendererRuntime/Public/GaussianAsset.h`
- Create: `GaussianRenderer/Source/GaussianRendererRuntime/Private/GaussianAsset.cpp`
- Create: `GaussianRenderer/Source/GaussianRendererEditor/Public/GaussianAssetFactory.h`
- Create: `GaussianRenderer/Source/GaussianRendererEditor/Private/GaussianAssetFactory.cpp`
- Reuse: `ThreeDGaussians/Source/ThreeDGaussians/Public/ThreeDGaussiansPlyReader.h` (copy into new plugin)

**Step 1: Define UGaussianAsset**

```cpp
UCLASS()
class GAUSSIANRENDERERRUNTIME_API UGaussianAsset : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY()
    int32 NumSplats = 0;

    // CPU-side data (kept for serialization, released after GPU upload in cooked builds)
    TArray<FVector3f> Positions;
    TArray<FVector3f> Scales;
    TArray<FQuat4f> Rotations;
    TArray<FColor> Colors;

    // Bounding box
    FVector3f BoundsMin;
    FVector3f BoundsMax;

    virtual void Serialize(FArchive& Ar) override;

    // Convert from PLY data (COLMAP convention) to UE convention
    static void ConvertFromPLY(
        const TArray<FThreeDGaussianPly>& PlyData,
        TArray<FVector3f>& OutPositions,
        TArray<FVector3f>& OutScales,
        TArray<FQuat4f>& OutRotations,
        TArray<FColor>& OutColors,
        FVector3f& OutMin, FVector3f& OutMax);
};
```

**Step 2: Implement ConvertFromPLY**

Key conversions (from existing ThreeDGaussiansLibrary.cpp):
- Position: COLMAP `(x,y,z)` → UE `(z, x, -y)` (meters, NOT * 100 — we handle scaling in component transform)
- Rotation: COLMAP `(w,x,y,z)` → UE `FQuat4f(z, x, -y, w)`, normalized
- Scale: COLMAP `log(s)` → UE `exp(s)`, axes reordered `(sz, sx, sy)`
- Color: SH0 DC → sRGB: `color = clamp((sh_dc * SH_C0 + 0.5) * 255, 0, 255)`
- Opacity: sigmoid `1/(1+exp(-opacity))` → uint8 `* 255`

**Step 3: Implement Serialize**

Standard FArchive serialization for Positions, Scales, Rotations, Colors arrays.

**Step 4: Create PLY import factory**

`UGaussianAssetFactory` extends `UFactory`:
- SupportedClass = `UGaussianAsset`
- Formats = `ply; Gaussian Splatting PLY`
- `FactoryCreateFile` → uses `FThreeDGaussiansPlyReader` to load PLY → `ConvertFromPLY` → populate asset

Copy `ThreeDGaussiansPlyReader.h` into the new plugin (it's a standalone header-only reader).

**Step 5: Verify import**

Drag a .ply file into UE Content Browser → should create a UGaussianAsset. Inspect property panel to verify NumSplats > 0.

**Step 6: Commit**

```
feat: add UGaussianAsset with PLY import factory
```

---

## Task 4: Shader Files (HLSL)

**Files:**
- Create: `GaussianRenderer/Shaders/Private/GaussianCommon.ush`
- Create: `GaussianRenderer/Shaders/Private/ComputeDepthCS.usf`
- Create: `GaussianRenderer/Shaders/Private/ComputeTransformCS.usf`
- Create: `GaussianRenderer/Shaders/Private/GaussianSplatVS.usf`
- Create: `GaussianRenderer/Shaders/Private/GaussianSplatPS.usf`

**Step 1: Create GaussianCommon.ush**

```hlsl
#pragma once

#define THREAD_GROUP_SIZE 64
#define GAUSSIAN_RADIUS_SIGMA 2.8284271247f  // sqrt(8)
#define CUTOFF_SQ_OVER_2 4.0f                // (sqrt(8))^2 / 2

// Reconstruct 3x3 covariance from quaternion + scale
float3x3 BuildCovariance(float4 quat, float3 scale)
{
    // R = rotation matrix from quaternion
    float r = quat.w, x = quat.x, y = quat.y, z = quat.z;
    float3x3 R = float3x3(
        1 - 2*(y*y + z*z), 2*(x*y - r*z),     2*(x*z + r*y),
        2*(x*y + r*z),     1 - 2*(x*x + z*z), 2*(y*z - r*x),
        2*(x*z - r*y),     2*(y*z + r*x),     1 - 2*(x*x + y*y)
    );
    // S = diag(scale)
    float3x3 RS = float3x3(
        R[0] * scale.x,
        R[1] * scale.y,
        R[2] * scale.z
    );
    // Cov = R * S * S^T * R^T = RS * RS^T
    return mul(RS, transpose(RS));
}
```

**Step 2: Create ComputeDepthCS.usf**

```hlsl
#include "/Engine/Private/Common.ush"
#include "/GaussianRenderer/Private/GaussianCommon.ush"

Buffer<float4> Positions;     // xyz + padding
float4x4 LocalToClip;
uint NumSplats;

RWBuffer<uint> OutIndices;
RWBuffer<uint> OutDepths;

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void MainCS(uint3 DTid : SV_DispatchThreadID)
{
    uint Idx = DTid.x;
    if (Idx >= NumSplats)
        return;

    float3 Pos = Positions[Idx].xyz;
    float4 Clip = mul(float4(Pos, 1.0f), LocalToClip);

    // Frustum cull
    bool Visible = Clip.w > 0.001f
        && abs(Clip.x) < Clip.w * 1.2f
        && abs(Clip.y) < Clip.w * 1.2f
        && Clip.z > 0.0f && Clip.z < Clip.w;

    OutIndices[Idx] = Idx;
    OutDepths[Idx] = Visible
        ? uint(saturate(Clip.z / Clip.w) * 65534.0f)  // 16-bit depth
        : 0xFFFF;  // NOT_VISIBLE sentinel
}
```

**Step 3: Create ComputeTransformCS.usf**

```hlsl
#include "/Engine/Private/Common.ush"
#include "/GaussianRenderer/Private/GaussianCommon.ush"

Buffer<float4> Positions;       // xyz
Buffer<float4> Rotations;       // quaternion xyzw
Buffer<float4> Scales;          // xyz
float4x4 LocalToView;
float2 FocalLength;             // fx, fy in pixels
uint NumSplats;

RWBuffer<float4> OutTransforms; // 2x2 transform as float4

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void MainCS(uint3 DTid : SV_DispatchThreadID)
{
    uint Idx = DTid.x;
    if (Idx >= NumSplats)
        return;

    float3 Pos = Positions[Idx].xyz;
    float4 Quat = Rotations[Idx];
    float3 Scale = Scales[Idx].xyz;

    // Transform position to view space
    float3 ViewPos = mul(float4(Pos, 1.0f), LocalToView).xyz;
    float Z = max(ViewPos.z, 0.001f);

    // Build 3D covariance
    float3x3 Cov3D = BuildCovariance(Quat, Scale);

    // View rotation (upper 3x3 of LocalToView)
    float3x3 W = (float3x3)LocalToView;

    // Jacobian of perspective projection
    float3x2 J = float3x2(
        FocalLength.x / Z, 0,
        0, FocalLength.y / Z,
        -FocalLength.x * ViewPos.x / (Z * Z), -FocalLength.y * ViewPos.y / (Z * Z)
    );

    // Project: Cov2D = J^T * W * Cov3D * W^T * J
    float3x3 WCov = mul(W, Cov3D);
    float3x3 WCovWT = mul(WCov, transpose(W));
    float2x2 Cov2D = float2x2(
        dot(J[0], mul(WCovWT, J[0])), dot(J[0], mul(WCovWT, J[1])),
        dot(J[1], mul(WCovWT, J[0])), dot(J[1], mul(WCovWT, J[1]))
    );

    // Low-pass filter (prevent aliasing for tiny splats)
    Cov2D[0][0] += 0.3f;
    Cov2D[1][1] += 0.3f;

    // Eigendecomposition of 2x2 symmetric matrix
    float a = Cov2D[0][0], b = Cov2D[0][1], d = Cov2D[1][1];
    float det = a * d - b * b;
    float trace = a + d;
    float disc = max(0.0f, trace * trace * 0.25f - det);
    float sqrtDisc = sqrt(disc);
    float lambda1 = max(0.0f, trace * 0.5f + sqrtDisc);
    float lambda2 = max(0.0f, trace * 0.5f - sqrtDisc);

    float s1 = sqrt(lambda1);
    float s2 = sqrt(lambda2);

    // Eigenvector for lambda1
    float2 v0;
    if (abs(b) > 1e-6f)
        v0 = normalize(float2(lambda1 - d, b));
    else
        v0 = float2(1, 0);

    // Output 2x2 rotation-scale matrix (for VS to generate quad corners)
    // M = [v0 * s1, v0_perp * s2] as float4
    float2 v1 = float2(-v0.y, v0.x);
    OutTransforms[Idx] = float4(v0.x * s1, v0.y * s1, v1.x * s2, v1.y * s2);
}
```

**Step 4: Create GaussianSplatVS.usf**

```hlsl
#include "/Engine/Private/Common.ush"
#include "/GaussianRenderer/Private/GaussianCommon.ush"

Buffer<float4> Positions;
Buffer<uint> Colors;          // RGBA8 packed
Buffer<uint> SortedIndices;
Buffer<float4> Transforms;    // 2x2 projected transform
float4x4 LocalToClip;
float2 InvRenderSize;         // 1/width, 1/height

void MainVS(
    uint VertexID : SV_VertexID,
    out float4 OutPosition : SV_POSITION,
    out float2 OutSigma : TEXCOORD0,
    out float4 OutColor : COLOR0)
{
    uint SplatIndex = SortedIndices[VertexID / 6];
    uint CornerIndex = VertexID % 6;

    // Quad corners: two triangles (0,1,2) (2,1,3)
    static const float2 Corners[6] = {
        float2(-1, -1), float2(1, -1), float2(-1, 1),
        float2(-1, 1),  float2(1, -1), float2(1, 1)
    };
    float2 Corner = Corners[CornerIndex] * GAUSSIAN_RADIUS_SIGMA;

    // Get splat position in clip space
    float3 Pos = Positions[SplatIndex].xyz;
    float4 Clip = mul(float4(Pos, 1.0f), LocalToClip);

    // Get 2x2 transform
    float4 T = Transforms[SplatIndex];
    float2x2 M = float2x2(T.x, T.z, T.y, T.w);

    // Apply transform to corner, convert to NDC offset
    float2 Offset = mul(Corner, M) * InvRenderSize * 2.0f;

    OutPosition = Clip;
    OutPosition.xy += Offset * Clip.w;

    // For PS: distance in sigma units / sqrt(2)
    OutSigma = Corner * 0.70710678f; // 1/sqrt(2)

    // Unpack color
    uint Packed = Colors[SplatIndex];
    OutColor = float4(
        float((Packed >> 0) & 0xFF) / 255.0f,
        float((Packed >> 8) & 0xFF) / 255.0f,
        float((Packed >> 16) & 0xFF) / 255.0f,
        float((Packed >> 24) & 0xFF) / 255.0f
    );
}
```

**Step 5: Create GaussianSplatPS.usf**

```hlsl
#include "/Engine/Private/Common.ush"
#include "/GaussianRenderer/Private/GaussianCommon.ush"

void MainPS(
    float4 SvPosition : SV_POSITION,
    float2 Sigma : TEXCOORD0,
    nointerpolation float4 Color : COLOR0,
    out float4 OutColor : SV_Target0)
{
    float SqOverTwo = dot(Sigma, Sigma);
    if (SqOverTwo > CUTOFF_SQ_OVER_2)
        discard;

    float Alpha = Color.a * exp(-SqOverTwo);
    OutColor = float4(Color.rgb, Alpha);
}
```

**Step 6: Commit**

```
feat: add HLSL shaders for gaussian compute + splatting pipeline
```

---

## Task 5: Shader C++ Bindings (Parameter Structs + Compilation)

**Files:**
- Create: `GaussianRenderer/Source/GaussianRendererRuntime/Private/Rendering/GaussianShaders.h`
- Create: `GaussianRenderer/Source/GaussianRendererRuntime/Private/Rendering/GaussianShaders.cpp`

**Step 1: Define shader parameter structs and shader classes**

```cpp
// GaussianShaders.h
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

// --- Compute Depth ---
class FComputeDepthCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FComputeDepthCS);
    SHADER_USE_PARAMETER_STRUCT(FComputeDepthCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_SRV(Buffer<float4>, Positions)
        SHADER_PARAMETER(FMatrix44f, LocalToClip)
        SHADER_PARAMETER(uint32, NumSplats)
        SHADER_PARAMETER_UAV(RWBuffer<uint>, OutIndices)
        SHADER_PARAMETER_UAV(RWBuffer<uint>, OutDepths)
    END_SHADER_PARAMETER_STRUCT()
};

// --- Compute Transform ---
class FComputeTransformCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FComputeTransformCS);
    SHADER_USE_PARAMETER_STRUCT(FComputeTransformCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_SRV(Buffer<float4>, Positions)
        SHADER_PARAMETER_SRV(Buffer<float4>, Rotations)
        SHADER_PARAMETER_SRV(Buffer<float4>, Scales)
        SHADER_PARAMETER(FMatrix44f, LocalToView)
        SHADER_PARAMETER(FVector2f, FocalLength)
        SHADER_PARAMETER(uint32, NumSplats)
        SHADER_PARAMETER_UAV(RWBuffer<float4>, OutTransforms)
    END_SHADER_PARAMETER_STRUCT()
};

// --- Vertex Shader ---
class FGaussianSplatVS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FGaussianSplatVS);
    SHADER_USE_PARAMETER_STRUCT(FGaussianSplatVS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_SRV(Buffer<float4>, Positions)
        SHADER_PARAMETER_SRV(Buffer<uint>, Colors)
        SHADER_PARAMETER_SRV(Buffer<uint>, SortedIndices)
        SHADER_PARAMETER_SRV(Buffer<float4>, Transforms)
        SHADER_PARAMETER(FMatrix44f, LocalToClip)
        SHADER_PARAMETER(FVector2f, InvRenderSize)
    END_SHADER_PARAMETER_STRUCT()
};

// --- Pixel Shader ---
class FGaussianSplatPS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FGaussianSplatPS);
    SHADER_USE_PARAMETER_STRUCT(FGaussianSplatPS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
    END_SHADER_PARAMETER_STRUCT()
};
```

**Step 2: Implement shader compilation in GaussianShaders.cpp**

```cpp
IMPLEMENT_GLOBAL_SHADER(FComputeDepthCS, "/GaussianRenderer/Private/ComputeDepthCS.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FComputeTransformCS, "/GaussianRenderer/Private/ComputeTransformCS.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FGaussianSplatVS, "/GaussianRenderer/Private/GaussianSplatVS.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FGaussianSplatPS, "/GaussianRenderer/Private/GaussianSplatPS.usf", "MainPS", SF_Pixel);
```

**Step 3: Verify shaders compile**

Launch editor, check log for shader compilation errors. Expected: all 4 shaders compile.

**Step 4: Commit**

```
feat: add shader parameter structs and compilation bindings
```

---

## Task 6: Scene Proxy + View Extension (Core Rendering Pipeline)

**Files:**
- Create: `GaussianRenderer/Source/GaussianRendererRuntime/Private/Rendering/GaussianSceneProxy.h`
- Create: `GaussianRenderer/Source/GaussianRendererRuntime/Private/Rendering/GaussianSceneProxy.cpp`
- Create: `GaussianRenderer/Source/GaussianRendererRuntime/Private/Rendering/GaussianViewExtension.h`
- Create: `GaussianRenderer/Source/GaussianRendererRuntime/Private/Rendering/GaussianViewExtension.cpp`
- Create: `GaussianRenderer/Source/GaussianRendererRuntime/Private/Rendering/GaussianRendering.h`
- Create: `GaussianRenderer/Source/GaussianRendererRuntime/Private/Rendering/GaussianRendering.cpp`

**Step 1: Create FGaussianSceneProxy**

```cpp
class FGaussianSceneProxy : public FPrimitiveSceneProxy
{
public:
    FGaussianSceneProxy(const UPrimitiveComponent* Component);
    virtual ~FGaussianSceneProxy();

    // Called on render thread
    void CreateRenderThreadResources() override;
    void DestroyRenderThreadResources() override;

    // Data access
    uint32 GetNumSplats() const { return NumSplats; }
    FShaderResourceViewRHIRef GetPositionsSRV() const;
    FShaderResourceViewRHIRef GetRotationsSRV() const;
    FShaderResourceViewRHIRef GetScalesSRV() const;
    FShaderResourceViewRHIRef GetColorsSRV() const;

    // Per-frame GPU buffers
    FGaussianDynamicBuffer& GetTransformsBuffer() { return TransformsBuffer; }
    FGaussianDynamicBuffer& GetIndicesBuffer() { return IndicesBuffer; }
    FGaussianDynamicBuffer& GetDepthsBuffer() { return DepthsBuffer; }

    // Upload new data (for sequence playback — called from game thread, enqueued to render thread)
    void UpdateSplatData_GameThread(TArray<FVector3f>&& Positions, TArray<FVector3f>&& Scales,
        TArray<FQuat4f>&& Rotations, TArray<FColor>&& Colors, uint32 NumSplats);

private:
    uint32 NumSplats = 0;

    // Static data (uploaded once for static, swapped for sequence)
    FGaussianStaticBuffer PositionsBuffer;
    FGaussianStaticBuffer RotationsBuffer;
    FGaussianStaticBuffer ScalesBuffer;
    FGaussianStaticBuffer ColorsBuffer;

    // Per-frame GPU intermediates
    FGaussianDynamicBuffer TransformsBuffer;
    FGaussianDynamicBuffer IndicesBuffer;
    FGaussianDynamicBuffer DepthsBuffer;

    // For sorting
    FGaussianDynamicBuffer IndicesBuffer2;  // ping-pong
    FGaussianDynamicBuffer DepthsBuffer2;   // ping-pong
};
```

**Step 2: Create FGaussianViewExtension**

Reference: PICOSplat's `SplatSceneViewExtension`. This hooks into UE's rendering pipeline.

```cpp
class FGaussianViewExtension : public FSceneViewExtensionBase
{
public:
    FGaussianViewExtension(const FAutoRegister& AutoRegister);

    // Register/unregister proxies
    void RegisterProxy(FGaussianSceneProxy* Proxy);
    void UnregisterProxy(FGaussianSceneProxy* Proxy);

    // FSceneViewExtensionBase
    virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
    virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
    virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
    virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override;
    virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs) override;
    virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

private:
    TArray<FGaussianSceneProxy*> RegisteredProxies;
    FCriticalSection ProxiesLock;
};
```

**Step 3: Implement PreRenderView_RenderThread**

For each registered proxy:
1. Dispatch `FComputeDepthCS` — fills indices + depths buffers
2. Call UE's `SortGPUBuffers` for GPU radix sort (16-bit keys)
3. Dispatch `FComputeTransformCS` — fills transforms buffer

**Step 4: Implement PrePostProcessPass_RenderThread**

For each registered proxy:
1. Set up graphics pipeline state:
   - BlendState: `SrcAlpha, InvSrcAlpha` (standard alpha blend)
   - DepthStencil: no depth write
   - Rasterizer: default
2. Bind VS + PS parameters
3. `RHICmdList.DrawPrimitive(0, NumSplats * 2, 1)` — 2 tris per splat, 6 verts per splat

**Step 5: Create dispatch helper functions in GaussianRendering.cpp**

```cpp
void DispatchComputeDepth(FRHICommandList& RHICmdList, FGaussianSceneProxy* Proxy, const FMatrix44f& LocalToClip);
void DispatchComputeTransform(FRHICommandList& RHICmdList, FGaussianSceneProxy* Proxy, const FMatrix44f& LocalToView, const FVector2f& FocalLength);
void RenderGaussianSplats(FRHICommandList& RHICmdList, FGaussianSceneProxy* Proxy, const FMatrix44f& LocalToClip, const FVector2f& InvRenderSize);
```

**Step 6: Register ViewExtension in module startup**

```cpp
// In GaussianRendererModule.cpp StartupModule():
ViewExtension = FSceneViewExtensions::NewExtension<FGaussianViewExtension>();
```

**Step 7: Commit**

```
feat: add scene proxy, view extension, and compute+render dispatch
```

---

## Task 7: GaussianComponent (Static Rendering)

**Files:**
- Create: `GaussianRenderer/Source/GaussianRendererRuntime/Public/GaussianComponent.h`
- Create: `GaussianRenderer/Source/GaussianRendererRuntime/Private/GaussianComponent.cpp`

**Step 1: Define UGaussianComponent**

```cpp
UCLASS(ClassGroup=(Rendering), meta=(BlueprintSpawnableComponent))
class GAUSSIANRENDERERRUNTIME_API UGaussianComponent : public UPrimitiveComponent
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian")
    UGaussianAsset* GaussianAsset;

    // UPrimitiveComponent
    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

    // Blueprint API
    UFUNCTION(BlueprintCallable, Category = "Gaussian")
    void SetGaussianAsset(UGaussianAsset* NewAsset);

private:
    void UploadDataToProxy();
};
```

**Step 2: Implement CreateSceneProxy**

- Create `FGaussianSceneProxy`
- Upload asset data (positions, rotations, scales, colors) to GPU buffers
- Register proxy with `FGaussianViewExtension`

**Step 3: Implement CalcBounds**

Use `GaussianAsset->BoundsMin/BoundsMax` to compute `FBoxSphereBounds`.

**Step 4: Create a simple test Actor (optional helper)**

```cpp
UCLASS()
class AGaussianActor : public AActor
{
    GENERATED_BODY()
public:
    AGaussianActor();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    UGaussianComponent* GaussianComponent;
};
```

**Step 5: Test end-to-end static rendering**

1. Import a .ply → UGaussianAsset
2. Place AGaussianActor in level
3. Assign asset
4. Expected: gaussian splats render in viewport

**Step 6: Commit**

```
feat: add GaussianComponent and GaussianActor for static PLY rendering
```

---

## Task 8: GaussianSequenceComponent (.bin Sequence Playback)

**Files:**
- Create: `GaussianRenderer/Source/GaussianRendererRuntime/Public/GaussianSequenceComponent.h`
- Create: `GaussianRenderer/Source/GaussianRendererRuntime/Private/GaussianSequenceComponent.cpp`

**Step 1: Define UGaussianSequenceComponent**

Port the playback logic from `UGSRawStream` but replace Niagara with GPU buffer swap:

```cpp
UCLASS(ClassGroup=(GaussianSplatting), meta=(BlueprintSpawnableComponent, DisplayName="Gaussian Sequence Player"))
class GAUSSIANRENDERERRUNTIME_API UGaussianSequenceComponent : public UPrimitiveComponent
{
    GENERATED_BODY()
public:
    // --- Properties (same as GSRawStream) ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sequence", meta = (FilePathFilter = "json"))
    FFilePath SequencePath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playback", meta = (ClampMin = "0.1", ClampMax = "5.0"))
    float PlaybackSpeed = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playback")
    bool bAutoPlay = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playback")
    bool bLoop = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    bool bPreloadToRAM = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playback", meta = (ClampMin = "-1"))
    int32 StartFrame = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playback", meta = (ClampMin = "-1"))
    int32 EndFrame = -1;

    // --- Events ---
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFrameChanged, int32, NewFrame);
    UPROPERTY(BlueprintAssignable) FOnFrameChanged OnFrameChanged;

    // --- API ---
    UFUNCTION(BlueprintCallable) bool LoadSequence(const FString& Path);
    UFUNCTION(BlueprintCallable) void Play();
    UFUNCTION(BlueprintCallable) void Pause();
    UFUNCTION(BlueprintCallable) void Stop();
    UFUNCTION(BlueprintCallable) void GoToFrame(int32 Frame);
    UFUNCTION(BlueprintCallable) bool IsPlaying() const;
    UFUNCTION(BlueprintCallable) int32 GetCurrentFrame() const;
    UFUNCTION(BlueprintCallable) int32 GetFrameCount() const;

    // UPrimitiveComponent
    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type) override;
    virtual void TickComponent(float DeltaTime, ELevelTick, FActorComponentTickFunction*) override;

private:
    // Sequence metadata (reuse FGSRawSequenceMeta / FGSRawFrameMeta from existing streamer, or define locally)
    // Frame loading from .bin files — same logic as GSRawStream
    // Instead of creating Texture2D → feed decoded half-float data into GPU buffers

    // Key difference from GSRawStream:
    // - No Niagara components
    // - No transient textures
    // - Instead: decode .bin → convert half-float texture data back to FVector3f/FQuat4f/FColor
    //   → upload to scene proxy GPU buffers via UpdateSplatData_GameThread()

    // N-buffering: use 2 sets of GPU buffers, swap active index
    // (handled by scene proxy's UpdateSplatData_GameThread which enqueues render command)

    void LoadAndUploadFrame(int32 FrameIndex);
    void ConvertTextureDataToSplatData(/* .bin raw data → FGaussianSplat arrays */);
};
```

**Step 2: Implement frame loading**

Reuse the .bin loading logic from `GSRawStream::LoadTextureFromDisk` / `CreateTextureFromRAM`, but instead of creating UTexture2D, decode the half-float RGBA data back into `FVector3f` position, `FQuat4f` rotation, etc., then call `Proxy->UpdateSplatData_GameThread()`.

Key conversion (from the existing texture format — this is what `WriteGaussianDataToTexture` in ThreeDGaussiansLibrary writes):
- `texture[0]` = position: `(z_ue, x_ue, -y_ue, 0)` in meters → `FVector3f(R, G, B)`
- `texture[1]` = rotation: `(qz, qx, -qy, qw)` → `FQuat4f(R, G, B, A)`
- `texture[2]` = scale+opacity: `(sz, sx, sy, opacity)` → `FVector3f(R,G,B)` + opacity `A`
- `texture[3]` = SH0 DC: `(dc_r, dc_g, dc_b, ...)` → bake to `FColor` via `SH_C0 * dc + 0.5`

**Step 3: Implement tick + playback logic**

Same frame accumulator / advance logic as `GSRawStream::TickComponent`, minus all Niagara component management.

**Step 4: Test sequence playback**

1. Use existing batch-converted .bin sequence
2. Place actor with GaussianSequenceComponent
3. Set SequencePath to sequence.json
4. Play → expected: smooth playback with compute shader rendering

**Step 5: Commit**

```
feat: add GaussianSequenceComponent for .bin sequence playback
```

---

## Task 9: Integration Testing + Polish

**Files:**
- Modify: Various files for bug fixes

**Step 1: Test static PLY rendering**

- Import several .ply files of different sizes (small < 100K, medium ~500K, large > 1M gaussians)
- Verify rendering quality matches original Niagara version
- Check no visual artifacts (sorting order, alpha blending)

**Step 2: Test sequence playback**

- Load existing .bin sequence
- Verify frame transitions are smooth (no flicker)
- Verify playback speed matches target FPS
- Test Play/Pause/Stop/GoToFrame

**Step 3: Performance comparison**

- Compare FPS between old Niagara plugin and new Compute Shader plugin
- Compare VRAM usage
- Log results

**Step 4: Fix any coordinate conversion issues**

The most likely bugs will be in coordinate convention mismatches between:
- PLY (COLMAP) → UE world space
- UE world space → view space → clip space
- The existing .bin format (which stores data in UE convention with specific axis swaps)

**Step 5: Commit**

```
fix: polish rendering pipeline and fix coordinate issues
```

---

## Summary: File Count

| Category | New Files | Modified Files |
|---|---|---|
| Plugin skeleton | 7 | 0 |
| Data types + buffers | 4 | 0 |
| Asset + import | 4 | 0 |
| Shaders (HLSL) | 5 | 0 |
| Shader bindings (C++) | 2 | 0 |
| Rendering pipeline | 6 | 0 |
| Static component | 2 | 0 |
| Sequence component | 2 | 0 |
| **Total** | **32** | **0** |

This is a brand new plugin — no modifications to existing code.

## Dependency Graph

```
Task 1 (skeleton)
  ↓
Task 2 (data types + buffers)
  ↓
  ├── Task 3 (asset + PLY import)
  │     ↓
  │   Task 7 (static component) ──→ Task 9 (testing)
  │
  ├── Task 4 (shaders HLSL)
  │     ↓
  │   Task 5 (shader C++ bindings)
  │     ↓
  │   Task 6 (scene proxy + view extension + rendering)
  │     ↓
  │   Task 7 + Task 8
  │
  └── Task 8 (sequence component) ──→ Task 9 (testing)
```

Tasks 3, 4 can be done in parallel after Task 2.
Tasks 7, 8 can be done in parallel after Task 6.
