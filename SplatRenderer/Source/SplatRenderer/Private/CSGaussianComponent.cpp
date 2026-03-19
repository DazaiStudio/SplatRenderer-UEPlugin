#include "CSGaussianComponent.h"
#include "Rendering/CSGaussianSceneProxy.h"
#include "RenderingThread.h"
#include "TextureResource.h"

UCSGaussianComponent::UCSGaussianComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    bCastDynamicShadow = false;
    bCastStaticShadow = false;
}

#if WITH_EDITOR
void UCSGaussianComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    const FName PropName = PropertyChangedEvent.GetMemberPropertyName();

    // FX-related properties: push to proxy without recreating it
    static const TSet<FName> FXProps = {
        GET_MEMBER_NAME_CHECKED(UCSGaussianComponent, bEnableParticleFX),
        GET_MEMBER_NAME_CHECKED(UCSGaussianComponent, bEnableNoise),
        GET_MEMBER_NAME_CHECKED(UCSGaussianComponent, NoiseAmplitude),
        GET_MEMBER_NAME_CHECKED(UCSGaussianComponent, NoiseFrequency),
        GET_MEMBER_NAME_CHECKED(UCSGaussianComponent, NoiseSpeed),
        GET_MEMBER_NAME_CHECKED(UCSGaussianComponent, bEnableWind),
        GET_MEMBER_NAME_CHECKED(UCSGaussianComponent, WindDirection),
        GET_MEMBER_NAME_CHECKED(UCSGaussianComponent, WindStrength),
        GET_MEMBER_NAME_CHECKED(UCSGaussianComponent, bEnableGravity),
        GET_MEMBER_NAME_CHECKED(UCSGaussianComponent, GravityStrength),
        GET_MEMBER_NAME_CHECKED(UCSGaussianComponent, bEnableAttract),
        GET_MEMBER_NAME_CHECKED(UCSGaussianComponent, AttractCenter),
        GET_MEMBER_NAME_CHECKED(UCSGaussianComponent, AttractStrength),
        GET_MEMBER_NAME_CHECKED(UCSGaussianComponent, Drag),
        GET_MEMBER_NAME_CHECKED(UCSGaussianComponent, bEnableVortex),
        GET_MEMBER_NAME_CHECKED(UCSGaussianComponent, VortexAxis),
        GET_MEMBER_NAME_CHECKED(UCSGaussianComponent, VortexCenter),
        GET_MEMBER_NAME_CHECKED(UCSGaussianComponent, VortexStrength),
    };

    if (FXProps.Contains(PropName))
    {
        UpdateProxyParticleFX();
    }

    if (PropName == GET_MEMBER_NAME_CHECKED(UCSGaussianComponent, Brightness))
    {
        UpdateProxyBrightness();
    }

    if (PropName == GET_MEMBER_NAME_CHECKED(UCSGaussianComponent, SplatScale))
    {
        SetSplatScale(SplatScale);
    }
}
#endif

void UCSGaussianComponent::OnUnregister()
{
    // Flush pending render commands to ensure no enqueued commands reference the proxy
    // after it is destroyed. Critical for streaming level unload.
    FlushRenderingCommands();
    Super::OnUnregister();
}

FPrimitiveSceneProxy* UCSGaussianComponent::CreateSceneProxy()
{
    // Always create proxy — textures will be set dynamically via UpdateProxyTextures
    return new CSGaussian::FCSGaussianSceneProxy(*this);
}

FBoxSphereBounds UCSGaussianComponent::CalcBounds(const FTransform& LocalToWorld) const
{
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
    UTexture2D* InCov1,
    UTexture2D* InCov2Opacity,
    UTexture2D* InSH0,
    int32 InGaussianCount)
{
    bUsePrecomputedCov = true;
    PositionTexture = InPosition;
    Cov1Texture = InCov1;
    Cov2OpacityTexture = InCov2Opacity;
    RotationTexture = nullptr;
    ScaleOpacityTexture = nullptr;
    SH0Texture = InSH0;
    GaussianCount = InGaussianCount;

    UpdateProxyTextures();
}

void UCSGaussianComponent::SetSequenceTextures(
    UTexture2D* InPosition,
    UTexture2D* InRotation,
    UTexture2D* InScaleOpacity,
    UTexture2D* InSH0,
    int32 InGaussianCount)
{
    bUsePrecomputedCov = false;
    PositionTexture = InPosition;
    Cov1Texture = nullptr;
    Cov2OpacityTexture = nullptr;
    RotationTexture = InRotation;
    ScaleOpacityTexture = InScaleOpacity;
    SH0Texture = InSH0;
    GaussianCount = InGaussianCount;

    UpdateProxyTextures();
}

void UCSGaussianComponent::UpdateProxyTextures()
{
    if (!SceneProxy)
    {
        MarkRenderStateDirty();
        return;
    }

    auto GetTextureRHI = [](UTexture2D* Tex) -> FTextureRHIRef
    {
        if (Tex && Tex->GetResource())
            return Tex->GetResource()->TextureRHI;
        return nullptr;
    };

    FTextureRHIRef PosRHI = GetTextureRHI(PositionTexture);
    FTextureRHIRef Cov1RHI = GetTextureRHI(Cov1Texture);
    FTextureRHIRef Cov2OpRHI = GetTextureRHI(Cov2OpacityTexture);
    FTextureRHIRef RotRHI = GetTextureRHI(RotationTexture);
    FTextureRHIRef ScaleOpRHI = GetTextureRHI(ScaleOpacityTexture);
    FTextureRHIRef SH0RHI = GetTextureRHI(SH0Texture);

    // If texture RHI isn't ready yet (async UpdateResource), skip this frame
    if (!PosRHI && PositionTexture)
        return;

    uint32 Count = (uint32)FMath::Max(GaussianCount, 0);
    uint32 TexWidth = GetTextureWidth();
    bool bPrecomputed = bUsePrecomputedCov;
    float BrightnessVal = Brightness;
    bool bCrop = bEnableCrop;
    FVector3f Center3f(CropCenter);
    FVector3f HalfSize3f(CropHalfSize);
    FMatrix InvRot = FRotationMatrix(CropRotation.Inverse().Rotator());

    CSGaussian::FCSGaussianSceneProxy* Proxy =
        static_cast<CSGaussian::FCSGaussianSceneProxy*>(SceneProxy);

    bool bFXEnable = bEnableParticleFX;
    float NA = bEnableNoise ? NoiseAmplitude : 0.f;
    float NF = NoiseFrequency, NS = NoiseSpeed;
    FVector3f WD(WindDirection);
    float WS = bEnableWind ? WindStrength : 0.f;
    float GS = bEnableGravity ? GravityStrength : 0.f;
    FVector3f AC(AttractCenter);
    float AS = bEnableAttract ? AttractStrength : 0.f;
    float D = Drag;
    FVector3f VA(VortexAxis);
    FVector3f VC(VortexCenter);
    float VS_Vortex = bEnableVortex ? VortexStrength : 0.f;
    float SScale = SplatScale;

    ENQUEUE_RENDER_COMMAND(UpdateCSGaussianTextures)(
        [Proxy, PosRHI, Cov1RHI, Cov2OpRHI, RotRHI, ScaleOpRHI, SH0RHI, Count, TexWidth, bPrecomputed, BrightnessVal, bCrop, Center3f, HalfSize3f, InvRot,
         bFXEnable, NA, NF, NS, WD, WS, GS, AC, AS, D, VA, VC, VS_Vortex, SScale]
        (FRHICommandListImmediate& RHICmdList)
        {
            Proxy->SetBrightness(BrightnessVal);
            Proxy->SetSplatScale(SScale);
            Proxy->SetCrop(bCrop, Center3f, HalfSize3f, InvRot);
            Proxy->UpdateTextures_RenderThread(
                PosRHI, Cov1RHI, Cov2OpRHI, RotRHI, ScaleOpRHI, SH0RHI,
                Count, TexWidth, bPrecomputed);
            Proxy->SetParticleFXEnabled(bFXEnable);
            Proxy->FX_NoiseAmplitude = NA;
            Proxy->FX_NoiseFrequency = NF;
            Proxy->FX_NoiseSpeed = NS;
            Proxy->FX_WindDirection = WD;
            Proxy->FX_WindStrength = WS;
            Proxy->FX_GravityStrength = GS;
            Proxy->FX_AttractCenter = AC;
            Proxy->FX_AttractStrength = AS;
            Proxy->FX_Drag = D;
            Proxy->FX_VortexAxis = VA;
            Proxy->FX_VortexCenter = VC;
            Proxy->FX_VortexStrength = VS_Vortex;
        });
}

void UCSGaussianComponent::SetBrightness(float InBrightness)
{
    Brightness = FMath::Max(InBrightness, 0.0f);
    UpdateProxyBrightness();
}

void UCSGaussianComponent::SetCropVolume(bool bEnable, FVector InCenter, FVector InHalfSize, FQuat InRotation)
{
    bEnableCrop = bEnable;
    CropCenter = InCenter;
    CropHalfSize = InHalfSize;
    CropRotation = InRotation;

    if (!SceneProxy) return;

    FVector3f Center3f(InCenter);
    FVector3f HalfSize3f(InHalfSize);
    FMatrix InvRot = FRotationMatrix(InRotation.Inverse().Rotator());
    CSGaussian::FCSGaussianSceneProxy* Proxy =
        static_cast<CSGaussian::FCSGaussianSceneProxy*>(SceneProxy);

    ENQUEUE_RENDER_COMMAND(UpdateCSGaussianCrop)(
        [Proxy, bEnable, Center3f, HalfSize3f, InvRot](FRHICommandListImmediate&)
        {
            Proxy->SetCrop(bEnable, Center3f, HalfSize3f, InvRot);
        });
}

void UCSGaussianComponent::SetParticleFXEnabled(bool bEnable)
{
    bEnableParticleFX = bEnable;
    UpdateProxyParticleFX();
}

void UCSGaussianComponent::SetParticleFXParams(
    float InNoiseAmplitude, float InNoiseFrequency, float InNoiseSpeed,
    FVector InWindDirection, float InWindStrength,
    float InGravityStrength,
    FVector InAttractCenter, float InAttractStrength,
    float InDrag)
{
    NoiseAmplitude = InNoiseAmplitude;
    NoiseFrequency = InNoiseFrequency;
    NoiseSpeed = InNoiseSpeed;
    WindDirection = InWindDirection;
    WindStrength = InWindStrength;
    GravityStrength = InGravityStrength;
    AttractCenter = InAttractCenter;
    AttractStrength = InAttractStrength;
    Drag = InDrag;
    UpdateProxyParticleFX();
}

void UCSGaussianComponent::UpdateProxyParticleFX()
{
    if (!SceneProxy) return;

    CSGaussian::FCSGaussianSceneProxy* Proxy =
        static_cast<CSGaussian::FCSGaussianSceneProxy*>(SceneProxy);

    bool bEnable = bEnableParticleFX;
    float NA = bEnableNoise ? NoiseAmplitude : 0.f;
    float NF = NoiseFrequency, NS = NoiseSpeed;
    FVector3f WD(WindDirection);
    float WS = bEnableWind ? WindStrength : 0.f;
    float GS = bEnableGravity ? GravityStrength : 0.f;
    FVector3f AC(AttractCenter);
    float AS = bEnableAttract ? AttractStrength : 0.f;
    float D = Drag;
    FVector3f VA(VortexAxis);
    FVector3f VC(VortexCenter);
    float VS_Vortex = bEnableVortex ? VortexStrength : 0.f;

    ENQUEUE_RENDER_COMMAND(UpdateCSGaussianFX)(
        [Proxy, bEnable, NA, NF, NS, WD, WS, GS, AC, AS, D, VA, VC, VS_Vortex](FRHICommandListImmediate&)
        {
            Proxy->SetParticleFXEnabled(bEnable);
            Proxy->FX_NoiseAmplitude = NA;
            Proxy->FX_NoiseFrequency = NF;
            Proxy->FX_NoiseSpeed = NS;
            Proxy->FX_WindDirection = WD;
            Proxy->FX_WindStrength = WS;
            Proxy->FX_GravityStrength = GS;
            Proxy->FX_AttractCenter = AC;
            Proxy->FX_AttractStrength = AS;
            Proxy->FX_Drag = D;
            Proxy->FX_VortexAxis = VA;
            Proxy->FX_VortexCenter = VC;
            Proxy->FX_VortexStrength = VS_Vortex;
        });
}

void UCSGaussianComponent::UpdateProxyBrightness()
{
    if (!SceneProxy) return;

    float BrightnessVal = Brightness;
    CSGaussian::FCSGaussianSceneProxy* Proxy =
        static_cast<CSGaussian::FCSGaussianSceneProxy*>(SceneProxy);

    ENQUEUE_RENDER_COMMAND(UpdateCSGaussianBrightness)(
        [Proxy, BrightnessVal](FRHICommandListImmediate&)
        {
            Proxy->SetBrightness(BrightnessVal);
        });
}

void UCSGaussianComponent::SetSplatScale(float InScale)
{
    SplatScale = FMath::Max(InScale, 0.0f);

    if (!SceneProxy) return;

    float Scale = SplatScale;
    CSGaussian::FCSGaussianSceneProxy* Proxy =
        static_cast<CSGaussian::FCSGaussianSceneProxy*>(SceneProxy);

    ENQUEUE_RENDER_COMMAND(UpdateCSGaussianSplatScale)(
        [Proxy, Scale](FRHICommandListImmediate&)
        {
            Proxy->SetSplatScale(Scale);
        });
}
