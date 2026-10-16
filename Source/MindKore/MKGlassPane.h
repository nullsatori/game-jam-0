#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
// Jam lift: MKBodyResponseComponent.h replaced by MKImpactHistory.h - only FMKImpactHistory was used; the body response is not lifted.
#include "MKImpactHistory.h"
#include "MKGlassFracture.h"
#include "MKGlassShardDebris.h"
#include "MKImpactReceiver.h"
#include "MKImpactTypes.h"
#include "MKGlassPane.generated.h"

class UMaterialInterface;
class UNiagaraSystem;
class UProceduralMeshComponent;
class USoundBase;

/**
 * One sheet of breakable glass, in the Half-Life 2 / Counter-Strike reading of
 * the word: whole until it is hit, and then irregular. A round cuts radial
 * cracks out of the point it landed on, chords across them, drops the pieces it
 * landed among, and leaves the surrounding shards standing in the frame until
 * something knocks them out too.
 *
 * The geometry is built at runtime from convex shards (`MKGlassFracture`) into
 * one `UProceduralMeshComponent`: two sections - whole glass and cracked glass -
 * and one convex hull per shard for collision, so the hole is a hole for the
 * bullet, the eye and the navmesh alike.
 *
 * Ownership. The pane owns its own geometry and its own damage state. It is NOT
 * an AMKImpactTarget subclass: that base owns health, death and a whole-body
 * flinch, and a pane has none of those - it has no health (every round takes
 * glass out), it never dies (an empty frame is still the same actor), and
 * flinching a window would be wrong. It implements the same one damage channel,
 * IMKImpactReceiver, so MKDeliverResolvedImpact reaches it - from a round and
 * from the crowbar alike - without a second code path.
 *
 * Placement contract. Pane space is the actor's own: width along local X,
 * height along local Z, thickness along local Y, all in centimetres from the
 * actor origin at the pane's CENTRE. Size the pane with the properties below
 * and leave the actor's scale at 1 - a scaled actor would scale the radii, and
 * "break 55 cm around the hit" would then mean a different distance per pane.
 */
/**
 * How many pieces come down on shed step StepIndex (0 is the first), and how
 * long that step waits. Free functions because the pacing of a collapse is
 * worth a test of its own and spawning a world to read a curve is not.
 */
MINDKORE_API int32 MKShedStepCount(int32 StepIndex, int32 FirstStepCount, float Falloff);

/**
 * The break radius for one impact: a round breaks RoundRadiusCm around the
 * hit, a blunt blow (the crowbar) BluntRadiusCm. Negative or non-finite radii
 * read as zero.
 */
MINDKORE_API float MKGlassBreakRadiusFor(EMKImpactKind Kind, float RoundRadiusCm, float BluntRadiusCm);
MINDKORE_API float MKShedStepInterval(int32 StepIndex, float BaseIntervalSeconds, float Growth);
/** Distribute all unsupported glass over at most four diminishing batches. */
inline int32 MKShedBatchCount(int32 StepIndex, int32 FirstStepCount, float Falloff, int32 Remaining)
{
	if (Remaining <= 0)
	{
		return 0;
	}
	constexpr int32 MaxSteps = 4;
	const int32 StepsLeft = FMath::Max(MaxSteps - FMath::Max(StepIndex, 0), 1);
	const float Decay = FMath::Clamp(Falloff, 0.05f, 1.0f);
	float RemainingWeight = 0.0f;
	for (int32 Index = 0; Index < StepsLeft; ++Index)
	{
		RemainingWeight += FMath::Pow(Decay, static_cast<float>(Index));
	}
	// Scale the rush to the actual collapse. A fixed five-piece rush followed
	// by singletons left large unsupported islands hanging for tens of seconds.
	return FMath::Clamp(FMath::Max(
		MKShedStepCount(StepIndex, FirstStepCount, Falloff),
		FMath::CeilToInt(Remaining / RemainingWeight)), 1, Remaining);
}

UCLASS()
class MINDKORE_API AMKGlassPane : public AActor, public IMKImpactReceiver
{
	GENERATED_BODY()

public:
	AMKGlassPane();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Glass")
	TObjectPtr<USceneComponent> PaneRoot = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Glass")
	TObjectPtr<UProceduralMeshComponent> GlassMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass")
	float PaneWidthCm = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass")
	float PaneHeightCm = 260.0f;

	/**
	 * Glass, not armour. An office partition is 10-12 mm; the first version of
	 * this actor carried 8 CENTIMETRES, and at that thickness every shard drew a
	 * wide lit rim and the broken pane read as a stack of slabs rather than as
	 * glass.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass")
	float PaneThicknessCm = 1.2f;

	/**
	 * Pre-cut the sheet into blocks of this pitch before anything is shot.
	 * Zero - the default - is a single unbroken sheet, which is what a window
	 * is. The vitrine in J is a glass-BLOCK wall and sets this to its 25 cm
	 * pitch so it keeps reading as one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass")
	float BlockPitchCm = 0.0f;

	/** Shards whose centre lands inside this fall out. One round, one hole. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass")
	float BreakRadiusCm = 45.0f;

	/**
	 * HYPOTHESIS: the same for a crowbar blow (EMKImpactKind::Blunt). A bar
	 * swung into a pane takes out more than a round's hole; how much is a PIE
	 * call. Kept apart from BreakRadiusCm so tuning one does not move the other.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass")
	float BluntBreakRadiusCm = 60.0f;

	/** How far the cracks reach. Past it the glass is not touched at all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass")
	float FractureRadiusCm = 130.0f;

	/**
	 * How far from the hit the glass DRAWS as cracked. Smaller than the fracture
	 * radius on purpose: the first version drew every piece a cut touched with
	 * the cracked material, and one round turned a see-through partition into a
	 * sheet of frosted glass.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass")
	float CrackedLookRadiusCm = 90.0f;

	/**
	 * How ragged the edge of that frosted zone is, as a fraction of its radius.
	 * At zero the frost ends on a circle - which is the one outline damage never
	 * has, and it read as a decal laid over the glass rather than as the glass
	 * itself being wrecked.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass", meta = (ClampMin = "0", ClampMax = "1"))
	float CrackedLookJitter = 0.3f;

	/** Radial cracks out of the impact point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass", meta = (ClampMin = "3"))
	int32 SpokeCount = 9;

	/**
	 * How many of those cracks run past the web and reach the frame. Without
	 * them the damage ends on the circle the fracture radius draws, and a shot
	 * into a four-metre partition reads as a small tidy disc; with them the
	 * whole pane is visibly finished. They are hairlines: their only visible
	 * width is the fresh cut edge, so they need FractureEdgeMaterial to read.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass", meta = (ClampMin = "0"))
	int32 RunnerCrackCount = 2;

	/** Chords across the spokes: how many rings of shards a hit leaves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass", meta = (ClampMin = "0"))
	int32 RingCount = 2;

	/** A cut that would leave a piece smaller than this is skipped (see MKGlassFracture). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass")
	float MinShardAreaCm2 = 45.0f;

	/**
	 * A piece smaller than this holds on to nothing and comes down with the rest.
	 * Broken windows do not leave a chip of glass standing on its own, and the
	 * first version did exactly that: little triangles left upright in an empty
	 * frame.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass")
	float MinSupportedAreaCm2 = 150.0f;

	/** Open-topped counter glass is seated only along the bottom, not in an imaginary perimeter frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass")
	bool bBottomSupportedOnly = false;

	/**
	 * Once the pane holds this many pieces a round still opens a hole and still
	 * cracks what it reaches, but cuts no new ones. Every shard is a convex hull
	 * cooked and a rim drawn on the next shot (see MKGlassFracture).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass", meta = (ClampMin = "1"))
	int32 MaxShardCount = 300;

	/**
	 * Seeds the crack pattern. Two panes with the same seed break identically,
	 * which is wanted in a test and not wanted on a level: give neighbouring
	 * panes different seeds.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass")
	int32 FractureSeed = 1337;

	/**
	 * Centimetres of glass per tile of the material's textures. The glass pack's
	 * dirt and scratch maps are authored for roughly a metre of surface; UVs
	 * normalised over the pane instead stretched one tile across four metres and
	 * read as smeared bands.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass", meta = (ClampMin = "1"))
	float UvTilingCm = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass")
	TObjectPtr<UMaterialInterface> IntactMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass")
	TObjectPtr<UMaterialInterface> CrackedMaterial = nullptr;

	/**
	 * A restrained emissive accent drawn only on the freshly exposed thickness
	 * of cracked pieces. Without it the procedural cut is physically present but
	 * disappears into a dark room; putting the accent on the faces would turn the
	 * whole pane into a glowing spiderweb.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass")
	TObjectPtr<UMaterialInterface> FractureEdgeMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass")
	TObjectPtr<UNiagaraSystem> BreakVfx = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass")
	TObjectPtr<USoundBase> BreakSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass", meta = (ClampMin = "0"))
	float BreakSoundVolume = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass", meta = (ClampMin = "0.1"))
	float BreakSoundPitchMin = 0.92f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass", meta = (ClampMin = "0.1"))
	float BreakSoundPitchMax = 1.06f;

	/**
	 * Breaking glass is the loudest stimulus on the level (DESIGN_LEVEL 1.3 п. 1)
	 * and MKPerceptionEnvelope takes stimulus strength on the input, so it is
	 * reported above the shot's own 1.0 rather than equal to it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass")
	float BreakNoiseLoudness = 2.0f;

	/**
	 * The reception counter in B is broken BEFORE the player arrives (section
	 * 4.2, object 1): it teaches the material at zero cost. Set on that pane
	 * only - a second pre-broken pane makes the lesson ambiguous.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass")
	bool bStartBroken = false;

	/** Where the pre-broken hole sits, in pane space (cm from the centre). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass", meta = (EditCondition = "bStartBroken"))
	FVector2D StartBrokenCentre = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass", meta = (EditCondition = "bStartBroken"))
	float StartBrokenRadiusCm = 90.0f;

	/**
	 * Glass leaves the frame as debris that falls, lands and stays: DESIGN_LEVEL
	 * section 1.3 point 3 wants a noisy floor after a pane goes, and a particle
	 * burst leaves nothing behind.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass|Debris")
	bool bSpawnDebris = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass|Debris")
	TSubclassOf<AMKGlassShardDebris> DebrisClass;

	/**
	 * What the falling pieces draw with. Unset means they keep the look they had
	 * in the frame; MI_MK_Glass_Shard is what the material kit provides for loose
	 * glass, and it belongs on debris rather than on a pane a player looks
	 * through.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass|Debris")
	TObjectPtr<UMaterialInterface> DebrisMaterial = nullptr;

	/**
	 * The largest piece allowed to reach the floor, in square centimetres.
	 *
	 * The frame and the floor do not want the same size of glass. A shard is cut
	 * to read as part of a hole in a partition; the same polygon lying on a
	 * counter is a slab, and a floor of slabs is the one thing a broken window
	 * never leaves. Anything bigger than this is cut again on its way out
	 * (MKShatterShard) - the pane's own shards, and so the hole the player looks
	 * through, are not touched. Zero drops the frame's pieces unchanged.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass|Debris", meta = (ClampMin = "0"))
	float DebrisPieceMaxAreaCm2 = 90.0f;

	/**
	 * How many pieces one released shard may become. This is a budget, not a
	 * target: a pane taken out by a single round is mostly large shards, and
	 * letting every one of them dice itself down to the area above would put a
	 * hundred simulated bodies on the floor for one shot.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass|Debris", meta = (ClampMin = "1"))
	int32 DebrisMaxPiecesPerShard = 4;

	/**
	 * How hard the round throws the glass it knocks out, in cm/s, at the impact
	 * point itself. A round does not nudge glass out of a frame - it blows it
	 * out - and the first pass at 150 cm/s had the pieces drifting off like
	 * paper. Pieces further from the hit are thrown proportionally slower, down
	 * to DebrisEdgeSpeedScale at the fracture radius.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass|Debris")
	float DebrisLaunchSpeedCmS = 650.0f;

	/** What is left of that speed at the far edge of the fracture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass|Debris", meta = (ClampMin = "0"))
	float DebrisEdgeSpeedScale = 0.3f;

	/**
	 * Glass that comes down later because it lost its hold is not being shot -
	 * it is letting go. It leaves the frame at this fraction of the launch speed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass|Debris", meta = (ClampMin = "0"))
	float ShedSpeedScale = 0.18f;

	/**
	 * Older pieces are cleared once a pane has thrown more than this. It counts
	 * PIECES, not shards, so it moved up with DebrisPieceMaxAreaCm2: at the old
	 * 64 the extra break spent the whole budget on the first round and the glass
	 * from the second one deleted the glass from the first.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass|Debris", meta = (ClampMin = "0"))
	int32 MaxLiveDebris = 128;

	/**
	 * The pane sheds what the hole left unsupported over time rather than in the
	 * frame the round lands: the hole is a bullet and belongs in one frame, the
	 * glass letting go around it is a collapse and needs a few tenths of a
	 * second, or the whole break reads as one flicker.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass|Debris", meta = (ClampMin = "0.01"))
	float ShedIntervalSeconds = 0.07f;

	/**
	 * How many pieces come down on the FIRST shed step. The collapse is not a
	 * metronome: glass lets go in a rush and then drips, so each step after the
	 * first carries ShedFalloff of the previous one and arrives ShedIntervalGrowth
	 * later. A constant two-per-tick reads as a machine unloading a magazine of
	 * shards.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass|Debris", meta = (ClampMin = "1"))
	int32 ShedShardsPerStep = 5;

	/** What is left of the step's count on the next step. Never below one piece. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass|Debris", meta = (ClampMin = "0.05", ClampMax = "1"))
	float ShedFalloff = 0.55f;

	/** How much longer each step waits than the one before it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass|Debris", meta = (ClampMin = "1"))
	float ShedIntervalGrowth = 1.35f;

	/**
	 * The collapse's own voice: a light, short glass sound per shed step, under
	 * the break itself. Without it the pane is silent from the moment the round
	 * lands, and the half second of glass still coming down happens in a hole in
	 * the mix - which is the opposite of what breaking glass is for on this level
	 * (DESIGN_LEVEL 1.3: the loudest stimulus we have).
	 *
	 * PLACEHOLDER: it is the footstep-on-glass cue, played quiet and high. The
	 * shot got its own layers cut from a donor (DESIGN_AUDIO 13) and glass has
	 * had no donor pass at all. Replace when it does; the field stays either way.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass|Debris")
	TObjectPtr<USoundBase> ShedSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass|Debris", meta = (ClampMin = "0"))
	float ShedSoundVolume = 0.3f;

	/**
	 * What a piece of glass says when it reaches the floor. Handed to the debris,
	 * which plays it on its first real contact - the tail of the break is the
	 * player hearing where the glass went, and that is also the moment the floor
	 * he is about to walk on becomes audible to him (DESIGN_LEVEL 4.2, object 1).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass|Debris")
	TObjectPtr<USoundBase> DebrisLandingSound = nullptr;

	/**
	 * How many pieces of one break are allowed to be heard landing. Thirty
	 * shards landing with thirty sounds is a rainstick, not a window: the first
	 * few carry the event and the rest land quietly.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass|Debris", meta = (ClampMin = "0"))
	int32 MaxLandingSounds = 6;

	/** How many pieces of glass are still in the frame. Zero is an empty frame. */
	UFUNCTION(BlueprintPure, Category = "MindKore|Glass")
	int32 GetShardCount() const { return Shards.Num(); }

	/** How much glass is left, in square centimetres. */
	UFUNCTION(BlueprintPure, Category = "MindKore|Glass")
	float GetRemainingAreaCm2() const;

	/**
	 * Fracture around a point given in pane space, and return how many shards
	 * fell out. The world-space entry point is ReceiveResolvedImpact; this one
	 * exists for the pre-broken counter and for anything that breaks glass
	 * without a bullet.
	 *
	 * PushDirection is the world direction the debris is thrown along - the
	 * round's own direction, normally. Zero throws the pieces straight down,
	 * which is what an authored pre-broken pane wants.
	 */
	UFUNCTION(BlueprintCallable, Category = "MindKore|Glass")
	int32 BreakAt(const FVector2D& PaneUV, float BreakRadius, float FractureRadius, FVector PushDirection);

	virtual void ReceiveResolvedImpact_Implementation(const FMKResolvedImpact& ResolvedImpact) override;

	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Back to one whole sheet (or one grid of blocks), then rebuild the mesh. */
	void RebuildPane();

	/** Push the current shard list into the procedural mesh and its collision. */
	void RefreshGlassMesh();

	/**
	 * Throw these pieces into the world as falling glass. SpeedScale is 1 for
	 * what a round knocked out and ShedSpeedScale for what let go afterwards.
	 */
	void ThrowDebris(TArrayView<const FMKGlassShard> FallenShards, const FVector& PushDirection, float SpeedScale);

	/** One step of the collapse: drop the pieces that lost their hold. */
	void ShedUnsupportedShards();

	/** Arm the next collapse step, whose interval grows with the step index. */
	void ScheduleShedStep();

	/** The collapse's voice for one step: quieter and higher as it runs down. */
	void PlayShedVoice(int32 ShardsThisStep);

private:
	// Not UPROPERTYs: FMKGlassShard lives in a UWorld-free header on purpose,
	// and the shard list is runtime damage - a pane starts whole every time the
	// level loads, except for the authored pre-broken one.
	TArray<FMKGlassShard> Shards;
	FRandomStream FractureRandom;

	/**
	 * Its own stream, not the fracture's: where a released piece breaks is not
	 * part of what the pane looks like, and drawing those cuts from
	 * FractureRandom would make the NEXT round's web depend on how many pieces
	 * the last one happened to drop.
	 */
	FRandomStream DebrisRandom;

	/** The direction the last round came from, reused while the pane sheds. */
	FVector LastPushDirection = FVector::ZeroVector;

	/** Where it landed, in pane space: the glass nearest it lets go first. */
	FVector2D LastHitUV = FVector2D::ZeroVector;

	FTimerHandle ShedTimer;

	/** Which step of the current collapse is next: 0 is the rush after the round. */
	int32 ShedStepIndex = 0;

	/** How many pieces of this break may still be heard landing. */
	int32 LandingSoundBudget = 0;

	/**
	 * One impact event breaks this pane once: the same ShotId and
	 * InteractionIndex arriving again is a replay and is dropped. The enemy's
	 * guard (FMKImpactHistory, from its body response) reused, not copied. A
	 * round's later interaction carries its own index and still breaks; impacts
	 * without a ShotId always pass. Runtime state, like the shard list.
	 */
	FMKImpactHistory ImpactHistory;

	UPROPERTY()
	TArray<TObjectPtr<AMKGlassShardDebris>> LiveDebris;
};
