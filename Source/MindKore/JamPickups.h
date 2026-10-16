#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MKInteractable.h"
#include "MKPhysicsProp.h"
#include "JamPickups.generated.h"

class UStaticMeshComponent;

/**
 * A static thing that gives something once on E (a locker with a gun, a tool,
 * a suit). Subclasses say what (Give); after a successful give the pickup
 * offers nothing more and, when bHideWhenTaken, disappears. Blockout: an
 * engine cube until art replaces the mesh.
 */
UCLASS(Abstract)
class MINDKORE_API AJamPickupBase : public AActor, public IMKInteractable
{
	GENERATED_BODY()

public:
	AJamPickupBase();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam|Pickup")
	TObjectPtr<UStaticMeshComponent> Mesh = nullptr;

	/** The verb under the reticle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Pickup")
	FText ActionLabel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Pickup")
	bool bHideWhenTaken = true;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Jam|Pickup")
	bool bTaken = false;

	virtual FMKInteractionOffer QueryInteraction(const AActor* Hand) const override;
	virtual void CompleteInteraction(AActor* Hand) override;

protected:
	/** Why Hand cannot take it now (empty = it can). */
	virtual FText RefusalFor(const AActor* Hand) const { return FText::GetEmpty(); }
	/** Hand the thing over; true when given. */
	virtual bool Give(AActor* Hand) { return false; }
};

/** Unlocks a gun of AJamZeroGPawn (0 pistol, 1 shotgun, 2 sniper): on the floating pawn, or, for a walker in the ship mode, in its progress. */
UCLASS()
class MINDKORE_API AJamWeaponPickup : public AJamPickupBase
{
	GENERATED_BODY()

public:
	AJamWeaponPickup();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Pickup", meta = (ClampMin = "0"))
	int32 WeaponIndex = 0;

protected:
	virtual FText RefusalFor(const AActor* Hand) const override;
	virtual bool Give(AActor* Hand) override;
};

/** Unlocks a tool by name (a flag in AJamShipGameMode's progress; no mechanic yet). */
UCLASS()
class MINDKORE_API AJamToolPickup : public AJamPickupBase
{
	GENERATED_BODY()

public:
	AJamToolPickup();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Pickup")
	FName ToolId = TEXT("Grapple");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Pickup")
	FText ToolName;

protected:
	virtual bool Give(AActor* Hand) override;
};

/** The suit: puts the key MKJamSuitKey ("Jam.Suit") on the hand's keyring - the airlock's outer door asks for it. */
UCLASS()
class MINDKORE_API AJamSuitLocker : public AJamPickupBase
{
	GENERATED_BODY()

public:
	AJamSuitLocker();

protected:
	virtual FText RefusalFor(const AActor* Hand) const override;
	virtual bool Give(AActor* Hand) override;
};

/** Valuable cargo (tag Cargo): a small loose prop; E takes it and counts it in AJamShipGameMode. */
UCLASS()
class MINDKORE_API AJamCargo : public AMKPhysicsProp
{
	GENERATED_BODY()

public:
	AJamCargo();

	virtual FMKInteractionOffer QueryInteraction(const AActor* Hand) const override;
	virtual void CompleteInteraction(AActor* Hand) override;
};

/** The keyring key that is the suit. */
inline FName MKJamSuitKey() { return FName(TEXT("Jam.Suit")); }
