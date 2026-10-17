#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "JamGameMode.h"
#include "JamShip.generated.h"

class UBoxComponent;

/**
 * Turns the view relative to the pawn's gravity: the view is kept as pitch
 * and yaw in a frame that is carried along (quaternions) as gravity turns,
 * and that gravity eases toward the pawn's - so on the planetoid the mouse
 * looks around the local up, landing turns the world under you smoothly,
 * and walking around the sphere keeps the view level. Outside every gravity
 * zone optional free body rotation uses the player's local axes. On return,
 * the gravity frame is rebased to preserve the view before easing upright.
 */
UCLASS()
class MINDKORE_API AJamShipController : public APlayerController
{
	GENERATED_BODY()

public:
	/** How fast the view's "down" follows a change of gravity, 1/s: 4 is ~95 % in 0.75 s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Walker", meta = (ClampMin = "0.1"))
	float GravityAlignRate = 4.0f;

	virtual void UpdateRotation(float DeltaTime) override;

	/** The gravity direction the view uses now (eased toward the pawn's). */
	FVector GetViewGravity() const { return SmoothedGravity; }

private:
	FVector SmoothedGravity = FVector::ZeroVector;
	bool bWasFreeBodyRotation = false;
	/** World from the view's local frame, carried along as gravity turns. */
	FQuat GravityFrame = FQuat::Identity;
	/** The view in that frame: pitch and yaw, no roll. */
	FRotator RelativeView = FRotator::ZeroRotator;
	FRotator LastWrittenRotation = FRotator(1000.0f, 0.0f, 0.0f);
};

/**
 * A place with gravity and/or air. With bWalk the player's pawn walks inside
 * (AJamZeroGPawn::SetGravity) and floats outside; without it the zone only
 * gives air. A box (the cabin and airlock) or, with SphereRadiusCm > 0, a
 * sphere around the actor (the planetoid: a thin walking shell inside a
 * wider breathable one). bRadial pulls toward the actor's centre.
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

	/** Gravity strength while walking here, cm/s^2 (980 = Earth). */
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
 * The ship level's mode («Почини и улети»): one physics pawn (AJamZeroGPawn)
 * for the whole run - every tick it gets the gravity of the AJamGravityZone it
 * is in (walking) or none (zero-G), and the air of the zone. Shows toasts,
 * counts cargo, and ends the run when the launch lever is pulled
 * (AJamShipPanel): it broadcasts OnLaunchAttempt (the hook for the planetoid
 * boss, not built yet), then shows «Улетел» and restarts. The guns start
 * locked; pickups unlock them.
 */
UCLASS()
class MINDKORE_API AJamShipGameMode : public AJamZeroGGameMode
{
	GENERATED_BODY()

public:
	AJamShipGameMode();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship", meta = (ClampMin = "0.0"))
	float ToastSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship", meta = (ClampMin = "0.0"))
	float EndRestartSeconds = 6.0f;

	/** How many Cargo pickups the level holds (for «Груз n/N»). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Ship", meta = (ClampMin = "0"))
	int32 CargoTotal = 6;

	/** Cargo taken this run. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Jam|Ship")
	int32 Cargo = 0;

	/** Tools taken this run (AJamToolPickup; no mechanic yet). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Jam|Ship")
	TArray<FName> Tools;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Jam|Ship")
	bool bEnded = false;

	/** The lever was pulled with all three components done. The planetoid boss will hang here. */
	UPROPERTY(BlueprintAssignable, Category = "Jam|Ship")
	FJamLaunchAttempt OnLaunchAttempt;

	void Toast(const FText& Text);
	FText GetToast() const;

	/** The zone the pawn is in now, or null (for the HUD). */
	const AJamGravityZone* ZoneOf(const APawn* Pawn) const;

	/** The first walking (gravity) zone containing Location, or null. */
	const AJamGravityZone* GravityZoneAt(const FVector& Location) const { return ZoneAt(Location, 0.0f); }

	/** The launch lever: the run is won. */
	void EndGame();

	virtual void Tick(float DeltaSeconds) override;
	virtual void StartPlay() override;

private:
	/** The first zone containing Location: walking zones only, or any zone. */
	const AJamGravityZone* ZoneAt(const FVector& Location, float InsetCm, bool bWalkOnly = true) const;

	FText ToastText;
	double ToastUntil = 0.0;
};

/** HUD of the ship level: the zero-G HUD plus the three lamps, cargo, the zone's text, toasts and the ending. */
UCLASS()
class MINDKORE_API AJamShipHUD : public AJamZeroGHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
