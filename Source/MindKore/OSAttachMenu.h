#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OSSuitProjector.h"
#include "OSAttachMenu.generated.h"

class UOSWeaponsComponent;
class USoundBase;
struct FOSAttachment;

/** How the menu last ended (probes, os.Hands.Probe). */
UENUM()
enum class EOSAttachMenuOutcome : uint8
{
	None,
	/** Released or LMB on a full frame: the selected row applied (Mounted, Unmounted or a magazine swapped). */
	Applied,
	/** Released before the full frame, or nothing to apply. */
	Cancelled,
	/** Dark at once, nothing applied: a slot key, a tool, a grab, a tap. */
	Killed,
	/** Not opened: a reload running. */
	Refused,
};

/**
 * The attachment menu (Docs/Attachments.md): Tab held with a gun in hand turns the gun over in the hand
 * (the scope's menu pose, OSScope.h) and the suit's projector (UOSSuitProjectorComponent) lays the
 * gun's attachments as light on the wall ahead - MK Pribor N rows, the selection's corners, the soft keys
 * - with a bracket of light on the gun round the place the selected one goes. Everything the light does
 * changes on the device's 12 Hz ticks: Opening (point, line, sweep, squeezed frame, frame: 5 ticks),
 * Open, Closing (dim with the Apply, line, point: 3 ticks). No wall within the projector's reach: the
 * rows go to the HUD as one line (GetHudLine) and only the bracket is lit.
 *
 * Made by UOSWeaponsComponent on BeginPlay; the pawn drives it (Press / Release / Step / ApplyNow / Kill,
 * AOSZeroGPawn::Route). It ticks only while open or while a module it applied is on its way.
 */
UCLASS(ClassGroup = (OS))
class MINDKORE_API UOSAttachMenu : public UActorComponent
{
	GENERATED_BODY()

public:
	UOSAttachMenu();

	/** Ticks of the open and the close (MindKore's projector: point, line, sweep, squeeze, frame / dim, line, point). */
	static constexpr int32 OpenTicks = 5;
	static constexpr int32 CloseTicks = 3;
	/** Rows of 72 px from y 96 before «▾ ЕЩЁ N» at y 400 (Docs/Attachments.md, «Отличия от замысла»: 4, not 6). */
	static constexpr int32 VisibleRows = 4;

	/** The header types at this many signs a second (the weld frame's 45). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Menu")
	float TypeSignsPerSecond = 45.0f;

	/** The bracket on the gun: its corners' side, cm, and its flight between places, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Menu")
	float BracketCm = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Menu")
	float BracketFlightSeconds = 0.3f;

	/** After the close the gun fires once its pose is back near rest and not before this (Docs/Vision/Attachments.md §2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Menu")
	float ReturnSeconds = 0.3f;

	/** The relay in the suit as the projector wakes and sleeps (a stand-in until the Foley: the holster's click, quiet). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Menu")
	TObjectPtr<USoundBase> RelaySound = nullptr;

	/** Tab down: open (or wait for the shot's cycle to end); refused during a reload. */
	void Press();
	/** Tab up: on a full frame apply the selection and close; earlier, close without it. */
	void Release();
	/** LMB: as Release on a full frame; nothing before it. */
	void ApplyNow();
	/** The wheel: a row up (-1) or down (+1), round; the corners jump on the next device tick. */
	void Step(int32 Direction);
	/** Dark at once, nothing applied (a slot key, a tool, a grab, a tap of Tab). */
	void Kill();

	/** Open or opening, closing, or waiting to open. */
	bool IsActive() const { return State != EState::Closed || bWaiting; }
	/** The full frame is up: input counts. */
	bool IsOpen() const { return State == EState::Open; }
	/** The gun wants the menu's pose: the menu up, or a module it applied still on its way. */
	bool WantsMenuPose() const;
	/** The gun is back from the menu: closed, nothing on its way, ReturnSeconds after, near its rest pose. */
	bool IsGunFree() const;

	EOSAttachMenuOutcome GetLastOutcome() const { return LastOutcome; }
	int32 GetSelected() const { return Selected; }
	int32 GetRowCount() const { return Rows.Num(); }
	/** The rows' names as the picture shows them, for probes and the log. */
	FString DescribeRows() const;
	/** The menu as one HUD line while open with no wall in reach; a refusal's word for a moment; else empty. */
	FString GetHudLine() const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	enum class EState : uint8 { Closed, Opening, Open, Closing };

	struct FRow
	{
		FName Id;
		FText Name;
		FText Description;
		/** Its place on the gun in hand (Mounts index) and whether it is on it. */
		int32 Mount = INDEX_NONE;
		bool bOn = false;
		bool bMagazine = false;
		/** A place with nothing for it: «ВЕРХ — ПУСТО», dim, not selectable (§8). */
		bool bEmpty = false;
	};

	EState State = EState::Closed;
	bool bWaiting = false;
	bool bApplyOnClose = false;
	int64 StateTick = 0;
	int64 LastTick = MIN_int64;
	double OpenAt = 0.0;
	double ClosedAt = -100.0;
	int32 Selected = 0;
	int32 PendingStep = 0;
	/** The corners jumped this tick: the core laser lights them once. */
	bool bJumped = false;
	TArray<FRow> Rows;
	EOSAttachMenuOutcome LastOutcome = EOSAttachMenuOutcome::None;
	FString Refusal;
	double RefusalUntil = 0.0;
	/** The bracket's flight: from the last place's pixel to the selected one's, from BracketFrom. */
	FVector2D BracketFromPx = FVector2D::ZeroVector;
	FVector2D BracketToPx = FVector2D::ZeroVector;
	float BracketFromSize = 0.0f;
	int32 BracketMount = INDEX_NONE;
	double BracketSince = -100.0;

	UPROPERTY(Transient)
	TObjectPtr<UOSWeaponsComponent> Guns = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UOSSuitProjectorComponent> Projector = nullptr;

	void Open();
	void BeginClose(bool bApply);
	void Finish(EOSAttachMenuOutcome Outcome);
	/** The rows of the gun in hand: what fits it, on it or in the pouch. */
	void ReadRows();
	/** The selected row's verb: НАДЕТЬ / СНЯТЬ, СМЕНИТЬ / ВЕРНУТЬ ШТАТНЫЙ. */
	FText Verb() const;
	bool ApplySelected();
	/** The picture for this moment (the device tick Tick, Now on the world's clock) into the projector. */
	void Compose(int64 Tick, double Now);
	/** The bracket's corners on the gun (pixels; false when the place is out of the raster). */
	bool Bracket(double Now, FVector2D& OutCentre, float& OutSize);
	void Draw(TArray<FOSWeldBox>& Boxes, double Now, float Level) const;
};
