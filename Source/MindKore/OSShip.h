#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "OSGameMode.h"
#include "OSGravity.h"
#include "OSShip.generated.h"

class UBoxComponent;
class AVolume;
class AOSZeroGPawn;

/**
 * Turns the view relative to the pawn's gravity: the view is kept as pitch
 * and yaw in a frame that is carried along (quaternions) as gravity turns,
 * and that gravity eases toward the pawn's - so on the planetoid the mouse
 * looks around the local up, landing turns the world under you smoothly,
 * and walking around the sphere keeps the view level. Outside every gravity
 * zone optional free rotation turns the view around its own axes (the physics
 * body never turns). On return the frame is built upright for the gravity
 * at once, the view keeps its heading, and the leftover roll fades out.
 */
UCLASS()
class MINDKORE_API AOSShipController : public APlayerController
{
	GENERATED_BODY()

public:
	/** How fast the view's "down" follows a change of gravity, 1/s: 4 is ~95 % in 0.75 s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Walker", meta = (ClampMin = "0.1"))
	float GravityAlignRate = 4.0f;

	/** Speed cap of the turn upright after free rotation (landing upside down), deg/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Walker", meta = (ClampMin = "10.0"))
	float RecoveryMaxDegPerSec = 240.0f;

	virtual void UpdateRotation(float DeltaTime) override;

	/**
	 * A view kick (a shot's recoil, a gun's draw nudge), degrees of pitch and
	 * yaw in the view's own frame. Kept until the next UpdateRotation, which
	 * turns the view by it as by the mouse: around the view's own axes in
	 * free rotation, on the upright view in gravity - so it is never a
	 * foreign write that would rebuild the view from the shown one.
	 */
	void AddViewKick(const FRotator& Kick) { PendingViewKick += Kick; }

	/** The gravity direction the view uses now (eased toward the pawn's). */
	FVector GetViewGravity() const { return SmoothedGravity; }

private:
	FRotator PendingViewKick = FRotator::ZeroRotator;
	FVector SmoothedGravity = FVector::ZeroVector;
	bool bWasFreeBodyRotation = false;
	/** World from the view's local frame, carried along as gravity turns. */
	FQuat GravityFrame = FQuat::Identity;
	/** The view in that frame: pitch and yaw, no roll. */
	FRotator RelativeView = FRotator::ZeroRotator;
	/** World-space turn from the upright view to the one shown: what is left of a free-rotation pose, fading to identity. */
	FQuat RecoveryOffset = FQuat::Identity;
	FRotator LastWrittenRotation = FRotator(1000.0f, 0.0f, 0.0f);
};

/**
 * A place with gravity and/or air. With bWalk the player's pawn walks inside
 * (AOSZeroGPawn::SetGravity) and every loose body falls (OSPullBodies);
 * outside they float. Without bWalk the zone only gives air. A box (the cabin
 * and airlock) or, with SphereRadiusCm > 0, a sphere around the actor (the
 * planetoid: a walking ball inside a wider breathable one). bRadial pulls
 * toward the actor's centre. Where zones overlap, OSGravityAt / OSAirZoneAt
 * (OSGravity.h) pick the higher Priority, then the smaller zone.
 */
/** A zone's fog (v15): none, a cold layer on the floor, or a thin even haze through the room. */
UENUM(BlueprintType)
enum class EOSZoneFog : uint8
{
	None,
	Floor,
	Haze,
};

UCLASS()
class MINDKORE_API AOSGravityZone : public AActor
{
	GENERATED_BODY()

public:
	AOSGravityZone();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OS|Ship")
	TObjectPtr<UBoxComponent> Zone = nullptr;

	/** Optional editable UE volume defining the actual boundary. Takes precedence over primitive shapes. Use a TriggerVolume, not a BlockingVolume. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "OS|Ship")
	TObjectPtr<AVolume> BoundaryVolume = nullptr;

	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	/** Creates an editable TriggerVolume from the current box/oval dimensions. Existing boundaries are selected, never rebuilt. */
	UFUNCTION(CallInEditor, Category = "OS|Ship")
	void CreateEditableBoundary();

	UFUNCTION(CallInEditor, Category = "OS|Ship")
	void RebuildBoundaryCollision();
#endif

	/** > 0: the zone is this sphere instead of the box, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Ship", meta = (ClampMin = "0.0"))
	float SphereRadiusCm = 0.0f;

	/** Oval footprint with flat floor/ceiling. Zone Box Extent X/Y are ellipse radii, Z is half-height. SphereRadiusCm takes precedence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Ship")
	bool bEllipticalCylinder = false;

	/** Inside, the player walks with gravity; off: air only, the player keeps floating. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Ship")
	bool bWalk = true;

	/** Gravity strength while walking here, cm/s^2 (980 = Earth). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Ship", meta = (ClampMin = "0.0"))
	float GravityCm = 980.0f;

	/** Gravity toward the actor's centre (a planetoid) instead of world down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Ship")
	bool bRadial = false;

	/** Not radial: pull along the actor's own down axis (a tilted room) instead of world down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Ship")
	bool bPullAlongActorDown = false;

	/** Where zones overlap, the higher Priority gives the gravity and the air; on a tie, the smaller zone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Ship")
	int32 Priority = 0;

	/** Breathing restores oxygen here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Ship")
	bool bBreathable = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Ship", meta = (ClampMin = "0.0"))
	float RefillPerSecond = 5.0f;

	/**
	 * The smoke's haze here (v12 7a): an old shot's gas stays in the zone's air and thins with this half-life, s (the
	 * room's ventilation); 0: no haze, the gas fades as it ages.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Ship", meta = (ClampMin = "0.0"))
	float HazeHalfLifeSeconds = 45.0f;

	/** The zone's own fog (v15): the gas system keeps it in the room's air as its background, held against ventilation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Fog")
	EOSZoneFog Fog = EOSZoneFog::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Fog", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FogDensity = 0.5f;

	/** A floor layer's height, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Fog", meta = (ClampMin = "30.0", ClampMax = "120.0"))
	float FogHeightCm = 60.0f;

	/** Set from the fog's kind when it is picked in the editor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Fog")
	FLinearColor FogColour = FLinearColor(0.88f, 0.88f, 0.88f);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Shown on the HUD while inside (empty: nothing). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Ship")
	FText HudText;

	/** Is Location inside? InsetCm shrinks primitive shapes; brush volumes use their exact boundary (negative inset grows it). */
	UFUNCTION(BlueprintPure, Category = "OS|Ship")
	bool Contains(const FVector& Location, float InsetCm) const;

	/** Gravity direction (unit) at Location. */
	FVector GravityAt(const FVector& Location) const;

	/** What bounds the zone for overlap queries: its sphere, or the world box around its box, oval or boundary volume. */
	FCollisionShape GetQueryShape(FVector& OutCentre) const;

	/** The size that breaks priority ties, cm^3: of the query shape. */
	double GetVolumeCm3() const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOSLaunchAttempt);

/**
 * The ship level's mode («Почини и улети»): one physics pawn (AOSZeroGPawn)
 * for the whole run - every tick it gets the gravity of the AOSGravityZone it
 * is in (walking) or none (zero-G), and the air of the zone; every loose body
 * in a walking zone gets the same gravity (OSPullBodies). Shows toasts,
 * counts cargo, and ends the run when the launch lever is pulled
 * (AOSShipPanel): it broadcasts OnLaunchAttempt (the hook for the planetoid
 * boss, not built yet), then shows «Улетел» and restarts. The guns start
 * locked (the pawn's default); pickups unlock them. It owns death: the
 * player's vitals report it (OnDepleted), the mode revives the same pawn in
 * the cabin (RespawnInCabin).
 */
UCLASS()
class MINDKORE_API AOSShipGameMode : public AOSZeroGGameMode
{
	GENERATED_BODY()

public:
	AOSShipGameMode();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Ship", meta = (ClampMin = "0.0"))
	float ToastSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Ship", meta = (ClampMin = "0.0"))
	float EndRestartSeconds = 6.0f;

	/** How many AOSCargo the level holds (for «Груз n/N»), counted at StartPlay. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Ship")
	int32 CargoTotal = 0;

	/** Cargo taken this run. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Ship")
	int32 Cargo = 0;

	/** Tools taken this run (AOSToolPickup): OSGrappleTool lets Left Alt hook. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Ship")
	TArray<FName> Tools;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "OS|Ship")
	bool bEnded = false;

	/** The lever was pulled with all three components done. The planetoid boss will hang here. */
	UPROPERTY(BlueprintAssignable, Category = "OS|Ship")
	FOSLaunchAttempt OnLaunchAttempt;

	void Toast(const FText& Text);
	FText GetToast() const;

	/** The zone the pawn is in now (OSAirZoneAt), or null (for the HUD). */
	const AOSGravityZone* ZoneOf(const APawn* Pawn) const;

	/** The launch lever: the run is won. */
	void EndGame();

	/** Dead to awake in the cabin, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Ship", meta = (ClampMin = "0.0"))
	float RespawnDelaySeconds = 3.0f;

	/**
	 * After death: the same pawn wakes at the PlayerStart tagged Cabin (any
	 * PlayerStart, with a warning, when none is), with full health and air.
	 * Everything it had stays - the suit key, the guns and their ammo, the tools,
	 * the strap's lines - and so does the world: welded seams, a cleaned hull
	 * (Docs/HullRepair.md).
	 */
	void RespawnInCabin(AOSZeroGPawn* Pawn);

	virtual void Tick(float DeltaSeconds) override;
	virtual void StartPlay() override;
	/** Listens to the player's vitals: their OnDepleted is the death this mode decides. */
	virtual void SetPlayerDefaults(APawn* PlayerPawn) override;

private:
	/** The vitals hit zero: the body goes limp and lets go of everything, the input stops; RespawnDelaySeconds later, RespawnInCabin. */
	void HandleDeath(TWeakObjectPtr<AOSZeroGPawn> Pawn);

	FText ToastText;
	double ToastUntil = 0.0;
};

/** HUD of the ship level: the zero-G HUD plus the three lamps, cargo, the zone's text, toasts and the ending. */
UCLASS()
class MINDKORE_API AOSShipHUD : public AOSZeroGHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
