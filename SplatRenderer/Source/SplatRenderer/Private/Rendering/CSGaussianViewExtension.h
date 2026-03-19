#pragma once

#include "Containers/Set.h"
#include "SceneViewExtension.h"
#include "CSGaussianSceneProxy.h"

namespace CSGaussian
{

class FCSGaussianViewExtension final : public FSceneViewExtensionBase
{
public:
    FCSGaussianViewExtension(const FAutoRegister& AutoRegister);

    virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
    virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
    virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

    virtual void PreRenderView_RenderThread(
        FRDGBuilder& GraphBuilder, FSceneView& InView) override;

    virtual void PrePostProcessPass_RenderThread(
        FRDGBuilder& GraphBuilder,
        const FSceneView& View,
        const FPostProcessingInputs& Inputs) override;

    void AddGaussianProxy_RenderThread(FCSGaussianSceneProxy* Proxy)
    {
        check(IsInRenderingThread());
        Proxies.Add(Proxy);
    }

    void RemoveGaussianProxy_RenderThread(FCSGaussianSceneProxy* Proxy)
    {
        check(IsInRenderingThread());
        Proxies.Remove(Proxy);
    }

private:
    TSet<FCSGaussianSceneProxy*> Proxies;
};

} // namespace CSGaussian
