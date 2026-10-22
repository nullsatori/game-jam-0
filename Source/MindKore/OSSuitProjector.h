#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "OSSuitProjector.generated.h"

class UCanvas;
class UCanvasRenderTarget2D;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USpotLightComponent;

/** One stroke of a projector raster: a box in raster pixels and the drive of each laser, 0..1 (MindKore's FMKProjectorBox). */
struct FOSWeldBox
{
	int16 X = 0, Y = 0, W = 0, H = 0;
	float Halo = 0.0f;
	float Core = 0.0f;
	/** Turned about its centre, degrees (a plate's frame at a slant); 0 for the breach's own strokes. */
	float Angle = 0.0f;
};

/** A glyph of MindKore's character generator, MK Pribor N: ten rows from the top, bit 0 leftmost; Advance in cells. */
struct FOSProjectorGlyph
{
	uint16 Code;
	uint8 Advance;
	uint16 Rows[10];
};

/** The device clock of the suit (MindKore's MKDeviceClockHz): everything the light does changes on its ticks. */
inline constexpr double OSProjectorClockHz = 12.0;

/** The glyph for Code (unknown: the space's; the space itself: null - it only advances). */
MINDKORE_API const FOSProjectorGlyph* OSProjectorGlyph(TCHAR Code);

/** Width of a line in cells, without the gap after its last glyph (MKDeviceTextCells). */
MINDKORE_API int32 OSProjectorTextCells(FStringView Text);

/** A number in 0..1 that is the same for the same pair and unrelated for neighbours (MindKore's Hash01). */
MINDKORE_API float OSProjectorHash(int64 A, int64 B);

/** The lit cells of a line as horizontal runs: Emit(X, Y, Length) in cells, Y from the top row (MKDeviceTextRuns). */
template <typename EmitType>
void OSProjectorTextRuns(FStringView Text, EmitType&& Emit)
{
	int32 PenX = 0;
	for (const TCHAR Char : Text)
	{
		const FOSProjectorGlyph* Glyph = OSProjectorGlyph(Char);
		for (int32 Row = 0; Glyph && Row < 10; ++Row)
		{
			for (int32 Bit = 0; Bit < 16;)
			{
				if (!(Glyph->Rows[Row] >> Bit & 1))
				{
					++Bit;
					continue;
				}
				int32 End = Bit;
				while (End < 16 && (Glyph->Rows[Row] >> End & 1))
				{
					++End;
				}
				Emit(PenX + Bit, Row, End - Bit);
				Bit = End;
			}
		}
		PenX += Glyph ? Glyph->Advance : 0;
	}
}

/**
 * Strokes in raster pixels (MindKore's FPainter over FOSWeldBox): boxes, cut-outs, MK Pribor N text,
 * dotted rules, selection corners and outlines. Boxes are drawn in order and each replaces what is
 * under it, so a cut (a black box) carves a word out of a plate.
 */
struct MINDKORE_API FOSProjectorPaint
{
	TArray<FOSWeldBox>& Out;
	/** The raster's size, for clamping. */
	int32 Width = 848;
	int32 Height = 480;

	void Box(int32 X, int32 Y, int32 W, int32 H, float Halo, float Core = 0.0f) const;
	/** Black: replaces what is under it. */
	void Cut(int32 X, int32 Y, int32 W, int32 H) const { Box(X, Y, W, H, 0.0f, 0.0f); }
	/** A line of MK Pribor N from its top-left corner, Cell pixels a cell; returns its width in pixels. Cut: carved, not lit. */
	int32 Text(FStringView Line, int32 X, int32 Y, int32 Cell, float Halo, float Core = 0.0f, bool bCut = false) const;
	/** The same, its right edge at Right. */
	int32 TextRight(FStringView Line, int32 Right, int32 Y, int32 Cell, float Halo, float Core = 0.0f) const;
	/** A horizontal rule of dashes from X0 to X1, Thick pixels, Dash on and Gap off. */
	void Dotted(int32 X0, int32 X1, int32 Y, float Halo, int32 Thick = 2, int32 Dash = 6, int32 Gap = 6) const;
	/** Four corners of the box (Arm long, Thick wide). */
	void Corners(int32 X, int32 Y, int32 W, int32 H, int32 Arm, int32 Thick, float Halo, float Core = 0.0f) const;
	/** The box's contour, Thick wide. */
	void Outline(int32 X, int32 Y, int32 W, int32 H, int32 Thick, float Halo) const;

	static int32 TextWidth(FStringView Line, int32 Cell) { return OSProjectorTextCells(Line) * Cell; }
	static int32 TextHeight(int32 Cell) { return 10 * Cell; }
};

/**
 * What the scanner does to its own picture (MindKore's Distort, as the weld frame's): the raster in bands
 * of eight rows, each band shifted by a wave of WaveRows pixels or dimmed whole inside the sync-lost band
 * starting at SyncBandY (< 0: none); in the star's light (bGrain) each box loses up to a fifth of its drive.
 */
MINDKORE_API void OSProjectorDistort(TArray<FOSWeldBox>& Boxes, int32 Width, int32 Height, float WaveRows, int32 SyncBandY, int64 Tick, bool bGrain);

/** Who draws with the suit's projector (the higher wins: the menu over the weld frame, Docs/Attachments.md). */
UENUM()
enum class EOSProjectorClient : uint8
{
	None,
	Weld,
	Menu,
};

/**
 * The suit's projector as a device (Docs/Attachments.md, «Прибор»): two lasers under the lens (the halo
 * on the raster's G, the core on R), one canvas raster of 848 x 480 in the projector's own frame (it rides
 * the camera; half-angles 32 / 19.5 deg, MindKore's throw 0.8), the 12 Hz device clock, the range finder
 * (power by the square of the distance and the slant, never under MinRangeGain), the star's light (sync
 * lost for a second and a half, grain, the lasers driven SunDrive harder), the jolt bending the scan lines,
 * and the light function's warm-up on BeginPlay.
 *
 * One owner of the picture at a time (Acquire / Release): the attachment menu (UOSAttachMenu) draws with
 * it; the weld frame (UOSWeldProjectorComponent) keeps its own lasers in the breach's frame and goes dark
 * while the menu holds the device (IsHeldBy), converging again after - Docs/Attachments.md, «Отличия от
 * замысла». Dark, it does not tick.
 *
 * M_OS_Projection (the light function; built on the PC, stage 2 - Docs/Attachments.md): Raster (texture),
 * Channel (which of R / G this laser draws), TanHalf (x, y: the half-angles' tangents) and the projector's
 * frame in the world - Origin, AxisX (forward), AxisY (right), AxisZ (up), set every frame while lit. A
 * lit point P maps to u = dot(P - Origin, AxisY) / (dot(P - Origin, AxisX) TanHalf.x), v the same with
 * AxisZ and TanHalf.y; the raster pixel is ((0.5 + 0.5 u) W, (0.5 - 0.5 v) H). Missing: logged once, dark.
 */
UCLASS(ClassGroup = (OS), meta = (BlueprintSpawnableComponent))
class MINDKORE_API UOSSuitProjectorComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UOSSuitProjectorComponent();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OS|Projector")
	TObjectPtr<USpotLightComponent> HaloLaser = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OS|Projector")
	TObjectPtr<USpotLightComponent> CoreLaser = nullptr;

	/** M_OS_Projection; soft, so the PC's Python can rebuild it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector")
	TSoftObjectPtr<UMaterialInterface> PictureFunction;

	/** The raster's half-angles, degrees (MindKore's throw 0.8 over 848 x 480). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector", meta = (ClampMin = "5.0", ClampMax = "60.0"))
	float HalfAngleXDeg = 32.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector", meta = (ClampMin = "5.0", ClampMax = "60.0"))
	float HalfAngleYDeg = 19.5f;

	/** A wall farther than this along the axis: no picture on the wall (the menu's rows go to the HUD), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector", meta = (ClampMin = "50.0"))
	float WallRangeCm = 600.0f;

	/** The lasers' colours and power: the weld frame's (Docs/HullRepair.md, «Проектор скафандра»). Linear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look")
	FLinearColor HaloColour = FLinearColor(0.045f, 0.065f, 0.15f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look")
	FLinearColor CoreColour = FLinearColor(0.62f, 0.74f, 1.0f);

	/** Lux each laser lays on the wall at full drive at FullPowerCm and beyond (the power follows the square of the distance). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look", meta = (ClampMin = "0.1"))
	float HaloLux = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look", meta = (ClampMin = "0.1"))
	float CoreLux = 8.0f;

	/** The range finder: full power is set for this distance, cm; nearer, the power drops with the square, never under MinRangeGain of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look", meta = (ClampMin = "10.0"))
	float FullPowerCm = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinRangeGain = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look", meta = (ClampMin = "1.0"))
	float SunDrive = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look", meta = (ClampMin = "0.0"))
	float LitLux = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Projector|Look", meta = (ClampMin = "0.01"))
	float SunLux = 2.5f;

	static constexpr int32 RasterWidth = 848;
	static constexpr int32 RasterHeight = 480;

	/** Take the picture for Client: true when it is Client's now (a lower client loses it, a higher one keeps it). */
	bool Acquire(EOSProjectorClient Client);
	/** Client is done: dark at once if it held the device. */
	void Release(EOSProjectorClient Client);
	EOSProjectorClient GetHolder() const { return Holder; }
	bool IsHeldBy(EOSProjectorClient Client) const { return Holder == Client; }

	/** The holder's picture (raster pixels); drawn, distorted, on the next tick. Empty: the lasers go dark (the holder keeps the device). */
	void SetPicture(TArray<FOSWeldBox>&& Boxes);

	/** The wall along the axis within WallRangeCm, read on the device ticks while held. */
	bool HasWall() const { return bWall; }
	float GetWallDistanceCm() const { return WallCm; }
	float GetStarLux() const { return StarLux; }
	float GetCandelas() const { return Candelas; }
	/** The light function loaded (M_OS_Projection): without it the device stays dark but the menu runs. */
	bool HasPictureFunction() const { return HaloFunction != nullptr; }
	bool IsLit() const;
	/** The last picture as drawn (after the distortion), for probes. */
	int32 GetBoxCount() const { return Drawn.Num(); }

	/** The device tick at world time Seconds (12 Hz; the weld frame and the menu share it). */
	static int64 TickAt(double Seconds) { return FMath::FloorToInt64(Seconds * OSProjectorClockHz); }

	/** World point -> raster pixel through the projector as it stands now; false behind the lens. */
	bool RasterOf(const FVector& World, FVector2D& OutPixel) const;
	/** Raster pixels a length of Cm spans at Distance cm from the lens (across). */
	float PixelsAt(float Cm, float DistanceCm) const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	static constexpr int32 SyncLossTicks = 18;

	EOSProjectorClient Holder = EOSProjectorClient::None;
	TArray<FOSWeldBox> Picture;
	TArray<FOSWeldBox> Drawn;
	bool bPictureDirty = false;
	bool bWall = false;
	float WallCm = 0.0f;
	float Slant = 1.0f;
	FVector WallPoint = FVector::ZeroVector;
	FVector WallNormal = FVector::UpVector;
	FVector StarPoint = FVector(1.0e9f);
	float StarLux = 0.0f;
	bool bBright = false;
	int32 SyncTicksLeft = 0;
	float Candelas = 0.0f;
	FVector LastVelocity = FVector::ZeroVector;
	float Jolt = 0.0f;
	int64 LastTick = MIN_int64;
	double PrimeUntil = -1.0;
	bool bWarnedFunction = false;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasRenderTarget2D> Raster = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HaloFunction = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CoreFunction = nullptr;

	/** The range finder and the star's light, on a device tick. */
	void ReadSurroundings();
	/** Lights on at the power the range finder sets; their functions get the frame. */
	void Aim();
	void GoDark();

	UFUNCTION()
	void DrawRaster(UCanvas* Canvas, int32 Width, int32 Height);
};
