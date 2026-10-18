// MindKore module rules (jam starter: the lifted systems plus the jam pawn).

using UnrealBuildTool;

public class MindKore : ModuleRules
{
	public MindKore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Flat module layout (headers next to .cpp), as in MindKore.
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"Niagara",
			"PhysicsCore",
			// jam.Audio.LimiterProbe records the master output (UAudioMixerBlueprintLibrary).
			"AudioMixer",
			// RHIGetGPUFrameCycles: the scope's cost probe (jam.Scope.Shots).
			"RHI",
			// AMKGlassPane builds its shards at runtime; the plugin is enabled by
			// default in UE 5.8, so it needs no .uproject entry.
			"ProceduralMeshComponent",
			// MKReportMechanismNoise (hearing), MKDeliverResolvedImpact (damage
			// sense), AMKDoor's AI route test and nav link.
			"AIModule",
			"GameplayTasks",
			"NavigationSystem",
		});
	}
}
