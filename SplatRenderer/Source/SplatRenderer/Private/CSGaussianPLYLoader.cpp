#include "CSGaussianPLYLoader.h"
#include "Misc/FileHelper.h"
#include "TextureResource.h"

namespace CSGaussian
{

int32 FCSPLYLoader::GetTypeSize(const FString& TypeName)
{
    if (TypeName == TEXT("float") || TypeName == TEXT("int") || TypeName == TEXT("uint"))
        return 4;
    if (TypeName == TEXT("double"))
        return 8;
    if (TypeName == TEXT("short") || TypeName == TEXT("ushort"))
        return 2;
    if (TypeName == TEXT("char") || TypeName == TEXT("uchar"))
        return 1;
    return 0;
}

bool FCSPLYLoader::ParseHeader(const TArray<uint8>& FileData, FPLYHeader& OutHeader)
{
    // Find end_header in the file
    FString HeaderText;
    int32 DataStart = -1;

    for (int32 i = 0; i < FMath::Min(FileData.Num(), 65536); i++)
    {
        HeaderText.AppendChar((char)FileData[i]);

        // Check for end_header\n
        if (HeaderText.EndsWith(TEXT("end_header\n")))
        {
            DataStart = i + 1;
            break;
        }
        // Also handle \r\n
        if (HeaderText.EndsWith(TEXT("end_header\r\n")))
        {
            DataStart = i + 1;
            break;
        }
    }

    if (DataStart < 0)
        return false;

    OutHeader.DataOffset = DataStart;

    // Parse header lines
    TArray<FString> Lines;
    HeaderText.ParseIntoArrayLines(Lines);

    bool bInVertex = false;

    for (const FString& Line : Lines)
    {
        TArray<FString> Tokens;
        Line.ParseIntoArrayWS(Tokens);

        if (Tokens.Num() == 0 || Tokens[0] == TEXT("comment"))
            continue;

        if (Tokens[0] == TEXT("element"))
        {
            if (Tokens.Num() >= 3 && Tokens[1] == TEXT("vertex"))
            {
                OutHeader.VertexCount = FCString::Atoi(*Tokens[2]);
                bInVertex = true;
            }
            else
            {
                bInVertex = false;
            }
        }
        else if (Tokens[0] == TEXT("property") && bInVertex && Tokens.Num() >= 3)
        {
            int32 Size = GetTypeSize(Tokens[1]);
            if (Size == 0)
                return false;

            FPLYProperty Prop;
            Prop.Name = Tokens[2];
            Prop.Size = Size;
            Prop.Offset = OutHeader.VertexStride;

            OutHeader.PropertyIndex.Add(Prop.Name, OutHeader.Properties.Num());
            OutHeader.Properties.Add(Prop);
            OutHeader.VertexStride += Size;
        }
    }

    return OutHeader.VertexCount > 0 && OutHeader.VertexStride > 0;
}

float FCSPLYLoader::ReadFloat(const uint8* VertexPtr, const FPLYHeader& Header, const FString& PropName, float Default)
{
    const int32* IdxPtr = Header.PropertyIndex.Find(PropName);
    if (!IdxPtr)
        return Default;

    const FPLYProperty& Prop = Header.Properties[*IdxPtr];
    if (Prop.Size == 4)
    {
        float Val;
        FMemory::Memcpy(&Val, VertexPtr + Prop.Offset, 4);
        return Val;
    }
    if (Prop.Size == 8)
    {
        double Val;
        FMemory::Memcpy(&Val, VertexPtr + Prop.Offset, 8);
        return (float)Val;
    }
    return Default;
}

bool FCSPLYLoader::LoadPLY(const FString& FilePath, FCSPLYResult& OutResult, UObject* Outer)
{
    TArray<uint8> FileData;
    if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("CSGaussian PLY: Failed to load file: %s"), *FilePath);
        return false;
    }

    FPLYHeader Header;
    if (!ParseHeader(FileData, Header))
    {
        UE_LOG(LogTemp, Error, TEXT("CSGaussian PLY: Failed to parse header: %s"), *FilePath);
        return false;
    }

    const int32 NumGaussians = Header.VertexCount;
    const int64 ExpectedSize = Header.DataOffset + (int64)NumGaussians * Header.VertexStride;
    if (FileData.Num() < ExpectedSize)
    {
        UE_LOG(LogTemp, Error, TEXT("CSGaussian PLY: File truncated. Expected %lld bytes, got %d"), ExpectedSize, FileData.Num());
        return false;
    }

    // Square texture size
    const int32 TexWidth = FMath::CeilToInt(FMath::Sqrt((float)NumGaussians));
    const int32 TexPixels = TexWidth * TexWidth;

    // Allocate texture data (RGBA32F)
    TArray<FLinearColor> PositionData;
    TArray<FLinearColor> Cov1Data;          // (cov00, cov01, cov02, cov11)
    TArray<FLinearColor> Cov2OpacityData;   // (cov12, cov22, unused, opacity)
    TArray<FLinearColor> SH0Data;

    PositionData.SetNumZeroed(TexPixels);
    Cov1Data.SetNumZeroed(TexPixels);
    Cov2OpacityData.SetNumZeroed(TexPixels);
    SH0Data.SetNumZeroed(TexPixels);

    const uint8* DataPtr = FileData.GetData() + Header.DataOffset;

    for (int32 i = 0; i < NumGaussians; i++)
    {
        const uint8* V = DataPtr + (int64)i * Header.VertexStride;

        // Position: COLMAP (x,y,z) → UE (z, x, -y)
        float px = ReadFloat(V, Header, TEXT("x"));
        float py = ReadFloat(V, Header, TEXT("y"));
        float pz = ReadFloat(V, Header, TEXT("z"));
        PositionData[i] = FLinearColor(pz, px, -py, 0.f);

        // Rotation quaternion: rot_0=w, rot_1=x, rot_2=y, rot_3=z (COLMAP convention)
        float qw = ReadFloat(V, Header, TEXT("rot_0"));
        float qx = ReadFloat(V, Header, TEXT("rot_1"));
        float qy = ReadFloat(V, Header, TEXT("rot_2"));
        float qz = ReadFloat(V, Header, TEXT("rot_3"));
        // Normalize
        float qlen = FMath::Sqrt(qw*qw + qx*qx + qy*qy + qz*qz);
        if (qlen > 1e-8f) { qw /= qlen; qx /= qlen; qy /= qlen; qz /= qlen; }

        // Scale: exp(raw) * 100 (empirical scaling factor for rendering)
        float s0 = FMath::Exp(ReadFloat(V, Header, TEXT("scale_0"))) * 100.f;
        float s1 = FMath::Exp(ReadFloat(V, Header, TEXT("scale_1"))) * 100.f;
        float s2 = FMath::Exp(ReadFloat(V, Header, TEXT("scale_2"))) * 100.f;

        // Opacity: sigmoid(raw)
        float opRaw = ReadFloat(V, Header, TEXT("opacity"));
        float opacity = 1.f / (1.f + FMath::Exp(-opRaw));

        // Build standard rotation matrix from COLMAP quaternion (column-vector convention)
        float xx = qx*qx, yy = qy*qy, zz = qz*qz;
        float xy = qx*qy, xz = qx*qz, yz = qy*qz;
        float wx = qw*qx, wy = qw*qy, wz = qw*qz;
        // R such that v' = R * v (standard math convention)
        float R[3][3] = {
            {1 - 2*(yy+zz), 2*(xy-wz),     2*(xz+wy)},
            {2*(xy+wz),     1 - 2*(xx+zz), 2*(yz-wx)},
            {2*(xz-wy),     2*(yz+wx),     1 - 2*(xx+yy)}
        };

        // Covariance in COLMAP space: Σ = R * diag(s²) * R^T
        float sc[3] = {s0, s1, s2};
        float CovC[3][3] = {};
        for (int32 ci = 0; ci < 3; ci++)
        {
            for (int32 cj = ci; cj < 3; cj++)
            {
                float sum = 0.f;
                for (int32 k = 0; k < 3; k++)
                    sum += R[ci][k] * sc[k] * sc[k] * R[cj][k];
                CovC[ci][cj] = sum;
                CovC[cj][ci] = sum;
            }
        }

        // Transform covariance from COLMAP (x,y,z) to UE (z,x,-y) space
        // T = [[0,0,1],[1,0,0],[0,-1,0]]
        // Cov_UE = T * CovC * T^T
        float Cov[3][3];
        Cov[0][0] = CovC[2][2];            // (Z,Z)
        Cov[0][1] = CovC[2][0];            // (Z,X)
        Cov[0][2] = -CovC[2][1];           // (Z,-Y)
        Cov[1][0] = Cov[0][1];             // symmetric
        Cov[1][1] = CovC[0][0];            // (X,X)
        Cov[1][2] = -CovC[0][1];           // (X,-Y)
        Cov[2][0] = Cov[0][2];             // symmetric
        Cov[2][1] = Cov[1][2];             // symmetric
        Cov[2][2] = CovC[1][1];            // (-Y,-Y) = (Y,Y)

        // Pack covariance into two textures
        // Cov1: (cov00, cov01, cov02, cov11)
        Cov1Data[i] = FLinearColor(Cov[0][0], Cov[0][1], Cov[0][2], Cov[1][1]);
        // Cov2Opacity: (cov12, cov22, unused, opacity)
        Cov2OpacityData[i] = FLinearColor(Cov[1][2], Cov[2][2], 0.f, opacity);

        // SH0: raw DC coefficients (no activation)
        float dc0 = ReadFloat(V, Header, TEXT("f_dc_0"));
        float dc1 = ReadFloat(V, Header, TEXT("f_dc_1"));
        float dc2 = ReadFloat(V, Header, TEXT("f_dc_2"));
        SH0Data[i] = FLinearColor(dc0, dc1, dc2, 0.f);
    }

    // Create textures with unique names to prevent cross-contamination between actors
    FString UniquePrefix = FString::Printf(TEXT("CSG_%08X_"), FPlatformTime::Cycles());
    auto CreateTex = [TexWidth, Outer, &UniquePrefix](const TArray<FLinearColor>& Data, const TCHAR* Name) -> UTexture2D*
    {
        FName UniqueName = FName(*(UniquePrefix + Name));
        UTexture2D* Tex = UTexture2D::CreateTransient(TexWidth, TexWidth, PF_A32B32G32R32F, UniqueName);
        if (!Tex) return nullptr;

        Tex->Filter = TF_Nearest;
        Tex->SRGB = false;
        Tex->NeverStream = true;
        Tex->CompressionSettings = TC_HDR;

        void* MipData = Tex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memcpy(MipData, Data.GetData(), Data.Num() * sizeof(FLinearColor));
        Tex->GetPlatformData()->Mips[0].BulkData.Unlock();
        Tex->UpdateResource();

        return Tex;
    };

    OutResult.PositionTexture = CreateTex(PositionData, TEXT("CSG_Position"));
    OutResult.Cov1Texture = CreateTex(Cov1Data, TEXT("CSG_Cov1"));
    OutResult.Cov2OpacityTexture = CreateTex(Cov2OpacityData, TEXT("CSG_Cov2Opacity"));
    OutResult.SH0Texture = CreateTex(SH0Data, TEXT("CSG_SH0"));
    OutResult.GaussianCount = NumGaussians;
    OutResult.TextureWidth = TexWidth;

    if (!OutResult.PositionTexture || !OutResult.Cov1Texture ||
        !OutResult.Cov2OpacityTexture || !OutResult.SH0Texture)
    {
        UE_LOG(LogTemp, Error, TEXT("CSGaussian PLY: Failed to create textures"));
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("CSGaussian PLY: Loaded %d gaussians from %s (tex: %dx%d)"),
        NumGaussians, *FilePath, TexWidth, TexWidth);

    return true;
}

} // namespace CSGaussian
