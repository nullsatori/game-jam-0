using UnrealBuildTool;

public class OuterSpaceTarget : TargetRules
{
	public OuterSpaceTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("MindKore");
		ExtraModuleNames.Add("MindKoreShaders");
	}
}
