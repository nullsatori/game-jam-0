// MindKore module rules (OS starter: the lifted systems plus the OS pawn).

using UnrealBuildTool;

public class MindKore : ModuleRules
{
	public MindKore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
			PrivateDependencyModuleNames.Add("BSPUtils");
		}

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
			// os.Audio.LimiterProbe records the master output (UAudioMixerBlueprintLibrary).
			"AudioMixer",
			// FFrameGrabber: os.Flyer.Clip records the viewport (OSClipRecorder).
			"MovieSceneCapture",
			// RHIGetGPUFrameCycles: the scope's cost probe (os.Scope.Shots).
			"RHI",
			// AMKGlassPane builds its shards at runtime; the plugin is enabled by
			// default in UE 5.8, so it needs no .uproject entry.
			"ProceduralMeshComponent",
			// MKReportMechanismNoise (hearing), MKDeliverResolvedImpact (damage
			// sense), AMKDoor's AI route test and nav link.
			"AIModule",
			"GameplayTasks",
			"NavigationSystem",
			// The debug menu (OSDebug.cpp, OSDebugMenu.cpp): Slate panel, JSON configs, the clipboard,
			// the preset assets' listing.
			"Slate",
			"SlateCore",
			"ApplicationCore",
			"Json",
			"AssetRegistry",
			// The cabin's convolution reverb: UOSAudioTools fills its impulse response asset (the plugin
			// is enabled by default in UE 5.8, so it needs no .uproject entry).
			"Synthesis",
		});
	}
}
