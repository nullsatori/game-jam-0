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
class UJamWeaponsComponent;
class UMKCarryComponent;
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
 * time, G toggles the MK.Crowbar tag that AMKFastener reads; R, 1/2/3 and the
 * wheel belong to the guns.
 *
 * The guns are the zero-G pawn's (UJamWeaponsComponent, Guns): the same
 * pistol, shotgun and sniper with their ammo. On foot a shot's recoil moves
 * the pawn by only RecoilFraction of what it would in zero-G. Rounds that hit
 * this pawn are paid into UMKVitalsComponent.
 */
UCLASS()
class MINDKORE_API AJamCharacter : public ACharacter, public IMKImpactReceiver
{
	GENERATED_BODY()

public:
	AJamCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam")
	TObjectPtr<UCameraComponent> Camera = nullptr;

	/** Horizontal field of view, degrees: MindKore's bodycam (UMKCameraComponent::BodyCamFov). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|View", meta = (ClampMin = "60.0", ClampMax = "150.0"))
	float CameraFOV = 134.0f;

	/** MindKore's barrel lens pass (M_MK_LensDistort); keeps the 134-degree frame from looking stretched. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|View")
	bool bLensDistortion = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam")
	TObjectPtr<UMKInteractionComponent> Interaction = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam")
	TObjectPtr<UMKCarryComponent> Carry = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam")
	TObjectPtr<UMKKeyringComponent> Keyring = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam")
	TObjectPtr<UMKVitalsComponent> Vitals = nullptr;

	/** The guns (shared with the zero-G pawn). The test room starts with all three; the ship walker with none. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam")
	TObjectPtr<UJamWeaponsComponent> Guns = nullptr;

	/**
	 * Part of a gun's recoil that moves the walker (velocity = RecoilImpulse / mass
	 * x this). Small, so the sniper nudges you but you stay on your feet; on the
	 * ground the kick is kept along the ground.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Fire", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RecoilFraction = 0.1f;

	/** M_MK_BulletTime: scalar parameters Amount, EntryAge, ArcVisible, Aspect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|BulletTime")
	TObjectPtr<UMaterialInterface> BulletTimeMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|BulletTime")
	TObjectPtr<USoundBase> BulletTimeEntrySound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|BulletTime")
	TObjectPtr<USoundBase> BulletTimeExitSound = nullptr;

	/** Fire the current gun from the camera along the view; a fraction of its recoil moves the pawn. False when it cannot fire now. */
	UFUNCTION(BlueprintCallable, Category = "Jam")
	bool Fire();

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
