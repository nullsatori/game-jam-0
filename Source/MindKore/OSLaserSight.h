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
class FCanvasTileItem;

/**
 * The laser's light in the world (Docs/Attachments.md, «ЛЦУ»): while a laser sits on the gun in view, switched on
 * and latched (UOSWeaponsComponent::GetLaserLens), one trace a frame on the Shot channel along the round's own line
 * (UOSWeaponsComponent::GetShotLine: out of the muzzle along the bore) within the gun's RangeCm puts the dot where
 * the round lands (owner 23.10: honest about the round), 0.5 cm off the surface; the beam runs from the module's
 * lens to it, seen only where there is something to scatter it: in air (the zones, polled every 0.2 s, a 0.3 s
 * fade) - and in the RMB-93's smoke once the smoke exists (stub). Off: hidden, no trace.
 *
 * Both are drawn on the HUD (DrawHud), not as meshes in the world (owner's playtest 22.10: «лазер гостит»): the
 * temporal upscaler smeared a few-pixel dot that moves with the view. The look is the PS1/PS2's light (owner 23.10:
 * «ближе к psx-ps2 эстетике», not pixel art): smooth-shaded additive bands and discs, colour per corner - a hot
 * core, a red body and a wide dim halo for the beam, haze sliding along it and dust motes; a white-hot dot in a red
 * glow, a dim halo and a faint flat streak. What of the beam lies over the gun's outline is not drawn.
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

	/** The dot's light, pixels at 1080 lines (4 m off; a little smaller farther): the white core, the red glow, the halo, the streak's half length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.5"))
	float DotCorePixels = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.5"))
	float DotGlowPixels = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.5"))
	float DotHaloPixels = 9.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.0"))
	float DotStreakPixels = 0.0f;

	/** The beam's hot core, cm across in the world (fat by the lens, a hair far off), never under BeamMinPixels at 1080. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.01"))
	float ThreadCm = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.5"))
	float BeamMinPixels = 1.5f;

	/** The beam's light against the dot's, and its brightness by the lens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.0"))
	float ThreadShare = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.0"))
	float ThreadBrightness = 1.8f;

	/** Dust caught in the beam: how many motes, and how fast they drift along it (beam lengths a second). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0"))
	int32 DustMotes = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.0"))
	float DustDrift = 0.12f;

	/** The dot's Glow (M_OS_SightDot's param), its grain (+-share a frame) and its drive's flutter with a rare drop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.0"))
	float Glow = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Grain = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser")
	FLinearColor RedColour = FLinearColor(1.0f, 0.045f, 0.02f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.01"))
	float AirPollSeconds = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.01"))
	float AirFadeSeconds = 0.3f;

	/** M_OS_SightDot until the PC's dot material (a variant of M_OS_ScopeReticle, after motion blur). */
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
	/** Traces since play began (os.Attach.Cost: one a lit frame). */
	int32 GetTraceCount() const { return Traces; }

	/** The dot and the thread on the HUD's canvas, every frame while lit (AOSZeroGHUD). */
	void DrawHud(AHUD& Hud, UCanvas& Canvas) const;

	/** UOSWeaponsComponent: a laser is (or is no longer) on the gun in view and switched on - the tick runs only then. */
	void Wake(bool bAwake);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	void Hide();

	UPROPERTY(Transient)
	TObjectPtr<UOSWeaponsComponent> Guns = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DotMID = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ThreadMID = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> Camera = nullptr;

	bool bLit = false;
	bool bDotHit = false;
	bool bThreadShown = false;
	FVector LensAt = FVector::ZeroVector;
	float DotDrive = 1.0f;
	float ThreadLevel = 0.0f;
	FVector DotAt = FVector::ZeroVector;
	bool bAir = false;
	float AirShare = 0.0f;
	float SinceAirPoll = 1000.0f;
	int32 Traces = 0;
};
