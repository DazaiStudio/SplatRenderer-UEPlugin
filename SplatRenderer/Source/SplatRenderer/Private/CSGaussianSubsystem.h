#pragma once

#include "Rendering/CSGaussianSceneProxy.h"
#include "Rendering/CSGaussianViewExtension.h"
#include "Subsystems/EngineSubsystem.h"

#include "CSGaussianSubsystem.generated.h"

UCLASS()
class UCSGaussianSubsystem final : public UEngineSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override
    {
        Extension = FSceneViewExtensions::NewExtension<
            CSGaussian::FCSGaussianViewExtension>();
    }

    void AddGaussianProxy_RenderThread(CSGaussian::FCSGaussianSceneProxy* Proxy)
    {
        check(Extension);
        Extension->AddGaussianProxy_RenderThread(Proxy);
    }

    void RemoveGaussianProxy_RenderThread(CSGaussian::FCSGaussianSceneProxy* Proxy)
    {
        check(Extension);
        Extension->RemoveGaussianProxy_RenderThread(Proxy);
    }

private:
    TSharedPtr<CSGaussian::FCSGaussianViewExtension, ESPMode::ThreadSafe> Extension;
};
