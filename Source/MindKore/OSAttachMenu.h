#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OSSuitProjector.h"
#include "OSAttachMenu.generated.h"

class AHUD;
class UCameraComponent;
class UCanvas;
class UOSWeaponsComponent;
class USoundBase;
struct FOSAttachment;

/**
 * A world point to the HUD canvas' pixels through the camera: with the camera's first-person field of view (what the
 * gun in hand is drawn with, OSScope blends it) when bFirstPerson, else the world's. Horizontal field of view across
 * the canvas, square pixels; false behind the eye.
 */
MINDKORE_API bool OSProjectToHud(const UCameraComponent* Camera, const UCanvas& Canvas, const FVector& World, bool bFirstPerson, FVector2D& Out);

/**
 * OSProjectToHud made once for many points (the laser's mask of the gun projects its every vertex). The frame's own
 * view and projection (the canvas' scene view, what the picture is drawn with: owner 23.10, the gun's mask stood ~20 px
 * off the drawn gun at the hip) when the canvas has one, else the camera's; first-person points stretched across the
 * view by tan(world / 2) / tan(gun / 2) as the gun is drawn; then the bodycam lens' bend. Depth: cm along the view.
 */
struct MINDKORE_API FOSHudProjector
{
	FOSHudProjector(const UCameraComponent* Camera, const UCanvas& Canvas);
	bool Project(const FVector& World, bool bFirstPerson, FVector2D& Out, double* OutDepth = nullptr) const;

	bool bValid = false;
	bool bSceneView = false;
	FMatrix ViewMatrix = FMatrix::Identity;
	FMatrix ProjectionMatrix = FMatrix::Identity;
	FTransform Eye;
	double Drawn = 1.0;
	double WorldScale = 1.0;
	double LensK = 0.0;
	FVector2D Clip = FVector2D::ZeroVector;
};

/** The gun in hand and what hangs on it (modules, the magazine), on the HUD: their meshes' boxes' corners (OSProjectToHud, first person). */
MINDKORE_API void OSGunScreenPoints(const UOSWeaponsComponent& Guns, const UCameraComponent* Camera, const UCanvas& Canvas, TArray<FVector2D>& Out);

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
 * (the scope's menu pose, OSScope.h) and the suit's HUD lays a node on every place of the gun - the rail,
 * the muzzle, the well - with a leader out to a box beside the gun that lists what goes there (Crysis'
 * customisation, owner's playtests 22-23.10). The mouse moves a cursor instead of the view while it is up:
 * a line under the cursor (or the wheel) selects, LMB puts the selected thing on or takes it off and the
 * menu stays up (the module seats in the menu's pose, its line shows the progress); letting Tab go closes
 * it, never applies.
 *
 * Drawn every frame on the HUD in MK Pribor N on a grid of font cells, steady: the boxes are laid out once,
 * when the gun has turned into the menu's pose, and stay where they are on screen (only the nodes ride the
 * gun's sway; the leaders follow them). The open: the nodes flash, the leaders run out, the boxes unfold, the
 * text decodes; the selection bar slides between lines. The state machine steps on the device's 12 Hz ticks:
 * Opening (OpenTicks), Open, Closing (CloseTicks: fade).
 *
 * Made by UOSWeaponsComponent on BeginPlay; the pawn drives it (Press / Release / Step / ApplyNow / Kill /
 * MoveCursor, AOSZeroGPawn::Route and Look); the zero-G HUD calls DrawHud. It ticks only while open or while
 * a module it applied is on its way.
 */
UCLASS(ClassGroup = (OS))
class MINDKORE_API UOSAttachMenu : public UActorComponent
{
	GENERATED_BODY()

public:
	UOSAttachMenu();

	/** Device ticks of the open and the close (fade), 12 Hz. */
	static constexpr int32 OpenTicks = 5;
	static constexpr int32 CloseTicks = 3;

	/** The HUD's colour: the projector's pale core. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Menu")
	FLinearColor HudColour = FLinearColor(0.62f, 0.74f, 1.0f, 1.0f);

	/** The boxes' back plate (readability over a lit wall). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Menu")
	FLinearColor PlateColour = FLinearColor(0.0f, 0.012f, 0.035f, 0.72f);

	/** A box's gap from the gun's outline on screen and from the screen's edge, in font cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Menu")
	int32 GapCells = 10;

	/** The boxes are laid out once the gun is this far into the menu's pose (or this late after the open). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Menu")
	float LayoutAtPose = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Menu")
	float LayoutLatestSeconds = 0.45f;

	/**
	 * The cursor's speed against the view's (owner 23.10: «нужно сохранять основную сенсу»): 1 - a mouse move takes
	 * the cursor as far on screen as the same move would turn the view (the look sensitivity, at the view's centre).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Menu", meta = (ClampMin = "0.05"))
	float CursorGain = 1.0f;

	/** After the close the gun fires once its pose is back near rest and not before this (Docs/Vision/Attachments.md §2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Menu")
	float ReturnSeconds = 0.3f;

	/** The relay in the suit as the menu wakes and sleeps (a stand-in until the Foley: the holster's click, quiet). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Menu")
	TObjectPtr<USoundBase> RelaySound = nullptr;

	/** Tab down: open (or wait for the shot's cycle to end); refused during a reload. */
	void Press();
	/** Tab up: close; nothing is applied by the release (the clicks did it). */
	void Release();
	/** LMB: the selected line on (or off) the gun; the menu stays up. Not while a module is seating. */
	void ApplyNow();
	/** The wheel: a line up (-1) or down (+1), round, at once. */
	void Step(int32 Direction);
	/** The mouse while the menu is up: the cursor moves, the view does not (degrees the view would turn, X right, Y up). */
	void MoveCursor(const FVector2D& Degrees);
	/** Dark at once, nothing applied (a slot key, a tool, a grab, a tap of Tab). */
	void Kill();

	/** Open or opening, closing, or waiting to open. */
	bool IsActive() const { return State != EState::Closed || bWaiting; }
	/** The full frame is up: input counts. */
	bool IsOpen() const { return State == EState::Open; }
	/** The menu takes the mouse (opening or open). */
	bool TakesMouse() const { return State == EState::Opening || State == EState::Open; }
	/** The gun wants the menu's pose: the menu up, or a module it applied still on its way. */
	bool WantsMenuPose() const;
	/** The gun is back from the menu: closed, nothing on its way, ReturnSeconds after, near its rest pose. */
	bool IsGunFree() const;

	EOSAttachMenuOutcome GetLastOutcome() const { return LastOutcome; }
	int32 GetSelected() const { return Selected; }
	int32 GetRowCount() const { return Rows.Num(); }
	/** The rows' names as the picture shows them, for probes and the log. */
	FString DescribeRows() const;
	/** A refusal's word for a moment (the menu itself is DrawHud's); else empty. */
	FString GetHudLine() const;
	/** The nodes, leaders, boxes and cursor, on the HUD's canvas (AOSZeroGHUD::DrawHud), every frame while lit. */
	void DrawHud(AHUD& Hud, UCanvas& Canvas);

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
		/** A place with nothing for it: «ПУСТО», dim, not selectable (§8). */
		bool bEmpty = false;
		/** The laser's colour, a line under the laser while it is on the gun: a click takes the palette's next. */
		bool bTint = false;
	};

	/** A place's box, laid out once per open: its rows (indices into Rows), where it is, its side of the gun. */
	struct FPlaceBox
	{
		int32 Mount = INDEX_NONE;
		TArray<int32> Lines;
		FVector2D At = FVector2D::ZeroVector;
		/** Where it slides from after a re-layout (MovedAt). */
		FVector2D From = FVector2D::ZeroVector;
		FVector2D Size = FVector2D::ZeroVector;
		/** 0 right of the gun, 1 left, 2 above, 3 below. */
		int32 Side = 0;
	};

	EState State = EState::Closed;
	bool bWaiting = false;
	int64 StateTick = 0;
	int64 LastTick = MIN_int64;
	double OpenAt = 0.0;
	double ClosingAt = 0.0;
	double ClosedAt = -100.0;
	int32 Selected = 0;
	/** The selection's last move: the line it came from and when (the bar's slide). */
	int32 PreviousSelected = INDEX_NONE;
	double SelectedAt = -100.0;
	TArray<FRow> Rows;
	EOSAttachMenuOutcome LastOutcome = EOSAttachMenuOutcome::None;
	bool bAppliedAny = false;
	/** The last click: what it put on or took off, when, and the seat's whole length then (the line's progress). */
	FName AppliedId;
	int32 AppliedMount = INDEX_NONE;
	double AppliedAt = -100.0;
	float AppliedSeconds = 0.0f;
	/** A click refused (a module still seating): the keys line blinks «ЖДИ». */
	double BusyAt = -100.0;
	FString Refusal;
	double RefusalUntil = 0.0;
	/** The layout, frozen on screen once the gun is in the menu's pose. */
	TArray<FPlaceBox> Boxes;
	bool bLaidOut = false;
	double LaidOutAt = -100.0;
	/** A re-layout (the gun came to sit under a box: the pose settled, a suppressor went on): the boxes slide over. */
	double MovedAt = -100.0;
	double OverlapSince = -1.0;
	/** The cursor, HUD pixels; moved this frame (hover selects only after a move); the wheel's selection stands until the mouse moves. */
	FVector2D Cursor = FVector2D::ZeroVector;
	bool bCursorPlaced = false;
	bool bCursorMoved = false;
	bool bWheelChose = false;
	bool bHovering = false;
	FVector2D LastClip = FVector2D(1920.0, 1080.0);

	UPROPERTY(Transient)
	TObjectPtr<UOSWeaponsComponent> Guns = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> Camera = nullptr;

	void Open();
	void BeginClose();
	void Finish(EOSAttachMenuOutcome Outcome);
	/** The rows of the gun in hand: what fits it, on it or in the pouch; place by place. */
	void ReadRows();
	/** The selected row's verb: НАДЕТЬ / СНЯТЬ, СМЕНИТЬ / ВЕРНУТЬ ШТАТНЫЙ. */
	FText Verb() const;
	bool ApplySelected();
	/** The selection moved to Index: the bar's slide, the switch's choice, the gun's nod, a click. */
	void Select(int32 Index);
	/** A point on the gun in view to the HUD's pixels, with the first-person field of view the gun is drawn with. */
	bool ProjectGun(const UCanvas& Canvas, const FVector& World, FVector2D& Out) const;
	/** Where on the gun a place's node sits, in the world. */
	bool MountAnchor(int32 Mount, FVector& Out) const;
	/** The gun and what hangs on it, on screen (its meshes' boxes' corners). */
	FBox2D GunOnScreen(const UCanvas& Canvas) const;
	/** The boxes beside the gun's outline, once (bLaidOut). */
	void LayOut(const UCanvas& Canvas, float Cell);
};
