#pragma once

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"

namespace CSGaussian
{

constexpr uint32 THREAD_GROUP_SIZE_X = 64;
constexpr uint32 DepthMask = 0x0000FFFF;

// --- Compute Distance ---

class FCSGaussianDistanceCS final : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FCSGaussianDistanceCS);
    SHADER_USE_PARAMETER_STRUCT(FCSGaussianDistanceCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FMatrix44f, local_to_clip)
        SHADER_PARAMETER(uint32, num_splats)
        SHADER_PARAMETER(uint32, texture_width)
        SHADER_PARAMETER(uint32, enable_crop)
        SHADER_PARAMETER(FVector3f, crop_center)
        SHADER_PARAMETER(FVector3f, crop_half_size)
        SHADER_PARAMETER(FMatrix44f, crop_inv_rotation)
        SHADER_PARAMETER_TEXTURE(Texture2D<float4>, PositionTexture)
        SHADER_PARAMETER_SRV(Buffer<float4>, displaced_positions)
        SHADER_PARAMETER(uint32, use_displaced)
        SHADER_PARAMETER_UAV(RWBuffer<uint>, indices)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, distances)
        SHADER_PARAMETER_UAV(RWBuffer<uint>, visible_count)
    END_SHADER_PARAMETER_STRUCT()

public:
    static void ModifyCompilationEnvironment(
        const FGlobalShaderPermutationParameters& Parameters,
        FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE_X"), THREAD_GROUP_SIZE_X);
    }
};

// --- Compute Transform ---

class FCSGaussianTransformCS final : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FCSGaussianTransformCS);
    SHADER_USE_PARAMETER_STRUCT(FCSGaussianTransformCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FMatrix44f, local_to_view)
        SHADER_PARAMETER(float, two_focal_length)
        SHADER_PARAMETER(float, splat_scale)
        SHADER_PARAMETER(uint32, num_splats)
        SHADER_PARAMETER(uint32, texture_width)
        SHADER_PARAMETER(uint32, use_precomputed_cov)
        SHADER_PARAMETER_TEXTURE(Texture2D<float4>, PositionTexture)
        SHADER_PARAMETER_SRV(Buffer<float4>, displaced_positions)
        SHADER_PARAMETER(uint32, use_displaced)
        SHADER_PARAMETER_TEXTURE(Texture2D<float4>, Cov1Texture)
        SHADER_PARAMETER_TEXTURE(Texture2D<float4>, Cov2OpacityTexture)
        SHADER_PARAMETER_TEXTURE(Texture2D<float4>, RotationTexture)
        SHADER_PARAMETER_TEXTURE(Texture2D<float4>, ScaleOpacityTexture)
        SHADER_PARAMETER_UAV(RWBuffer<float4>, transforms)
    END_SHADER_PARAMETER_STRUCT()

public:
    static void ModifyCompilationEnvironment(
        const FGlobalShaderPermutationParameters& Parameters,
        FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE_X"), THREAD_GROUP_SIZE_X);
    }
};

// --- Write Indirect Args ---

class FCSGaussianIndirectArgsCS final : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FCSGaussianIndirectArgsCS);
    SHADER_USE_PARAMETER_STRUCT(FCSGaussianIndirectArgsCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_SRV(Buffer<uint>, visible_count)
        SHADER_PARAMETER_UAV(RWBuffer<uint>, indirect_args)
    END_SHADER_PARAMETER_STRUCT()
};

// --- Render VS/PS shared parameters ---

BEGIN_SHADER_PARAMETER_STRUCT(FCSGaussianRenderSharedParams, )
    SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
    SHADER_PARAMETER_STRUCT_REF(FInstancedViewUniformShaderParameters, InstancedView)
    SHADER_PARAMETER(FMatrix44f, local_to_world)
    SHADER_PARAMETER(uint32, texture_width)
    SHADER_PARAMETER(float, brightness)
    SHADER_PARAMETER_TEXTURE(Texture2D<float4>, PositionTexture)
    SHADER_PARAMETER_SRV(Buffer<float4>, displaced_positions)
    SHADER_PARAMETER(uint32, use_displaced)
    SHADER_PARAMETER_TEXTURE(Texture2D<float4>, SH0Texture)
    SHADER_PARAMETER_TEXTURE(Texture2D<float4>, Cov2OpacityTexture)
    SHADER_PARAMETER_SRV(Buffer<float4>, computed_transforms)
    SHADER_PARAMETER_SRV(Buffer<uint>, sorted_indices)
END_SHADER_PARAMETER_STRUCT()

// --- Vertex Shader ---

class FCSGaussianRenderVS final : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FCSGaussianRenderVS);
    SHADER_USE_PARAMETER_STRUCT(FCSGaussianRenderVS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_STRUCT_INCLUDE(FCSGaussianRenderSharedParams, Shared)
    END_SHADER_PARAMETER_STRUCT()
};

// --- Pixel Shader ---

class FCSGaussianRenderPS final : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FCSGaussianRenderPS);
    SHADER_USE_PARAMETER_STRUCT(FCSGaussianRenderPS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
};

// --- Particle FX Compute ---

class FCSGaussianParticleFXCS final : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FCSGaussianParticleFXCS);
    SHADER_USE_PARAMETER_STRUCT(FCSGaussianParticleFXCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, num_splats)
        SHADER_PARAMETER(uint32, texture_width)
        SHADER_PARAMETER(uint32, first_frame)
        SHADER_PARAMETER(float, time_seconds)
        SHADER_PARAMETER(float, delta_time)
        SHADER_PARAMETER(float, noise_amplitude)
        SHADER_PARAMETER(float, noise_frequency)
        SHADER_PARAMETER(float, noise_speed)
        SHADER_PARAMETER(FVector3f, wind_direction)
        SHADER_PARAMETER(float, wind_strength)
        SHADER_PARAMETER(float, gravity_strength)
        SHADER_PARAMETER(FVector3f, attract_center)
        SHADER_PARAMETER(float, attract_strength)
        SHADER_PARAMETER(float, drag)
        SHADER_PARAMETER(FVector3f, vortex_axis)
        SHADER_PARAMETER(FVector3f, vortex_center)
        SHADER_PARAMETER(float, vortex_strength)
        SHADER_PARAMETER_TEXTURE(Texture2D<float4>, PositionTexture)
        SHADER_PARAMETER_UAV(RWBuffer<float4>, velocities)
        SHADER_PARAMETER_UAV(RWBuffer<float4>, displaced_positions)
    END_SHADER_PARAMETER_STRUCT()

public:
    static void ModifyCompilationEnvironment(
        const FGlobalShaderPermutationParameters& Parameters,
        FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE_X"), THREAD_GROUP_SIZE_X);
    }
};

} // namespace CSGaussian
