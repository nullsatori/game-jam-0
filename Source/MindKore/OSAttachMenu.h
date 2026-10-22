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
 * customisation, owner's playtest 22.10: «ноды привязаны к слотам на оружии»). Drawn every frame on the
 * HUD in MK Pribor N, steady (no projector, no scan distortion); the boxes keep clear of the gun's own
 * outline on screen and follow its sway smoothed. The state machine still steps on the device's 12 Hz
 * ticks: Opening (OpenTicks: the leaders grow, the boxes type in), Open, Closing (CloseTicks: fade).
 *
 * Made by UOSWeaponsComponent on BeginPlay; the pawn drives it (Press / Release / Step / ApplyNow / Kill,
 * AOSZeroGPawn::Route); the zero-G HUD calls DrawHud. It ticks only while open or while a module it
 * applied is on its way.
 */
UCLASS(ClassGroup = (OS))
class MINDKORE_API UOSAttachMenu : public UActorComponent
{
	GENERATED_BODY()

public:
	UOSAttachMenu();

	/** Device ticks of the open (leaders out, boxes typed in) and the close (fade), 12 Hz. */
	static constexpr int32 OpenTicks = 5;
	static constexpr int32 CloseTicks = 3;

	/** The header types at this many signs a second (the weld frame's 45). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Menu")
	float TypeSignsPerSecond = 45.0f;

	/** The HUD's colour: the projector's pale core, its brightness for the selected line and the dim one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Menu")
	FLinearColor HudColour = FLinearColor(0.62f, 0.74f, 1.0f, 1.0f);

	/** The boxes' back plate (readability over a lit wall). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Menu")
	FLinearColor PlateColour = FLinearColor(0.0f, 0.01f, 0.03f, 0.55f);

	/** A box's gap from the gun's outline on screen and from the screen's edge, in font cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Menu")
	int32 GapCells = 8;

	/** How fast a box follows its node with the gun's sway, 1/s (higher: stiffer). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Menu")
	float FollowRate = 10.0f;

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
	/** A refusal's word for a moment (the menu itself is DrawHud's); else empty. */
	FString GetHudLine() const;
	/** The nodes, leaders and boxes, on the HUD's canvas (AOSZeroGHUD::DrawHud), every frame while lit. */
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
		/** A place with nothing for it: «ВЕРХ — ПУСТО», dim, not selectable (§8). */
		bool bEmpty = false;
	};

	EState State = EState::Closed;
	bool bWaiting = false;
	bool bApplyOnClose = false;
	int64 StateTick = 0;
	int64 LastTick = MIN_int64;
	double OpenAt = 0.0;
	double ClosingAt = 0.0;
	double ClosedAt = -100.0;
	int32 Selected = 0;
	int32 PendingStep = 0;
	TArray<FRow> Rows;
	EOSAttachMenuOutcome LastOutcome = EOSAttachMenuOutcome::None;
	FString Refusal;
	double RefusalUntil = 0.0;
	/** The boxes' top-left corners on screen, smoothed after their nodes (per mount); reset on open. */
	TArray<FVector2D> BoxAt;
	double LastDrawAt = -100.0;

	UPROPERTY(Transient)
	TObjectPtr<UOSWeaponsComponent> Guns = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> Camera = nullptr;

	void Open();
	void BeginClose(bool bApply);
	void Finish(EOSAttachMenuOutcome Outcome);
	/** The rows of the gun in hand: what fits it, on it or in the pouch. */
	void ReadRows();
	/** The selected row's verb: НАДЕТЬ / СНЯТЬ, СМЕНИТЬ / ВЕРНУТЬ ШТАТНЫЙ. */
	FText Verb() const;
	bool ApplySelected();
	/** A point on the gun in view to the HUD's pixels, with the first-person field of view the gun is drawn with. */
	bool ProjectGun(const UCanvas& Canvas, const FVector& World, FVector2D& Out) const;
	/** Where on the gun a place's node sits, in the world. */
	bool MountAnchor(int32 Mount, FVector& Out) const;
};
