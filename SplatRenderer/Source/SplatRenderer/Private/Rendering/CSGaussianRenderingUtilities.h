#pragma once

#include "CSGaussianSceneProxy.h"
#include "CSGaussianShaders.h"
#include "SceneView.h"

namespace CSGaussian
{

inline float CSGaussianGetFocalLength(const FSceneView& View)
{
    return View.UnconstrainedViewRect.Width() /
           (2.f * tanf(View.ViewMatrices.ComputeHalfFieldOfViewPerAxis().X));
}

inline FMatrix CSGaussianGetViewMatrix(const FSceneView& View)
{
    return View.ViewMatrices.GetViewMatrix();
}

inline FMatrix CSGaussianGetViewProjectionMatrix(const FSceneView& View)
{
    return View.ViewMatrices.GetViewProjectionMatrix();
}

inline uint32 CSGaussianCalcThreadGroups(uint32 NumElements)
{
    return (NumElements + (THREAD_GROUP_SIZE_X - 1)) / THREAD_GROUP_SIZE_X;
}

} // namespace CSGaussian
