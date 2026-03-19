#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"

namespace CSGaussian
{

struct FCSPLYResult
{
    UTexture2D* PositionTexture = nullptr;
    UTexture2D* Cov1Texture = nullptr;          // (cov00, cov01, cov02, cov11)
    UTexture2D* Cov2OpacityTexture = nullptr;   // (cov12, cov22, unused, opacity)
    UTexture2D* SH0Texture = nullptr;
    int32 GaussianCount = 0;
    int32 TextureWidth = 0;
};

/**
 * Loads a standard 3DGS PLY file and creates GPU textures.
 * Handles coordinate conversion (COLMAP → UE), activation functions,
 * and texture packing. Self-contained, no external plugin dependency.
 */
class FCSPLYLoader
{
public:
    /**
     * Load a PLY file and create textures.
     * @param FilePath Absolute path to .ply file
     * @param OutResult Receives created textures and counts
     * @param Outer UObject outer for texture creation (typically the component)
     * @return true if successful
     */
    static bool LoadPLY(const FString& FilePath, FCSPLYResult& OutResult, UObject* Outer = nullptr);

private:
    struct FPLYProperty
    {
        FString Name;
        int32 Size = 0;
        int32 Offset = 0;
    };

    struct FPLYHeader
    {
        int32 VertexCount = 0;
        int32 VertexStride = 0;
        TArray<FPLYProperty> Properties;
        TMap<FString, int32> PropertyIndex;
        int64 DataOffset = 0; // byte offset where binary data starts
    };

    static bool ParseHeader(const TArray<uint8>& FileData, FPLYHeader& OutHeader);
    static int32 GetTypeSize(const FString& TypeName);

    static float ReadFloat(const uint8* VertexPtr, const FPLYHeader& Header, const FString& PropName, float Default = 0.f);
};

} // namespace CSGaussian
