#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "JamScope.generated.h"

class UCameraComponent;
class UEnhancedInputComponent;
class UInputAction;
class UInputMappingContext;
class UJamWeaponsComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneCaptureComponent2D;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

/**
 * The SV-98's telescopic sight, a true 3D one (owner 18.10: «сделай true 3d прицел»).
 *
 * Holding ScopeKey brings the rifle up to the eye on a smootherstep curve over
 * AimSeconds: its eyepiece comes onto the camera's axis EyeReliefCm in front of
 * the eye. The world keeps the bodycam's field of view and barrel lens (owner
 * 18.10: «за пределами прицела не должно быть подзума»); only the gun, drawn as
 * a first-person primitive, narrows its own field to AimGunFieldOfView, so the
 * eyepiece comes up large while the camera stays outside the 10 cm near plane.
 *
 * The picture is a picture-in-picture: a scene capture on the gun's scope axis
 * (it moves with the gun - its kick, its cant in the bolt cycle) renders the
 * world in HDR into a render target, Magnification times the size it has in
 * the outer view next to the eyepiece (owner: «оптика 3х пусть будет»). A disc
 * at the eyepiece shows it through M_Jam_ScopeLens (Scripts/jam_scope.py): the
 * picture, a PSO-1 style reticle, the field stop and the eye-relief shadow -
 * the dark side of the tube that grows as the eye leaves the eyepiece's axis
 * or its relief distance.
 *
 * Scene colour is captured before post-processing and shown as unlit emissive:
 * the main view's exposure and tonemapper apply to it once, as to the world
 * around it. The capture has no barrel lens of its own (the camera's
 * blendables are not its); the bodycam's lens pass bends the eyepiece with the
 * frame, near the centre mostly a uniform enlargement of both. The capture hides every primitive the owner has when it is made
 * (the gun, its moving part, flash, smoke, dust); rounds' tracers, made later,
 * stay visible. It renders only while the aim is up.
 *
 * Made and fed by UJamWeaponsComponent: it creates this in BindInput and asks
 * AimPose for the gun's rest pose in view every tick (one writer of the gun).
 */
UCLASS(ClassGroup = (Jam))
class MINDKORE_API UJamScopeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UJamScopeComponent();

	/** Held: aim through the scope. The one place to change the key. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Scope")
	FKey ScopeKey = EKeys::MiddleMouseButton;

	/** The gun with the scope (UJamWeaponsComponent::Weapons, 2: the SV-98). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Scope")
	int32 ScopedWeapon = 2;

	/** Seconds from the hip to the eye (and back). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Scope", meta = (ClampMin = "0.01"))
	float AimSeconds = 0.3f;

	/** The eyepiece's centre in the scoped gun's ViewMesh space, cm (SM_Jam_SV98: the eyecup's back face, Scripts/blender/jam_guns.py). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Scope")
	FVector EyepieceCm = FVector(-4.1f, 0.0f, 11.5f);

	/** The objective's front, same space: where the capture looks from, along the gun's X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Scope")
	FVector ObjectiveCm = FVector(35.5f, 0.0f, 11.5f);

	/** Radius of the glass in the eyepiece, cm (the eyecup around it is 2.3). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Scope", meta = (ClampMin = "0.1"))
	float LensRadiusCm = 2.15f;

	/** Eye to eyepiece when aimed, cm. Above the 10 cm near clip plane with room for the kick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Scope", meta = (ClampMin = "10.0"))
	float EyeReliefCm = 14.0f;

	/** The picture in the eyepiece is this many times larger than the world right next to it; mouse look slows as much. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Scope", meta = (ClampMin = "1.0"))
	float Magnification = 3.0f;

	/** The gun's own field of view when aimed (first-person primitives; the world keeps the bodycam's 134). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Scope", meta = (ClampMin = "10.0", ClampMax = "170.0"))
	float AimGunFieldOfView = 52.0f;

	/** The gun's kick in view (back and rise) when aimed, share of the hip kick: shouldered, it keeps off the eye. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Scope", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AimKickScale = 0.25f;

	/** The eye-relief shadow: how far the tube's bright disc moves per cm the eye is off the axis, in lens radii. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Scope", meta = (ClampMin = "0.0"))
	float ShadowPerCm = 0.7f;

	/** Square render target side, px (the eyepiece is about 600 px across in a 1080p frame). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Scope", meta = (ClampMin = "64", ClampMax = "2048"))
	int32 RenderTargetSize = 768;

	/** M_Jam_ScopeLens (Scripts/jam_scope.py); soft, so rebuilding it from Python is not blocked by a rooted reference. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Scope")
	TSoftObjectPtr<UMaterialInterface> LensMaterial;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Jam|Scope")
	TObjectPtr<USceneCaptureComponent2D> Capture = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Jam|Scope")
	TObjectPtr<UStaticMeshComponent> Lens = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Jam|Scope")
	TObjectPtr<UTextureRenderTarget2D> Target = nullptr;

	/** 0 at the hip .. 1 at the eye, on the smootherstep curve. */
	UFUNCTION(BlueprintPure, Category = "Jam|Scope")
	float GetAimAlpha() const { return Alpha; }

	/** Mouse look multiplier: the picture in the scope moves across the screen as the world does at the hip. */
	float GetLookScale() const;

	/** The capture's full field of view, degrees: the eyepiece's angle as drawn in the world's field / Magnification. */
	float GetCaptureFieldOfView() const;

	/** Held from code (probes); the key does the same. */
	void SetHeld(bool bInHeld) { bHeld = bInHeld; }

	/** UJamWeaponsComponent: Weapon's rest pose in view moved toward the eye, and the share of its kick that stays. */
	void AimPose(int32 Weapon, FTransform& Rest, float& KickScale) const;

	void BindInput(UEnhancedInputComponent& Input, UInputMappingContext& Mapping);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** One log line: aim, capture on/off, its field of view, the target's size, the outer view. */
	FString Describe() const;

private:
	/** Capture, target and lens on the gun in view, once the weapons component has made it. */
	bool Build();

	UPROPERTY(Transient)
	TObjectPtr<UJamWeaponsComponent> Weapons = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> Camera = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LensMID = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> AimAction = nullptr;

	bool bHeld = false;
	float Progress = 0.0f;
	float Alpha = 0.0f;
	/** The camera's (world's) field of view when the scope was built. */
	float WorldFieldOfView = 134.0f;
};
