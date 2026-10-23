#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "OSSuitProjector.h"
#include "OSWeldProjector.generated.h"

class AOSPlateSocket;
class APawn;
class UCanvas;
class UCanvasRenderTarget2D;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class USpotLightComponent;

// FOSWeldBox, the projector's font and its strokes are shared with the suit's device (OSSuitProjector.h).

/** A plate's outline where it would lie on the hull now, in the breach frame (cm), and how it fits. */
struct FOSWeldFootprint
{
	FVector2D Corner[4];
	/** Close and square enough: drawn locked onto the breach. */
	bool bSnapped = false;
	/** The share of the hole it would cover, and whether it covers it and every edge of it. */
	float Coverage = 0.0f;
	bool bCovers = false;
	/** It is the plate the breach measures over the hole. */
	bool bPlaced = false;
};

/**
 * The suit's projector for hull repair (Docs/HullRepair.md): it lays a breach's
 * weld frame onto the hull itself as laser light. After MindKore's bodycam
 * projector (L22, UMKBodycamProjectorComponent and MKProjectorModel): spot
 * lights under the lens whose light functions draw one canvas raster, two
 * lasers on its two channels, the raster redrawn on ticks of the device clock
 * (12 Hz, MindKore's MKDeviceClockHz); a jolt shakes its scan bands, taking a
 * breach in the star's light the sync is lost for a second and a half and the
 * picture goes grainy, the lasers are driven harder there and by the square of
 * the distance; a breach is taken after a dwell of two ticks and kept through
 * three ticks of looking away, as the device frame (L20, FMKDeviceFrameState)
 * takes a target. Letters are MindKore's character generator, MK Pribor N, with
 * wide tracking; numbers are segment readouts.
 *
 * The owner's look (19.10): noir dark blue - the halo laser (HaloColour, the
 * raster's G) draws everything in an ink navy, the core laser (CoreColour, R)
 * lays a pale blue-white sliver only down the brightest strokes (a fresh bead,
 * the sealed ring's one pulse) and is switched off otherwise; the frame a thin
 * dashed line that trembles slightly; on appearing the dashes converge from
 * several sides with anticipation, overshoot and settle, some points strobing,
 * the readout typed in last; on going they break apart outward. Every colour,
 * timing and amplitude is a property here, in the Look categories; the strap
 * threads (UOSHullToolsComponent) take the colours from here too.
 *
 * The raster is in the breach's frame, not the projector's (MindKore's is the
 * projector's): the light function takes the lit pixel into the breach frame
 * (M_OS_WeldProjection), so the contour stays on the hull while the lights,
 * riding the camera, move between redraws.
 *
 * It reads AOSPlateSocket and writes nothing there. It is lit only while the
 * view is on a breach within ViewRangeCm; dark, it ticks at 24 Hz to look for
 * one and its lights are hidden; it redraws every frame only while animating.
 */
UCLASS(ClassGroup = (OS), meta = (BlueprintSpawnableComponent))
class MINDKORE_API UOSWeldProjectorComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UOSWeldProjectorComponent();

	/** Draws the whole picture (the raster's G). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OS|Projector")
	TObjectPtr<USpotLightComponent> HaloLaser = nullptr;

	/** The pale sliver down the brightest strokes (the raster's R). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OS|Projector")
	TObjectPtr<USpotLightComponent> CoreLaser = nullptr;

	/** The light function (Scripts/os_weld_projector.py). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector")
	TSoftObjectPtr<UMaterialInterface> PictureFunction;

	/** A breach farther than this is not taken, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector", meta = (ClampMin = "100.0"))
	float ViewRangeCm = 1500.0f;

	/** Looking at a breach: the view ray crosses its frame, or its centre is this close to the view axis, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector", meta = (ClampMin = "1.0", ClampMax = "80.0"))
	float LookConeDeg = 25.0f;

	// --- colour and power -----------------------------------------------------------

	/** Ink navy: the frame, the text, the threads' glow. Linear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look")
	FLinearColor HaloColour = FLinearColor(0.045f, 0.065f, 0.15f);

	/** Pale blue-white: the core of a fresh bead, the sealed ring's pulse, a thread's beads. Linear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look")
	FLinearColor CoreColour = FLinearColor(0.62f, 0.74f, 1.0f);

	/**
	 * Illuminance each laser lays on the hull in the dark at full drive, lux of a white light (the colour
	 * takes its share), at any distance: the power follows the square of it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look", meta = (ClampMin = "0.1"))
	float HaloLux = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look", meta = (ClampMin = "0.1"))
	float CoreLux = 8.0f;

	/** How much harder the lasers are driven on a breach in full starlight (MindKore's SunDrive). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look", meta = (ClampMin = "1.0"))
	float SunDrive = 6.0f;

	/** Star light on the breach (lux on its face) from which it counts as lit: grain and a lost sync; full SunDrive at SunLux. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look", meta = (ClampMin = "0.0"))
	float LitLux = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look", meta = (ClampMin = "0.01"))
	float SunLux = 2.5f;

	// --- the dashed line and its tremble ---------------------------------------------

	/** Stroke width in pixels of the view (1-2 px at any range), never under MinStrokeCm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Dash", meta = (ClampMin = "0.5"))
	float StrokePx = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Dash", meta = (ClampMin = "0.1"))
	float MinStrokeCm = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Dash", meta = (ClampMin = "0.5"))
	float DashCm = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Dash", meta = (ClampMin = "0.0"))
	float DashGapCm = 2.2f;

	/** Each dash's wobble across the line, cm (a few millimetres), its width wobble (share) and brightness flicker (share). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Dash", meta = (ClampMin = "0.0"))
	float JitterCm = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Dash", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float JitterWidth = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Dash", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float JitterFlicker = 0.12f;

	/** How often the tremble takes a new position, Hz (at most the device clock's 12). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Dash", meta = (ClampMin = "0.5", ClampMax = "12.0"))
	float JitterHz = 8.0f;

	/** A gap: the share of a dash left, the share of dashes out on each tremble, its tremble and flicker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Dash", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float GapDashShare = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Dash", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GapDropout = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Dash", meta = (ClampMin = "1.0"))
	float GapJitterGain = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Dash", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GapFlicker = 0.55f;

	/** Sealed: the ring's one pulse, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Dash", meta = (ClampMin = "0.1"))
	float SealPulseSeconds = 0.9f;

	// --- appear and vanish -------------------------------------------------------------

	/** From the first group's start to the last group's lock, s; each group flies FlightSeconds of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Appear", meta = (ClampMin = "0.1"))
	float AppearSeconds = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Appear", meta = (ClampMin = "0.05"))
	float FlightSeconds = 0.4f;

	/** How far a group starts out, cm (each a little more or less). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Appear", meta = (ClampMin = "0.0"))
	float FlyCm = 70.0f;

	/** The wind-up back before the travel and the overshoot past the lock, shares of the flight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Appear", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float Anticipation = 0.07f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Appear", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float Overshoot = 0.12f;

	/** The tremble on the way in, times the settled one; it falls back as each group settles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Appear", meta = (ClampMin = "1.0"))
	float AppearJitterGain = 5.0f;

	/** The share of dashes that strobe on the way in, their rates and how long the strobing outlives the lock, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Appear", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StrobeShare = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Appear", meta = (ClampMin = "0.5"))
	float StrobeMinHz = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Appear", meta = (ClampMin = "0.5"))
	float StrobeMaxHz = 13.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Appear", meta = (ClampMin = "0.0"))
	float StrobeLingerSeconds = 0.5f;

	/** The readout types in after the frame has locked, signs a second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Appear", meta = (ClampMin = "1.0"))
	float TypeSignsPerSecond = 45.0f;

	/** Going: the groups break apart outward and fade, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Appear", meta = (ClampMin = "0.05"))
	float VanishSeconds = 0.3f;

	// --- text ----------------------------------------------------------------------------

	/** Empty hands: the square frame round the hole, this far out from its edge - where a plate's rim must rest, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Plate", meta = (ClampMin = "0.0"))
	float PlateMarginCm = 25.0f;

	/** A plate held, on a strap or over the hole and nearer than this is framed instead, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Plate", meta = (ClampMin = "100.0"))
	float PlateRangeCm = 1000.0f;

	/** A plate's frame locks onto the breach when its centre is this near the hole's and it is this square to it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Plate", meta = (ClampMin = "0.0"))
	float SnapCm = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Plate", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float SnapDeg = 6.0f;

	/** Picking a plate up (or letting it go) the frame converges again, quicker: this long in all, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Appear", meta = (ClampMin = "0.05"))
	float ReconvergeSeconds = 0.4f;

	/** Extra cells after each letter (wide tracking). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look|Text", meta = (ClampMin = "0"))
	int32 TrackCells = 2;

	// Readouts for probes.
	UPROPERTY(BlueprintReadOnly, Transient, Category = "OS|Projector")
	float StarLux = 0.0f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "OS|Projector")
	float Candelas = 0.0f;

	/** The raster: RasterSize square over 2 RasterHalfCm of the breach frame. */
	static constexpr int32 RasterSize = 1024;
	static constexpr float RasterHalfCm = 220.0f;
	/** The contour flies in as this many groups: two per side and the four corners. */
	static constexpr int32 GroupCount = 12;

	bool IsLit() const { return Phase != EPhase::Closed; }
	bool IsCoreOn() const { return bCoreOn; }
	AOSPlateSocket* GetTarget() const { return Target.Get(); }
	/** The text lines of the last picture, for probes. */
	const TArray<FString>& GetPictureLines() const { return Lines; }
	int32 GetBoxCount() const { return Boxes.Num(); }

	/**
	 * Probes: hold the picture at this many seconds after its appear began (a still per moment; time
	 * stands still for the frame, not for the world), or let it run again with a negative value.
	 * Replay starts the appear anew on the breach in view.
	 */
	void HoldClock(double SecondsAfterAppear);
	void Replay();

	/** A group's flight on the way in: 0 out, 1 locked, with the wind-up below 0 and the overshoot above 1. */
	static float FlightCurve(float Tau, float AnticipationShare, float OvershootShare);
	/** When group Index starts its flight after the appear begins, s. */
	float GroupDelay(int32 Index) const;

	/** Radians one pixel of the pawn's view spans across (FOV over the viewport width); a guess without a viewport. */
	static float ViewPixelAngle(const APawn* Pawn);

	/** The plate to frame for Socket: held, on a strap, or lying over the hole; null with empty hands. */
	UPrimitiveComponent* FindPlate(const AOSPlateSocket& Socket) const;
	/** Where Plate would lie on Socket's hull now (projected along the hull's normal) and how it would fit. */
	FOSWeldFootprint Footprint(const AOSPlateSocket& Socket, const UPrimitiveComponent& Plate) const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	enum class EPhase : uint8 { Closed, Appearing, Open, Vanishing };

	// FMKDeviceFrameState's ticks.
	static constexpr int32 DwellTicks = 2;
	static constexpr int32 LoseTicks = 3;
	static constexpr int32 ReacquireTicks = 12;
	static constexpr int32 SyncLossTicks = 18;

	EPhase Phase = EPhase::Closed;
	double PhaseStart = 0.0;
	double HeldAt = -1.0;
	// When the last appear began (HoldClock counts from it), and its pace: 1 for a full appear, less for a re-converge.
	double AppearOrigin = 0.0;
	float AppearScale = 1.0f;
	TWeakObjectPtr<UPrimitiveComponent> FramedPlate;
	int64 LastTick = MIN_int64;
	TWeakObjectPtr<AOSPlateSocket> Target;
	TWeakObjectPtr<AOSPlateSocket> Seen;
	int32 SeenTicks = 0;
	int32 MissTicks = 0;
	TWeakObjectPtr<AOSPlateSocket> Left;
	int64 LeftTick = MIN_int64;
	// Until this game time after BeginPlay the lasers burn at a power nobody sees, so their light
	// functions are ready before the first real light (MindKore: the first frames shone without them).
	double PrimeUntil = -1.0;
	bool bBright = false;
	bool bCoreOn = false;
	bool bWasSealed = false;
	double SealAt = -100.0;
	int32 SyncTicksLeft = 0;
	FVector LastVelocity = FVector::ZeroVector;
	float Jolt = 0.0f;
	TArray<FOSWeldBox> Boxes;
	TArray<FString> Lines;
	// The suit's device: while the attachment menu holds it the weld frame is dark, and converges again after.
	TWeakObjectPtr<UOSSuitProjectorComponent> Suit;
	bool bYielded = false;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasRenderTarget2D> Raster = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HaloFunction = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CoreFunction = nullptr;

	double Clock() const;
	bool IsAnimating(double Now) const;
	AOSPlateSocket* FindLookedAt() const;
	void Acquire(AOSPlateSocket* Socket);
	void Compose(const AOSPlateSocket& Socket, double Now, float WaveRows);
	void Aim(const AOSPlateSocket& Socket);
	void GoDark();

	UFUNCTION()
	void DrawRaster(UCanvas* Canvas, int32 Width, int32 Height);
};
