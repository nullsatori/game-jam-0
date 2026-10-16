#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MKImpactReceiver.h"
#include "JamCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMKCarryComponent;
class UMKImpactDataAsset;
class UMKInteractionComponent;
class UMKKeyringComponent;
class UMKVitalsComponent;
class USoundBase;
struct FInputActionValue;

/**
 * The jam's first-person pawn: a camera at eye height (no arms mesh) and the
 * lifted MindKore hands - interaction, carry, keyring, vitals.
 *
 * Input is built here at runtime (UInputAction / UInputMappingContext objects,
 * no input assets): WASD move, mouse look, Space jump, C / LeftCtrl crouch
 * (held; a crouch pressed in the air tucks the legs - the crouch-jump),
 * E use, LMB fire or throw while carrying, RMB / F put down, Q held bullet
 * time, G toggles the MK.Crowbar tag that AMKFastener reads.
 *
 * Fire is a test hitscan on the Shot channel through the lifted impact path
 * (MKResolveImpactFromHit + MKDeliverResolvedImpact), the way
 * UMKFirearmComponent fills FMKResolvedImpact. Rounds that hit this pawn are
 * paid into UMKVitalsComponent.
 */
UCLASS()
class MINDKORE_API AJamCharacter : public ACharacter, public IMKImpactReceiver
{
	GENERATED_BODY()

public:
	AJamCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Fire", meta = (ClampMin = "100.0"))
	float ShotRangeCm = 5000.0f;

	/** Same as UMKFirearmComponent::BaseDamage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Fire", meta = (ClampMin = "0.0"))
	float ShotDamage = 60.0f;

	/** Same as UMKFirearmComponent::ImpactImpulseNewtonSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Fire", meta = (ClampMin = "0.0"))
	float ShotImpulseNewtonSeconds = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Fire")
	TObjectPtr<UMKImpactDataAsset> ImpactData = nullptr;

	/** Played together, 2D, on every shot (the split blast of MindKore's revolver). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Fire")
	TArray<TObjectPtr<USoundBase>> ShotSounds;

	/** Used when the resolved feedback has no decal of its own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Fire")
	TObjectPtr<UMaterialInterface> DefaultImpactDecal = nullptr;

	/** M_MK_BulletTime: scalar parameters Amount, EntryAge, ArcVisible, Aspect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|BulletTime")
	TObjectPtr<UMaterialInterface> BulletTimeMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|BulletTime")
	TObjectPtr<USoundBase> BulletTimeEntrySound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|BulletTime")
	TObjectPtr<USoundBase> BulletTimeExitSound = nullptr;

	UFUNCTION(BlueprintCallable, Category = "Jam")
	void Fire();

	UFUNCTION(BlueprintCallable, Category = "Jam")
	void ToggleCrowbar();

	UFUNCTION(BlueprintPure, Category = "Jam")
	bool HasCrowbar() const;

	virtual void ReceiveResolvedImpact_Implementation(const FMKResolvedImpact& ResolvedImpact) override;

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;

protected:
	virtual void BeginPlay() override;

private:
	void BuildInput();
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void JumpPressed();
	void PrimaryPressed();
	void PutDown();
	void Use();
	void BulletTimePressed();
	void BulletTimeReleased();
	void UpdateBulletTimePresentation();

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> Mapping = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MoveAction = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookAction = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> JumpAction = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CrouchAction = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> UseAction = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> PrimaryAction = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> PutAction = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> BulletTimeAction = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CrowbarAction = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BulletTimeMID = nullptr;

	bool bBulletTimeWasSlowed = false;
};
