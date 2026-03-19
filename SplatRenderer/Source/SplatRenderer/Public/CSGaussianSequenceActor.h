#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "CSGaussianComponent.h"
#include "CSCropBoxComponent.h"
#include "CSGaussianSequencePlayer.h"
#include "CSGaussianSequenceActor.generated.h"

UCLASS(HideCategories = (Input, HLOD, LOD, Cooking, DataLayers, Replication, WorldPartition, Networking),
    meta = (DisplayName = "CS Gaussian Sequence Actor"))
class SPLATRENDERER_API ACSGaussianSequenceActor : public AActor
{
    GENERATED_BODY()

public:
    ACSGaussianSequenceActor();

    // ── Sequence ──

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS Sequence")
    FFilePath SequencePath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS Sequence")
    FFilePath AudioPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS Sequence",
        meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
    float AudioVolume = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS Sequence")
    bool bPlaying = true;

    // ── Playback ──

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS Playback",
        meta = (ClampMin = "0.0", UIMin = "0.0"))
    float PlaybackSpeed = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS Playback")
    ECSPlaybackMode PlaybackMode = ECSPlaybackMode::Loop;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS Playback",
        meta = (ClampMin = "-1", UIMin = "-1"))
    int32 StartFrame = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS Playback",
        meta = (ClampMin = "-1", UIMin = "-1"))
    int32 EndFrame = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS Playback",
        meta = (ClampMin = "0", UIMin = "0"))
    int32 ScrubFrame = 0;

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

    // ── Blueprint Functions ──

    UFUNCTION(BlueprintCallable, Category = "GS Rendering")
    void SetBrightness(float InBrightness);

    UFUNCTION(BlueprintCallable, Category = "GS Rendering")
    void SetSplatScale(float InScale);

    UFUNCTION(BlueprintCallable, Category = "GS Crop Volume")
    void SetCropVolume(bool bEnable, FVector InCenter, FVector InSize, FQuat InRotation);

    UFUNCTION(BlueprintCallable, Category = "GS Audio")
    void SetAudioVolume(float InVolume);

    // ── Components (visible in viewport) ──

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
    void SyncToPlayer();
    bool LoadAudioFromFile(const FString& FilePath);
    void SyncAudioToPlayback();
    void SyncCropToComponent();
    void SyncBoxFromCropParams();
    void OnCropBoxTransformUpdated(USceneComponent* Comp, EUpdateTransformFlags Flags, ETeleportType Teleport);
    void OnCropBoxExtentChanged();

    UPROPERTY()
    TObjectPtr<UCSGaussianSequencePlayer> SequencePlayer;

    UPROPERTY()
    TObjectPtr<UAudioComponent> AudioComponent;

    UPROPERTY()
    TObjectPtr<USoundWaveProcedural> LoadedSoundWave;

    TArray<uint8> AudioPCMData;
    float AudioDuration = 0.0f;
    bool bUpdatingCropBox = false;
};
