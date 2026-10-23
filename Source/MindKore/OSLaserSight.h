#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OSLaserSight.generated.h"

class AHUD;
class UCameraComponent;
class UCanvas;
struct FOSHudProjector;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UOSWeaponsComponent;
class UPointLightComponent;
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
 * temporal upscaler smeared a few-pixel dot that moves with the view. The geometry is worked out again at draw time
 * from the camera as it stands for this frame (the tick ran before the camera turned with the mouse: a stored world
 * point lagged a frame behind the picture - owner 23.10: «дёрганно двигается за мышью»); the tick's pass is for the
 * probes. The look is the game's low resolution without pixel art (owner 23.10: «psx-ps2», «лоурезовость перестала
 * считываться»): the light is rasterised onto a coarse texel grid (GridLines; the smoke's texel) with its brightness
 * continuous, and each texel is drawn as a tent of light to its neighbours - a bilinear upscale of a low-res frame,
 * soft steps, no dither, no sprite. A thin beam of one texel, Wu-weighted across the rows; a dot as a small blob,
 * stretched by its slant, with grains of speckle that boil while the dot moves and freeze when it rests; a faint red
 * light on the surface (DotLight). What of the beam the gun's own parts stand in front of (their boxes between the eye
 * and the beam) is not drawn: out of the lens from the side, from under the slide at the hip, unseen in the aim.
 *
 * Its motion is the project's kind (owner 23.10: «анимация должна соответствовать уровню анимаций проекта»), not
 * noise a frame: the diode's drive drifts slowly and ripples with its supply (the ripple beats against the frame
 * rate), drops out rarely as a dip with an exponential recovery, comes up on a spring with a slight overshoot and dies
 * over a few frames; the shot rattles the module's contacts for a tenth of a second. The beam's grit is haze fixed in
 * the world (3D value noise drifting slowly) that the beam sweeps through as the gun sways. The dot is a real disc on
 * the surface: stretched by the angle it lands at, a little wider far off, with a faint speckle.
 */
/** What the dot lies on: it reads the surface (Docs/Attachments.md, «ЛЦУ читает поверхность»). */
UENUM()
enum class EOSLaserSurface : uint8
{
	Plain,
	Metal,
	Glass,
	Flesh,
};

/**
 * One colour of the laser (owner 23.10: «выбор цвета лазера ... 3 цвета + 3 экспериментальных»): the beam's, the
 * dot's (they part where the dot is not the beam's light - a fluorescence), the hot of the core and speckle, and the
 * beam's brightness against the red's. Chosen in the attachment menu (the line under the laser) or os.Laser.Tint.
 */
USTRUCT(BlueprintType)
struct FOSLaserTint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Tint")
	FText Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Tint")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Tint")
	FLinearColor Beam = FLinearColor(1.0f, 0.09f, 0.03f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Tint")
	FLinearColor Dot = FLinearColor(1.0f, 0.045f, 0.02f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Tint")
	FLinearColor Hot = FLinearColor(1.0f, 0.3f, 0.18f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Tint", meta = (ClampMin = "0.0"))
	float BeamScale = 1.0f;
};

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

	/** The grid of the light, lines of the screen (4 px a texel at 1080, 5 at 1440; owner 23.10: a touch less low-res than the smoke's 216). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "90.0"))
	float GridLines = 288.0f;

	/** On screen the dot's core is never under this many texels across; its glow's width (sigma, texels) and share. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Dot", meta = (ClampMin = "0.3"))
	float DotCoreTexels = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Dot", meta = (ClampMin = "0.3"))
	float DotGlowTexels = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Dot", meta = (ClampMin = "0.0"))
	float DotGlowShare = 0.18f;

	/** The speckle in the dot: grains this bright against the core; they boil as the dot moves (a new set every this many cm) and rest when it rests (every this many seconds) - a hard switch, no fade (owner 23.10: «резко моргать, этож лазеры»). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Dot", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Speckle = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Dot", meta = (ClampMin = "0.1"))
	float SpeckleBoilCm = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Dot", meta = (ClampMin = "0.1"))
	float SpeckleRestSeconds = 2.0f;

	/**
	 * The dot's light on the surface, candelas (none at 0) and its reach, cm. Off: the camera's motion blur smeared its spill
	 * into a long red streak on every turn (owner 23.10); the streak is the HUD's own now (Trail*), short and a hair thin.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Dot", meta = (ClampMin = "0.0"))
	float DotLightCandela = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Dot", meta = (ClampMin = "1.0"))
	float DotLightRadiusCm = 8.0f;

	/** The dot's streak as it sweeps the screen: back toward where it was last frame, at most this many texels, this bright at the dot (share), fading out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Dot", meta = (ClampMin = "0.0"))
	float TrailTexels = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Dot", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TrailShare = 0.35f;

	/** The hot of the light (the dot's core, the speckle, the glints): a hotter red, never white (owner 23.10: no flashing in another colour). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Dot")
	FLinearColor HotColour = FLinearColor(1.0f, 0.3f, 0.18f);

	/** Glass: the beam goes through; the ghost of the dot left on the pane where the beam from the lens crosses it, share of the dot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Surface", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GlassGhost = 0.3f;

	/** Metal: the core tight, the glow this share of the plain one, the speckle this many times. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Surface", meta = (ClampMin = "0.0"))
	float MetalGlow = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Surface", meta = (ClampMin = "0.0"))
	float MetalSpeckle = 2.0f;

	/** Flesh: the dot sinks in - the core this share, the glow this many times wider and this bright, throbbing at this rate (Hz) by this share. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Surface", meta = (ClampMin = "0.0"))
	float FleshCore = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Surface", meta = (ClampMin = "1.0"))
	float FleshGlowTimes = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Surface", meta = (ClampMin = "0.0"))
	float FleshGlowShare = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Surface", meta = (ClampMin = "0.0"))
	float FleshPulseHz = 1.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Surface", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FleshPulse = 0.25f;

	/** The beam's light against the dot's, and its brightness by the lens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Beam", meta = (ClampMin = "0.0"))
	float ThreadShare = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Beam", meta = (ClampMin = "0.0"))
	float ThreadBrightness = 1.8f;

	/** The beam's width, texels (under one: a hair, its light split by coverage), its colour, and its sheath either side (share). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Beam", meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float BeamTexels = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Beam")
	FLinearColor BeamColour = FLinearColor(1.0f, 0.09f, 0.03f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Beam", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BeamSheath = 0.08f;

	/** The gun's silhouette in the beam's mask shrunk by this many texels: the beam runs over its edge by that much. 0: once the projection was the frame's own, one texel stood on the gun (owner 23.10). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Beam", meta = (ClampMin = "0", ClampMax = "4"))
	int32 MaskErodeTexels = 0;

	/** The beam's root: this many texels past where it comes out from behind the gun it always shows, fading into the haze's gaps (a gap there read as the beam torn off the module). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Beam", meta = (ClampMin = "0.0"))
	float BeamRootTexels = 8.0f;

	/** Below this of the haze's noise the beam has nothing to show in (gaps: a sparse beam). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Beam", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float HazeFloor = 0.3f;

	/** Glints: where the haze is densest (above this of its noise) a texel of the beam grows brighter by up to this share - in its own red. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Beam", meta = (ClampMin = "0.5", ClampMax = "1.0"))
	float GlintAbove = 0.86f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Beam", meta = (ClampMin = "0.0"))
	float Glint = 0.3f;

	/** The haze the beam shows in: its cell, cm, and its drift, cm/s. */
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

	/** Dropouts: how often (a second), how deep (share of the light) and how long - a hard blink, off and back at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Diode", meta = (ClampMin = "0.0"))
	float DropsPerSecond = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Diode", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DropDepth = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Diode", meta = (ClampMin = "0.01"))
	float DropSeconds = 0.05f;

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

	/** The palette (the constructor's six: three real diodes, three of the project's own) and the one in use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Tint")
	TArray<FOSLaserTint> Palette;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser|Tint", meta = (ClampMin = "0"))
	int32 Tint = 0;

	/** Takes the palette's Index (wrapped) into the colours the laser draws with. */
	void SetTint(int32 Index);
	const FOSLaserTint* GetTint() const { return Palette.IsValidIndex(Tint) ? &Palette[Tint] : nullptr; }

	/** The laser is lit this frame, the dot on something, the thread seen (probes). */
	bool IsLit() const { return bLit; }
	bool HasDot() const { return bLit && bDotHit; }
	bool IsThreadShown() const;
	FVector GetDotLocation() const { return DotAt; }
	/** Traces since play began (os.Attach.Cost: three or more a lit frame - the round's line, the beam's for glass, the eye's sight of the dot; one more for each pane passed). */
	int32 GetTraceCount() const { return Traces; }

	/** The dot and the thread on the HUD's canvas, every frame while lit (AOSZeroGHUD); the geometry is refreshed from this frame's camera first. */
	void DrawHud(AHUD& Hud, UCanvas& Canvas);

	/** UOSWeaponsComponent: a laser is (or is no longer) on the gun in view and switched on - the tick runs only then (and through the off fade). */
	void Wake(bool bAwake);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	void Hide();
	/** The diode's light this frame, 0..~1.1: the spring, the drift, the ripple, a dropout, the shot's rattle. */
	float StepDrive(float DeltaTime, bool bWant, bool bShot);
	/** The spans of the beam (0..1 of LensAt -> BeamEnd) that no part of the gun stands in front of, from the eye (by the parts' boxes: the fallback where the meshes' triangles are not on the CPU). */
	void FindBeamSpans(const FVector& EyeAt);
	/** The gun's parts drawn into a depth mask on the light's texel grid over Region (texels): the beam hides exactly behind the gun's silhouette. False where no part's triangles are on the CPU. */
	bool DrawGunMask(const FOSHudProjector& Projector, float Texel, const FIntRect& Region);
	/** The geometry from the camera as it stands: the round's line, the dot, its disc, the beam's spans, the light. */
	void Refresh();

	UPROPERTY(Transient)
	TObjectPtr<UOSWeaponsComponent> Guns = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DotMID = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ThreadMID = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> Camera = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> DotLight = nullptr;

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
	EOSLaserSurface Surface = EOSLaserSurface::Plain;
	/** Glass on the way: the ghost of the dot on the pane. */
	bool bGhost = false;
	FVector GhostAt = FVector::ZeroVector;
	/** The disc's half axes on the surface, cm: along the beam's slant (stretched) and across. */
	FVector DotAxisU = FVector::ZeroVector;
	FVector DotAxisV = FVector::ZeroVector;
	float DotDrive = 1.0f;
	float ThreadLevel = 0.0f;
	/** The camera the geometry was last worked out from; the speckle's boil (cm travelled + rest time). */
	FTransform RefreshedView;
	FVector LastDotAt = FVector::ZeroVector;
	float Boil = 0.0f;
	float DeltaSecondsNow = 0.0f;
	/** The dot on screen last frame (texels), for its streak. */
	FVector2D LastDotTexel = FVector2D::ZeroVector;
	bool bLastDotTexel = false;
	/** The gun's depth mask (DrawGunMask): the nearest 1/depth of the gun per texel over MaskRegion, 0 where none. */
	FIntRect MaskRegion;
	TArray<float> MaskNearInv;
	/** The tint's beam brightness against the red's (SetTint). */
	float TintBeamScale = 1.0f;
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
