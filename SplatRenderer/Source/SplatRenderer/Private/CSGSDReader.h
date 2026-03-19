#pragma once

#include "CoreMinimal.h"
#include "CSGaussianSequencePlayer.h"

struct FCSGSDFrameEntry
{
    int64 FileOffset = 0;
    int32 CompressedSize = 0;
};

class FCSGSDReader
{
public:
    ~FCSGSDReader();

    bool Open(const FString& FilePath);
    void Close();
    bool IsOpen() const { return FileHandle != nullptr; }

    // Thread-safe: reads and decompresses a frame
    FCSPreloadedFrame ReadFrame(int32 FrameIndex);

    // Metadata accessors
    int32 GetFrameCount() const { return FrameCount; }
    float GetTargetFPS() const { return TargetFPS; }
    int32 GetSHDegree() const { return SHDegree; }
    FString GetSequenceName() const { return SequenceName; }
    int32 GetTextureWidth() const { return TextureWidth; }
    int32 GetTextureHeight() const { return TextureHeight; }
    int32 GetGaussianCount() const { return GaussianCount; }

    int32 GetPositionPrecision() const { return PositionPrecision; }
    int32 GetRotationPrecision() const { return RotationPrecision; }
    int32 GetScaleOpacityPrecision() const { return ScaleOpacityPrecision; }
    int32 GetSHPrecision() const { return SHPrecision; }

private:
    static void ByteUnshuffle(const uint8* Src, uint8* Dst, int32 PixelCount, int32 BytesPerPixel);
    int32 GetBytesPerPixel(int32 Precision) const;
    int32 GetRawFrameSize() const;

    IFileHandle* FileHandle = nullptr;
    FCriticalSection FileLock;

    // Global metadata
    FString SequenceName;
    int32 FrameCount = 0;
    float TargetFPS = 30.0f;
    int32 SHDegree = 0;
    int32 TextureWidth = 0;
    int32 TextureHeight = 0;
    int32 GaussianCount = 0;
    int32 PositionPrecision = 0;
    int32 RotationPrecision = 1;
    int32 ScaleOpacityPrecision = 1;
    int32 SHPrecision = 1;

    // Frame index
    TArray<FCSGSDFrameEntry> FrameEntries;
};
