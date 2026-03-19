#include "CSGaussianSequenceActor.h"
#include "Misc/FileHelper.h"

ACSGaussianSequenceActor::ACSGaussianSequenceActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;

    GaussianComponent = CreateDefaultSubobject<UCSGaussianComponent>(TEXT("GaussianComponent"));
    RootComponent = GaussianComponent;

    SequencePlayer = CreateDefaultSubobject<UCSGaussianSequencePlayer>(TEXT("SequencePlayer"));

    AudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("AudioComponent"));
    AudioComponent->SetupAttachment(RootComponent);
    AudioComponent->bAutoActivate = false;

    CropBoxComponent = CreateDefaultSubobject<UCSCropBoxComponent>(TEXT("CropBox"));
    CropBoxComponent->SetupAttachment(RootComponent);
    CropBoxComponent->SetBoxExtent(FVector(1000, 1000, 1000));
    CropBoxComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CropBoxComponent->SetHiddenInGame(true);
    CropBoxComponent->ShapeColor = FColor::Green;
    CropBoxComponent->SetLineThickness(4.0f);
    CropBoxComponent->SetVisibility(false);
    CropBoxComponent->TransformUpdated.AddUObject(this, &ACSGaussianSequenceActor::OnCropBoxTransformUpdated);
    CropBoxComponent->OnExtentChanged.BindUObject(this, &ACSGaussianSequenceActor::OnCropBoxExtentChanged);
}

void ACSGaussianSequenceActor::SyncToPlayer()
{
    if (!SequencePlayer) return;

    SequencePlayer->SequencePath = SequencePath;
    SequencePlayer->PlaybackSpeed = PlaybackSpeed;
    SequencePlayer->PlaybackMode = PlaybackMode;
    SequencePlayer->bAutoPlay = bPlaying;
    SequencePlayer->StartFrame = StartFrame;
    SequencePlayer->EndFrame = EndFrame;
}

void ACSGaussianSequenceActor::SyncBoxFromCropParams()
{
    if (!CropBoxComponent || bUpdatingCropBox) return;
    bUpdatingCropBox = true;
    CropBoxComponent->SetRelativeLocation(CropCenter);
    CropBoxComponent->SetBoxExtent(CropSize * 0.5f);
    CropBoxComponent->SetVisibility(bEnableCrop);
    bUpdatingCropBox = false;
}

void ACSGaussianSequenceActor::SyncCropToComponent()
{
    if (!GaussianComponent) return;
    FQuat Rotation = CropBoxComponent ? CropBoxComponent->GetRelativeRotation().Quaternion() : FQuat::Identity;
    GaussianComponent->SetCropVolume(bEnableCrop, CropCenter, CropSize * 0.5f, Rotation);
}

void ACSGaussianSequenceActor::SetBrightness(float InBrightness)
{
    Brightness = FMath::Max(InBrightness, 0.0f);
    if (GaussianComponent)
    {
        GaussianComponent->Brightness = Brightness;
        GaussianComponent->UpdateProxyBrightness();
    }
}

void ACSGaussianSequenceActor::SetCropVolume(bool bEnable, FVector InCenter, FVector InSize, FQuat InRotation)
{
    bEnableCrop = bEnable;
    CropCenter = InCenter;
    CropSize = FVector(FMath::Max(InSize.X, 0.0), FMath::Max(InSize.Y, 0.0), FMath::Max(InSize.Z, 0.0));
    if (CropBoxComponent)
    {
        CropBoxComponent->SetRelativeRotation(InRotation.Rotator());
    }
    SyncBoxFromCropParams();
    SyncCropToComponent();
}

void ACSGaussianSequenceActor::SetAudioVolume(float InVolume)
{
    AudioVolume = FMath::Clamp(InVolume, 0.0f, 1.0f);
    if (AudioComponent)
    {
        AudioComponent->SetVolumeMultiplier(AudioVolume);
    }
}

void ACSGaussianSequenceActor::SetSplatScale(float InScale)
{
    SplatScale = FMath::Max(InScale, 0.0f);
    if (GaussianComponent)
    {
        GaussianComponent->SetSplatScale(SplatScale);
    }
}

void ACSGaussianSequenceActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // Auto-load sequence on first editor tick, then stop ticking
    if (!SequencePath.FilePath.IsEmpty() && SequencePlayer && !SequencePlayer->IsSequenceLoaded())
    {
        SyncToPlayer();
        SequencePlayer->LoadSequence(SequencePath.FilePath);
        if (SequencePlayer->IsSequenceLoaded())
        {
            SequencePlayer->GoToFrame(0);
            if (GaussianComponent)
            {
                GaussianComponent->Brightness = Brightness;
            }
            SyncCropToComponent();
            GaussianComponent->SplatScale = SplatScale;
        }
    }
    SetActorTickEnabled(false);
}

void ACSGaussianSequenceActor::BeginPlay()
{
    SyncToPlayer();

    if (GaussianComponent)
    {
        GaussianComponent->Brightness = Brightness;
    }
    SyncCropToComponent();
    SyncBoxFromCropParams();
    if (GaussianComponent) GaussianComponent->SplatScale = SplatScale;

    if (!AudioPath.FilePath.IsEmpty())
    {
        LoadAudioFromFile(AudioPath.FilePath);
    }

    Super::BeginPlay();

    if (bPlaying && AudioComponent && AudioComponent->Sound)
    {
        AudioComponent->Play();
    }
}

void ACSGaussianSequenceActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (AudioComponent)
    {
        AudioComponent->Stop();
    }
    if (LoadedSoundWave)
    {
        LoadedSoundWave->OnSoundWaveProceduralUnderflow.Unbind();
    }
    AudioPCMData.Empty();

    Super::EndPlay(EndPlayReason);
}

bool ACSGaussianSequenceActor::LoadAudioFromFile(const FString& FilePath)
{
    TArray<uint8> FileData;
    if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("CSGaussian: Failed to load audio file: %s"), *FilePath);
        return false;
    }

    if (FileData.Num() < 44)
    {
        UE_LOG(LogTemp, Error, TEXT("CSGaussian: Audio file too small: %s"), *FilePath);
        return false;
    }

    const uint8* D = FileData.GetData();

    if (D[0] != 'R' || D[1] != 'I' || D[2] != 'F' || D[3] != 'F' ||
        D[8] != 'W' || D[9] != 'A' || D[10] != 'V' || D[11] != 'E')
    {
        UE_LOG(LogTemp, Error, TEXT("CSGaussian: Not a valid WAV file: %s"), *FilePath);
        return false;
    }

    int32 AudioFormat = D[20] | (D[21] << 8); // 1=PCM, 3=IEEE float
    int32 NumChannels = D[22] | (D[23] << 8);
    int32 SampleRate = D[24] | (D[25] << 8) | (D[26] << 16) | (D[27] << 24);
    int32 BitsPerSample = D[34] | (D[35] << 8);

    const uint8* RawData = nullptr;
    int32 RawDataSize = 0;
    int32 Offset = 12;
    while (Offset + 8 <= FileData.Num())
    {
        int32 ChunkSize = D[Offset+4] | (D[Offset+5] << 8) | (D[Offset+6] << 16) | (D[Offset+7] << 24);
        if (D[Offset] == 'd' && D[Offset+1] == 'a' && D[Offset+2] == 't' && D[Offset+3] == 'a')
        {
            RawData = D + Offset + 8;
            RawDataSize = FMath::Min(ChunkSize, FileData.Num() - Offset - 8);
            break;
        }
        Offset += 8 + ChunkSize;
        if (ChunkSize % 2 != 0) Offset++;
    }

    if (!RawData || RawDataSize <= 0)
    {
        UE_LOG(LogTemp, Error, TEXT("CSGaussian: No 'data' chunk in WAV: %s"), *FilePath);
        return false;
    }

    // Convert to 16-bit PCM (USoundWaveProcedural requires 16-bit)
    if (AudioFormat == 1 && BitsPerSample == 16)
    {
        AudioPCMData.SetNumUninitialized(RawDataSize);
        FMemory::Memcpy(AudioPCMData.GetData(), RawData, RawDataSize);
    }
    else if (AudioFormat == 1 && BitsPerSample == 24)
    {
        int32 NumSamples = RawDataSize / 3;
        AudioPCMData.SetNumUninitialized(NumSamples * 2);
        for (int32 i = 0; i < NumSamples; i++)
        {
            AudioPCMData[i * 2]     = RawData[i * 3 + 1];
            AudioPCMData[i * 2 + 1] = RawData[i * 3 + 2];
        }
    }
    else if (AudioFormat == 3 && BitsPerSample == 32)
    {
        int32 NumSamples = RawDataSize / 4;
        AudioPCMData.SetNumUninitialized(NumSamples * 2);
        int16* OutPtr = reinterpret_cast<int16*>(AudioPCMData.GetData());
        const float* InPtr = reinterpret_cast<const float*>(RawData);
        for (int32 i = 0; i < NumSamples; i++)
        {
            float Clamped = FMath::Clamp(InPtr[i], -1.0f, 1.0f);
            OutPtr[i] = (int16)(Clamped * 32767.f);
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("CSGaussian: Unsupported WAV format %d, %d-bit: %s"), AudioFormat, BitsPerSample, *FilePath);
        return false;
    }

    int32 NumSamples16 = AudioPCMData.Num() / 2;
    AudioDuration = (float)NumSamples16 / (float)(SampleRate * NumChannels);

    USoundWaveProcedural* SoundWave = NewObject<USoundWaveProcedural>(this);
    SoundWave->SetSampleRate(SampleRate);
    SoundWave->NumChannels = NumChannels;
    SoundWave->Duration = INDEFINITELY_LOOPING_DURATION;
    SoundWave->SoundGroup = SOUNDGROUP_Default;
    SoundWave->bLooping = true;

    SoundWave->QueueAudio(AudioPCMData.GetData(), AudioPCMData.Num());

    SoundWave->OnSoundWaveProceduralUnderflow.BindLambda(
        [this](USoundWaveProcedural* Wave, int32 SamplesNeeded)
        {
            if (bPlaying && AudioPCMData.Num() > 0)
            {
                Wave->QueueAudio(AudioPCMData.GetData(), AudioPCMData.Num());
            }
        });

    LoadedSoundWave = SoundWave;

    AudioComponent->Stop();
    AudioComponent->SetSound(SoundWave);
    AudioComponent->SetVolumeMultiplier(AudioVolume);

    UE_LOG(LogTemp, Log, TEXT("CSGaussian: Loaded audio — %.2fs, %d ch, %d Hz, %d-bit"),
        AudioDuration, NumChannels, SampleRate, BitsPerSample);

    return true;
}

void ACSGaussianSequenceActor::SyncAudioToPlayback()
{
    if (!AudioComponent || !LoadedSoundWave) return;

    AudioComponent->SetPitchMultiplier(PlaybackSpeed);

    if (bPlaying)
    {
        if (!AudioComponent->IsPlaying())
        {
            LoadedSoundWave->ResetAudio();
            LoadedSoundWave->QueueAudio(AudioPCMData.GetData(), AudioPCMData.Num());
            AudioComponent->Play();
        }
    }
    else
    {
        AudioComponent->Stop();
    }
}

#if WITH_EDITOR
void ACSGaussianSequenceActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    FName MemberName = PropertyChangedEvent.MemberProperty
        ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

    if (MemberName == GET_MEMBER_NAME_CHECKED(ACSGaussianSequenceActor, SequencePath))
    {
        SyncToPlayer();
        if (!SequencePath.FilePath.IsEmpty())
        {
            SequencePlayer->LoadSequence(SequencePath.FilePath);
            if (SequencePlayer->IsSequenceLoaded())
            {
                SequencePlayer->GoToFrame(0);
                ScrubFrame = 0;
            }
        }
    }
    else if (MemberName == GET_MEMBER_NAME_CHECKED(ACSGaussianSequenceActor, bPlaying))
    {
        SyncToPlayer();
        if (bPlaying)
        {
            if (!SequencePlayer->IsSequenceLoaded() && !SequencePath.FilePath.IsEmpty())
            {
                SequencePlayer->LoadSequence(SequencePath.FilePath);
            }
            if (!LoadedSoundWave && !AudioPath.FilePath.IsEmpty())
            {
                LoadAudioFromFile(AudioPath.FilePath);
            }
            SequencePlayer->Play();
            SyncAudioToPlayback();
        }
        else
        {
            SequencePlayer->Pause();
            SyncAudioToPlayback();
        }
    }
    else if (MemberName == GET_MEMBER_NAME_CHECKED(ACSGaussianSequenceActor, ScrubFrame))
    {
        if (SequencePlayer->IsSequenceLoaded())
        {
            ScrubFrame = FMath::Clamp(ScrubFrame, 0, SequencePlayer->GetFrameCount() - 1);
            SequencePlayer->GoToFrame(ScrubFrame);
        }
    }
    else if (MemberName == GET_MEMBER_NAME_CHECKED(ACSGaussianSequenceActor, PlaybackSpeed) ||
             MemberName == GET_MEMBER_NAME_CHECKED(ACSGaussianSequenceActor, PlaybackMode) ||
             MemberName == GET_MEMBER_NAME_CHECKED(ACSGaussianSequenceActor, StartFrame) ||
             MemberName == GET_MEMBER_NAME_CHECKED(ACSGaussianSequenceActor, EndFrame))
    {
        SyncToPlayer();
        if (MemberName == GET_MEMBER_NAME_CHECKED(ACSGaussianSequenceActor, PlaybackSpeed))
        {
            SyncAudioToPlayback();
        }
    }
    else if (MemberName == GET_MEMBER_NAME_CHECKED(ACSGaussianSequenceActor, Brightness))
    {
        if (GaussianComponent)
        {
            GaussianComponent->Brightness = Brightness;
            GaussianComponent->UpdateProxyBrightness();
        }
    }
    else if (MemberName == GET_MEMBER_NAME_CHECKED(ACSGaussianSequenceActor, bEnableCrop) ||
             MemberName == GET_MEMBER_NAME_CHECKED(ACSGaussianSequenceActor, CropCenter) ||
             MemberName == GET_MEMBER_NAME_CHECKED(ACSGaussianSequenceActor, CropSize))
    {
        SyncBoxFromCropParams();
        SyncCropToComponent();
    }
    else if (MemberName == GET_MEMBER_NAME_CHECKED(ACSGaussianSequenceActor, AudioPath))
    {
        if (!AudioPath.FilePath.IsEmpty())
        {
            LoadAudioFromFile(AudioPath.FilePath);
        }
    }
    else if (MemberName == GET_MEMBER_NAME_CHECKED(ACSGaussianSequenceActor, AudioVolume))
    {
        if (AudioComponent)
        {
            AudioComponent->SetVolumeMultiplier(AudioVolume);
        }
    }
    else if (MemberName == GET_MEMBER_NAME_CHECKED(ACSGaussianSequenceActor, SplatScale))
    {
        if (GaussianComponent)
        {
            GaussianComponent->SplatScale = SplatScale;
            GaussianComponent->SetSplatScale(SplatScale);
        }
    }
}

#endif

void ACSGaussianSequenceActor::OnCropBoxTransformUpdated(USceneComponent* Comp, EUpdateTransformFlags Flags, ETeleportType Teleport)
{
    if (Comp != CropBoxComponent || bUpdatingCropBox) return;
    bUpdatingCropBox = true;

    CropCenter = CropBoxComponent->GetRelativeLocation();
    CropSize = CropBoxComponent->GetScaledBoxExtent() * 2.0f;

    // Absorb scale into box extent, keep rotation
    CropBoxComponent->SetBoxExtent(CropSize * 0.5f);
    CropBoxComponent->SetRelativeScale3D(FVector::OneVector);

    SyncCropToComponent();
    bUpdatingCropBox = false;
}

void ACSGaussianSequenceActor::OnCropBoxExtentChanged()
{
    if (bUpdatingCropBox) return;
    bUpdatingCropBox = true;

    CropCenter = CropBoxComponent->GetRelativeLocation();
    CropSize = CropBoxComponent->GetUnscaledBoxExtent() * 2.0f;
    SyncCropToComponent();

    bUpdatingCropBox = false;
}
