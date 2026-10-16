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
class UMKCarryComponent;
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
	TObjectPtr<UMKCarryComponent> Carry = nullptr;

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

	/** Throw the held prop and take the opposite momentum. */
	UFUNCTION(BlueprintCallable, Category = "Jam|ZeroG")
	void ThrowHeld();

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
