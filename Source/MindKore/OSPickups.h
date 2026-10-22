#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MKInteractable.h"
#include "MKPhysicsProp.h"
#include "OSWeapons.h"
#include "OSPickups.generated.h"

class UStaticMeshComponent;

/** The tool id of the grapple (AOSToolPickup::ToolId, AOSShipGameMode::Tools). */
inline FName OSGrappleTool() { return FName(TEXT("Grapple")); }

/**
 * A static thing that gives something once on F (a locker with a gun, a tool,
 * a suit). Subclasses say what (Give); after a successful give the pickup
 * offers nothing more and, when bHideWhenTaken, disappears. Blockout: an
 * engine cube until art replaces the mesh.
 */
UCLASS(Abstract)
class MINDKORE_API AOSPickupBase : public AActor, public IMKInteractable
{
	GENERATED_BODY()

public:
	AOSPickupBase();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OS|Pickup")
	TObjectPtr<UStaticMeshComponent> Mesh = nullptr;

	/** The verb under the reticle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Pickup")
	FText ActionLabel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Pickup")
	bool bHideWhenTaken = true;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "OS|Pickup")
	bool bTaken = false;

	virtual FMKInteractionOffer QueryInteraction(const AActor* Hand) const override;
	virtual void CompleteInteraction(AActor* Hand) override;

protected:
	/** Why Hand cannot take it now (empty = it can). */
	virtual FText RefusalFor(const AActor* Hand) const { return FText::GetEmpty(); }
	/** Hand the thing over; true when given. */
	virtual bool Give(AActor* Hand) { return false; }
};

/** Which gun a weapon pickup hands over in its slot. */
UENUM(BlueprintType)
enum class EOSPickupGun : uint8
{
	/** The slot's own gun, as the pawn has it: the pickup only unlocks it. */
	Slot UMETA(DisplayName = "Как в слоте"),
	/** The RMB-93 «Рысь» (OSMakeRmb93) in place of the pump shotgun, with a full magazine. */
	Rmb93 UMETA(DisplayName = "Рысь (RMB-93)"),
};

/**
 * Unlocks a gun (0 pistol, 1 shotgun, 2 sniper) in the UOSWeaponsComponent of whoever takes it:
 * the zero-G pawn or a walker. Gun = Rmb93 first puts the RMB-93 into the slot (built in code from
 * the pump shotgun's defaults, so tuning it never needs the map rebuilt).
 */
UCLASS()
class MINDKORE_API AOSWeaponPickup : public AOSPickupBase
{
	GENERATED_BODY()

public:
	AOSWeaponPickup();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Pickup", meta = (ClampMin = "0"))
	int32 WeaponIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Pickup")
	EOSPickupGun Gun = EOSPickupGun::Slot;

protected:
	virtual void BeginPlay() override;
	virtual FText RefusalFor(const AActor* Hand) const override;
	virtual bool Give(AActor* Hand) override;

private:
	/** Gun = Rmb93: its definition and aim, built at BeginPlay so the meshes load with the level, not on F. */
	UPROPERTY(Transient)
	FOSWeapon Replacement;

	UPROPERTY(Transient)
	FOSGunAim ReplacementAim;

	bool bHasReplacement = false;
};

/** Unlocks a tool by name (a flag in AOSShipGameMode::Tools): OSGrappleTool lets Left Alt hook (AOSZeroGPawn). */
UCLASS()
class MINDKORE_API AOSToolPickup : public AOSPickupBase
{
	GENERATED_BODY()

public:
	AOSToolPickup();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Pickup")
	FName ToolId = OSGrappleTool();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Pickup")
	FText ToolName;

protected:
	virtual bool Give(AActor* Hand) override;
};

/** The suit: puts the key OSSuitKey ("OS.Suit") on the hand's keyring - the airlock's outer door asks for it. */
UCLASS()
class MINDKORE_API AOSSuitLocker : public AOSPickupBase
{
	GENERATED_BODY()

public:
	AOSSuitLocker();

protected:
	virtual FText RefusalFor(const AActor* Hand) const override;
	virtual bool Give(AActor* Hand) override;
};

/** Valuable cargo (tag Cargo): a small loose prop; F takes it and counts it in AOSShipGameMode. */
UCLASS()
class MINDKORE_API AOSCargo : public AMKPhysicsProp
{
	GENERATED_BODY()

public:
	AOSCargo();

	virtual FMKInteractionOffer QueryInteraction(const AActor* Hand) const override;
	virtual void CompleteInteraction(AActor* Hand) override;
};

/** The keyring key that is the suit. */
inline FName OSSuitKey() { return FName(TEXT("OS.Suit")); }
