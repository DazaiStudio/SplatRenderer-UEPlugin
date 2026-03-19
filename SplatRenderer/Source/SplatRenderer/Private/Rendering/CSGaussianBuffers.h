#pragma once

#include "RenderResource.h"
#include "RHI.h"
#include "RHIResources.h"
#include "RHICommandList.h"

namespace CSGaussian
{

// GPU-only buffer with both SRV and UAV (for intermediate compute results)
class FCSGaussianGPUBuffer : public FVertexBufferWithSRV
{
public:
    FCSGaussianGPUBuffer() = default;

    FCSGaussianGPUBuffer(uint32 InNumElements, EPixelFormat InFormat)
        : NumElements(InNumElements)
        , Format(InFormat)
    {
    }

    virtual void InitRHI(FRHICommandListBase& RHICmdList) override
    {
        if (NumElements == 0) return;

        uint32 Stride = GPixelFormats[Format].BlockBytes;
        uint32 Size = NumElements * Stride;

        FRHIResourceCreateInfo CreateInfo(TEXT("CSGPUBuffer"));
        VertexBufferRHI = RHICmdList.CreateBuffer(
            Size,
            EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess,
            Stride,
            ERHIAccess::UAVCompute,
            CreateInfo);
        check(VertexBufferRHI);

        FRHIViewDesc::FBufferSRV::FInitializer SRVDesc = FRHIViewDesc::CreateBufferSRV();
        SRVDesc.SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(Format);
        ShaderResourceViewRHI = RHICmdList.CreateShaderResourceView(VertexBufferRHI, SRVDesc);
        check(ShaderResourceViewRHI);

        FRHIViewDesc::FBufferUAV::FInitializer UAVDesc = FRHIViewDesc::CreateBufferUAV();
        UAVDesc.SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(Format);
        UnorderedAccessViewRHI = RHICmdList.CreateUnorderedAccessView(VertexBufferRHI, UAVDesc);
        check(UnorderedAccessViewRHI);
    }

    virtual void ReleaseRHI() override
    {
        UnorderedAccessViewRHI.SafeRelease();
        FVertexBufferWithSRV::ReleaseRHI();
    }

private:
    uint32 NumElements = 0;
    EPixelFormat Format = PF_Unknown;
};

} // namespace CSGaussian
