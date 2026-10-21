#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MKInteractable.h"
#include "MKPhysicsProp.h"
#include "JamWeapons.h"
#include "JamPickups.generated.h"

class UStaticMeshComponent;

/** The tool id of the grapple (AJamToolPickup::ToolId, AJamShipGameMode::Tools). */
inline FName MKJamGrappleTool() { return FName(TEXT("Grapple")); }

/**
 * A static thing that gives something once on F (a locker with a gun, a tool,
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

/** Which gun a weapon pickup hands over in its slot. */
UENUM(BlueprintType)
enum class EJamPickupGun : uint8
{
	/** The slot's own gun, as the pawn has it: the pickup only unlocks it. */
	Slot UMETA(DisplayName = "Как в слоте"),
	/** The RMB-93 «Рысь» (JamMakeRmb93) in place of the pump shotgun, with a full magazine. */
	Rmb93 UMETA(DisplayName = "Рысь (RMB-93)"),
};

/**
 * Unlocks a gun (0 pistol, 1 shotgun, 2 sniper) in the UJamWeaponsComponent of whoever takes it:
 * the zero-G pawn or a walker. Gun = Rmb93 first puts the RMB-93 into the slot (built in code from
 * the pump shotgun's defaults, so tuning it never needs the map rebuilt).
 */
UCLASS()
class MINDKORE_API AJamWeaponPickup : public AJamPickupBase
{
	GENERATED_BODY()

public:
	AJamWeaponPickup();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Pickup", meta = (ClampMin = "0"))
	int32 WeaponIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Pickup")
	EJamPickupGun Gun = EJamPickupGun::Slot;

protected:
	virtual void BeginPlay() override;
	virtual FText RefusalFor(const AActor* Hand) const override;
	virtual bool Give(AActor* Hand) override;

private:
	/** Gun = Rmb93: its definition and aim, built at BeginPlay so the meshes load with the level, not on F. */
	UPROPERTY(Transient)
	FJamWeapon Replacement;

	UPROPERTY(Transient)
	FJamGunAim ReplacementAim;

	bool bHasReplacement = false;
};

/** Unlocks a tool by name (a flag in AJamShipGameMode::Tools): MKJamGrappleTool lets Left Alt hook (AJamZeroGPawn). */
UCLASS()
class MINDKORE_API AJamToolPickup : public AJamPickupBase
{
	GENERATED_BODY()

public:
	AJamToolPickup();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Pickup")
	FName ToolId = MKJamGrappleTool();

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

/** Valuable cargo (tag Cargo): a small loose prop; F takes it and counts it in AJamShipGameMode. */
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
