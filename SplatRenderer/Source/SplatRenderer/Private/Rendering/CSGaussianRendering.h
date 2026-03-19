#pragma once

#include "CSGaussianSceneProxy.h"
#include "CSGaussianShaders.h"
#include "RHICommandList.h"
#include "RenderGraphBuilder.h"
#include "SceneView.h"

namespace CSGaussian
{

BEGIN_SHADER_PARAMETER_STRUCT(FCSGaussianRenderDeps, )
    SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, Indices)
    SHADER_PARAMETER_STRUCT_INCLUDE(FCSGaussianRenderVS::FParameters, VS)
    SHADER_PARAMETER_STRUCT_INCLUDE(FCSGaussianRenderPS::FParameters, PS)
END_SHADER_PARAMETER_STRUCT()

void DispatchParticleFXPass(
    FRHICommandList& RHICmdList,
    FCSGaussianSceneProxy* Proxy,
    float TimeSeconds,
    float DeltaTime);

FRDGPassRef DispatchDistancePass(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    FCSGaussianSceneProxy* Proxy,
    FRDGBufferRef Indices,
    FRDGBufferRef Distances);

FRDGPassRef DispatchTransformPass(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    FCSGaussianSceneProxy* Proxy);

FRDGPassRef DispatchSortPass(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    FCSGaussianSceneProxy* Proxy,
    FRDGBufferRef Indices,
    FRDGBufferRef Distances);

void DispatchIndirectArgsPass(
    FRHICommandList& RHICmdList,
    FCSGaussianSceneProxy* Proxy);

void DrawGaussianSplats(
    FRHICommandList& RHICmdList,
    FCSGaussianRenderDeps* SplatParameters,
    FCSGaussianSceneProxy* Proxy,
    const FSceneView& View);

} // namespace CSGaussian
