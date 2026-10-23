#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OSLaserSight.generated.h"

class AHUD;
class UCameraComponent;
class UCanvas;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UOSWeaponsComponent;
class UStaticMeshComponent;

/**
 * The laser's light in the world (Docs/Attachments.md, «ЛЦУ»): while a laser sits on the gun in view, switched on
 * and latched (UOSWeaponsComponent::GetLaserLens), one trace a frame on the Shot channel along the round's own line
 * (UOSWeaponsComponent::GetShotLine: out of the muzzle along the bore as the gun is drawn) within the gun's RangeCm
 * puts the dot where the round lands (owner 23.10: honest about the round); the beam runs from the module's lens to
 * it, seen only where there is something to scatter it: in air (the zones, polled every 0.2 s, a 0.3 s fade) - and in
 * the RMB-93's smoke once the smoke exists (stub). Off: hidden, no trace.
 *
 * Both are drawn on the HUD (DrawHud), not as meshes in the world (owner's playtest 22.10: «лазер гостит»): the
 * temporal upscaler smeared a few-pixel dot that moves with the view. The look is the PS1/PS2's light (owner 23.10:
 * «ближе к psx-ps2 эстетике», then «subtle, industrial, gritty»): smooth-shaded additive bands and discs, colour per
 * corner - a thin hard beam in a faint sheath, a small dot in a tight glow. What of the beam the gun's own parts stand
 * in front of (their boxes between the eye and the beam) is not drawn: out of the lens from the side, from under the
 * slide at the hip, unseen in the aim.
 *
 * Its motion is the project's kind (owner 23.10: «анимация должна соответствовать уровню анимаций проекта»), not
 * noise a frame: the diode's drive drifts slowly and ripples with its supply (the ripple beats against the frame
 * rate), drops out rarely as a dip with an exponential recovery, comes up on a spring with a slight overshoot and dies
 * over a few frames; the shot rattles the module's contacts for a tenth of a second. The beam's grit is haze fixed in
 * the world (3D value noise drifting slowly) that the beam sweeps through as the gun sways. The dot is a real disc on
 * the surface: stretched by the angle it lands at, a little wider far off, with a faint speckle.
 */
UCLASS(ClassGroup = (OS))
class MINDKORE_API UOSLaserSightComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOSLaserSightComponent();

	/** Off the surface along its normal, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.0"))
	float DotLiftCm = 0.5f;

	/** The dot on the surface: its radius at the lens, cm, its spread per metre of range, and how far a slant may stretch it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Dot", meta = (ClampMin = "0.05"))
	float DotRadiusCm = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Dot", meta = (ClampMin = "0.0"))
	float DotSpreadCmPerM = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Dot", meta = (ClampMin = "1.0"))
	float DotStretchMax = 6.0f;

	/** On screen the dot's core is never under this, pixels at 1080 lines; the glow and the halo reach this far past its rim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Dot", meta = (ClampMin = "0.5"))
	float DotCorePixels = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Dot", meta = (ClampMin = "0.0"))
	float DotGlowPixels = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Dot", meta = (ClampMin = "0.0"))
	float DotHaloPixels = 8.0f;

	/** The speckle in the dot's glow: a few grains, this bright against the core, re-rolled on the device's ticks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Dot", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Speckle = 0.15f;

	/** The beam's hot core, cm across in the world (fat by the lens, a hair far off), never under BeamMinPixels at 1080. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Beam", meta = (ClampMin = "0.01"))
	float ThreadCm = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Beam", meta = (ClampMin = "0.5"))
	float BeamMinPixels = 1.5f;

	/** The beam's light against the dot's, and its brightness by the lens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Beam", meta = (ClampMin = "0.0"))
	float ThreadShare = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Beam", meta = (ClampMin = "0.0"))
	float ThreadBrightness = 1.8f;

	/** The haze the beam shows in: how much its density varies (share of the light), its cell, cm, and its drift, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Beam", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HazeGrit = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Beam", meta = (ClampMin = "1.0"))
	float HazeCellCm = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Beam", meta = (ClampMin = "0.0"))
	float HazeDriftCmPerSec = 5.0f;

	/** The diode's drive: a slow drift and the supply's ripple (share of the light; the ripple's Hz beats against the frame rate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Diode", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float DriveDrift = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Diode", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float DriveRipple = 0.035f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Diode", meta = (ClampMin = "0.0"))
	float DriveRippleHz = 50.0f;

	/** Dropouts: how often (a second), how deep (share of the light) and how long the recovery takes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Diode", meta = (ClampMin = "0.0"))
	float DropsPerSecond = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Diode", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DropDepth = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Diode", meta = (ClampMin = "0.01"))
	float DropSeconds = 0.08f;

	/** Switching on: the light comes up on a spring (rise, overshoot share); off: it dies over this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Diode", meta = (ClampMin = "0.01"))
	float OnSeconds = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Diode", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float OnOvershoot = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Diode", meta = (ClampMin = "0.01"))
	float OffSeconds = 0.04f;

	/** The shot rattles the module's contacts: this deep (share of the light), for this long, dying out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Diode", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float KickRattle = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Diode", meta = (ClampMin = "0.01"))
	float KickRattleSeconds = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser")
	FLinearColor RedColour = FLinearColor(1.0f, 0.045f, 0.02f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.01"))
	float AirPollSeconds = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.01"))
	float AirFadeSeconds = 0.3f;

	/** M_OS_SightDot for the hidden meshes (kept for a world dot later). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser")
	TSoftObjectPtr<UMaterialInterface> DotMaterial;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Laser")
	TObjectPtr<UStaticMeshComponent> Dot = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Laser")
	TObjectPtr<UStaticMeshComponent> Thread = nullptr;

	/** The laser is lit this frame, the dot on something, the thread seen (probes). */
	bool IsLit() const { return bLit; }
	bool HasDot() const { return bLit && bDotHit; }
	bool IsThreadShown() const;
	FVector GetDotLocation() const { return DotAt; }
	/** Traces since play began (os.Attach.Cost: two a lit frame - the round's line and the eye's sight of the dot). */
	int32 GetTraceCount() const { return Traces; }

	/** The dot and the thread on the HUD's canvas, every frame while lit (AOSZeroGHUD). */
	void DrawHud(AHUD& Hud, UCanvas& Canvas) const;

	/** UOSWeaponsComponent: a laser is (or is no longer) on the gun in view and switched on - the tick runs only then (and through the off fade). */
	void Wake(bool bAwake);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	void Hide();
	/** The diode's light this frame, 0..~1.1: the spring, the drift, the ripple, a dropout, the shot's rattle. */
	float StepDrive(float DeltaTime, bool bWant, bool bShot);
	/** The spans of the beam (0..1 of LensAt -> BeamEnd) that no part of the gun stands in front of, from the eye. */
	void FindBeamSpans(const FVector& EyeAt);

	UPROPERTY(Transient)
	TObjectPtr<UOSWeaponsComponent> Guns = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DotMID = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ThreadMID = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> Camera = nullptr;

	bool bAwake = false;
	bool bLit = false;
	bool bDotHit = false;
	bool bDotSeen = false;
	bool bThreadShown = false;
	FVector LensAt = FVector::ZeroVector;
	/** The beam's far end in the gun's own space (the drawn gun's projection puts it on the dot). */
	FVector BeamEnd = FVector::ZeroVector;
	TArray<FVector2f> BeamSpans;
	FVector DotAt = FVector::ZeroVector;
	/** The disc's half axes on the surface, cm: along the beam's slant (stretched) and across. */
	FVector DotAxisU = FVector::ZeroVector;
	FVector DotAxisV = FVector::ZeroVector;
	float DotDrive = 1.0f;
	float ThreadLevel = 0.0f;
	// The diode's drive.
	float OnLevel = 0.0f;
	float OnVelocity = 0.0f;
	float DriftPhase = 0.0f;
	float RipplePhase = 0.0f;
	float SinceDrop = 1000.0f;
	float KickLeft = 0.0f;
	float LastSinceShot = 1000.0f;
	bool bAir = false;
	float AirShare = 0.0f;
	float SinceAirPoll = 1000.0f;
	int32 Traces = 0;
};
