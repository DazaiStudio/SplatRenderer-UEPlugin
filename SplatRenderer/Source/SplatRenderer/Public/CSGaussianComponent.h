#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Texture2D.h"
#include "CSGaussianComponent.generated.h"

UCLASS(ClassGroup=(Rendering),
    HideCategories = (Rendering, Physics, Collision, Input, HLOD, Mobile, Navigation, VirtualTexture, ComponentTick, Tags, Cooking, LOD, ComponentReplication, Activation, Trigger, AssetUserData, Sockets),
    meta=(BlueprintSpawnableComponent, DisplayName="CS Gaussian Renderer"))
class SPLATRENDERER_API UCSGaussianComponent : public UPrimitiveComponent
{
    GENERATED_BODY()

public:
    UCSGaussianComponent();

    // --- Textures (set programmatically, not visible in editor) ---

    UPROPERTY(Transient)
    UTexture2D* PositionTexture = nullptr;

    UPROPERTY(Transient)
    UTexture2D* Cov1Texture = nullptr;

    UPROPERTY(Transient)
    UTexture2D* Cov2OpacityTexture = nullptr;

    UPROPERTY(Transient)
    UTexture2D* RotationTexture = nullptr;

    UPROPERTY(Transient)
    UTexture2D* ScaleOpacityTexture = nullptr;

    UPROPERTY(Transient)
    UTexture2D* SH0Texture = nullptr;

    int32 GaussianCount = 0;

    // True = PLY path (pre-computed covariance), False = sequence path (raw rotation+scale)
    bool bUsePrecomputedCov = true;

    // Brightness & Crop — managed by Actor, not shown on Component
    UPROPERTY(BlueprintReadWrite, Category = "GS Rendering")
    float Brightness = 1.0f;

    UPROPERTY(BlueprintReadWrite, Category = "GS Crop")
    bool bEnableCrop = false;

    FVector CropCenter = FVector::ZeroVector;
    FVector CropHalfSize = FVector(1000, 1000, 1000);
    FQuat CropRotation = FQuat::Identity;

    // FX state — managed by Actor, not shown on Component
    bool bEnableParticleFX = false;
    bool bEnableNoise = true;
    float NoiseAmplitude = 500.0f;
    float NoiseFrequency = 0.01f;
    float NoiseSpeed = 1.0f;
    bool bEnableWind = false;
    FVector WindDirection = FVector(1, 0, 0);
    float WindStrength = 100.0f;
    bool bEnableGravity = false;
    float GravityStrength = 980.0f;
    bool bEnableAttract = false;
    FVector AttractCenter = FVector::ZeroVector;
    float AttractStrength = 500.0f;
    float Drag = 0.5f;
    bool bEnableVortex = false;
    FVector VortexAxis = FVector(0, 0, 1);
    FVector VortexCenter = FVector::ZeroVector;
    float VortexStrength = 500.0f;
    // Rendering
    float SplatScale = 1.0f;

    // --- Blueprint Functions ---

    UFUNCTION(BlueprintCallable, Category = "GS Rendering")
    void SetBrightness(float InBrightness);

    UFUNCTION(BlueprintCallable, Category = "GS Crop")
    void SetCropVolume(bool bEnable, FVector InCenter, FVector InHalfSize, FQuat InRotation);

    UFUNCTION(BlueprintCallable, Category = "GS Rendering")
    void SetSplatScale(float InScale);

    UFUNCTION(BlueprintCallable, Category = "GS FX")
    void SetParticleFXEnabled(bool bEnable);

    UFUNCTION(BlueprintCallable, Category = "GS FX")
    void SetParticleFXParams(
        float InNoiseAmplitude, float InNoiseFrequency, float InNoiseSpeed,
        FVector InWindDirection, float InWindStrength,
        float InGravityStrength,
        FVector InAttractCenter, float InAttractStrength,
        float InDrag);

    UFUNCTION(BlueprintCallable, Category = "Gaussian Data")
    void SetGaussianTextures(
        UTexture2D* InPosition,
        UTexture2D* InCov1,
        UTexture2D* InCov2Opacity,
        UTexture2D* InSH0,
        int32 InGaussianCount);

    UFUNCTION(BlueprintCallable, Category = "Gaussian Data")
    void SetSequenceTextures(
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
    virtual void OnUnregister() override;
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
    //~ End UPrimitiveComponent Interface

    void UpdateProxyBrightness();

private:
    void UpdateProxyTextures();
    void UpdateProxyParticleFX();
};
