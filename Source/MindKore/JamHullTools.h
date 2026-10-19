#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "JamHullTools.generated.h"

class AJamPlateSocket;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class UStaticMesh;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EJamHullTool : uint8
{
	None,
	Strap,
	Welder
};

/** One line of the strap: two ends on two bodies (a static hull end is fine), a rest length it pulls them to. */
USTRUCT()
struct FJamStrapLine
{
	GENERATED_BODY()

	TWeakObjectPtr<UPrimitiveComponent> A;
	TWeakObjectPtr<UPrimitiveComponent> B;
	FVector LocalA = FVector::ZeroVector;
	FVector LocalB = FVector::ZeroVector;
	/** B is the owner's body (the belt): the line tows until its second end is clicked. */
	bool bBelt = false;
	float LengthCm = 0.0f;
	/** Last tension, 0..1 of MaxForce (how the thread reads). */
	float Tension = 0.0f;
};

/**
 * The hull-repair tools in hand (owner, 17-18.10; Docs/HullRepair.md): slot 4
 * the strap, slot 5 the welder. LMB is the tool's action, R its second action,
 * the mouse wheel reels the strap.
 *
 * Strap: LMB on a body hooks end A and the line runs to the belt (towing).
 * LMB on another body moves the belt end there: a line A-B, the hands free.
 * Up to MaxLines lines. A line pulls both ends together with a force that grows
 * with the stretch over its length and stops at MaxForce, so nothing tears or
 * flies off; a static end (the hull) only anchors. The wheel shortens or
 * lengthens the last line; R cuts the line nearest the crosshair.
 *
 * Welder: LMB held lays the seam where the torch (the crosshair within
 * WeldReachCm) meets a breach's contour (AJamPlateSocket::Weld); R switches
 * weld and cut.
 *
 * Each line is drawn as a laser thread of the suit's projector (the engine
 * cylinder with M_Jam_StrapLaser, Scripts/jam_weld_projector.py) in the
 * projector's colours (UJamWeldProjectorComponent HaloColour, CoreColour), its
 * tension in its form: slack - broken and dropping out, taut - solid with pale
 * beads running to the first end, faster the harder it pulls, at the limit - a
 * pale flickering core.
 */
UCLASS(ClassGroup = (Jam), meta = (BlueprintSpawnableComponent))
class MINDKORE_API UJamHullToolsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UJamHullToolsComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Strap", meta = (ClampMin = "100.0"))
	float StrapReachCm = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Strap", meta = (ClampMin = "1"))
	int32 MaxLines = 3;

	/** Pull per cm of stretch, kg/s^2 (force units kg cm/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Strap", meta = (ClampMin = "0.0"))
	float Stiffness = 3000.0f;

	/** Along the line, on the ends' relative speed, kg/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Strap", meta = (ClampMin = "0.0"))
	float Damping = 400.0f;

	/** The most a line pulls, kg cm/s^2 (60000: a 60 kg plate at 10 m/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Strap", meta = (ClampMin = "0.0"))
	float MaxForce = 60000.0f;

	/** One wheel notch reels the last line this much, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Strap", meta = (ClampMin = "1.0"))
	float ReelStepCm = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Strap", meta = (ClampMin = "0.0"))
	float MinLengthCm = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Welder", meta = (ClampMin = "10.0"))
	float WeldReachCm = 150.0f;

	/** The thread a line is drawn with (M_Jam_StrapLaser) and its core's radius, cm (the material widens it to 3 px with range). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Strap")
	TSoftObjectPtr<UMaterialInterface> ThreadMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Strap", meta = (ClampMin = "0.1"))
	float ThreadRadiusCm = 0.6f;

	UFUNCTION(BlueprintPure, Category = "Jam")
	EJamHullTool GetTool() const { return Tool; }

	/** 4 / 5: take the tool (again: put it away). */
	void SelectTool(EJamHullTool NewTool);

	/** LMB press. */
	void PrimaryPressed();
	/** LMB held (every frame while down). */
	void PrimaryHeld(float DeltaSeconds);
	void PrimaryReleased();
	/** R. */
	void SecondaryPressed();
	/** Mouse wheel while the strap is in hand: +1 shortens the last line, -1 lengthens it. */
	void Reel(int32 Step);

	/** A line between two bodies at two world points, rest length = their distance now (probes; the strap's second click). */
	int32 AddLine(UPrimitiveComponent* A, const FVector& PointA, UPrimitiveComponent* B, const FVector& PointB, bool bBelt = false);
	void SetLineLength(int32 Index, float LengthCm);
	void CutLine(int32 Index);
	void CutAll();
	const TArray<FJamStrapLine>& GetLines() const { return Lines; }

	bool IsCutting() const { return bCutting; }
	/** What the torch did last frame (for the HUD), empty when idle. */
	const FText& GetWeldStatus() const { return WeldStatus; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	EJamHullTool Tool = EJamHullTool::None;
	TArray<FJamStrapLine> Lines;
	bool bCutting = false;
	bool bTorchOn = false;
	FText WeldStatus;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> ThreadMesh = nullptr;

	/** One thread per line, made on first need and hidden while its line is gone. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Threads;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> ThreadLooks;

	bool TraceView(float Reach, FHitResult& OutHit) const;
	UPrimitiveComponent* OwnerBody() const;
	void ApplyLines(float DeltaSeconds);
	void DrawLines();
};
