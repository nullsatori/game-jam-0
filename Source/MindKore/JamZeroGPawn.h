#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "JamCollisionImpact.h"
#include "MKImpactReceiver.h"
#include "JamZeroGPawn.generated.h"

class UCameraComponent;
class UInstancedStaticMeshComponent;
class UInputAction;
class UInputMappingContext;
class UJamOxygenComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPhysicsConstraintComponent;
class UMKImpactDataAsset;
class UMKInteractionComponent;
class UMKKeyringComponent;
class UMKVitalsComponent;
class UPhysicalMaterial;
class USoundBase;
class USphereComponent;
struct FInputActionValue;

/**
 * The zero-G player: a simulated sphere with its rotation locked, gravity
 * off and no damping, so momentum is kept until something changes it. The
 * camera follows the control rotation; the body never turns.
 *
 * Ways to move, strongest first: push off what is in reach (RMB) - against a
 * wall you fly, against light junk the junk flies; throw what you hold (LMB),
 * taking the opposite momentum; the recoil of a shot (LMB, empty hands).
 *
 * Holding is physical: a grab (E on a simulated body) joins the pawn's body
 * and the prop at the grab point with a soft linear spring (angular free), so
 * a heavy part drags the pawn as much as the pawn drags it. E, F or RMB lets
 * go; LMB throws with equal and opposite impulses.
 *
 * Towing: T throws a rope (a soft spherical distance limit, TetherLengthCm)
 * to the loose body under the view within TetherReachCm; the part follows on
 * the rope wherever you push or thrust. T again cuts it.
 *
 * LMB and RMB act on press (the pistol fires while LMB is held). Holding
 * Left Shift aims: the pawn draws where a push and the current gun's recoil
 * would send it, or where a throw sends the held prop - a dotted line, the
 * first contact marked, one bounce after it.
 *
 * Only the mouse turns the view; the body's rotation is locked, so hits, the
 * grab and the rope move it but never turn it.
 *
 * Crashes count: a hit above FeedbackMinSpeedCm thumps (sound, camera shake),
 * the blow is delivered to what was hit (CrashImpact, the same path as a
 * round), and above CrashDamageMinSpeedCm the pawn hurts itself.
 * Thrusters (WASD, Space/Ctrl, view-relative) are the precise movement: they
 * accelerate along the input direction until the velocity along it reaches
 * ThrustMaxSpeedCm and never brake anything else, so a push or a recoil on
 * top is not cancelled. X stabilises: a smooth brake to standstill.
 *
 * Space dust (a cube of tiny specks wrapped around the camera) shows motion.
 *
 * All velocity changes go into the body as impulses in kg cm/s, so they read
 * the body's mass (default 100 kg): the sniper's recoil 40000 is 4 m/s.
 *
 * Guns are movement: three weapons (1/2/3, mouse wheel), each an FJamWeapon
 * of tunables. Every shot kicks the pawn back along the view and pushes what
 * it hits; R reloads. Guns cost no oxygen.
 */
/** One gun of the zero-G pawn. Impulses are kg cm/s (velocity change = impulse / mass). */
USTRUCT(BlueprintType)
struct FJamWeapon
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon")
	FText Name;

	/** Per pellet, before the hit zone's multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "0.0"))
	float Damage = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "0.01"))
	float FireInterval = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "1"))
	int32 MagazineSize = 12;

	/** Rounds outside the magazine at the start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "0"))
	int32 Reserve = 60;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "0.0"))
	float ReloadSeconds = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "1"))
	int32 Pellets = 1;

	/** Half-angle of the pellet cone, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "0.0"))
	float SpreadDeg = 0.0f;

	/** Kick on the pawn per shot, along -view. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "0.0"))
	float RecoilImpulse = 1500.0f;

	/** Push on a hit body per pellet, at the hit point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "0.0"))
	float HitImpulse = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "100.0"))
	float RangeCm = 8000.0f;

	/** Keeps firing while LMB is held, at FireInterval. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon")
	bool bAutomatic = false;

	/** Pitch of the blast sound: a small gun cracks, a big one booms. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "0.1"))
	float SoundPitch = 1.0f;

	/** Rounds in the magazine now. Set to MagazineSize on BeginPlay. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Jam|Weapon")
	int32 Ammo = 0;
};

UCLASS()
class MINDKORE_API AJamZeroGPawn : public APawn, public IMKImpactReceiver
{
	GENERATED_BODY()

public:
	AJamZeroGPawn();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam")
	TObjectPtr<USphereComponent> Body = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam")
	TObjectPtr<UCameraComponent> Camera = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam")
	TObjectPtr<UMKInteractionComponent> Interaction = nullptr;

	/** Tiny specks kept in a cube around the camera (world space), streaming past as you move. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam")
	TObjectPtr<UInstancedStaticMeshComponent> Dust = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam")
	TObjectPtr<UMKKeyringComponent> Keyring = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam")
	TObjectPtr<UMKVitalsComponent> Vitals = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam")
	TObjectPtr<UJamOxygenComponent> Oxygen = nullptr;

	// --- view ---------------------------------------------------------------

	/** Horizontal field of view, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|View", meta = (ClampMin = "60.0", ClampMax = "150.0"))
	float CameraFOV = 105.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|View", meta = (ClampMin = "0"))
	int32 DustCount = 300;

	/** Edge of the dust cube around the camera, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|View", meta = (ClampMin = "100.0"))
	float DustCubeCm = 2000.0f;

	/** Size of one speck, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|View", meta = (ClampMin = "0.1"))
	float DustSpeckCm = 2.0f;

	/** Unlit speck material (made by Scripts/build_zerog.py); loaded at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|View")
	TSoftObjectPtr<UMaterialInterface> DustMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Jam/Materials/M_Jam_Dust.M_Jam_Dust")));

	// --- body ---------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Body", meta = (ClampMin = "1.0"))
	float BodyMassKg = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Body", meta = (ClampMin = "0.0"))
	float BodyFriction = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Body", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BodyRestitution = 0.3f;

	/** 0 = no cap. Clamped every tick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Body", meta = (ClampMin = "0.0"))
	float MaxSpeedCm = 1200.0f;

	// --- push-off (RMB) --------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Push", meta = (ClampMin = "10.0"))
	float PushReachCm = 180.0f;

	/** kg cm/s. Off a static thing all of it is yours: 30000 / 100 kg = 300 cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Push", meta = (ClampMin = "0.0"))
	float PushImpulse = 30000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Push", meta = (ClampMin = "0.0"))
	float PushCooldownSeconds = 0.35f;

	// --- grab (E) and throw (LMB while holding) --------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Grab", meta = (ClampMin = "10.0"))
	float GrabReachCm = 250.0f;

	/** Only absurd masses are refused: a heavy part is grabbed and drags you. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Grab", meta = (ClampMin = "1.0"))
	float MaxGrabMassKg = 5000.0f;

	/** Spring of the grab joint, force mode (kg/s^2): with 100 kg + 800 kg about 15 rad/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Grab", meta = (ClampMin = "0.0"))
	float GrabStiffness = 20000.0f;

	/** Damping of the grab joint, force mode (kg/s): about critical for a heavy part, overdamped for a light one - no wobble. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Grab", meta = (ClampMin = "0.0"))
	float GrabDamping = 3000.0f;

	/** The held thing's grab point is pulled to this far in front of the camera, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Grab", meta = (ClampMin = "0.0"))
	float HoldDistanceCm = 150.0f;

	/** Off: the held thing stays where it was grabbed, relative to the pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Grab")
	bool bHoldInFront = true;

	/** kg cm/s given to the prop and taken by the pawn on a throw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Grab", meta = (ClampMin = "0.0"))
	float ThrowImpulse = 8000.0f;

	/** A light prop is not thrown faster than this (the impulse shrinks to fit). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Grab", meta = (ClampMin = "1.0"))
	float MaxThrowSpeedCm = 1100.0f;

	// --- tether (T) ---------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Tether", meta = (ClampMin = "10.0"))
	float TetherReachCm = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Tether", meta = (ClampMin = "10.0"))
	float TetherLengthCm = 800.0f;

	/** Soft limit spring when the rope is taut (Chaos soft linear limit). 0 = a hard limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Tether", meta = (ClampMin = "0.0"))
	float TetherStiffness = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Tether", meta = (ClampMin = "0.0"))
	float TetherDamping = 50.0f;

	// --- prediction (while Left Shift is held) --------------------------------------

	/** How far ahead the path is drawn, seconds of flight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Prediction", meta = (ClampMin = "0.1"))
	float PredictSeconds = 5.0f;

	/** Normal velocity kept on the predicted bounce (the real one depends on both materials). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Prediction", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PredictRestitution = 0.3f;

	// --- crashes ------------------------------------------------------------------

	/** The pawn's blows on what it hits (glass breaks, dummies hurt). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Crash")
	FJamCollisionImpactSettings CrashImpact;

	/** A hit faster than this thumps: sound and camera shake, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Crash", meta = (ClampMin = "0.0"))
	float FeedbackMinSpeedCm = 250.0f;

	/** A hit faster than this hurts the pawn itself, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Crash", meta = (ClampMin = "0.0"))
	float CrashDamageMinSpeedCm = 600.0f;

	/** Self damage per cm/s over CrashDamageMinSpeedCm. The speed is the velocity change along the normal, bounce included: a head-on 1000 cm/s at restitution 0.3 reads ~1300 and costs ~35. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Crash", meta = (ClampMin = "0.0"))
	float CrashDamagePerCmS = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Crash")
	TObjectPtr<USoundBase> CrashSound = nullptr;

	/** Camera shake amplitude per 1000 cm/s of hit, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Crash", meta = (ClampMin = "0.0"))
	float ShakeCmPer1000 = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Crash", meta = (ClampMin = "0.0"))
	float ShakeSeconds = 0.25f;

	// --- guns (LMB, empty hands) ------------------------------------------------

	/** 1 pistol, 2 shotgun, 3 sniper by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Shot")
	TArray<FJamWeapon> Weapons;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Jam|ZeroG|Shot")
	int32 CurrentWeapon = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Shot")
	TObjectPtr<UMKImpactDataAsset> ImpactData = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Shot")
	TArray<TObjectPtr<USoundBase>> ShotSounds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Shot")
	TObjectPtr<UMaterialInterface> DefaultImpactDecal = nullptr;

	// --- thrusters and stabiliser ------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Thrusters")
	bool bThrustersEnabled = true;

	/** Acceleration along the input direction while a thrust key is held, cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Thrusters", meta = (ClampMin = "0.0"))
	float ThrustAccelCm = 600.0f;

	/** Thrust stops adding once the velocity along the input direction reaches this, cm/s (it never brakes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Thrusters", meta = (ClampMin = "0.0"))
	float ThrustMaxSpeedCm = 500.0f;

	/** X: the stabiliser's deceleration cap, cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Thrusters", meta = (ClampMin = "0.0"))
	float StabiliserAccelCm = 300.0f;

	/** X: deceleration = speed x this (1/s), up to the cap - an ease-out near standstill. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Thrusters", meta = (ClampMin = "0.0"))
	float StabiliserRate = 2.0f;

	// --- bullet time presentation (as AJamCharacter) --------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|BulletTime")
	TObjectPtr<UMaterialInterface> BulletTimeMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|BulletTime")
	TObjectPtr<USoundBase> BulletTimeEntrySound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|BulletTime")
	TObjectPtr<USoundBase> BulletTimeExitSound = nullptr;

	// --- actions ---------------------------------------------------------------

	/** Push off whatever the view meets within PushReachCm. False when nothing is in reach or on cooldown. */
	UFUNCTION(BlueprintCallable, Category = "Jam|ZeroG")
	bool Push();

	/** Fire the current weapon: hitscan pellets, recoil, a push on what is hit. False when it cannot fire yet. */
	UFUNCTION(BlueprintCallable, Category = "Jam|ZeroG")
	bool Fire();

	UFUNCTION(BlueprintCallable, Category = "Jam|ZeroG")
	void SelectWeapon(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "Jam|ZeroG")
	void Reload();

	const FJamWeapon* GetWeapon() const { return Weapons.IsValidIndex(CurrentWeapon) ? &Weapons[CurrentWeapon] : nullptr; }

	/** Seconds left of a reload; 0 when not reloading. */
	float GetReloadRemaining() const;

	/** Join Target to the body at Point (world). False when something is held, Target does not simulate or is absurdly heavy. */
	UFUNCTION(BlueprintCallable, Category = "Jam|ZeroG")
	bool Grab(UPrimitiveComponent* Target, FVector Point);

	UFUNCTION(BlueprintCallable, Category = "Jam|ZeroG")
	void Release();

	/** Throw the held prop and take the opposite momentum. */
	UFUNCTION(BlueprintCallable, Category = "Jam|ZeroG")
	void ThrowHeld();

	UFUNCTION(BlueprintPure, Category = "Jam|ZeroG")
	UPrimitiveComponent* GetGrabbed() const { return GrabbedBody.Get(); }

	/** Rope to Target's origin. False when Target does not simulate or is out of reach. */
	UFUNCTION(BlueprintCallable, Category = "Jam|ZeroG")
	bool AttachTether(UPrimitiveComponent* Target);

	UFUNCTION(BlueprintCallable, Category = "Jam|ZeroG")
	void DetachTether();

	UFUNCTION(BlueprintPure, Category = "Jam|ZeroG")
	UPrimitiveComponent* GetTethered() const { return TetheredBody.Get(); }

	/** The simulated body E would grab now (null when none or something is held). */
	UPrimitiveComponent* GetGrabCandidate() const { return GrabCandidate.Get(); }

	UFUNCTION(BlueprintCallable, Category = "Jam")
	void ToggleCrowbar();

	UFUNCTION(BlueprintPure, Category = "Jam")
	bool HasCrowbar() const;

	UFUNCTION(BlueprintPure, Category = "Jam|ZeroG")
	FVector GetBodyVelocity() const;

	UFUNCTION(BlueprintPure, Category = "Jam|ZeroG")
	float GetHeldMassKg() const;

	UFUNCTION(BlueprintPure, Category = "Jam|ZeroG")
	float GetPushCooldownRemaining() const;

	/** Console probe: aim (as Left Shift) or stop; the next drawn path logs its first contact. */
	void ProbeAim(bool bAim);

	/** Console probe: thrust input for the next tick (view frame: X forward, Y right, Z up) and the stabiliser. */
	void ProbeThrust(const FVector& Input, bool bStabilise) { ThrustInput = Input; bStabilising = bStabilise; }

	virtual void ReceiveResolvedImpact_Implementation(const FMKResolvedImpact& ResolvedImpact) override;

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;

protected:
	virtual void BeginPlay() override;

	/** The body takes an impulse in kg cm/s (velocity change = Impulse / mass). */
	void AddBodyImpulse(const FVector& Impulse);

	FVector ViewLocation() const;
	FVector ViewDirection() const;
	void RefreshGrabCandidate();

	UPROPERTY(Transient)
	TObjectPtr<UPhysicsConstraintComponent> GrabConstraint = nullptr;

	TWeakObjectPtr<UPrimitiveComponent> GrabbedBody;
	/** Where the grab point sat from the body's centre at the grab, body space: the target when not holding in front. */
	FVector GrabOffsetLocal = FVector::ZeroVector;
	void UpdateHoldTarget();
	void UpdateDust();

	UPROPERTY(Transient)
	TObjectPtr<UPhysicsConstraintComponent> TetherConstraint = nullptr;

	TWeakObjectPtr<UPrimitiveComponent> TetheredBody;
	TWeakObjectPtr<UPrimitiveComponent> GrabCandidate;
	FVector GrabCandidatePoint = FVector::ZeroVector;

	// Input state, consumed in Tick.
	FVector ThrustInput = FVector::ZeroVector;
	bool bStabilising = false;
	double LastPushSeconds = -100.0;

private:
	void BuildInput();
	void Look(const FInputActionValue& Value);
	void Thrust(const FInputActionValue& Value);
	void Lift(const FInputActionValue& Value);
	void PrimaryPressed();
	void PushPressed();
	void PutDown();
	void FireRay(const FJamWeapon& Weapon, const FVector& Direction);
	double NextShotSeconds = 0.0;
	double ReloadEndSeconds = 0.0;
	UFUNCTION()
	void HandleBodyHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);
	double LastCrashSeconds = -100.0;
	float ShakeRemaining = 0.0f;
	float ShakeAmplitude = 0.0f;
	void DrawPrediction() const;
	void DrawPath(const FVector& Start, const FVector& Velocity, float RadiusCm, const AActor* Ignored, const FColor& Color) const;

	bool bAiming = false;
	mutable bool bLogNextPrediction = false;
	void ToggleTether();
	void DrawTether() const;
	void Use();
	void BulletTimePressed();
	void BulletTimeReleased();
	void ApplyThrusters(float DeltaSeconds);
	void UpdateBulletTimePresentation();

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> BodyMaterial = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> Mapping = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> Actions;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BulletTimeMID = nullptr;

	bool bBulletTimeWasSlowed = false;
};
