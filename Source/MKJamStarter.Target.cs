using UnrealBuildTool;

public class MKJamStarterTarget : TargetRules
{
	public MKJamStarterTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("MindKore");
	}
}
