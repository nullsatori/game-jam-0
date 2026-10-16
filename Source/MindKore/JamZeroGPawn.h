#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "MKImpactReceiver.h"
#include "JamZeroGPawn.generated.h"

class UCameraComponent;
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
 * LMB and RMB act on release: while one is held the pawn draws where the
 * action will send it (push off, recoil) or where the throw sends the held
 * prop - a dotted line, the first contact marked, one bounce after it.
 * Weak thrusters (WASD, Space/Ctrl, view-relative) and the stabiliser (X) are
 * a safety net that costs oxygen.
 *
 * All velocity changes go into the body as impulses in kg cm/s, so they read
 * the body's mass (default 100 kg): ShotRecoilImpulse 6000 is 60 cm/s.
 */
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam")
	TObjectPtr<UMKKeyringComponent> Keyring = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam")
	TObjectPtr<UMKVitalsComponent> Vitals = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam")
	TObjectPtr<UJamOxygenComponent> Oxygen = nullptr;

	// --- body ---------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Body", meta = (ClampMin = "1.0"))
	float BodyMassKg = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Body", meta = (ClampMin = "0.0"))
	float BodyFriction = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Body", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BodyRestitution = 0.3f;

	/** 0 = no cap. Clamped every tick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Body", meta = (ClampMin = "0.0"))
	float MaxSpeedCm = 1500.0f;

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

	/** Spring of the grab joint (Chaos linear drive, acceleration mode). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Grab", meta = (ClampMin = "0.0"))
	float GrabStiffness = 3000.0f;

	/** Damping of the grab joint (Chaos linear drive, acceleration mode). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Grab", meta = (ClampMin = "0.0"))
	float GrabDamping = 300.0f;

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

	// --- prediction (while LMB/RMB is held) ---------------------------------------

	/** How far ahead the path is drawn, seconds of flight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Prediction", meta = (ClampMin = "0.1"))
	float PredictSeconds = 5.0f;

	/** Normal velocity kept on the predicted bounce (the real one depends on both materials). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Prediction", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PredictRestitution = 0.3f;

	// --- shot (LMB, empty hands) ------------------------------------------------

	/** kg cm/s against the view: 6000 / 100 kg = 60 cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Shot", meta = (ClampMin = "0.0"))
	float ShotRecoilImpulse = 6000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Shot", meta = (ClampMin = "100.0"))
	float ShotRangeCm = 8000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Shot", meta = (ClampMin = "0.0"))
	float ShotDamage = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Shot", meta = (ClampMin = "0.0"))
	float ShotImpulseNewtonSeconds = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Shot")
	TObjectPtr<UMKImpactDataAsset> ImpactData = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Shot")
	TArray<TObjectPtr<USoundBase>> ShotSounds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Shot")
	TObjectPtr<UMaterialInterface> DefaultImpactDecal = nullptr;

	// --- thrusters and stabiliser ------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Thrusters")
	bool bThrustersEnabled = true;

	/** cm/s^2 while a thrust key is held. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Thrusters", meta = (ClampMin = "0.0"))
	float ThrustAccelCm = 150.0f;

	/** cm/s^2 toward standstill while X is held. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG|Thrusters", meta = (ClampMin = "0.0"))
	float StabiliserAccelCm = 200.0f;

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

	/** The test hitscan plus recoil and its oxygen cost. */
	UFUNCTION(BlueprintCallable, Category = "Jam|ZeroG")
	void Fire();

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

	/** Console probe: aim (true = push, false = primary) or stop aiming; the next drawn path logs its first contact. */
	void ProbeAim(bool bAim, bool bPush);

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
	void DrawPrediction() const;
	void DrawPath(const FVector& Start, const FVector& Velocity, float RadiusCm, const AActor* Ignored, const FColor& Color) const;

	enum class EAim : uint8 { None, Primary, Push };
	EAim Aim = EAim::None;
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
