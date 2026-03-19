#include "CSGaussianSequencePlayer.h"
#include "CSGaussianComponent.h"
#include "CSGSDReader.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Async/Async.h"
#include "RenderingThread.h"
#include "TextureResource.h"

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
    StopPreloadWorker();
    GSDReader.Reset();
    Super::EndPlay(EndPlayReason);
}

void UCSGaussianSequencePlayer::TickComponent(
    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bSequenceLoaded || SequenceMetadata.FrameCount <= 0)
        return;

    // Runtime ScrubFrame: if not playing and ScrubFrame changed, jump to that frame
    if (!bIsPlaying)
    {
        if (ScrubFrame != LastScrubFrame)
        {
            ScrubFrame = FMath::Clamp(ScrubFrame, 0, SequenceMetadata.FrameCount - 1);
            GoToFrame(ScrubFrame);
            LastScrubFrame = ScrubFrame;
        }
        return;
    }

    // Step 1: If a swap is pending (uploaded last tick), apply it now
    if (bPendingSwap)
    {
        FrontBufferIndex = PendingSwapBufferIndex;
        ApplyBufferToComponent(FrontBufferIndex);

        LastDisplayedFrame = PendingSwapFrameIndex;
        ScrubFrame = PendingSwapFrameIndex;
        OnFrameChanged.Broadcast(PendingSwapFrameIndex);

        bPendingSwap = false;
    }

    // Step 2: Advance playback time
    FrameAccumulator += DeltaTime * PlaybackSpeed;
    float FrameDuration = 1.0f / SequenceMetadata.TargetFPS;

    int32 EffStart = GetEffectiveStartFrame();
    int32 EffEnd = GetEffectiveEndFrame();
    while (FrameAccumulator >= FrameDuration)
    {
        FrameAccumulator -= FrameDuration;
        CurrentFrameIndex++;

        if (CurrentFrameIndex > EffEnd)
        {
            if (PlaybackMode == ECSPlaybackMode::Loop)
            {
                CurrentFrameIndex = EffStart;
            }
            else
            {
                CurrentFrameIndex = EffEnd;
                bIsPlaying = false;
                OnSequenceComplete.Broadcast();
                return;
            }
        }
    }

    // Step 3: If frame changed, try to get from ring buffer
    if (CurrentFrameIndex != LastDisplayedFrame)
    {
        int32 FrameRange = FMath::Max(EffEnd - EffStart + 1, 1);
        int32 BaseFrame = LastDisplayedFrame >= 0 ? LastDisplayedFrame : EffStart;
        FCSPreloadedFrame FrameCopy;
        int32 DisplayFrame = CurrentFrameIndex;
        {
            FScopeLock Lock(&PreloadLock);

            // Purge stale frames and find best frame to display.
            // At high speed the preloader skips frames, so the exact target
            // may not exist — find the closest frame between LDF and CI.
            int32 BestDist = INT32_MAX;
            int32 BestIdx = -1;
            for (int32 Idx = PreloadRing.Num() - 1; Idx >= 0; --Idx)
            {
                int32 FIdx = PreloadRing[Idx].FrameIndex;
                int32 Dist = ((FIdx - CurrentFrameIndex) % FrameRange + FrameRange) % FrameRange;

                // Purge frames behind playhead
                if (Dist > PRELOAD_RING_SIZE)
                {
                    PreloadRing.RemoveAtSwap(Idx);
                    continue;
                }

                if (!PreloadRing[Idx].bValid) continue;

                // Prefer exact match; otherwise closest frame ahead of LDF
                if (FIdx == CurrentFrameIndex)
                {
                    BestIdx = Idx;
                    BestDist = 0;
                    DisplayFrame = FIdx;
                    break;
                }

                int32 DistFromLDF = ((FIdx - BaseFrame) % FrameRange + FrameRange) % FrameRange;
                if (DistFromLDF > 0 && DistFromLDF < BestDist)
                {
                    BestDist = DistFromLDF;
                    BestIdx = Idx;
                    DisplayFrame = FIdx;
                }
            }

            if (BestIdx >= 0)
            {
                FrameCopy = MoveTemp(PreloadRing[BestIdx]);
                PreloadRing.RemoveAtSwap(BestIdx);
            }
        }

        if (FrameCopy.bValid)
        {
            int32 BackIndex = 1 - FrontBufferIndex;
            UploadToBuffer(BackIndex, FrameCopy);

            bPendingSwap = true;
            PendingSwapBufferIndex = BackIndex;
            PendingSwapFrameIndex = DisplayFrame;
            // Sync playhead to displayed frame
            CurrentFrameIndex = DisplayFrame;
        }
        else
        {
            // Ring empty — hold position
            CurrentFrameIndex = LastDisplayedFrame >= 0 ? LastDisplayedFrame : EffStart;
        }
    }
}

bool UCSGaussianSequencePlayer::LoadSequence(const FString& InSequencePath)
{
    // Detect format by extension
    if (InSequencePath.EndsWith(TEXT(".gsd"), ESearchCase::IgnoreCase))
    {
        return LoadSequenceGSD(InSequencePath);
    }

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

    GSDReader.Reset(); // Not using GSD

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

    // Pre-cache all frame metadata
    for (int32 i = 0; i < SequenceMetadata.FrameCount; i++)
    {
        FCSSequenceFrameMeta Dummy;
        LoadFrameMetadata(i, Dummy);
    }

    bSequenceLoaded = true;

    UE_LOG(LogTemp, Log, TEXT("CSGaussian: Loaded sequence '%s' with %d frames at %g FPS"),
        *SequenceMetadata.SequenceName,
        SequenceMetadata.FrameCount,
        SequenceMetadata.TargetFPS);

    // Start preloading immediately so frames are ready when Play() is called
    StartPreloadWorker(GetEffectiveStartFrame());

    return true;
}

bool UCSGaussianSequencePlayer::LoadSequenceGSD(const FString& InGSDPath)
{
    StopPreloadWorker();

    GSDReader = MakeShared<FCSGSDReader>();
    if (!GSDReader->Open(InGSDPath))
    {
        GSDReader.Reset();
        return false;
    }

    // Populate metadata from GSD header
    SequenceMetadata.SequenceName = GSDReader->GetSequenceName();
    SequenceMetadata.FrameCount = GSDReader->GetFrameCount();
    SequenceMetadata.TargetFPS = GSDReader->GetTargetFPS();
    SequenceMetadata.SHDegree = GSDReader->GetSHDegree();

    bSequenceLoaded = true;

    UE_LOG(LogTemp, Log, TEXT("CSGaussian: Loaded GSD '%s' — %d frames at %g FPS"),
        *SequenceMetadata.SequenceName,
        SequenceMetadata.FrameCount,
        SequenceMetadata.TargetFPS);

    StartPreloadWorker(GetEffectiveStartFrame());
    return true;
}

void UCSGaussianSequencePlayer::Play()
{
    if (bSequenceLoaded)
    {
        bIsPlaying = true;
        SetComponentTickEnabled(true);

        // Start at effective start frame if not already displaying a frame
        if (LastDisplayedFrame == -1)
        {
            CurrentFrameIndex = GetEffectiveStartFrame();
            if (LoadFrameTextures(CurrentFrameIndex, FrontBufferIndex))
            {
                ApplyBufferToComponent(FrontBufferIndex);
                LastDisplayedFrame = CurrentFrameIndex;
                ScrubFrame = CurrentFrameIndex;
            }
        }
        StartPreloadWorker(GetNextFrameIndex(CurrentFrameIndex));
    }
}

void UCSGaussianSequencePlayer::Pause()
{
    bIsPlaying = false;
}

void UCSGaussianSequencePlayer::Stop()
{
    bIsPlaying = false;
    CurrentFrameIndex = GetEffectiveStartFrame();
    LastDisplayedFrame = -1;
    FrameAccumulator = 0.0f;
    bPendingSwap = false;
    StopPreloadWorker();
}

void UCSGaussianSequencePlayer::GoToFrame(int32 Frame)
{
    if (!bSequenceLoaded) return;

    StopPreloadWorker();

    CurrentFrameIndex = FMath::Clamp(Frame, 0, SequenceMetadata.FrameCount - 1);

    if (LoadFrameTextures(CurrentFrameIndex, FrontBufferIndex))
    {
        // Flush to ensure texture RHI resources are ready before applying to proxy
        FlushRenderingCommands();
        ApplyBufferToComponent(FrontBufferIndex);
        LastDisplayedFrame = CurrentFrameIndex;
        ScrubFrame = CurrentFrameIndex;
    }

    if (bIsPlaying)
    {
        StartPreloadWorker(GetNextFrameIndex(CurrentFrameIndex));
    }
}

#if WITH_EDITOR
void UCSGaussianSequencePlayer::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    FName PropertyName = PropertyChangedEvent.GetPropertyName();

    if (PropertyName == GET_MEMBER_NAME_CHECKED(UCSGaussianSequencePlayer, ScrubFrame))
    {
        if (bSequenceLoaded && ScrubFrame != LastScrubFrame)
        {
            ScrubFrame = FMath::Clamp(ScrubFrame, 0, SequenceMetadata.FrameCount - 1);
            GoToFrame(ScrubFrame);
            LastScrubFrame = ScrubFrame;
        }
    }
    else if (PropertyName == GET_MEMBER_NAME_CHECKED(UCSGaussianSequencePlayer, StartFrame)
          || PropertyName == GET_MEMBER_NAME_CHECKED(UCSGaussianSequencePlayer, EndFrame))
    {
        if (bSequenceLoaded)
        {
            // Restart preloading with new range
            StopPreloadWorker();
            StartPreloadWorker(GetEffectiveStartFrame());
        }
    }
    else if (PropertyName == GET_MEMBER_NAME_CHECKED(UCSGaussianSequencePlayer, SequencePath))
    {
        if (!SequencePath.FilePath.IsEmpty())
        {
            LoadSequence(SequencePath.FilePath);
        }
    }
}
#endif

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

// --- Texture Helpers ---

UTexture2D* UCSGaussianSequencePlayer::GetOrCreateReusableTexture(
    UTexture2D*& ExistingTex, int32 Width, int32 Height, int32 Precision)
{
    EPixelFormat Format = (Precision == 0) ? PF_A32B32G32R32F : PF_FloatRGBA;

    if (ExistingTex && ExistingTex->GetSizeX() == Width && ExistingTex->GetSizeY() == Height
        && ExistingTex->GetPixelFormat() == Format)
    {
        return ExistingTex;
    }

    ExistingTex = UTexture2D::CreateTransient(Width, Height, Format);
    if (ExistingTex)
    {
        ExistingTex->Filter = TF_Nearest;
        ExistingTex->SRGB = false;
        ExistingTex->NeverStream = true;
        ExistingTex->UpdateResource();
        TextureGCRoots.AddUnique(ExistingTex);
    }
    return ExistingTex;
}

void UCSGaussianSequencePlayer::UpdateTextureData(UTexture2D* Tex, const uint8* Data, int32 DataSize)
{
    if (!Tex || !Data || DataSize <= 0)
        return;

    FTextureResource* Resource = Tex->GetResource();
    if (!Resource || !Resource->TextureRHI)
    {
        void* MipData = Tex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memcpy(MipData, Data, DataSize);
        Tex->GetPlatformData()->Mips[0].BulkData.Unlock();
        Tex->UpdateResource();
        return;
    }

    int32 Width = Tex->GetSizeX();
    int32 Height = Tex->GetSizeY();
    EPixelFormat Format = Tex->GetPixelFormat();
    int32 BytesPerPixel = (Format == PF_A32B32G32R32F) ? 16 : 8;
    int32 SrcPitch = Width * BytesPerPixel;

    TArray<uint8>* DataCopy = new TArray<uint8>();
    DataCopy->SetNumUninitialized(DataSize);
    FMemory::Memcpy(DataCopy->GetData(), Data, DataSize);

    FTextureRHIRef TextureRHI = Resource->TextureRHI;

    ENQUEUE_RENDER_COMMAND(UpdateCSGaussianTextureRHI)(
        [TextureRHI, DataCopy, SrcPitch, Height](FRHICommandListImmediate& RHICmdList)
        {
            FUpdateTextureRegion2D Region(0, 0, 0, 0, TextureRHI->GetSizeX(), Height);
            RHICmdList.UpdateTexture2D(TextureRHI, 0, Region, SrcPitch, DataCopy->GetData());
            delete DataCopy;
        });
}

void UCSGaussianSequencePlayer::UpdateTextureDataMove(UTexture2D* Tex, TArray<uint8>&& Data)
{
    if (!Tex || Data.Num() <= 0)
        return;

    FTextureResource* Resource = Tex->GetResource();
    if (!Resource || !Resource->TextureRHI)
    {
        void* MipData = Tex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memcpy(MipData, Data.GetData(), Data.Num());
        Tex->GetPlatformData()->Mips[0].BulkData.Unlock();
        Tex->UpdateResource();
        return;
    }

    int32 Width = Tex->GetSizeX();
    int32 Height = Tex->GetSizeY();
    EPixelFormat Format = Tex->GetPixelFormat();
    int32 BytesPerPixel = (Format == PF_A32B32G32R32F) ? 16 : 8;
    int32 SrcPitch = Width * BytesPerPixel;

    TArray<uint8>* MovedData = new TArray<uint8>(MoveTemp(Data));
    FTextureRHIRef TextureRHI = Resource->TextureRHI;

    ENQUEUE_RENDER_COMMAND(UpdateCSGaussianTextureRHIMove)(
        [TextureRHI, MovedData, SrcPitch, Height](FRHICommandListImmediate& RHICmdList)
        {
            FUpdateTextureRegion2D Region(0, 0, 0, 0, TextureRHI->GetSizeX(), Height);
            RHICmdList.UpdateTexture2D(TextureRHI, 0, Region, SrcPitch, MovedData->GetData());
            delete MovedData;
        });
}

// --- Double-Buffered Loading ---

bool UCSGaussianSequencePlayer::LoadFrameTextures(int32 FrameIndex, int32 BufferIndex)
{
    if (GSDReader.IsValid())
    {
        // GSD path: decompress frame and upload
        FCSPreloadedFrame Frame = GSDReader->ReadFrame(FrameIndex);
        if (!Frame.bValid) return false;
        UploadToBuffer(BufferIndex, Frame);
        return true;
    }

    // Raw .bin folder path
    FCSSequenceFrameMeta Meta;
    if (!LoadFrameMetadata(FrameIndex, Meta))
        return false;

    FTextureSet& Buf = TextureBuffers[BufferIndex];
    int32 W = Meta.TextureWidth;
    int32 H = Meta.TextureHeight;

    GetOrCreateReusableTexture(Buf.PositionTex, W, H, Meta.PositionPrecision);
    GetOrCreateReusableTexture(Buf.RotationTex, W, H, Meta.RotationPrecision);
    GetOrCreateReusableTexture(Buf.ScaleOpacityTex, W, H, Meta.ScaleOpacityPrecision);
    GetOrCreateReusableTexture(Buf.SH0Tex, W, H, Meta.SHPrecision);

    if (!Buf.PositionTex || !Buf.RotationTex || !Buf.ScaleOpacityTex)
        return false;

    auto LoadAndUpdate = [this](UTexture2D* Tex, const FString& Path)
    {
        TArray<uint8> RawData;
        if (FFileHelper::LoadFileToArray(RawData, *Path))
        {
            UpdateTextureData(Tex, RawData.GetData(), RawData.Num());
        }
    };

    LoadAndUpdate(Buf.PositionTex, FPaths::Combine(Meta.FrameFolderPath, TEXT("position.bin")));
    LoadAndUpdate(Buf.RotationTex, FPaths::Combine(Meta.FrameFolderPath, TEXT("rotation.bin")));
    LoadAndUpdate(Buf.ScaleOpacityTex, FPaths::Combine(Meta.FrameFolderPath, TEXT("scaleOpacity.bin")));
    LoadAndUpdate(Buf.SH0Tex, FPaths::Combine(Meta.FrameFolderPath, TEXT("sh_0.bin")));

    CachedGaussianCount = Meta.GaussianCount;
    return true;
}

void UCSGaussianSequencePlayer::UploadToBuffer(int32 BufferIndex, FCSPreloadedFrame& Frame)
{
    FTextureSet& Buf = TextureBuffers[BufferIndex];
    int32 W = Frame.TextureWidth;
    int32 H = Frame.TextureHeight;

    GetOrCreateReusableTexture(Buf.PositionTex, W, H, Frame.PositionPrecision);
    GetOrCreateReusableTexture(Buf.RotationTex, W, H, Frame.RotationPrecision);
    GetOrCreateReusableTexture(Buf.ScaleOpacityTex, W, H, Frame.ScaleOpacityPrecision);
    GetOrCreateReusableTexture(Buf.SH0Tex, W, H, Frame.SHPrecision);

    UpdateTextureDataMove(Buf.PositionTex, MoveTemp(Frame.PositionData));
    UpdateTextureDataMove(Buf.RotationTex, MoveTemp(Frame.RotationData));
    UpdateTextureDataMove(Buf.ScaleOpacityTex, MoveTemp(Frame.ScaleOpacityData));
    UpdateTextureDataMove(Buf.SH0Tex, MoveTemp(Frame.SH0Data));

    CachedGaussianCount = Frame.GaussianCount;
}

void UCSGaussianSequencePlayer::ApplyBufferToComponent(int32 BufferIndex)
{
    UCSGaussianComponent* Comp = FindGaussianComponent();
    if (!Comp) return;

    FTextureSet& Buf = TextureBuffers[BufferIndex];
    Comp->SetSequenceTextures(
        Buf.PositionTex, Buf.RotationTex,
        Buf.ScaleOpacityTex, Buf.SH0Tex,
        CachedGaussianCount);
}

// --- Ring Buffer Preload Worker ---

int32 UCSGaussianSequencePlayer::GetNextFrameIndex(int32 Current) const
{
    int32 EffEnd = GetEffectiveEndFrame();
    int32 Next = Current + 1;
    if (Next > EffEnd)
    {
        Next = (PlaybackMode == ECSPlaybackMode::Loop) ? GetEffectiveStartFrame() : EffEnd;
    }
    return Next;
}

int32 UCSGaussianSequencePlayer::GetEffectiveStartFrame() const
{
    if (StartFrame >= 0 && StartFrame < SequenceMetadata.FrameCount)
        return StartFrame;
    return 0;
}

int32 UCSGaussianSequencePlayer::GetEffectiveEndFrame() const
{
    if (EndFrame >= 0 && EndFrame < SequenceMetadata.FrameCount)
        return EndFrame;
    return SequenceMetadata.FrameCount - 1;
}

FCSPreloadedFrame UCSGaussianSequencePlayer::LoadFrameDataFromDisk(
    const FString& FrameFolderPath, const FCSSequenceFrameMeta& Meta)
{
    FCSPreloadedFrame Result;
    Result.FrameIndex = Meta.FrameIndex;
    Result.GaussianCount = Meta.GaussianCount;
    Result.TextureWidth = Meta.TextureWidth;
    Result.TextureHeight = Meta.TextureHeight;
    Result.PositionPrecision = Meta.PositionPrecision;
    Result.RotationPrecision = Meta.RotationPrecision;
    Result.ScaleOpacityPrecision = Meta.ScaleOpacityPrecision;
    Result.SHPrecision = Meta.SHPrecision;

    bool bOk = true;
    bOk &= FFileHelper::LoadFileToArray(Result.PositionData, *FPaths::Combine(FrameFolderPath, TEXT("position.bin")));
    bOk &= FFileHelper::LoadFileToArray(Result.RotationData, *FPaths::Combine(FrameFolderPath, TEXT("rotation.bin")));
    bOk &= FFileHelper::LoadFileToArray(Result.ScaleOpacityData, *FPaths::Combine(FrameFolderPath, TEXT("scaleOpacity.bin")));
    bOk &= FFileHelper::LoadFileToArray(Result.SH0Data, *FPaths::Combine(FrameFolderPath, TEXT("sh_0.bin")));

    Result.bValid = bOk;
    return Result;
}

void UCSGaussianSequencePlayer::StartPreloadWorker(int32 InStartFrame)
{
    StopPreloadWorker();

    PreloadNextFrame = InStartFrame;
    bPreloadWorkerRunning = true;

    int32 EffStart = GetEffectiveStartFrame();
    int32 EffEnd = GetEffectiveEndFrame();
    bool bLoop = (PlaybackMode == ECSPlaybackMode::Loop);

    if (GSDReader.IsValid())
    {
        // GSD path: read frames from compressed single file
        TSharedPtr<FCSGSDReader> ReaderRef = GSDReader;
        int32 FrameRange = FMath::Max(EffEnd - EffStart + 1, 1);
        PreloadWorkerFuture = Async(EAsyncExecution::Thread,
            [this, ReaderRef, EffStart, EffEnd, FrameRange, bLoop, PreloadStart = InStartFrame]()
            {
                int32 NextFrame = PreloadStart;
                while (bPreloadWorkerRunning)
                {
                    // Stay near the playhead — if preloader drifted too far, jump back
                    int32 PlayHead = CurrentFrameIndex;
                    int32 Dist = ((NextFrame - PlayHead) % FrameRange + FrameRange) % FrameRange;
                    if (Dist > PRELOAD_RING_SIZE)
                    {
                        NextFrame = PlayHead;
                        continue;
                    }

                    int32 RingSize;
                    {
                        FScopeLock Lock(&PreloadLock);
                        RingSize = PreloadRing.Num();
                    }
                    if (RingSize >= PRELOAD_RING_SIZE)
                    {
                        FPlatformProcess::Sleep(0.002f);
                        continue;
                    }

                    bool bAlreadyLoaded = false;
                    {
                        FScopeLock Lock(&PreloadLock);
                        for (const auto& F : PreloadRing)
                        {
                            if (F.FrameIndex == NextFrame) { bAlreadyLoaded = true; break; }
                        }
                    }

                    if (!bAlreadyLoaded)
                    {
                        FCSPreloadedFrame Frame = ReaderRef->ReadFrame(NextFrame);
                        if (Frame.bValid && bPreloadWorkerRunning)
                        {
                            FScopeLock Lock(&PreloadLock);
                            PreloadRing.Add(MoveTemp(Frame));
                        }
                    }

                    // Skip frames based on playback speed so preloader
                    // keeps up at high speed (e.g., speed 3 → load every 3rd frame)
                    int32 Step = FMath::Max(1, FMath::FloorToInt32(PlaybackSpeed));
                    NextFrame += Step;
                    if (NextFrame > EffEnd)
                    {
                        if (bLoop)
                        {
                            NextFrame = EffStart + (NextFrame - EffEnd - 1) % FrameRange;
                        }
                        else
                        {
                            // Once mode: all frames loaded, sleep until stopped
                            while (bPreloadWorkerRunning)
                                FPlatformProcess::Sleep(0.05f);
                            return;
                        }
                    }
                }
            });
    }
    else
    {
        // Raw .bin folder path
        TMap<int32, FCSSequenceFrameMeta> MetaCacheCopy = FrameMetaCache;
        int32 FrameRange = FMath::Max(EffEnd - EffStart + 1, 1);
        PreloadWorkerFuture = Async(EAsyncExecution::Thread,
            [this, MetaCacheCopy, EffStart, EffEnd, FrameRange, bLoop, PreloadStart = InStartFrame]()
            {
                int32 NextFrame = PreloadStart;
                while (bPreloadWorkerRunning)
                {
                    // Stay near the playhead — if preloader drifted too far, jump back
                    int32 PlayHead = CurrentFrameIndex;
                    int32 Dist = ((NextFrame - PlayHead) % FrameRange + FrameRange) % FrameRange;
                    if (Dist > PRELOAD_RING_SIZE)
                    {
                        NextFrame = PlayHead;
                        continue;
                    }

                    int32 RingSize;
                    {
                        FScopeLock Lock(&PreloadLock);
                        RingSize = PreloadRing.Num();
                    }
                    if (RingSize >= PRELOAD_RING_SIZE)
                    {
                        FPlatformProcess::Sleep(0.002f);
                        continue;
                    }

                    bool bAlreadyLoaded = false;
                    {
                        FScopeLock Lock(&PreloadLock);
                        for (const auto& F : PreloadRing)
                        {
                            if (F.FrameIndex == NextFrame) { bAlreadyLoaded = true; break; }
                        }
                    }

                    if (!bAlreadyLoaded)
                    {
                        const FCSSequenceFrameMeta* Meta = MetaCacheCopy.Find(NextFrame);
                        if (Meta)
                        {
                            FCSPreloadedFrame Frame = LoadFrameDataFromDisk(Meta->FrameFolderPath, *Meta);
                            if (Frame.bValid && bPreloadWorkerRunning)
                            {
                                FScopeLock Lock(&PreloadLock);
                                PreloadRing.Add(MoveTemp(Frame));
                            }
                        }
                    }

                    // Skip frames based on playback speed so preloader
                    // keeps up at high speed (e.g., speed 3 → load every 3rd frame)
                    int32 Step = FMath::Max(1, FMath::FloorToInt32(PlaybackSpeed));
                    NextFrame += Step;
                    if (NextFrame > EffEnd)
                    {
                        if (bLoop)
                        {
                            NextFrame = EffStart + (NextFrame - EffEnd - 1) % FrameRange;
                        }
                        else
                        {
                            // Once mode: all frames loaded, sleep until stopped
                            while (bPreloadWorkerRunning)
                                FPlatformProcess::Sleep(0.05f);
                            return;
                        }
                    }
                }
            });
    }
}

void UCSGaussianSequencePlayer::StopPreloadWorker()
{
    bPreloadWorkerRunning = false;
    if (PreloadWorkerFuture.IsValid())
    {
        PreloadWorkerFuture.Wait();
        PreloadWorkerFuture = {};
    }
    FScopeLock Lock(&PreloadLock);
    PreloadRing.Empty();
}

FCSPreloadedFrame* UCSGaussianSequencePlayer::FindInRing(int32 FrameIndex)
{
    FScopeLock Lock(&PreloadLock);
    for (auto& F : PreloadRing)
    {
        if (F.FrameIndex == FrameIndex)
            return &F;
    }
    return nullptr;
}
