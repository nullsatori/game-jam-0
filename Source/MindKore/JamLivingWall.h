#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MKImpactReceiver.h"
#include "JamLivingWall.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UNiagaraSystem;
class UPhysicalMaterial;
class USoundBase;

/**
 * A living wall of woven tissue - the Whomping Willow as a door. A pistol round
 * tears a strand or two and the weave closes again in about half a second; a
 * shotgun blast tears a hole that stays open for a few seconds and lets the
 * player (or a prop) through before it re-weaves, centre last.
 *
 * Wall space is the actor's own, like AMKGlassPane: width along local X, height
 * along local Z, the face toward local +Y (the "front"), origin at the centre,
 * scale 1. The wall is a grid of CellSizeCm cells; each cell is two crossed
 * strands (horizontal and vertical) drawn as two instances of one
 * UInstancedStaticMeshComponent (the engine cylinder), offset in depth in a
 * checker pattern so the grid reads as a basket weave. A torn cell's strands
 * shrink to a speck (no collision); instances are never removed.
 *
 * Tearing. Every impact (IMKImpactReceiver: rounds, and thrown props through
 * the collision-impact path) tears the cells within TearRadiusCm of its point.
 * BurstMinHits or more impacts from one source in one frame (the shotgun's
 * pellets) are one blast: it also tears the disc covering them all plus
 * BurstMarginCm, at most BurstMaxRadiusCm - otherwise eight pellets would
 * leave eight 20 cm holes and never one a body fits through.
 *
 * Regrowth. A torn cell waits RegrowDelaySeconds plus the blast's stun
 * (StunSecondsPerCell for every cell of its wound beyond StunFreeCells, at
 * most MaxStunSeconds) and then until a 4-neighbour is whole; then its strands
 * grow out of that neighbour's side over StrandGrowSeconds. So a hole closes
 * ring by ring from its rim, and a big wound starts later than a small one.
 * Whatever overlaps a regrowing cell is pushed out along the wall normal toward
 * the side its centre is on; a pawn with vitals takes SqueezeDamage.
 */
UCLASS()
class MINDKORE_API AJamLivingWall : public AActor, public IMKImpactReceiver
{
	GENERATED_BODY()

public:
	AJamLivingWall();

	/** The bundles: two per cell, the collision of the wall. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam|LivingWall")
	TObjectPtr<UInstancedStaticMeshComponent> Strands = nullptr;

	/** Thin vessel-like threads over the bundles, four per cell (two curved threads of two segments); no collision. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam|LivingWall")
	TObjectPtr<UInstancedStaticMeshComponent> Threads = nullptr;

	// --- shape -------------------------------------------------------------

	/** Rounded to whole cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Shape", meta = (ClampMin = "20.0"))
	float WidthCm = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Shape", meta = (ClampMin = "20.0"))
	float HeightCm = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Shape", meta = (ClampMin = "5.0"))
	float CellSizeCm = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Shape", meta = (ClampMin = "1.0"))
	float StrandThicknessCm = 12.0f;

	/** Each cell's bundles are StrandThicknessCm times a random factor in this range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Shape")
	FVector2D BundleThicknessRange = FVector2D(0.7f, 1.15f);

	/** Bundles lean off the grid by up to this, so the weave reads as grown, not woven. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Shape", meta = (ClampMin = "0.0"))
	float BundleLeanDeg = 10.0f;

	/** Thickness of the vessel threads, cm (each thread its own). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Shape")
	FVector2D ThreadThicknessRange = FVector2D(3.0f, 6.0f);

	/** A strand is this much longer than a cell, so neighbours overlap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Shape", meta = (ClampMin = "0.0"))
	float StrandOverlapCm = 6.0f;

	/** Over/under offset of the two strands of a cell, alternating cell to cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Shape", meta = (ClampMin = "0.0"))
	float WeaveDepthCm = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Shape")
	TObjectPtr<UMaterialInterface> WallMaterial = nullptr;

	/** Flesh: the resolver reports MK_Flesh, so hits sound and mark as a body. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Shape")
	TObjectPtr<UPhysicalMaterial> FleshMaterial = nullptr;

	// --- tearing ----------------------------------------------------------

	/** Every impact tears the cells whose centre is this close to it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Tear", meta = (ClampMin = "0.0"))
	float TearRadiusCm = 15.0f;

	/** Impacts from one source in one frame that count as one blast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Tear", meta = (ClampMin = "2"))
	int32 BurstMinHits = 3;

	/** A blast tears the disc covering its hits plus this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Tear", meta = (ClampMin = "0.0"))
	float BurstMarginCm = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Tear", meta = (ClampMin = "0.0"))
	float BurstMaxRadiusCm = 80.0f;

	// --- regrowth ---------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Regrow", meta = (ClampMin = "0.0"))
	float RegrowDelaySeconds = 0.2f;

	/** Extra wait for every cell of a wound beyond StunFreeCells: big wounds start late. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Regrow", meta = (ClampMin = "0.0"))
	float StunSecondsPerCell = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Regrow", meta = (ClampMin = "0"))
	int32 StunFreeCells = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Regrow", meta = (ClampMin = "0.0"))
	float MaxStunSeconds = 2.4f;

	/** One cell's strands grow back over this, out of a whole neighbour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Regrow", meta = (ClampMin = "0.01"))
	float StrandGrowSeconds = 0.3f;

	/** Share of the growth spent as a thin thread reaching across; the rest thickens it into a bundle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Regrow", meta = (ClampMin = "0.05", ClampMax = "0.95"))
	float ThreadStage = 0.5f;

	/** Jerk of the whole cells on a torn rim while the hole is open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Regrow", meta = (ClampMin = "0.0"))
	float TwitchCm = 1.5f;

	/** Writhing of a growing strand, fading as it finishes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Regrow", meta = (ClampMin = "0.0"))
	float WobbleCm = 3.0f;

	// --- thin spots -------------------------------------------------------

	/** Patches that glow (instance custom data 0 = 1) and regrow slower: where to shoot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|ThinSpots", meta = (ClampMin = "0"))
	int32 ThinSpotCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|ThinSpots", meta = (ClampMin = "0.0"))
	float ThinSpotRadiusCm = 30.0f;

	/** Delay and growth time of a thin cell, times this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|ThinSpots", meta = (ClampMin = "1.0"))
	float ThinSpotSlowdown = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|ThinSpots")
	int32 ThinSpotSeed = 1;

	// --- bodies in a closing hole ----------------------------------------

	/** Normal speed a body in a regrowing cell is pushed out with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Squeeze", meta = (ClampMin = "0.0"))
	float PushOutSpeedCm = 400.0f;

	/** Past this growth a body still inside is moved out at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Squeeze", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PushOutTeleportGrowth = 0.6f;

	/** Per squeeze of something with UMKVitalsComponent (the pawn), at most once per SqueezeIntervalSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Squeeze", meta = (ClampMin = "0.0"))
	float SqueezeDamage = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Squeeze", meta = (ClampMin = "0.0"))
	float SqueezeIntervalSeconds = 0.5f;

	// --- feedback ---------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Feedback")
	TObjectPtr<UNiagaraSystem> TearVfx = nullptr;

	/** Pulsed from the rim of a big hole until that rim cell regrows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Feedback")
	TObjectPtr<UNiagaraSystem> SpurtVfx = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Feedback")
	TObjectPtr<USoundBase> TearSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Feedback")
	TObjectPtr<USoundBase> GroanSound = nullptr;

	/** Played quietly, pitched down, when a hole has closed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Feedback")
	TObjectPtr<USoundBase> CloseSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Feedback", meta = (ClampMin = "0.0"))
	float TearSoundIntervalSeconds = 0.08f;

	/** A wound this big groans. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Feedback", meta = (ClampMin = "1"))
	int32 GroanCells = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Feedback", meta = (ClampMin = "0.1"))
	float GroanPitch = 0.55f;

	/** A wound this big spurts from its rim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Feedback", meta = (ClampMin = "1"))
	int32 SpurtHoleCells = 10;

	/** Spurting rim cells on this wall at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Feedback", meta = (ClampMin = "0"))
	int32 MaxActiveSpurts = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|LivingWall|Feedback", meta = (ClampMin = "0.05"))
	float SpurtPulseSeconds = 0.45f;

	// --- queries (probe, HUD) ----------------------------------------------

	/** Cells not whole (torn or regrowing). */
	UFUNCTION(BlueprintPure, Category = "Jam|LivingWall")
	int32 GetOpenCellCount() const;

	UFUNCTION(BlueprintPure, Category = "Jam|LivingWall")
	FVector GetFrontNormal() const { return GetActorRightVector(); }

	/** Half the weave's depth: strands reach this far from the wall plane. */
	float GetHalfDepthCm() const { return WeaveDepthCm + StrandThicknessCm * 0.5f; }

	/**
	 * Radius of the largest circle in the wall plane, centred at the point's
	 * projection, that no strand (whole or growing) crosses; 0 on a strand.
	 */
	float ClearRadiusAt(const FVector& WorldPoint) const;

	/** The widest such circle anywhere in the wall; its centre in OutCentre. */
	float LargestClearRadius(FVector& OutCentre) const;

	/** Physics bodies still alive on torn (Growth 0) cells' strands, and the largest scale among them: should be 0. */
	int32 CountTornBodies(float& OutLargestScale) const;

	/** Centre of the cell nearest the point (where its two strands cross), world. */
	FVector NearestCellCentre(const FVector& WorldPoint) const;

	/** Centre of the newest wound, world. */
	FVector GetLastWoundCentre() const;

	virtual void ReceiveResolvedImpact_Implementation(const FMKResolvedImpact& ResolvedImpact) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	struct FCell
	{
		/** 1 whole, 0 torn (waiting), between: regrowing. */
		float Growth = 1.0f;
		double RegrowAt = 0.0;
		/** The whole neighbour it grows out of, as a cell step. */
		FIntPoint From = FIntPoint::ZeroValue;
		float Phase = 0.0f;
		bool bThin = false;
		/** Bundle thickness factor, lean of each bundle (radians), colour variation. */
		float Thickness = 1.0f;
		float Lean[2] = { 0.0f, 0.0f };
		float Variation = 0.0f;
		/** Vessel threads: shown count, three points each (cell space), depth, radius. */
		int32 ThreadCount = 1;
		FVector2D ThreadPoints[2][3];
		float ThreadDepth[2] = { 0.0f, 0.0f };
		float ThreadRadius[2] = { 0.0f, 0.0f };
	};

	struct FWound
	{
		TArray<int32> Cells;
		TArray<int32> SpurtCells;
		FVector2D Centre = FVector2D::ZeroVector;
		/** +1: the blast came from the front (spurts go back out the front). */
		float Side = 1.0f;
		double TornAt = 0.0;
		double NextPulse = 0.0;
	};

	struct FPendingHit
	{
		FVector2D Point;
		float Side;
		TWeakObjectPtr<AActor> Source;
		FVector WorldPoint;
		FVector Direction;
	};

	void Build();
	void ProcessHits(double Now);
	void Tear(const TArray<FVector2D>& Points, float Radius, TSet<int32>& OutCells) const;
	void UpdateCells(float DeltaSeconds, double Now);
	void UpdateWounds(double Now);
	void UpdateRim(double Now);
	void PushOutBodies(double Now);
	void WriteCell(int32 Cell, double Now, float Twitch);
	void Spurt(const FWound& Wound, int32 Cell) const;

	FVector2D CellCentre(int32 Cell) const;
	int32 CellAt(int32 X, int32 Z) const { return (X >= 0 && X < CellsX && Z >= 0 && Z < CellsZ) ? X + Z * CellsX : INDEX_NONE; }
	bool IsWhole(int32 Cell) const { return Cell != INDEX_NONE && Cells[Cell].Growth >= 1.0f; }
	/** A cell's two strands as segments in wall space (X, Z) and their radius. */
	void StrandSegments(int32 Cell, double Now, float Twitch, FVector2D OutA[2], FVector2D OutB[2], float OutDepth[2], float& OutRadius) const;

	int32 CellsX = 0;
	int32 CellsZ = 0;
	TArray<FCell> Cells;
	/** Cells not whole. */
	TArray<int32> Open;
	/** Whole cells twitching on a rim last frame. */
	TSet<int32> Twitching;
	TArray<FWound> Wounds;
	TArray<FPendingHit> Pending;
	TMap<TWeakObjectPtr<AActor>, double> LastSqueeze;
	FVector2D LastWoundCentre = FVector2D::ZeroVector;
	double LastTearSound = -1.0;
	FRandomStream Random;
};
