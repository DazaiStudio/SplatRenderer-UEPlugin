#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "CSGaussianComponent.h"
#include "CSCropBoxComponent.h"
#include "CSGaussianActor.generated.h"

UCLASS(HideCategories = (Input, HLOD, LOD, Cooking, DataLayers, Replication, WorldPartition, Networking),
    meta = (DisplayName = "CS Gaussian Actor"))
class SPLATRENDERER_API ACSGaussianActor : public AActor
{
    GENERATED_BODY()

public:
    ACSGaussianActor();

    // ── Data ──

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS Data",
        meta = (FilePathFilter = "PLY files (*.ply)|*.ply"))
    FFilePath PLYFilePath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS Data")
    FFilePath AudioPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS Data",
        meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
    float AudioVolume = 1.0f;

    UFUNCTION(BlueprintCallable, Category = "GS Data")
    bool LoadPLY(const FString& FilePath);

    // ── Rendering ──

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS Rendering",
        meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "5.0"))
    float Brightness = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS Rendering",
        meta = (ClampMin = "0.0", UIMin = "0.01", UIMax = "5.0"))
    float SplatScale = 1.0f;

    // ── Crop Volume ──

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS Crop Volume")
    bool bEnableCrop = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS Crop Volume",
        meta = (EditCondition = "bEnableCrop"))
    FVector CropCenter = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS Crop Volume",
        meta = (EditCondition = "bEnableCrop", ClampMin = "0.0", UIMin = "0.0"))
    FVector CropSize = FVector(2000, 2000, 2000);

    // ── GS FX ──

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS FX")
    bool bEnableParticleFX = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS FX|Noise",
        meta = (EditCondition = "bEnableParticleFX"))
    bool bEnableNoise = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS FX|Noise",
        meta = (EditCondition = "bEnableParticleFX && bEnableNoise", ClampMin = "0.0", UIMin = "0.0", UIMax = "5000.0"))
    float NoiseAmplitude = 500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS FX|Noise",
        meta = (EditCondition = "bEnableParticleFX && bEnableNoise", ClampMin = "0.0001", UIMin = "0.001", UIMax = "1.0"))
    float NoiseFrequency = 0.01f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS FX|Noise",
        meta = (EditCondition = "bEnableParticleFX && bEnableNoise", ClampMin = "0.0", UIMin = "0.0", UIMax = "10.0"))
    float NoiseSpeed = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS FX|Wind",
        meta = (EditCondition = "bEnableParticleFX"))
    bool bEnableWind = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS FX|Wind",
        meta = (EditCondition = "bEnableParticleFX && bEnableWind"))
    FVector WindDirection = FVector(1, 0, 0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS FX|Wind",
        meta = (EditCondition = "bEnableParticleFX && bEnableWind", UIMin = "0.0", UIMax = "1000.0"))
    float WindStrength = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS FX|Gravity",
        meta = (EditCondition = "bEnableParticleFX"))
    bool bEnableGravity = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS FX|Gravity",
        meta = (EditCondition = "bEnableParticleFX && bEnableGravity", UIMin = "0.0", UIMax = "2000.0"))
    float GravityStrength = 980.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS FX|Attract",
        meta = (EditCondition = "bEnableParticleFX"))
    bool bEnableAttract = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS FX|Attract",
        meta = (EditCondition = "bEnableParticleFX && bEnableAttract"))
    FVector AttractCenter = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS FX|Attract",
        meta = (EditCondition = "bEnableParticleFX && bEnableAttract", UIMin = "-1000.0", UIMax = "1000.0"))
    float AttractStrength = 500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS FX|Vortex",
        meta = (EditCondition = "bEnableParticleFX"))
    bool bEnableVortex = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS FX|Vortex",
        meta = (EditCondition = "bEnableParticleFX && bEnableVortex"))
    FVector VortexAxis = FVector(0, 0, 1);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS FX|Vortex",
        meta = (EditCondition = "bEnableParticleFX && bEnableVortex"))
    FVector VortexCenter = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS FX|Vortex",
        meta = (EditCondition = "bEnableParticleFX && bEnableVortex", UIMin = "-2000.0", UIMax = "2000.0"))
    float VortexStrength = 500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS FX",
        meta = (EditCondition = "bEnableParticleFX", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
    float Drag = 0.5f;

    // ── Blueprint Functions ──

    UFUNCTION(BlueprintCallable, Category = "GS Rendering")
    void SetBrightness(float InBrightness);

    UFUNCTION(BlueprintCallable, Category = "GS Rendering")
    void SetSplatScale(float InScale);

    UFUNCTION(BlueprintCallable, Category = "GS Crop Volume")
    void SetCropVolume(bool bEnable, FVector InCenter, FVector InSize, FQuat InRotation);

    UFUNCTION(BlueprintCallable, Category = "GS Audio")
    void SetAudioVolume(float InVolume);

    // ── Components ──

    UPROPERTY(VisibleAnywhere, Category = "Components")
    TObjectPtr<UCSGaussianComponent> GaussianComponent;

    UPROPERTY(VisibleAnywhere, Category = "Components")
    TObjectPtr<UCSCropBoxComponent> CropBoxComponent;

    virtual void Tick(float DeltaSeconds) override;
    virtual bool ShouldTickIfViewportsOnly() const override { return true; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
    void SyncBoxFromCropParams();
    void SyncCropToComponent();
    void SyncFXToComponent();
    void OnCropBoxTransformUpdated(USceneComponent* Comp, EUpdateTransformFlags Flags, ETeleportType Teleport);
    void OnCropBoxExtentChanged();
    bool LoadAudioFromFile(const FString& FilePath);

    UPROPERTY(Transient)
    TArray<UTexture2D*> LoadedTextures;

    UPROPERTY()
    TObjectPtr<UAudioComponent> AudioComponent;

    UPROPERTY()
    TObjectPtr<USoundWaveProcedural> LoadedSoundWave;

    TArray<uint8> AudioPCMData;
    float AudioDuration = 0.0f;
    bool bUpdatingCropBox = false;
};
