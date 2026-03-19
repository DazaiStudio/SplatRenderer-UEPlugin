#include "CSGaussianSceneProxy.h"
#include "CSGaussianComponent.h"
#include "CSGaussianSubsystem.h"
#include "TextureResource.h"

namespace CSGaussian
{

FCSGaussianSceneProxy::FCSGaussianSceneProxy(UCSGaussianComponent& Component)
    : FPrimitiveSceneProxy(&Component)
{
    NumSplats = Component.GetGaussianCount();
    TextureWidth = Component.GetTextureWidth();
    MaxSplats = 0;

    auto GetTextureRHI = [](UTexture2D* Tex) -> FTextureRHIRef
    {
        if (Tex && Tex->GetResource())
            return Tex->GetResource()->TextureRHI;
        return nullptr;
    };

    bUsePrecomputedCov = Component.bUsePrecomputedCov;
    Brightness = Component.Brightness;
    bEnableCrop = Component.bEnableCrop;
    CropCenter = FVector3f(Component.CropCenter);
    CropHalfSize = FVector3f(Component.CropHalfSize);
    CropInvRotation = FRotationMatrix(Component.CropRotation.Inverse().Rotator());
    PositionTextureRHI = GetTextureRHI(Component.PositionTexture);
    Cov1TextureRHI = GetTextureRHI(Component.Cov1Texture);
    Cov2OpacityTextureRHI = GetTextureRHI(Component.Cov2OpacityTexture);
    RotationTextureRHI = GetTextureRHI(Component.RotationTexture);
    ScaleOpacityTextureRHI = GetTextureRHI(Component.ScaleOpacityTexture);
    SH0TextureRHI = GetTextureRHI(Component.SH0Texture);

    // Particle FX state
    bEnableParticleFX = Component.bEnableParticleFX;
    bFXFirstFrame = true;
    FX_NoiseAmplitude = Component.bEnableNoise ? Component.NoiseAmplitude : 0.f;
    FX_NoiseFrequency = Component.NoiseFrequency;
    FX_NoiseSpeed = Component.NoiseSpeed;
    FX_WindDirection = FVector3f(Component.WindDirection);
    FX_WindStrength = Component.bEnableWind ? Component.WindStrength : 0.f;
    FX_GravityStrength = Component.bEnableGravity ? Component.GravityStrength : 0.f;
    FX_AttractCenter = FVector3f(Component.AttractCenter);
    FX_AttractStrength = Component.bEnableAttract ? Component.AttractStrength : 0.f;
    FX_Drag = Component.Drag;
    FX_VortexAxis = FVector3f(Component.VortexAxis);
    FX_VortexCenter = FVector3f(Component.VortexCenter);
    FX_VortexStrength = Component.bEnableVortex ? Component.VortexStrength : 0.f;
    SplatScale = Component.SplatScale;

    Name = Component.GetOwner() ? Component.GetOwner()->GetName() : TEXT("Unknown");
}

void FCSGaussianSceneProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
{
    // Init GPU buffers if we already have splats at creation time
    if (NumSplats > 0 && MaxSplats == 0)
    {
        MaxSplats = NumSplats;
        Indices = FCSGaussianGPUBuffer(MaxSplats, PF_R32_UINT);
        Transforms = FCSGaussianGPUBuffer(MaxSplats, PF_FloatRGBA);
        VisibleCount = FCSGaussianGPUBuffer(1, PF_R32_UINT);
        Indices.InitRHI(RHICmdList);
        Transforms.InitRHI(RHICmdList);
        VisibleCount.InitRHI(RHICmdList);

        // Indirect args: {VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation}
        FRHIResourceCreateInfo ArgsCreateInfo(TEXT("CSIndirectArgs"));
        IndirectArgsBuffer = RHICmdList.CreateBuffer(
            sizeof(uint32) * 4,
            EBufferUsageFlags::DrawIndirect | EBufferUsageFlags::UnorderedAccess,
            sizeof(uint32),
            ERHIAccess::IndirectArgs,
            ArgsCreateInfo);

        // FX buffers
        DisplacedPositions = FCSGaussianGPUBuffer(MaxSplats, PF_A32B32G32R32F);
        Velocities = FCSGaussianGPUBuffer(MaxSplats, PF_A32B32G32R32F);
        DisplacedPositions.InitRHI(RHICmdList);
        Velocities.InitRHI(RHICmdList);
    }

    if (GEngine)
    {
        UCSGaussianSubsystem* Subsystem = GEngine->GetEngineSubsystem<UCSGaussianSubsystem>();
        if (Subsystem)
        {
            Subsystem->AddGaussianProxy_RenderThread(this);
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
            Subsystem->RemoveGaussianProxy_RenderThread(this);
        }
    }

    Indices.ReleaseResource();
    Transforms.ReleaseResource();
    VisibleCount.ReleaseResource();
    IndirectArgsBuffer.SafeRelease();
    DisplacedPositions.ReleaseResource();
    Velocities.ReleaseResource();
}

bool FCSGaussianSceneProxy::IsVisible(const FSceneView& View) const
{
    if (NumSplats == 0 || !PositionTextureRHI || !SH0TextureRHI ||
        !Indices.ShaderResourceViewRHI || !Transforms.ShaderResourceViewRHI)
        return false;

    if (bUsePrecomputedCov)
    {
        if (!Cov1TextureRHI || !Cov2OpacityTextureRHI)
            return false;
    }
    else
    {
        if (!RotationTextureRHI || !ScaleOpacityTextureRHI)
            return false;
    }

    if (!IsShown(&View) || &GetScene() != View.Family->Scene)
        return false;

    // Respect per-primitive visibility (sub-level hiding in editor, etc.)
    FPrimitiveComponentId PrimId = GetPrimitiveComponentId();
    if (View.HiddenPrimitives.Contains(PrimId))
        return false;
    if (View.ShowOnlyPrimitives.IsSet() && !View.ShowOnlyPrimitives->Contains(PrimId))
        return false;

    return true;
}

void FCSGaussianSceneProxy::UpdateTextures_RenderThread(
    FTextureRHIRef InPosition,
    FTextureRHIRef InCov1,
    FTextureRHIRef InCov2Opacity,
    FTextureRHIRef InRotation,
    FTextureRHIRef InScaleOpacity,
    FTextureRHIRef InSH0,
    uint32 InNumSplats,
    uint32 InTextureWidth,
    bool bInUsePrecomputedCov)
{
    check(IsInRenderingThread());

    bSortCacheValid = false;
    bFXFirstFrame = true; // Reset FX state when positions change (sequence frame change)
    bUsePrecomputedCov = bInUsePrecomputedCov;
    PositionTextureRHI = InPosition;
    Cov1TextureRHI = InCov1;
    Cov2OpacityTextureRHI = InCov2Opacity;
    RotationTextureRHI = InRotation;
    ScaleOpacityTextureRHI = InScaleOpacity;
    SH0TextureRHI = InSH0;
    NumSplats = InNumSplats;
    TextureWidth = InTextureWidth;

    // Allocate or grow GPU buffers as needed
    if (InNumSplats > MaxSplats)
    {
        Indices.ReleaseResource();
        Transforms.ReleaseResource();
        VisibleCount.ReleaseResource();
        IndirectArgsBuffer.SafeRelease();
        DisplacedPositions.ReleaseResource();
        Velocities.ReleaseResource();

        MaxSplats = InNumSplats;
        Indices = FCSGaussianGPUBuffer(MaxSplats, PF_R32_UINT);
        Transforms = FCSGaussianGPUBuffer(MaxSplats, PF_FloatRGBA);
        VisibleCount = FCSGaussianGPUBuffer(1, PF_R32_UINT);
        DisplacedPositions = FCSGaussianGPUBuffer(MaxSplats, PF_A32B32G32R32F);
        Velocities = FCSGaussianGPUBuffer(MaxSplats, PF_A32B32G32R32F);

        FRHICommandListImmediate& RHICmdList = FRHICommandListImmediate::Get();
        Indices.InitRHI(RHICmdList);
        Transforms.InitRHI(RHICmdList);
        VisibleCount.InitRHI(RHICmdList);
        DisplacedPositions.InitRHI(RHICmdList);
        Velocities.InitRHI(RHICmdList);

        bFXFirstFrame = true; // Reset FX state on buffer realloc

        FRHIResourceCreateInfo ArgsCreateInfo(TEXT("CSIndirectArgs"));
        IndirectArgsBuffer = RHICmdList.CreateBuffer(
            sizeof(uint32) * 4,
            EBufferUsageFlags::DrawIndirect | EBufferUsageFlags::UnorderedAccess,
            sizeof(uint32),
            ERHIAccess::IndirectArgs,
            ArgsCreateInfo);
    }
}

} // namespace CSGaussian
