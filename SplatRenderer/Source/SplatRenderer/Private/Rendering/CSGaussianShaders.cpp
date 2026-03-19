#include "CSGaussianShaders.h"

namespace CSGaussian
{

IMPLEMENT_GLOBAL_SHADER(
    FCSGaussianDistanceCS,
    "/Plugin/SplatRenderer/Private/CSComputeDistance.usf",
    "main",
    SF_Compute);

IMPLEMENT_GLOBAL_SHADER(
    FCSGaussianTransformCS,
    "/Plugin/SplatRenderer/Private/CSComputeTransform.usf",
    "main",
    SF_Compute);

IMPLEMENT_GLOBAL_SHADER(
    FCSGaussianIndirectArgsCS,
    "/Plugin/SplatRenderer/Private/CSWriteIndirectArgs.usf",
    "main",
    SF_Compute);

IMPLEMENT_GLOBAL_SHADER(
    FCSGaussianRenderVS,
    "/Plugin/SplatRenderer/Private/CSRenderSplatVS.usf",
    "main",
    SF_Vertex);

IMPLEMENT_GLOBAL_SHADER(
    FCSGaussianRenderPS,
    "/Plugin/SplatRenderer/Private/CSRenderSplatPS.usf",
    "main",
    SF_Pixel);

// ParticleFX shader registration disabled for now (advanced feature, not in release)
// IMPLEMENT_GLOBAL_SHADER(
//     FCSGaussianParticleFXCS,
//     "/Plugin/SplatRenderer/Private/CSComputeParticleFX.usf",
//     "main",
//     SF_Compute);

} // namespace CSGaussian
