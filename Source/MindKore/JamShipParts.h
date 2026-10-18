#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "JamDockZone.h"
#include "MKImpactReceiver.h"
#include "MKInteractable.h"
#include "JamShipParts.generated.h"

class UBoxComponent;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UStaticMeshComponent;

/** Counts foreign junk (tag Junk.Foreign) inside the hull: an AJamDockZone that captures nothing. */
UCLASS()
class MINDKORE_API AJamCleanZone : public AJamDockZone
{
	GENERATED_BODY()

public:
	AJamCleanZone();
};

UENUM()
enum class EJamWeldResult : uint8
{
	OffSeam,
	Gap,
	Junk,
	Welded,
	Cut
};

/**
 * A breach in the hull, sealed by hand (Docs/HullRepair.md): a Hull.Plate,
 * larger than the hole, is strapped over it from outside (UJamHullToolsComponent)
 * and welded along the hole's contour.
 *
 * Socket space: the hole is HoleHalfCm around the origin in X (along the hull)
 * and Y, the hull's outer face at SurfaceZ, +Z out. The contour is cut into
 * SegmentCm pieces. A piece takes the seam only where the plate lies on the
 * edge: a trace up through the hole just inside the edge meets the plate within
 * MaxGapCm of the outer face. Coverage: a grid of traces up through the hole,
 * the share that meets the plate.
 *
 * TackFraction of the contour welded: the plate is held by the seam (it stops
 * simulating where it lies; the strap can go). SealFraction welded with
 * SealCoverage covered and no foreign junk in CleanZone: the breach counts as
 * sealed (bFilled, the panel's hull lamp). Junk inside: the torch does not take.
 * Cutting takes the seam back; under the tack the plate is loose again.
 */
UCLASS()
class MINDKORE_API AJamPlateSocket : public AActor
{
	GENERATED_BODY()

public:
	AJamPlateSocket();

	/** Where plates are looked for (and the contour drawn), socket space. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam|Ship")
	TObjectPtr<UBoxComponent> Catch = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship")
	FName RequiredTag = TEXT("Hull.Plate");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship")
	TObjectPtr<AJamCleanZone> CleanZone = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship")
	FVector2D HoleHalfCm = FVector2D(125.0f, 125.0f);

	/** The hull's outer face above the socket's origin, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship")
	float SurfaceZ = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship|Weld", meta = (ClampMin = "2.0"))
	float SegmentCm = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship|Weld", meta = (ClampMin = "0.0"))
	float MaxGapCm = 3.0f;

	/** The torch melts the contour within this of its point, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship|Weld", meta = (ClampMin = "1.0"))
	float TorchRadiusCm = 12.0f;

	/** One piece of seam takes this long under the torch, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship|Weld", meta = (ClampMin = "0.01"))
	float SecondsPerSegment = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship|Weld", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TackFraction = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship|Weld", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SealFraction = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship|Weld", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SealCoverage = 0.9f;

	/** The contour is drawn (and coverage measured) while a plate is in Catch or the player is this close, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship|Weld", meta = (ClampMin = "0.0"))
	float ShowContourCm = 600.0f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Jam|Ship")
	bool bFilled = false;

	/** The torch at WorldPoint for DeltaSeconds: lays (or with bCut takes back) the seam within TorchRadiusCm. */
	EJamWeldResult Weld(const FVector& WorldPoint, float DeltaSeconds, bool bCut);

	/** Welded share of the contour, 0..1. */
	float GetSeamFraction() const;
	/** Share of the hole the plate covers, 0..1 (last measured). */
	float GetCoverage() const { return Coverage; }
	/** The plate over the hole now (last measured), or null. */
	AActor* GetPlate() const { return Plate.Get(); }
	int32 JunkCount() const;
	bool IsTacked() const { return bTacked; }

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	/** Contour pieces in socket space (centre on the edge, outward normal in XY) and their seam, 0..1. */
	TArray<FVector2D> SegmentCentre;
	TArray<FVector2D> SegmentNormal;
	TArray<float> Seam;
	TArray<double> LastWelded;
	/** Gap at each piece, last measured (cm; huge where no plate). */
	TArray<float> Gap;
	float Coverage = 0.0f;
	TWeakObjectPtr<AActor> Plate;
	bool bTacked = false;
	double NextMeasure = 0.0;

	/** The first thing a trace up through the hole at Local (socket XY) meets: its actor and height over the outer face. */
	AActor* ProbeUp(const FVector2D& Local, float& OutGapCm) const;
	void Measure();
	void UpdateState();
	void DrawContour() const;
};

/**
 * The cabin's control panel and launch lever. Three lamps - hull (all
 * HullSockets filled), fuel (FuelDock complete), navigation (NavDock
 * complete) - red until done, green after. The lever (E on the panel) lists
 * what is missing; with all three done it ends the run (AJamShipGameMode).
 */
UCLASS()
class MINDKORE_API AJamShipPanel : public AActor, public IMKInteractable
{
	GENERATED_BODY()

public:
	AJamShipPanel();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam|Ship")
	TObjectPtr<UStaticMeshComponent> Desk = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam|Ship")
	TObjectPtr<UStaticMeshComponent> Lever = nullptr;

	/** Hull, fuel, navigation, left to right. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam|Ship")
	TArray<TObjectPtr<UStaticMeshComponent>> Lamps;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship")
	TArray<TObjectPtr<AJamPlateSocket>> HullSockets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship")
	TObjectPtr<AJamDockZone> FuelDock = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship")
	TObjectPtr<AJamDockZone> NavDock = nullptr;

	int32 HullDone() const;
	int32 FuelDone() const;
	int32 NavDone() const;
	int32 FuelNeeded() const;
	int32 NavNeeded() const;
	bool IsHullDone() const { return HullSockets.Num() > 0 && HullDone() >= HullSockets.Num(); }
	bool IsFuelDone() const { return FuelDock && FuelDock->IsCompleted(); }
	bool IsNavDone() const { return NavDock && NavDock->IsCompleted(); }

	virtual FMKInteractionOffer QueryInteraction(const AActor* Hand) const override;
	virtual void CompleteInteraction(AActor* Hand) override;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> LampMaterials;

	int32 LampState = -1;
};

/**
 * A rock of the debris belt: static until broken. Only a round's push counts
 * (the impulse a gun gives what it hits, in N s): each hit costs
 * max(0, impulse - ImpulseThreshold) of Health, so a pistol (30 N s) barely
 * scratches, a shotgun (8 pellets of 40) breaks one in a few shots and the
 * sniper (600) in one. Broken, it falls apart into SplitCount small loose
 * pieces thrown outward.
 */
UCLASS()
class MINDKORE_API AJamBreakableRock : public AActor, public IMKImpactReceiver
{
	GENERATED_BODY()

public:
	AJamBreakableRock();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam|Rock")
	TObjectPtr<UStaticMeshComponent> Mesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Rock", meta = (ClampMin = "1.0"))
	float Health = 600.0f;

	/** N s of a hit's push that the rock shrugs off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Rock", meta = (ClampMin = "0.0"))
	float ImpulseThreshold = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Rock", meta = (ClampMin = "0"))
	int32 SplitCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Rock", meta = (ClampMin = "0.1"))
	float PieceMassKg = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Rock", meta = (ClampMin = "0.0"))
	float PieceSpeedCm = 250.0f;

	virtual void ReceiveResolvedImpact_Implementation(const FMKResolvedImpact& ResolvedImpact) override;

private:
	void Break(const FVector& Direction);
	bool bBroken = false;
};
