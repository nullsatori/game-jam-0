#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "OSScope.generated.h"

class UCameraComponent;
class UOSWeaponsComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneCaptureComponent2D;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

/**
 * One channel of the rifle's way to the eye: a damped spring - the rifle's mass on the arm - with its
 * own swing, damping and start. Low gravity: little damping, so it floats past and settles slowly.
 */
USTRUCT(BlueprintType)
struct FOSAimSpring
{
	GENERATED_BODY()

	/** Natural frequency, Hz: how fast the channel swings (settles in about 4 / (2 pi Hz Damping) s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope", meta = (ClampMin = "0.1"))
	float Hz = 2.0f;

	/** Damping ratio: 1 arrives without overshoot; 0.5 floats ~16% past and swings back; lower floats longer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope", meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float Damping = 0.5f;

	/** Seconds after the press (or release) before this channel starts: the arm's reaction; staggered, it bends the path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope", meta = (ClampMin = "0.0"))
	float Delay = 0.0f;
};

/**
 * One gun's pose in the attachment menu (Docs/Attachments.md, «Поза меню»): turned over in the hand so its left
 * side and rail face the eye, the roll leading. On the same springs as the aim, which it excludes.
 */
USTRUCT(BlueprintType)
struct FOSGunMenuPose
{
	GENERATED_BODY()

	/** Where the gun's origin goes, camera frame (cm forward, right, up) - absolute, like the aim's end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Menu")
	FVector OffsetCm = FVector(22.0f, 11.0f, -7.0f);

	/** Its turn from the rest pose: pitch, yaw, roll (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Menu")
	FRotator Turn = FRotator(-12.0f, -25.0f, -50.0f);

	/** In and out, per channel as FOSGunAim's (X, Y, Z, pitch, yaw, roll); Out empty: the aim's Out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Menu")
	TArray<FOSAimSpring> In;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Menu")
	TArray<FOSAimSpring> Out;

	/** The gun's own field of view in the menu's pose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Menu", meta = (ClampMin = "10.0", ClampMax = "170.0"))
	float GunFieldOfView = 95.0f;

	/** The dip before it turns (cm down, s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Menu", meta = (ClampMin = "0.0"))
	float AnticipationCm = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Menu", meta = (ClampMin = "0.0"))
	float AnticipationSeconds = 0.05f;

	/** The nod on a row change: the pose springs' kick (cm/s toward the eye is -X; deg/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Menu")
	FVector RowKickMove = FVector(-6.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Menu")
	FRotator RowKickTurn = FRotator(0.0f, 0.0f, 15.0f);
};

/**
 * One gun's way to the eye (owner 18.10: aim on every gun, «настолько же проработанное, как у снайперки по
 * анимации»): where its sight line is, how far behind it the eye sits, how the gun moves there and back.
 * Aimed, the line from SightRearCm to SightFrontCm lies on the view axis with the rear point EyeCm ahead.
 */
USTRUCT(BlueprintType)
struct FOSGunAim
{
	GENERATED_BODY()

	/** The rear sight in the gun's ViewMesh space, cm: the SV-98's eyepiece, the USP's rear dots, the pump gun's receiver top. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Aim")
	FVector SightRearCm = FVector::ZeroVector;

	/** The front sight: the objective, the front dot, the bead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Aim")
	FVector SightFrontCm = FVector::ForwardVector;

	/** Eye to the rear sight when aimed, cm: more than the 10 cm near clip plane, with room for the kick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Aim", meta = (ClampMin = "10.0"))
	float EyeCm = 20.0f;

	/** The gun's own field of view when aimed (first-person primitives; the world keeps the bodycam's 134). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Aim", meta = (ClampMin = "10.0", ClampMax = "170.0"))
	float GunFieldOfView = 90.0f;

	/** The gun's kick in view (back and rise) when aimed, share of the hip kick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Aim", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float KickScale = 0.5f;

	/**
	 * Raising, per channel of the gun's pose in the camera's frame: X (toward the eye), Y (across), Z (up),
	 * then pitch, yaw, roll. Staggered delays bend the path into an arc; under-damped springs float past
	 * the aim and settle (low gravity). The turns head for the aimed turn plus the drag of the motion
	 * (LagDegPerCmPerSec) on their own springs, so the muzzle comes round late.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Aim")
	TArray<FOSAimSpring> In;

	/** Lowering: the same channels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Aim")
	TArray<FOSAimSpring> Out;

	/** The muzzle trails the motion, degrees per cm/s of the gun: X pitch per cm/s up (it dips while rising), Y yaw and Z roll per cm/s across. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Aim")
	FVector LagDegPerCmPerSec = FVector(0.1f, 0.04f, 0.05f);

	/** Anticipation: before it rises the gun dips this far (cm, down) for AnticipationSeconds - the arm gathering itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Aim", meta = (ClampMin = "0.0"))
	float AnticipationCm = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Aim", meta = (ClampMin = "0.0"))
	float AnticipationSeconds = 0.08f;

	/** The gun lags the camera's turn: degrees of yaw/pitch per deg/s of the view (roll half the yaw), at most 4 deg. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Aim", meta = (ClampMin = "0.0"))
	float LookLag = 0.015f;

	/** Aimed, the gun breathes: a slow sway of this many degrees (pitch; yaw 0.6 of it) at ~0.23 / 0.15 Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Aim", meta = (ClampMin = "0.0"))
	float BreathDeg = 0.15f;

	/** The attachment menu's pose (Tab held). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Menu")
	FOSGunMenuPose Menu;
};

/**
 * The SV-98's telescopic sight, a true 3D one (owner 18.10: «сделай true 3d прицел»).
 *
 * Holding MMB (AOSZeroGPawn: SetHeld) brings the rifle up to the eye as a mass on damped springs, one
 * per channel (Aims[2].In / Out: a late start, an arc, a floaty overshoot, the muzzle
 * swinging late; the body's velocity changes sway it): its eyepiece settles onto
 * the camera's axis Aims[2].EyeCm in front of
 * the eye. The world keeps the bodycam's field of view and barrel lens (owner
 * 18.10: «за пределами прицела не должно быть подзума»); only the gun, drawn as
 * a first-person primitive, narrows its own field to Aims[2].GunFieldOfView, so the
 * eyepiece comes up large while the camera stays outside the 10 cm near plane.
 * The pistol and the pump gun come to the eye the same way, on their own spring
 * tables, without a scope: their sight line onto the view axis (Aims[0], Aims[1]).
 * RMB aims too, through the pawn (SetAimHeld; AOSZeroGPawn::Use).
 *
 * The picture is a picture-in-picture: a scene capture on the gun's scope axis
 * (it moves with the gun - its kick, its cant in the bolt cycle) renders the
 * world in HDR into a render target, Magnification times the size it has in
 * the outer view next to the eyepiece (owner: «оптика 3х пусть будет»). A disc
 * at the eyepiece shows it through M_OS_ScopeLens (Scripts/os_scope.py): the
 * picture, the field stop and the eye-relief shadow - the dark side of the tube
 * that grows as the eye leaves the eyepiece's axis or its relief distance. A
 * second disc in front of it draws the PSO-1 style reticle as MindKore's red
 * projector laser (M_OS_ScopeReticle, after TSR). The same red lights the
 * pistol's sight dots (SightDots).
 *
 * Scene colour is captured before post-processing and shown as unlit emissive:
 * the main view's exposure and tonemapper apply to it once, as to the world
 * around it. The capture has no barrel lens of its own (the camera's
 * blendables are not its); the bodycam's lens pass bends the eyepiece with the
 * frame, near the centre mostly a uniform enlargement of both. The capture hides every primitive the owner has when it is made
 * (the gun, its moving part, flash, smoke, dust); rounds' tracers, made later,
 * stay visible. It renders only while the aim is up.
 *
 * Made and fed by UOSWeaponsComponent: it creates this on BeginPlay and asks
 * AimPose for the gun's rest pose in view every tick (one writer of the gun).
 */
UCLASS(ClassGroup = (OS))
class MINDKORE_API UOSScopeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOSScopeComponent();

	/** The gun with the scope (UOSWeaponsComponent::Weapons, 2: the SV-98). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope")
	int32 ScopedWeapon = 2;


	/**
	 * How each gun comes to the eye, indexed like UOSWeaponsComponent::Weapons (0 pistol, 1 shotgun, 2 SV-98);
	 * a gun without an entry has no aim.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Motion")
	TArray<FOSGunAim> Aims;

	/** The body's velocity changes (thrust, recoil, landing) push the gun the other way: cm/s of the gun per cm/s of the body. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Motion", meta = (ClampMin = "0.0"))
	float SwayPerBodyVelocity = 0.015f;

	/** ...but no more than this in one frame, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Motion", meta = (ClampMin = "0.0"))
	float MaxSwayCmPerSec = 25.0f;

	/** The eyepiece's centre in the scoped gun's ViewMesh space, cm (SM_OS_SV98: the eyecup's back face, Scripts/blender/os_guns.py). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope")
	FVector EyepieceCm = FVector(-4.1f, 0.0f, 11.5f);

	/** The objective's front, same space: where the capture looks from, along the gun's X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope")
	FVector ObjectiveCm = FVector(35.5f, 0.0f, 11.5f);

	/** Radius of the glass in the eyepiece, cm (the eyecup around it is 2.3). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope", meta = (ClampMin = "0.1"))
	float LensRadiusCm = 2.15f;


	/** The picture in the eyepiece is this many times larger than the world right next to it; mouse look slows as much. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope", meta = (ClampMin = "1.0"))
	float Magnification = 3.0f;


	/** The eye-relief shadow: how far the tube's bright disc moves per cm the eye is off the axis, in lens radii. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope", meta = (ClampMin = "0.0"))
	float ShadowPerCm = 0.7f;

	/** Square render target side, px (the eyepiece is about 450 px across in a 1080p frame; 768 leaves room for 4K). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope", meta = (ClampMin = "64", ClampMax = "2048"))
	int32 RenderTargetSize = 768;

	/** M_OS_ScopeLens (Scripts/os_scope.py); soft, so rebuilding it from Python is not blocked by a rooted reference. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope")
	TSoftObjectPtr<UMaterialInterface> LensMaterial;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Scope")
	TObjectPtr<USceneCaptureComponent2D> Capture = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Scope")
	TObjectPtr<UStaticMeshComponent> Lens = nullptr;

	/**
	 * M_OS_ScopeReticle: the reticle as a red laser projection (MindKore's bodycam projector), additive,
	 * drawn after motion blur on a second disc just in front of the glass - past TSR, which smeared the
	 * 1 px lines when they were part of the lens.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope")
	TSoftObjectPtr<UMaterialInterface> ReticleMaterial;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Scope")
	TObjectPtr<UStaticMeshComponent> Reticle = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Scope")
	TObjectPtr<UTextureRenderTarget2D> Target = nullptr;

	/**
	 * The pistol's illuminated sights (owner: «на пистолет на целик тоже можно их добавить»): glowing
	 * dots in the scope reticle's red, on the moving part of Weapons[SightDotWeapon] - the USP's sights
	 * are on its slide, so they ride with it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Sights")
	int32 SightDotWeapon = 0;

	/** The dots in that moving part's mesh space, cm (SM_OS_USP_Slide: the rear sight's two posts and the front blade; measured from its vertices: rear posts centred at y -1.877 / -0.985 (the notch's centre -1.431); the front blade 0.96 mm thick, centred at y -1.4534, its rear face leaning 4 deg (x -13.295 at z 6.02, -13.311 at its top 6.25) with an insert slot at z 6.04..6.23 - the dot in its middle, on the face). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Sights")
	TArray<FVector> SightDotsCm = { FVector(3.6f, -1.877f, 5.82f), FVector(3.6f, -0.985f, 5.82f), FVector(-13.303f, -1.4534f, 6.134f) };

	/** A dot's diameter, cm: a 2 mm insert, half sunk into the sight's face (owner: the first ones were cheap and too big). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Sights", meta = (ClampMin = "0.01"))
	float SightDotCm = 0.2f;

	/**
	 * The last dot's (the front post's) size, cm (depth, width, height): the blade is 0.96 mm thick, so a 2 mm dot stuck
	 * out of both its sides and from the hip showed as a sliver at its edge (owner 22.10). An upright 0.9 x 1.8 mm insert
	 * filling the blade's slot (1.9 mm tall) instead - as wide as the blade, twice the area of a round one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Sights")
	FVector FrontSightDotCm = FVector(0.09f, 0.09f, 0.18f);

	/** The front insert's Glow (M_OS_SightDot's is 6): nearly twice as far from the eye and thinned by TSR, it reads dim at the rear dots' 6. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Sights", meta = (ClampMin = "0"))
	float FrontSightDotGlow = 30.0f;

	/** M_OS_SightDot (Scripts/os_scope.py). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Scope|Sights")
	TSoftObjectPtr<UMaterialInterface> SightDotMaterial;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Scope|Sights")
	TArray<TObjectPtr<UStaticMeshComponent>> SightDots;

	/** 0 at the hip .. 1 at the eye: how far the gun is along its way (an overshoot past the eye reads 1). */
	UFUNCTION(BlueprintPure, Category = "OS|Scope")
	float GetAimAlpha() const { return Alpha; }

	/** Mouse look multiplier: the picture in the scope moves across the screen as the world does at the hip. */
	float GetLookScale() const;

	/** The capture's full field of view, degrees: the eyepiece's angle as drawn in the world's field / Magnification. */
	float GetCaptureFieldOfView() const;

	/** MMB held (AOSZeroGPawn binds it; probes call it too). */
	void SetHeld(bool bInHeld) { bHeld = bInHeld; }

	/** The pawn's RMB while a gun is out (AOSZeroGPawn::Use): aim as with the key. */
	void SetAimHeld(bool bInHeld) { bHeldByHands = bInHeld; }

	/** The attachment menu wants its pose (UOSAttachMenu): over the aim, which drops while it is held. */
	void SetMenuHeld(bool bInHeld) { bMenuHeld = bInHeld; }

	/** 0 at the hip .. 1 in the menu's pose. */
	float GetMenuAlpha() const { return MenuAlpha; }

	/** The menu's pose is what the springs head for now. */
	bool IsMenuTarget() const { return Heading == ETarget::Menu; }

	/** The gun the springs are moving now (INDEX_NONE: none). */
	int32 GetMotionWeapon() const { return bMoving ? MotionWeapon : INDEX_NONE; }

	/** UOSWeaponsComponent: Weapon's rest pose in view moved toward the eye, and the share of its kick that stays. */
	void AimPose(int32 Weapon, FTransform& Rest, float& KickScale) const;

	/**
	 * After AimPose: nothing more - the springs carry the aim and the menu's pose alike, AimPose already wrote them;
	 * here only the kick's share for a gun in the menu's pose (none fires there). Kept beside AimPose as the brief's seam.
	 */
	void MenuPose(int32 Weapon, FTransform& Rest, float& KickScale) const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** One log line: aim, capture on/off, its field of view, the target's size, the outer view. */
	FString Describe() const;

private:
	/** Capture, target and lens on the gun in view, once the weapons component has made it. */
	bool Build();

	/** Where the springs head: the hip, the eye, the menu's pose. */
	enum class ETarget : uint8 { Rest, Aim, Menu };

	/** Step the springs by DeltaTime toward the hip, the eye or the menu's pose. */
	void StepMotion(float DeltaTime, ETarget Wanted);

	/** The menu's pose end for MotionWeapon: its place and turn, as AimEnd's. */
	void MenuEnd(FVector& OutPlace, FVector& OutTurn) const;

	/** MotionWeapon's aimed pose: its place (cm) and its turn from the rest pose (pitch, yaw, roll, deg) in the camera's frame. */
	void AimEnd(FVector& OutPlace, FVector& OutTurn) const;

	UPROPERTY(Transient)
	TObjectPtr<UOSWeaponsComponent> Weapons = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> Camera = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LensMID = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ReticleMID = nullptr;

	bool bHeld = false;
	bool bHeldByHands = false;
	bool bMenuHeld = false;
	ETarget Heading = ETarget::Rest;
	/** The target before the last change (the way back from the menu takes the menu's Out springs). */
	ETarget LeftTarget = ETarget::Rest;
	float MenuAlpha = 0.0f;
	/** The gun whose pose the springs hold. */
	int32 MotionWeapon = INDEX_NONE;
	float Alpha = 0.0f;
	/** The springs' state: the gun's place (cm) and turn (pitch, yaw, roll, deg) in the camera's frame. */
	FVector Place = FVector::ZeroVector;
	FVector PlaceVelocity = FVector::ZeroVector;
	FVector Turn = FVector::ZeroVector;
	FVector TurnVelocity = FVector::ZeroVector;
	/** The springs are running (off: the gun sits at its plain rest pose). */
	bool bMoving = false;
	bool bAimTarget = false;
	float SinceSwitch = 0.0f;
	FVector LastBodyVelocity = FVector::ZeroVector;
	/** The view's turn last tick, and the gun's lag behind it (pitch, yaw, roll, degrees) this tick. */
	FQuat LastView = FQuat::Identity;
	FVector LookLagTurn = FVector::ZeroVector;
};
