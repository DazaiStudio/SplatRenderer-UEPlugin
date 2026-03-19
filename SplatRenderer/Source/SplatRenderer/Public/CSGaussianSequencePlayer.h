#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/Texture2D.h"
#include "Async/Future.h"
#include "HAL/ThreadSafeBool.h"
#include "CSGaussianSequencePlayer.generated.h"

class UCSGaussianComponent;
class FCSGSDReader;

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

// Pre-loaded frame data (loaded on background thread)
struct FCSPreloadedFrame
{
    int32 FrameIndex = -1;
    int32 GaussianCount = 0;
    int32 TextureWidth = 0;
    int32 TextureHeight = 0;
    int32 PositionPrecision = 0;
    int32 RotationPrecision = 1;
    int32 ScaleOpacityPrecision = 1;
    int32 SHPrecision = 1;
    TArray<uint8> PositionData;
    TArray<uint8> RotationData;
    TArray<uint8> ScaleOpacityData;
    TArray<uint8> SH0Data;
    bool bValid = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCSSequenceComplete);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCSFrameChanged, int32, NewFrame);

UCLASS(ClassGroup=(Rendering), meta=(BlueprintSpawnableComponent, DisplayName="CS Gaussian Sequence Player"))
class SPLATRENDERER_API UCSGaussianSequencePlayer : public UActorComponent
{
    GENERATED_BODY()

public:
    UCSGaussianSequencePlayer();

    // --- Properties (set by owning actor, hidden from editor) ---

    UPROPERTY(BlueprintReadWrite, Category = "GS Sequence")
    FFilePath SequencePath;

    UPROPERTY(BlueprintReadWrite, Category = "GS Playback")
    float PlaybackSpeed = 1.0f;

    UPROPERTY(BlueprintReadWrite, Category = "GS Playback")
    ECSPlaybackMode PlaybackMode = ECSPlaybackMode::Loop;

    UPROPERTY(BlueprintReadWrite, Category = "GS Playback")
    bool bAutoPlay = true;

    UPROPERTY(BlueprintReadWrite, Category = "GS Playback")
    int32 StartFrame = -1;

    UPROPERTY(BlueprintReadWrite, Category = "GS Playback")
    int32 EndFrame = -1;

    UPROPERTY(BlueprintReadWrite, Category = "GS Playback")
    int32 ScrubFrame = 0;

    // --- Events ---

    UPROPERTY(BlueprintAssignable)
    FOnCSSequenceComplete OnSequenceComplete;

    UPROPERTY(BlueprintAssignable)
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

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
    UCSGaussianComponent* FindGaussianComponent() const;
    int32 GetEffectiveStartFrame() const;
    int32 GetEffectiveEndFrame() const;
    bool LoadFrameMetadata(int32 FrameIndex, FCSSequenceFrameMeta& OutMeta);

    // Double-buffered texture loading
    bool LoadFrameTextures(int32 FrameIndex, int32 BufferIndex);
    void UploadToBuffer(int32 BufferIndex, FCSPreloadedFrame& Frame);
    void ApplyBufferToComponent(int32 BufferIndex);

    // Async preloading
    void KickPreload(int32 FrameIndex);
    bool IsPreloadReady() const;
    FCSPreloadedFrame ConsumePreload();
    int32 GetNextFrameIndex(int32 Current) const;

    bool LoadSequenceGSD(const FString& InGSDPath);

    // Background IO (runs on thread pool)
    static FCSPreloadedFrame LoadFrameDataFromDisk(
        const FString& FrameFolderPath, const FCSSequenceFrameMeta& Meta);

    // Double-buffered textures
    struct FTextureSet
    {
        UTexture2D* PositionTex = nullptr;
        UTexture2D* RotationTex = nullptr;
        UTexture2D* ScaleOpacityTex = nullptr;
        UTexture2D* SH0Tex = nullptr;
    };

    UTexture2D* GetOrCreateReusableTexture(UTexture2D*& ExistingTex, int32 Width, int32 Height, int32 Precision);
    void UpdateTextureData(UTexture2D* Tex, const uint8* Data, int32 DataSize);
    void UpdateTextureDataMove(UTexture2D* Tex, TArray<uint8>&& Data);
    int32 CachedGaussianCount = 0;

    FCSSequenceMeta SequenceMetadata;
    TMap<int32, FCSSequenceFrameMeta> FrameMetaCache;
    bool bSequenceLoaded = false;

    // GSD reader (null when using raw .bin folders)
    TSharedPtr<FCSGSDReader> GSDReader;

    // Double buffer: upload to BackBuffer, render from FrontBuffer, then swap
    // UPROPERTY textures to prevent GC
    UPROPERTY()
    TArray<UTexture2D*> TextureGCRoots;
    FTextureSet TextureBuffers[2];
    int32 FrontBufferIndex = 0; // Currently being rendered

    // Ring buffer preload: multiple frames ahead in RAM
    static constexpr int32 PRELOAD_RING_SIZE = 10;
    TArray<FCSPreloadedFrame> PreloadRing;
    FCriticalSection PreloadLock;
    TFuture<void> PreloadWorkerFuture;
    FThreadSafeBool bPreloadWorkerRunning;
    int32 PreloadNextFrame = -1; // Next frame to start loading

    void StartPreloadWorker(int32 InStartFrame);
    void StopPreloadWorker();
    FCSPreloadedFrame* FindInRing(int32 FrameIndex);

    // Pending swap: wait one tick after upload for UpdateResource to finish
    bool bPendingSwap = false;
    int32 PendingSwapBufferIndex = -1;
    int32 PendingSwapFrameIndex = -1;

    // Playback state
    bool bIsPlaying = false;
    float FrameAccumulator = 0.0f;
    int32 CurrentFrameIndex = 0;
    int32 LastDisplayedFrame = -1;
    int32 LastScrubFrame = -1;
};
