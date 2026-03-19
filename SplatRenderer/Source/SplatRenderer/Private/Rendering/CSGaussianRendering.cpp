#include "CSGaussianRendering.h"

#include "GPUSort.h"
#include "RenderGraphUtils.h"
#include "SceneRendering.h"
#include "CSGaussianRenderingUtilities.h"

namespace CSGaussian
{

namespace
{
BEGIN_SHADER_PARAMETER_STRUCT(FCSGPUSortSetupParams, )
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, IndicesUAV)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, Indices2UAV)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, Distances2UAV)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FCSGPUSortExecutionParams, )
    SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, IndicesSRV)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, IndicesUAV)
    SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, Indices2SRV)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, Indices2UAV)
    SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, DistancesSRV)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DistancesUAV)
    SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, Distances2SRV)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, Distances2UAV)
END_SHADER_PARAMETER_STRUCT()
} // namespace

void DispatchParticleFXPass(
    FRHICommandList& RHICmdList,
    FCSGaussianSceneProxy* Proxy,
    float TimeSeconds,
    float DeltaTime)
{
    // ParticleFX disabled in release build
}

FRDGPassRef DispatchDistancePass(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    FCSGaussianSceneProxy* Proxy,
    FRDGBufferRef Indices,
    FRDGBufferRef Distances)
{
    check(Proxy);

    const FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
    TShaderRef<FCSGaussianDistanceCS> Shader =
        GlobalShaderMap->GetShader<FCSGaussianDistanceCS>();

    FCSGaussianDistanceCS::FParameters* Params =
        GraphBuilder.AllocParameters<FCSGaussianDistanceCS::FParameters>();
    Params->local_to_clip = FMatrix44f(Proxy->GetLocalToWorld() * CSGaussianGetViewProjectionMatrix(View));
    Params->num_splats = Proxy->GetNumSplats();
    Params->texture_width = Proxy->GetTextureWidth();
    Params->enable_crop = Proxy->IsCropEnabled() ? 1 : 0;
    Params->crop_center = Proxy->GetCropCenter();
    Params->crop_half_size = Proxy->GetCropHalfSize();
    Params->crop_inv_rotation = Proxy->GetCropInvRotation();
    Params->PositionTexture = Proxy->GetPositionTextureRHI();
    Params->displaced_positions = Proxy->GetDisplacedPositionsSRV();
    Params->use_displaced = Proxy->IsParticleFXEnabled() ? 1 : 0;
    Params->indices = Proxy->GetIndicesUAV();
    Params->distances = GraphBuilder.CreateUAV(Distances, PF_R16_UINT);
    Params->visible_count = Proxy->GetVisibleCountUAV();

    FUnorderedAccessViewRHIRef VisCountUAV = Proxy->GetVisibleCountUAV();

    return GraphBuilder.AddPass(
        RDG_EVENT_NAME("CSGaussian: Distances %s", *Proxy->GetName()),
        Params,
        ERDGPassFlags::Compute,
        [Shader, Params, VisCountUAV, NumSplats = Proxy->GetNumSplats()](FRHIComputeCommandList& RHICmdList)
        {
            // Clear visible count to 0 before dispatch
            static_cast<FRHICommandList&>(RHICmdList).ClearUAVUint(
                VisCountUAV, FUintVector4(0, 0, 0, 0));

            FComputeShaderUtils::Dispatch(
                static_cast<FRHICommandList&>(RHICmdList),
                Shader,
                *Params,
                FIntVector(CSGaussianCalcThreadGroups(NumSplats), 1, 1));
        });
}

FRDGPassRef DispatchTransformPass(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    FCSGaussianSceneProxy* Proxy)
{
    check(Proxy);

    const FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
    TShaderRef<FCSGaussianTransformCS> Shader =
        GlobalShaderMap->GetShader<FCSGaussianTransformCS>();

    FCSGaussianTransformCS::FParameters* Params =
        GraphBuilder.AllocParameters<FCSGaussianTransformCS::FParameters>();
    Params->local_to_view = FMatrix44f(Proxy->GetLocalToWorld() * CSGaussianGetViewMatrix(View));
    Params->two_focal_length = 2.f * CSGaussianGetFocalLength(View);
    Params->num_splats = Proxy->GetNumSplats();
    Params->texture_width = Proxy->GetTextureWidth();
    Params->use_precomputed_cov = Proxy->UsePrecomputedCov() ? 1 : 0;
    Params->splat_scale = Proxy->GetSplatScale();
    Params->PositionTexture = Proxy->GetPositionTextureRHI();
    Params->displaced_positions = Proxy->GetDisplacedPositionsSRV();
    Params->use_displaced = Proxy->IsParticleFXEnabled() ? 1 : 0;

    // Bind textures for whichever path is active; use Position as dummy for the other
    FTextureRHIRef DummyTex = Proxy->GetPositionTextureRHI();
    if (Proxy->UsePrecomputedCov())
    {
        Params->Cov1Texture = Proxy->GetCov1TextureRHI();
        Params->Cov2OpacityTexture = Proxy->GetCov2OpacityTextureRHI();
        Params->RotationTexture = DummyTex;
        Params->ScaleOpacityTexture = DummyTex;
    }
    else
    {
        Params->RotationTexture = Proxy->GetRotationTextureRHI();
        Params->ScaleOpacityTexture = Proxy->GetScaleOpacityTextureRHI();
        Params->Cov1Texture = DummyTex;
        Params->Cov2OpacityTexture = DummyTex;
    }

    Params->transforms = Proxy->GetTransformsUAV();

    return FComputeShaderUtils::AddPass(
        GraphBuilder,
        RDG_EVENT_NAME("CSGaussian: Transforms %s", *Proxy->GetName()),
        ERDGPassFlags::AsyncCompute,
        Shader,
        Params,
        FIntVector(CSGaussianCalcThreadGroups(Proxy->GetNumSplats()), 1, 1));
}

FRDGPassRef DispatchSortPass(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    FCSGaussianSceneProxy* Proxy,
    FRDGBufferRef Indices,
    FRDGBufferRef Distances)
{
    check(Proxy);

    uint32 NumSplats = Proxy->GetNumSplats();

    FRDGBufferDesc IndexDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumSplats);
    FRDGBuffer* Indices2 = GraphBuilder.CreateBuffer(IndexDesc, TEXT("Indices2"));

    FRDGBufferDesc DistanceDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint16), NumSplats);
    FRDGBuffer* Distances2 = GraphBuilder.CreateBuffer(DistanceDesc, TEXT("Distances2"));

    FCSGPUSortSetupParams* SetupParams =
        GraphBuilder.AllocParameters<FCSGPUSortSetupParams>();
    SetupParams->IndicesUAV = GraphBuilder.CreateUAV(Indices, PF_R32_UINT);
    SetupParams->Indices2UAV = GraphBuilder.CreateUAV(Indices2, PF_R32_UINT);
    SetupParams->Distances2UAV = GraphBuilder.CreateUAV(Distances2, PF_R16_UINT);

    GraphBuilder.AddPass(
        RDG_EVENT_NAME("CSGaussian: RDG Producer"),
        SetupParams,
        ERDGPassFlags::Compute,
        [](FRHIComputeCommandList& RHICmdList) {});

    FCSGPUSortExecutionParams* SortParams =
        GraphBuilder.AllocParameters<FCSGPUSortExecutionParams>();
    SortParams->IndicesSRV = GraphBuilder.CreateSRV(Indices, PF_R32_UINT);
    SortParams->IndicesUAV = GraphBuilder.CreateUAV(Indices, PF_R32_UINT);
    SortParams->Indices2SRV = GraphBuilder.CreateSRV(Indices2, PF_R32_UINT);
    SortParams->Indices2UAV = GraphBuilder.CreateUAV(Indices2, PF_R32_UINT);
    SortParams->DistancesSRV = GraphBuilder.CreateSRV(Distances, PF_R16_UINT);
    SortParams->DistancesUAV = GraphBuilder.CreateUAV(Distances, PF_R16_UINT);
    SortParams->Distances2SRV = GraphBuilder.CreateSRV(Distances2, PF_R16_UINT);
    SortParams->Distances2UAV = GraphBuilder.CreateUAV(Distances2, PF_R16_UINT);

    return GraphBuilder.AddPass(
        RDG_EVENT_NAME("CSGaussian: Sort %s", *Proxy->GetName()),
        SortParams,
        ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
        [NumSplats, SortParams,
         SRV = Proxy->GetIndicesSRV(),
         UAV = Proxy->GetIndicesUAV()](FRHIComputeCommandList& RHICmdList)
        {
            FGPUSortBuffers SortBuffers;
            SortBuffers.RemoteKeySRVs[0] = SortParams->DistancesSRV->GetRHI();
            SortBuffers.RemoteKeySRVs[1] = SortParams->Distances2SRV->GetRHI();
            SortBuffers.RemoteKeyUAVs[0] = SortParams->DistancesUAV->GetRHI();
            SortBuffers.RemoteKeyUAVs[1] = SortParams->Distances2UAV->GetRHI();
            SortBuffers.RemoteValueSRVs[0] = SRV;
            SortBuffers.RemoteValueSRVs[1] = SortParams->Indices2SRV->GetRHI();
            SortBuffers.RemoteValueUAVs[0] = UAV;
            SortBuffers.RemoteValueUAVs[1] = SortParams->Indices2UAV->GetRHI();

            int32 ResultIndex = SortGPUBuffers(
                static_cast<FRHICommandList&>(RHICmdList),
                SortBuffers,
                0,
                DepthMask,
                NumSplats,
                GMaxRHIFeatureLevel);
            check(ResultIndex == 0);
        });
}

void DispatchIndirectArgsPass(
    FRHICommandList& RHICmdList,
    FCSGaussianSceneProxy* Proxy)
{
    const FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
    TShaderRef<FCSGaussianIndirectArgsCS> Shader =
        GlobalShaderMap->GetShader<FCSGaussianIndirectArgsCS>();

    FCSGaussianIndirectArgsCS::FParameters Params;
    Params.visible_count = Proxy->GetVisibleCountSRV();

    FRHIViewDesc::FBufferUAV::FInitializer UAVDesc = FRHIViewDesc::CreateBufferUAV();
    UAVDesc.SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(PF_R32_UINT);
    Params.indirect_args = RHICmdList.CreateUnorderedAccessView(
        Proxy->GetIndirectArgsBuffer(), UAVDesc);

    FComputeShaderUtils::Dispatch(RHICmdList, Shader, Params, FIntVector(1, 1, 1));
}

void DrawGaussianSplats(
    FRHICommandList& RHICmdList,
    FCSGaussianRenderDeps* SplatParameters,
    FCSGaussianSceneProxy* Proxy,
    const FSceneView& View)
{
    check(SplatParameters);

    const FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
    TShaderRef<FCSGaussianRenderVS> VertexShader =
        GlobalShaderMap->GetShader<FCSGaussianRenderVS>();
    TShaderRef<FCSGaussianRenderPS> PixelShader =
        GlobalShaderMap->GetShader<FCSGaussianRenderPS>();

    check(View.bIsViewInfo);
    const FIntRect ViewRect = static_cast<const FViewInfo&>(View).ViewRect;
    RHICmdList.SetViewport(
        float(ViewRect.Min.X), float(ViewRect.Min.Y), 0.f,
        float(ViewRect.Max.X), float(ViewRect.Max.Y), 1.f);

    FGraphicsPipelineStateInitializer GraphicsPSOInit;
    GraphicsPSOInit.PrimitiveType = PT_TriangleList;
    GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI =
        PipelineStateCache::GetOrCreateVertexDeclaration({});
    GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
    GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
    GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false>::GetRHI();
    GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
    GraphicsPSOInit.BlendState = TStaticBlendState<
        CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha>::GetRHI();
    RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

    SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
    SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), SplatParameters->VS);
    SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), SplatParameters->PS);

    FBufferRHIRef IndirectArgs = Proxy->GetIndirectArgsBuffer();
    if (IndirectArgs)
    {
        RHICmdList.DrawPrimitiveIndirect(IndirectArgs, 0);
    }
    else
    {
        RHICmdList.DrawPrimitive(0, 2 * Proxy->GetNumSplats(), 1);
    }
}

} // namespace CSGaussian
