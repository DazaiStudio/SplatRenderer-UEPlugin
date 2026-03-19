#include "CSGSDReader.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Compression.h"

FCSGSDReader::~FCSGSDReader()
{
    Close();
}

bool FCSGSDReader::Open(const FString& FilePath)
{
    Close();

    FileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*FilePath);
    if (!FileHandle)
    {
        UE_LOG(LogTemp, Error, TEXT("CSGaussian: Failed to open GSD file: %s"), *FilePath);
        return false;
    }

    // Read magic
    char Magic[4];
    if (!FileHandle->Read(reinterpret_cast<uint8*>(Magic), 4) ||
        FMemory::Memcmp(Magic, "GSD1", 4) != 0)
    {
        UE_LOG(LogTemp, Error, TEXT("CSGaussian: Invalid GSD magic"));
        Close();
        return false;
    }

    // Read header length
    uint32 HeaderLen = 0;
    if (!FileHandle->Read(reinterpret_cast<uint8*>(&HeaderLen), 4))
    {
        UE_LOG(LogTemp, Error, TEXT("CSGaussian: Failed to read GSD header length"));
        Close();
        return false;
    }

    // Read JSON header
    TArray<uint8> HeaderBytes;
    HeaderBytes.SetNumUninitialized(HeaderLen);
    if (!FileHandle->Read(HeaderBytes.GetData(), HeaderLen))
    {
        UE_LOG(LogTemp, Error, TEXT("CSGaussian: Failed to read GSD header"));
        Close();
        return false;
    }

    FString HeaderJson = FString(HeaderLen, reinterpret_cast<const ANSICHAR*>(HeaderBytes.GetData()));

    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(HeaderJson);
    if (!FJsonSerializer::Deserialize(Reader, Json))
    {
        UE_LOG(LogTemp, Error, TEXT("CSGaussian: Failed to parse GSD header JSON"));
        Close();
        return false;
    }

    // Parse global metadata
    SequenceName = Json->GetStringField(TEXT("sequenceName"));
    FrameCount = Json->GetIntegerField(TEXT("frameCount"));
    TargetFPS = (float)Json->GetNumberField(TEXT("targetFPS"));
    SHDegree = Json->GetIntegerField(TEXT("shDegree"));
    TextureWidth = Json->GetIntegerField(TEXT("textureWidth"));
    TextureHeight = Json->GetIntegerField(TEXT("textureHeight"));
    GaussianCount = Json->GetIntegerField(TEXT("gaussianCount"));
    PositionPrecision = Json->GetIntegerField(TEXT("positionPrecision"));
    RotationPrecision = Json->GetIntegerField(TEXT("rotationPrecision"));
    ScaleOpacityPrecision = Json->GetIntegerField(TEXT("scaleOpacityPrecision"));
    SHPrecision = Json->GetIntegerField(TEXT("shPrecision"));

    // Parse frame entries and compute file offsets
    const TArray<TSharedPtr<FJsonValue>>& FramesArray = Json->GetArrayField(TEXT("frames"));
    int64 CurrentOffset = 8 + (int64)HeaderLen; // After magic(4) + headerLen(4) + header

    FrameEntries.SetNum(FramesArray.Num());
    for (int32 i = 0; i < FramesArray.Num(); i++)
    {
        const TSharedPtr<FJsonObject>& FrameObj = FramesArray[i]->AsObject();
        FCSGSDFrameEntry& Entry = FrameEntries[i];
        Entry.CompressedSize = FrameObj->GetIntegerField(TEXT("compressedSize"));
        // Each frame: [4B size prefix] [CompressedSize bytes of LZ4 data]
        Entry.FileOffset = CurrentOffset + 4; // Skip the 4-byte prefix, point to LZ4 data
        CurrentOffset += 4 + Entry.CompressedSize;
    }

    UE_LOG(LogTemp, Log, TEXT("CSGaussian: Opened GSD '%s' — %d frames, %dx%d, %d gaussians"),
        *SequenceName, FrameCount, TextureWidth, TextureHeight, GaussianCount);

    return true;
}

void FCSGSDReader::Close()
{
    if (FileHandle)
    {
        delete FileHandle;
        FileHandle = nullptr;
    }
    FrameEntries.Empty();
    FrameCount = 0;
}

int32 FCSGSDReader::GetBytesPerPixel(int32 Precision) const
{
    return (Precision == 0) ? 16 : 8; // 32-bit RGBA = 16 bpp, 16-bit RGBA = 8 bpp
}

int32 FCSGSDReader::GetRawFrameSize() const
{
    int32 Pixels = TextureWidth * TextureHeight;
    return Pixels * (
        GetBytesPerPixel(PositionPrecision) +
        GetBytesPerPixel(RotationPrecision) +
        GetBytesPerPixel(ScaleOpacityPrecision) +
        GetBytesPerPixel(SHPrecision));
}

void FCSGSDReader::ByteUnshuffle(const uint8* Src, uint8* Dst, int32 PixelCount, int32 BytesPerPixel)
{
    // Reverse of byte-shuffle: input has B groups of N bytes (same byte position grouped)
    // Output restores pixel-interleaved layout
    for (int32 b = 0; b < BytesPerPixel; b++)
    {
        const uint8* Group = Src + b * PixelCount;
        for (int32 p = 0; p < PixelCount; p++)
        {
            Dst[p * BytesPerPixel + b] = Group[p];
        }
    }
}

FCSPreloadedFrame FCSGSDReader::ReadFrame(int32 FrameIndex)
{
    FCSPreloadedFrame Result;
    Result.FrameIndex = FrameIndex;

    if (FrameIndex < 0 || FrameIndex >= FrameEntries.Num())
    {
        UE_LOG(LogTemp, Error, TEXT("CSGaussian: GSD frame %d out of range"), FrameIndex);
        return Result;
    }

    const FCSGSDFrameEntry& Entry = FrameEntries[FrameIndex];

    // Read compressed data from file (thread-safe)
    TArray<uint8> CompressedData;
    CompressedData.SetNumUninitialized(Entry.CompressedSize);
    {
        FScopeLock Lock(&FileLock);
        if (!FileHandle)
            return Result;

        FileHandle->Seek(Entry.FileOffset);
        if (!FileHandle->Read(CompressedData.GetData(), Entry.CompressedSize))
        {
            UE_LOG(LogTemp, Error, TEXT("CSGaussian: Failed to read GSD frame %d"), FrameIndex);
            return Result;
        }
    }

    // LZ4 decompress via UE FCompression API
    int32 RawSize = GetRawFrameSize();
    TArray<uint8> RawData;
    RawData.SetNumUninitialized(RawSize);

    bool bDecompressOk = FCompression::UncompressMemory(
        NAME_LZ4,
        RawData.GetData(), (int64)RawSize,
        CompressedData.GetData(), (int64)Entry.CompressedSize);

    if (!bDecompressOk)
    {
        UE_LOG(LogTemp, Error, TEXT("CSGaussian: LZ4 decompression failed for frame %d"), FrameIndex);
        return Result;
    }

    // Byte-unshuffle and split into per-texture arrays
    int32 Pixels = TextureWidth * TextureHeight;
    int32 PosBPP = GetBytesPerPixel(PositionPrecision);
    int32 RotBPP = GetBytesPerPixel(RotationPrecision);
    int32 ScaBPP = GetBytesPerPixel(ScaleOpacityPrecision);
    int32 ShBPP = GetBytesPerPixel(SHPrecision);

    int32 PosSize = Pixels * PosBPP;
    int32 RotSize = Pixels * RotBPP;
    int32 ScaSize = Pixels * ScaBPP;
    int32 ShSize = Pixels * ShBPP;

    // Data layout in decompressed buffer: position | rotation | scaleOpacity | sh_0
    // Each section is byte-shuffled
    const uint8* PosShuffled = RawData.GetData();
    const uint8* RotShuffled = PosShuffled + PosSize;
    const uint8* ScaShuffled = RotShuffled + RotSize;
    const uint8* ShShuffled = ScaShuffled + ScaSize;

    Result.PositionData.SetNumUninitialized(PosSize);
    Result.RotationData.SetNumUninitialized(RotSize);
    Result.ScaleOpacityData.SetNumUninitialized(ScaSize);
    Result.SH0Data.SetNumUninitialized(ShSize);

    ByteUnshuffle(PosShuffled, Result.PositionData.GetData(), Pixels, PosBPP);
    ByteUnshuffle(RotShuffled, Result.RotationData.GetData(), Pixels, RotBPP);
    ByteUnshuffle(ScaShuffled, Result.ScaleOpacityData.GetData(), Pixels, ScaBPP);
    ByteUnshuffle(ShShuffled, Result.SH0Data.GetData(), Pixels, ShBPP);

    Result.GaussianCount = GaussianCount;
    Result.TextureWidth = TextureWidth;
    Result.TextureHeight = TextureHeight;
    Result.PositionPrecision = PositionPrecision;
    Result.RotationPrecision = RotationPrecision;
    Result.ScaleOpacityPrecision = ScaleOpacityPrecision;
    Result.SHPrecision = SHPrecision;
    Result.bValid = true;

    return Result;
}
