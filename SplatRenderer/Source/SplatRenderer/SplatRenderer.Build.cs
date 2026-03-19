using System.IO;
using UnrealBuildTool;

public class SplatRenderer : ModuleRules
{
    public SplatRenderer(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Projects",
            "RenderCore",
            "Renderer",
            "RHI",
            "Json",
            "JsonUtilities",
        });

        PrivateIncludePaths.AddRange(new string[]
        {
            Path.Combine(GetModuleDirectory("Renderer"), "Private"),
            Path.Combine(GetModuleDirectory("Renderer"), "Internal"),
        });
    }
}
