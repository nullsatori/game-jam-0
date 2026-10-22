using UnrealBuildTool;

public class OuterSpaceEditorTarget : TargetRules
{
	public OuterSpaceEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("MindKore");
	}
}
