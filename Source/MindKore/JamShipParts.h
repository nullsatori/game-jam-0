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

/**
 * A breach in the hull. A Hull.Plate prop that enters the box snaps into the
 * socket's transform and turns static - but only while CleanZone counts no
 * foreign junk; otherwise the socket refuses with a HUD toast
 * «В корпусе мусор: n». The plate keeps its scale; place the socket where the
 * plate's centre goes, rotated as the plate (X along the hull, Z out).
 */
UCLASS()
class MINDKORE_API AJamPlateSocket : public AActor
{
	GENERATED_BODY()

public:
	AJamPlateSocket();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam|Ship")
	TObjectPtr<UBoxComponent> Catch = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship")
	FName RequiredTag = TEXT("Hull.Plate");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship")
	TObjectPtr<AJamCleanZone> CleanZone = nullptr;

	/** A refusal is repeated no more often than this while the plate stays, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship", meta = (ClampMin = "0.0"))
	float RefuseRepeatSeconds = 2.0f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Jam|Ship")
	bool bFilled = false;

	virtual void Tick(float DeltaSeconds) override;

private:
	double LastRefuseSeconds = -100.0;
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
