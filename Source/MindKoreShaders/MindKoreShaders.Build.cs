// The gas's global shaders and its screen-block pass (v11 stage 5, Docs/RecoilWeapons-SmokeV11.md). Loaded at
// PostConfigInit: a module's global shaders must be known before the shader maps are built.

using UnrealBuildTool;

public class MindKoreShaders : ModuleRules
{
	public MindKoreShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "RenderCore", "Renderer", "RHI", "Projects" });
	}
}
