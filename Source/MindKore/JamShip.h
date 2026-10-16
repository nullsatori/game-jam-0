#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "JamCharacter.h"
#include "JamGameMode.h"
#include "JamShip.generated.h"

class UBoxComponent;
class UJamOxygenComponent;

/**
 * What survives a pawn swap (not a restart): health, keys, flags (actor tags
 * such as MK.Crowbar), unlocked guns, oxygen, cargo, tools. Kept by
 * AJamShipGameMode - one game mode per level, so a restart clears it.
 */
USTRUCT(BlueprintType)
struct FJamProgress
{
	GENERATED_BODY()

	/** Negative: not stored yet (the first pawn keeps its own). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Jam|Progress")
	float Health = -1.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Jam|Progress")
	float Armor = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Jam|Progress")
	TArray<FName> Keys;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Jam|Progress")
	TArray<FName> Flags;

	/** By weapon index of AJamZeroGPawn::Weapons. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Jam|Progress")
	TArray<bool> Weapons;

	/** Negative: not stored yet. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Jam|Progress")
	float Oxygen = -1.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Jam|Progress")
	int32 Cargo = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Jam|Progress")
	TArray<FName> Tools;
};

/** Gravity for the walker in a level whose world gravity is 0; the direction comes from SetGravityDirection. */
UCLASS()
class MINDKORE_API UJamWalkerMovement : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	/** cm/s^2 toward the gravity direction, negative as UE's GravityZ; GravityScale still multiplies it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Walker")
	float WalkerGravityZ = -980.0f;

	virtual float GetGravityZ() const override { return WalkerGravityZ * GravityScale; }
};

/**
 * AJamCharacter that falls in a zero-gravity level: the walker inside an
 * AJamGravityZone. Its capsule is turned every tick so its up is against the
 * gravity direction (straight up in the cabin, away from the planetoid's
 * centre on it). It breathes (UJamOxygenComponent) so oxygen carries over.
 */
UCLASS()
class MINDKORE_API AJamShipWalker : public AJamCharacter
{
	GENERATED_BODY()

public:
	AJamShipWalker(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam")
	TObjectPtr<UJamOxygenComponent> Oxygen = nullptr;

	virtual void Tick(float DeltaSeconds) override;
};

/**
 * Turns the view relative to the walker's gravity (Epic's custom-gravity
 * controller pattern): on the planetoid the mouse looks around the local
 * up, and a change of gravity carries the view along instead of snapping it.
 * With gravity straight down it is the stock behaviour.
 */
UCLASS()
class MINDKORE_API AJamShipController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void UpdateRotation(float DeltaTime) override;

private:
	FVector LastFrameGravity = FVector::ZeroVector;
};

/**
 * A place with gravity and/or air. With bWalk the player walks inside
 * (AJamShipWalker) and floats outside (AJamZeroGPawn); without it the zone
 * only gives air. A box (the cabin and airlock) or, with SphereRadiusCm > 0,
 * a sphere around the actor (the planetoid: a thin walking shell inside a
 * wider warm, breathable one). bRadial pulls toward the actor's centre.
 */
UCLASS()
class MINDKORE_API AJamGravityZone : public AActor
{
	GENERATED_BODY()

public:
	AJamGravityZone();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam|Ship")
	TObjectPtr<UBoxComponent> Zone = nullptr;

	/** > 0: the zone is this sphere instead of the box, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship", meta = (ClampMin = "0.0"))
	float SphereRadiusCm = 0.0f;

	/** Inside, the player walks with gravity; off: air only, the player keeps floating. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship")
	bool bWalk = true;

	/** Gravity strength while walking here, cm/s^2 (980 = Earth; a planetoid much less, so a jump leaves it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship", meta = (ClampMin = "0.0"))
	float GravityCm = 980.0f;

	/** Gravity toward the actor's centre (a planetoid) instead of world down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship")
	bool bRadial = false;

	/** Breathing restores oxygen here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship")
	bool bBreathable = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship", meta = (ClampMin = "0.0"))
	float RefillPerSecond = 5.0f;

	/** Shown on the HUD while inside (empty: nothing). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship")
	FText HudText;

	/** Is Location inside the zone shrunk by InsetCm (negative grows it)? */
	bool Contains(const FVector& Location, float InsetCm) const;

	/** Gravity direction (unit) at Location. */
	FVector GravityAt(const FVector& Location) const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FJamLaunchAttempt);

/**
 * The ship level's mode («Почини и улети»): starts the player walking in the
 * cabin, swaps the pawn at the edge of every AJamGravityZone (walker inside,
 * zero-G pawn outside, same place, velocity and view), carries FJamProgress
 * across the swap, sets gravity and air for the pawn each tick, shows toasts,
 * and ends the run when the launch lever is pulled (AJamShipPanel): it
 * broadcasts OnLaunchAttempt (the hook for the planetoid boss, not built
 * yet), then shows «Улетел» and restarts.
 */
UCLASS()
class MINDKORE_API AJamShipGameMode : public AJamZeroGGameMode
{
	GENERATED_BODY()

public:
	AJamShipGameMode();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship")
	TSubclassOf<APawn> WalkerClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship")
	TSubclassOf<APawn> FloaterClass;

	/** Hysteresis at a zone's edge: leave or enter only this far past it, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship", meta = (ClampMin = "0.0"))
	float SwapMarginCm = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship", meta = (ClampMin = "0.0"))
	float ToastSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship", meta = (ClampMin = "0.0"))
	float EndRestartSeconds = 6.0f;

	/** How many Cargo pickups the level holds (for «Груз n/N»). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship", meta = (ClampMin = "0"))
	int32 CargoTotal = 6;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Jam|Ship")
	FJamProgress Progress;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Jam|Ship")
	bool bEnded = false;

	/** The lever was pulled with all three components done. The planetoid boss will hang here. */
	UPROPERTY(BlueprintAssignable, Category = "Jam|Ship")
	FJamLaunchAttempt OnLaunchAttempt;

	void Toast(const FText& Text);
	FText GetToast() const;

	/** The zone the pawn is in now, or null (for the HUD). */
	const AJamGravityZone* ZoneOf(const APawn* Pawn) const;

	/** The launch lever: the run is won. */
	void EndGame();

	/** Swap the controller's pawn to Class at the same place, velocity and view, carrying Progress. */
	APawn* SwapPawn(APlayerController* PlayerController, TSubclassOf<APawn> Class);

	virtual void Tick(float DeltaSeconds) override;

private:
	/** The first zone containing Location: walking zones only, or any zone. */
	const AJamGravityZone* ZoneAt(const FVector& Location, float InsetCm, bool bWalkOnly = true) const;
	void StoreProgress(APawn* Pawn);
	void ApplyProgress(APawn* Pawn) const;

	FText ToastText;
	double ToastUntil = 0.0;
};

/** HUD of the ship level: the zero-G HUD or a walker's basics, plus the three lamps, cargo, air, toasts and the ending. */
UCLASS()
class MINDKORE_API AJamShipHUD : public AJamZeroGHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
