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
struct FCanvasTileItem;

/**
 * The laser's light in the world (Docs/Attachments.md, «ЛЦУ»): while a laser sits on the gun in view, switched on
 * and latched (UOSWeaponsComponent::GetLaserLens), one trace a frame on the Shot channel from the eye along the
 * view within the gun's RangeCm - the rounds fly from the eye, so the dot is always true about the round - puts the
 * dot on the first thing in the way, a constant few pixels across, 0.5 cm off the surface; the thread runs from the
 * module's lens to it, seen only where there is something to scatter it: in air (the zones, polled every 0.2 s,
 * a 0.3 s fade) - and in the RMB-93's smoke once the smoke exists (stub). Off: hidden, no trace.
 *
 * Both are drawn on the HUD (DrawHud), not as meshes in the world (owner's playtest 22.10: «лазер гостит»):
 * the temporal upscaler smeared a few-pixel dot that moves with the view and flickers every frame. On the HUD
 * the dot is crisp pixels at the traced point - it is the first thing along the view, so nothing hides it - and
 * the thread a pixel line from the lens (the gun's first-person projection) to the dot. The meshes stay hidden.
 */
UCLASS(ClassGroup = (OS))
class MINDKORE_API UOSLaserSightComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOSLaserSightComponent();

	/** The dot's size, pixels of the view across (on 10 m about 2 px at 1280 x 720, owner: not fatter). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.5"))
	float DotPixels = 2.0f;

	/** Off the surface along its normal, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.0"))
	float DotLiftCm = 0.5f;

	/** The thread's thickness, cm, and its glow's share of the dot's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.01"))
	float ThreadCm = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.0"))
	float ThreadShare = 0.25f;

	/**
	 * The HUD's pixel grid for the dot and the thread: this many lines of the screen (3 px a pixel at 1080), the
	 * game's low resolution - stepped, posterised, dithered light rather than a thin anti-aliased line.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "90.0"))
	float PixelLines = 360.0f;

	/** The thread's light levels (posterised, ordered dither between them) and its brightness by the lens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "2"))
	int32 ThreadLevels = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.0"))
	float ThreadBrightness = 2.6f;

	/** Dust caught in the beam: the share of its pixels that glint, and how fast the glints drift along it (pixels a second). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DustShare = 0.035f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Laser", meta = (ClampMin = "0.0"))
	float DustDrift = 9.0f;

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
