#include "CSGaussianViewExtension.h"
#include "CSGaussianRendering.h"
#include "CSGaussianRenderingUtilities.h"
#include "PostProcess/PostProcessing.h"
#include "StereoRendering.h"

namespace CSGaussian
{

// Minimal param struct for no-op RDG producer pass (sort cache hit)
BEGIN_SHADER_PARAMETER_STRUCT(FCSCacheHitParams, )
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, IndicesUAV)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DistancesUAV)
END_SHADER_PARAMETER_STRUCT()

FCSGaussianViewExtension::FCSGaussianViewExtension(
    const FAutoRegister& AutoRegister)
    : FSceneViewExtensionBase(AutoRegister)
{
    FSceneViewExtensionIsActiveFunctor IsActiveFunctor;
    IsActiveFunctor.IsActiveFunction =
        [](const ISceneViewExtension* Extension,
           const FSceneViewExtensionContext& Context)
    {
        const FCSGaussianViewExtension* Self =
            static_cast<const FCSGaussianViewExtension*>(Extension);
        return TOptional<bool>(Self->Proxies.Num() > 0);
    };
    IsActiveThisFrameFunctions.Add(IsActiveFunctor);
}

void FCSGaussianViewExtension::PreRenderView_RenderThread(
    FRDGBuilder& GraphBuilder, FSceneView& View)
{
    if (IStereoRendering::IsASecondaryView(View))
        return;

    for (auto& Proxy : Proxies)
    {
        if (!Proxy || !Proxy->IsVisible(View))
            continue;

        uint32 NumSplats = Proxy->GetNumSplats();

        // Check if we can reuse the previous frame's sort result
        FMatrix44f LocalToClip = FMatrix44f(Proxy->GetLocalToWorld() * CSGaussianGetViewProjectionMatrix(View));

        // Always create RDG buffers (needed by render pass for dependency tracking)
        FRDGBufferDesc IndexDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumSplats);
        Proxy->GetIndicesFake() = GraphBuilder.CreateBuffer(IndexDesc, TEXT("Indices"));

        FRDGBufferDesc DistanceDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint16), NumSplats);
        Proxy->GetDistancesFake() = GraphBuilder.CreateBuffer(DistanceDesc, TEXT("Distances"));

        if (Proxy->IsSortCacheValid(LocalToClip))
        {
            // Camera and data unchanged — skip all compute passes, reuse cached sort.
            // Register a no-op producer so RDG validation doesn't complain about unwritten buffers.
            FCSCacheHitParams* CacheParams = GraphBuilder.AllocParameters<FCSCacheHitParams>();
            CacheParams->IndicesUAV = GraphBuilder.CreateUAV(Proxy->GetIndicesFake(), PF_R32_UINT);
            CacheParams->DistancesUAV = GraphBuilder.CreateUAV(Proxy->GetDistancesFake(), PF_R16_UINT);

            GraphBuilder.AddPass(
                RDG_EVENT_NAME("CSGaussian: CacheHit %s", *Proxy->GetName()),
                CacheParams,
                ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
                [](FRHIComputeCommandList&) {});
            continue;
        }

        Proxy->SetSortCache(LocalToClip);

        // Particle FX pass (before distance/sort so displaced positions are used)
        if (Proxy->IsParticleFXEnabled())
        {
            float TimeSeconds = View.Family->Time.GetWorldTimeSeconds();
            float DeltaTime = View.Family->Time.GetDeltaWorldTimeSeconds();

            GraphBuilder.AddPass(
                RDG_EVENT_NAME("CSGaussian: ParticleFX %s", *Proxy->GetName()),
                ERDGPassFlags::None | ERDGPassFlags::NeverCull,
                [Proxy, TimeSeconds, DeltaTime](FRHICommandList& RHICmdList)
                {
                    DispatchParticleFXPass(RHICmdList, Proxy, TimeSeconds, DeltaTime);
                });

            Proxy->InvalidateSortCache();
        }

        // Compute transforms (projection to 2D)
        DispatchTransformPass(GraphBuilder, View, Proxy);

        // GPU sort pipeline
        DispatchDistancePass(GraphBuilder, View, Proxy,
            Proxy->GetIndicesFake(), Proxy->GetDistancesFake());

        DispatchSortPass(GraphBuilder, View, Proxy,
            Proxy->GetIndicesFake(), Proxy->GetDistancesFake());

        // Write indirect draw args from visible count (after sort)
        GraphBuilder.AddPass(
            RDG_EVENT_NAME("CSGaussian: WriteIndirectArgs %s", *Proxy->GetName()),
            ERDGPassFlags::None | ERDGPassFlags::NeverCull,
            [Proxy](FRHICommandList& RHICmdList)
            {
                DispatchIndirectArgsPass(RHICmdList, Proxy);
            });
    }
}

void FCSGaussianViewExtension::PrePostProcessPass_RenderThread(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FPostProcessingInputs& Inputs)
{
    for (auto& Proxy : Proxies)
    {
        if (!Proxy || !Proxy->IsVisible(View))
            continue;

        FCSGaussianRenderSharedParams Shared;
        Shared.View = View.ViewUniformBuffer;
        Shared.InstancedView = View.GetInstancedViewUniformBuffer();
        Shared.local_to_world = FMatrix44f(Proxy->GetLocalToWorld());
        Shared.texture_width = Proxy->GetTextureWidth();
        Shared.brightness = Proxy->GetBrightness();
        Shared.PositionTexture = Proxy->GetPositionTextureRHI();
        Shared.displaced_positions = Proxy->GetDisplacedPositionsSRV();
        Shared.use_displaced = Proxy->IsParticleFXEnabled() ? 1 : 0;
        Shared.SH0Texture = Proxy->GetSH0TextureRHI();
        // VS reads opacity from alpha channel — use Cov2Opacity (PLY) or ScaleOpacity (sequence)
        Shared.Cov2OpacityTexture = Proxy->UsePrecomputedCov()
            ? Proxy->GetCov2OpacityTextureRHI()
            : Proxy->GetScaleOpacityTextureRHI();
        Shared.computed_transforms = Proxy->GetTransformsSRV();
        Shared.sorted_indices = Proxy->GetIndicesSRV();

        FCSGaussianRenderPS::FParameters ParamsPS;
        check(Inputs.SceneTextures);
        ParamsPS.RenderTargets[0] = FRenderTargetBinding(
            (*Inputs.SceneTextures)->SceneColorTexture,
            ERenderTargetLoadAction::ELoad);
        ParamsPS.RenderTargets.DepthStencil = FDepthStencilBinding(
            (*Inputs.SceneTextures)->SceneDepthTexture,
            ERenderTargetLoadAction::ELoad,
            FExclusiveDepthStencil::DepthWrite_StencilNop);

        FCSGaussianRenderDeps* PassParams =
            GraphBuilder.AllocParameters<FCSGaussianRenderDeps>();
        PassParams->Indices =
            GraphBuilder.CreateSRV(Proxy->GetIndicesFake(), PF_R32_UINT);
        PassParams->VS.Shared = Shared;
        PassParams->PS = ParamsPS;

        GraphBuilder.AddPass(
            RDG_EVENT_NAME("CSGaussian: Render %s", *Proxy->GetName()),
            PassParams,
            ERDGPassFlags::Raster,
            [PassParams, Proxy, &View](FRHICommandList& RHICmdList)
            {
                DrawGaussianSplats(RHICmdList, PassParams, Proxy, View);
            });
    }
}

} // namespace CSGaussian
