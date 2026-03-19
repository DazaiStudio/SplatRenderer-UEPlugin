#pragma once

#include "CSGaussianBuffers.h"
#include "PrimitiveSceneProxy.h"
#include "SceneView.h"
#include "Engine/Texture2D.h"

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
    FTextureRHIRef GetCov1TextureRHI() const { return Cov1TextureRHI; }
    FTextureRHIRef GetCov2OpacityTextureRHI() const { return Cov2OpacityTextureRHI; }
    FTextureRHIRef GetRotationTextureRHI() const { return RotationTextureRHI; }
    FTextureRHIRef GetScaleOpacityTextureRHI() const { return ScaleOpacityTextureRHI; }
    FTextureRHIRef GetSH0TextureRHI() const { return SH0TextureRHI; }
    bool UsePrecomputedCov() const { return bUsePrecomputedCov; }
    float GetBrightness() const { return Brightness; }
    float GetSplatScale() const { return SplatScale; }
    void SetSplatScale(float InScale) { SplatScale = InScale; }
    bool IsCropEnabled() const { return bEnableCrop; }
    FVector3f GetCropCenter() const { return CropCenter; }
    FVector3f GetCropHalfSize() const { return CropHalfSize; }
    FMatrix44f GetCropInvRotation() const { return FMatrix44f(CropInvRotation); }

    // GPU buffer accessors
    FShaderResourceViewRHIRef GetIndicesSRV() const { return Indices.ShaderResourceViewRHI; }
    FUnorderedAccessViewRHIRef GetIndicesUAV() const { return Indices.UnorderedAccessViewRHI; }
    FShaderResourceViewRHIRef GetTransformsSRV() const { return Transforms.ShaderResourceViewRHI; }
    FUnorderedAccessViewRHIRef GetTransformsUAV() const { return Transforms.UnorderedAccessViewRHI; }
    FShaderResourceViewRHIRef GetVisibleCountSRV() const { return VisibleCount.ShaderResourceViewRHI; }
    FUnorderedAccessViewRHIRef GetVisibleCountUAV() const { return VisibleCount.UnorderedAccessViewRHI; }
    FBufferRHIRef GetIndirectArgsBuffer() const { return IndirectArgsBuffer; }

    FRDGBufferRef& GetIndicesFake() { return IndicesFake; }
    FRDGBufferRef& GetDistancesFake() { return DistancesFake; }

    FString GetName() const { return Name; }

    // Particle FX
    bool IsParticleFXEnabled() const { return bEnableParticleFX && HasAnyActiveForce(); }
    bool HasAnyActiveForce() const
    {
        return FX_NoiseAmplitude > 0.f || FX_WindStrength != 0.f ||
               FX_GravityStrength != 0.f || FX_AttractStrength != 0.f ||
               FX_VortexStrength != 0.f;
    }
    bool IsParticleFXFirstFrame() const { return bFXFirstFrame; }
    void ClearFXFirstFrame() { bFXFirstFrame = false; }
    FShaderResourceViewRHIRef GetDisplacedPositionsSRV() const { return DisplacedPositions.ShaderResourceViewRHI; }
    FUnorderedAccessViewRHIRef GetDisplacedPositionsUAV() const { return DisplacedPositions.UnorderedAccessViewRHI; }
    FUnorderedAccessViewRHIRef GetVelocitiesUAV() const { return Velocities.UnorderedAccessViewRHI; }
    FBufferRHIRef GetDisplacedPositionsBuffer() const { return DisplacedPositions.VertexBufferRHI; }
    FBufferRHIRef GetVelocitiesBuffer() const { return Velocities.VertexBufferRHI; }

    // FX parameters (set from game thread via ENQUEUE_RENDER_COMMAND)
    float FX_NoiseAmplitude = 0.f;
    float FX_NoiseFrequency = 0.f;
    float FX_NoiseSpeed = 0.f;
    FVector3f FX_WindDirection = FVector3f::ZeroVector;
    float FX_WindStrength = 0.f;
    float FX_GravityStrength = 0.f;
    FVector3f FX_AttractCenter = FVector3f::ZeroVector;
    float FX_AttractStrength = 0.f;
    float FX_Drag = 0.f;
    FVector3f FX_VortexAxis = FVector3f(0, 0, 1);
    FVector3f FX_VortexCenter = FVector3f::ZeroVector;
    float FX_VortexStrength = 0.f;

    void SetParticleFXEnabled(bool bEnable)
    {
        if (bEnable && !bEnableParticleFX)
        {
            bFXFirstFrame = true;
            bSortCacheValid = false; // Force re-sort on FX enable
        }
        if (!bEnable && bEnableParticleFX)
        {
            bFXFirstFrame = true;
            bSortCacheValid = false; // Force re-sort on FX disable
        }
        bEnableParticleFX = bEnable;
    }

    void SetBrightness(float InBrightness) { Brightness = InBrightness; }
    void SetCrop(bool bEnable, const FVector3f& InCenter, const FVector3f& InHalfSize, const FMatrix& InInvRotation)
    {
        if (bEnableCrop != bEnable || CropCenter != InCenter || CropHalfSize != InHalfSize)
            bSortCacheValid = false;
        bEnableCrop = bEnable;
        CropCenter = InCenter;
        CropHalfSize = InHalfSize;
        CropInvRotation = InInvRotation;
    }

    // Sort cache: skip recompute when camera + data unchanged
    bool IsSortCacheValid(const FMatrix44f& CurrentLocalToClip) const
    {
        if (!bSortCacheValid) return false;
        // Epsilon comparison — exact equality fails due to float rounding noise
        const float* A = &CachedLocalToClip.M[0][0];
        const float* B = &CurrentLocalToClip.M[0][0];
        for (int32 i = 0; i < 16; ++i)
        {
            if (FMath::Abs(A[i] - B[i]) > 1e-5f)
                return false;
        }
        return true;
    }
    void SetSortCache(const FMatrix44f& LocalToClip) { CachedLocalToClip = LocalToClip; bSortCacheValid = true; }
    void InvalidateSortCache() { bSortCacheValid = false; }

    // Called from game thread via ENQUEUE_RENDER_COMMAND to update textures
    void UpdateTextures_RenderThread(
        FTextureRHIRef InPosition,
        FTextureRHIRef InCov1,
        FTextureRHIRef InCov2Opacity,
        FTextureRHIRef InRotation,
        FTextureRHIRef InScaleOpacity,
        FTextureRHIRef InSH0,
        uint32 InNumSplats,
        uint32 InTextureWidth,
        bool bInUsePrecomputedCov);

private:
    // Texture RHI refs (updated dynamically for sequence mode)
    FTextureRHIRef PositionTextureRHI;
    FTextureRHIRef Cov1TextureRHI;        // PLY path: pre-computed covariance
    FTextureRHIRef Cov2OpacityTextureRHI; // PLY path: pre-computed covariance + opacity
    FTextureRHIRef RotationTextureRHI;     // Sequence path: raw quaternion
    FTextureRHIRef ScaleOpacityTextureRHI; // Sequence path: raw scale + opacity
    FTextureRHIRef SH0TextureRHI;
    bool bUsePrecomputedCov = true;
    float Brightness = 1.0f;
    float SplatScale = 1.0f;
    bool bEnableCrop = false;
    FVector3f CropCenter = FVector3f::ZeroVector;
    FVector3f CropHalfSize = FVector3f(1000, 1000, 1000);
    FMatrix CropInvRotation = FMatrix::Identity;

    uint32 NumSplats = 0;
    uint32 TextureWidth = 0;

    // GPU intermediate buffers
    FCSGaussianGPUBuffer Indices;
    FCSGaussianGPUBuffer Transforms;
    FCSGaussianGPUBuffer VisibleCount; // Single uint: atomic visible splat count
    FBufferRHIRef IndirectArgsBuffer; // DrawPrimitiveIndirect args

    // Particle FX buffers
    FCSGaussianGPUBuffer DisplacedPositions; // float4: xyz=pos
    FCSGaussianGPUBuffer Velocities;         // float4: xyz=velocity
    bool bEnableParticleFX = false;
    bool bFXFirstFrame = true;

    // Fake RDG buffers for resource tracking
    FRDGBufferRef IndicesFake;
    FRDGBufferRef DistancesFake;

    FString Name;

    // Sort cache (skip re-sort when view unchanged)
    FMatrix44f CachedLocalToClip;
    bool bSortCacheValid = false;

    // Max capacity (for buffer reallocation)
    uint32 MaxSplats = 0;
};

} // namespace CSGaussian
