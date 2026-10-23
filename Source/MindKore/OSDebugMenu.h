#pragma once

#include "CoreMinimal.h"
#include "OSDebug.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class SEditableTextBox;
class ITableRow;
class SSearchBox;
class STableViewBase;
class SVerticalBox;
class SWidgetSwitcher;
template <typename ItemType> class SListView;

/** A line of the parameter list. */
struct FOSDebugRow
{
	enum class EKind : uint8
	{
		Param,
		/** A group of a tab (Param.Group), folded or open; Param.Owner is its owner, for its reset. */
		Group,
		/** A section label of «Главное» (Мир, Взгляд, Рука...); also «Снаряжение» on «Игрок». */
		Section,
		/** A row of action buttons after a label (Docs/DebugMenu.md, «Ряд действий»): «Снаряжение»'s guns and attachments. */
		Actions,
		/** «Оружие»: the give-a-gun buttons (UOSDebugSubsystem::GiveGun), on any map. */
		Give,
	};
	/** One button of an Actions row: its label and state read on paint (cheap: the menu is open), a click runs it. */
	struct FAction
	{
		TFunction<FString()> Label;
		/** Lit (the accent) when true: the gun open, the attachment on a gun. */
		TFunction<bool()> IsOn;
		TFunction<void()> Run;
		FString Tip;
	};
	EKind Kind = EKind::Param;
	/** Group: Param.Group; section: its title. */
	FString Header;
	FString Label;
	/** The unit column («см/с»); empty: the parameter's own Units. */
	FString Unit;
	FOSDebugParam Param;
	/** Group: how many parameters it has (shown or not), and their keys (for its changed count). */
	int32 Count = 0;
	TArray<FString> Keys;
	/** Actions: the buttons. */
	TArray<FAction> Actions;
};

/**
 * The debug menu's widget (Docs/DebugMenu.md, «Раскладка»), one per game viewport: the panel (F6),
 * the pinned overlay (Shift+F6) and the watermark. It only draws and forwards: every value is read
 * from and written through UOSDebugSubsystem. Which groups are open is the widget's own, per user
 * (Saved/OSDebug/OpenGroups.txt).
 */
class SOSDebugMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOSDebugMenu) {}
		SLATE_ARGUMENT(UOSDebugSubsystem*, Debug)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	TSharedPtr<SWidget> GetFocusTarget() const;
	void FocusSearch();
	void OnModeChanged();
	void ShowTab(const FString& Name);
	void SetSearch(const FString& Text);
	void ToggleChangedOnly();
	/** Opens the groups of the tab whose name has this text; "all" opens every one, "none" folds every group. */
	void SetGroups(const FString& Text);

private:
	TSharedRef<SWidget> MakePanel();
	TSharedRef<SWidget> MakePinned();
	TSharedRef<SWidget> MakeWatermark();
	TSharedRef<SWidget> MakeSystemTab();
	/** «Главное»: the FPS cap, vsync, game speed and pause above the pinned parameters. */
	TSharedRef<SWidget> MakeQuickControls();
	TSharedRef<SWidget> MakeChoices(const FString& Key, const TArray<TPair<FString, double>>& Options);
	TSharedRef<SWidget> MakeTabButton(const FString& Name);
	TSharedRef<SWidget> MakeGroupHeader(TSharedPtr<FOSDebugRow> Row);
	/** A parameter's editor: a spin box for numbers, a check box for bools, text for the rest; the changed mark and the star. */
	TSharedRef<SWidget> MakeParamLine(const FOSDebugParam& Param, const FString& Label, const FString& Unit = FString());
	TSharedRef<SWidget> MakeKeyLine(const FString& Key, const FString& Label);
	TSharedRef<ITableRow> MakeRow(TSharedPtr<FOSDebugRow> Row, const TSharedRef<STableViewBase>& Table);
	void Rebuild();
	void RebuildPresets();
	void RebuildPinned();
	void ToggleGroup(const FString& Group);
	void SaveGroups() const;
	/** The status line in words («Длина руки → 90 см»), and how visible it still is (a receipt fades, an error stays). */
	FString MenuStatusText(const FString& Status) const;
	float StatusAlpha() const;
	/** The menu's own scale on top of the engine's DPI curve: never under the floor that keeps it readable at 720p. */
	float MenuScale() const;
	/** Keeps the panel off the game HUD, which is drawn in pixels at the left (lamps at the top, O2 and HP at the bottom). */
	FMargin HudClearance() const;
	/** A spin box change is written at once; its undo step is recorded when the edit settles. */
	void EditNumber(const FString& Key, const FString& Value);
	void FlushEdit();

	TWeakObjectPtr<UOSDebugSubsystem> Debug;
	FString Tab = TEXT("Главное");
	FString Search;
	bool bChangedOnly = false;
	int32 SeenGeneration = -1;
	int32 SeenChanged = -1;
	TArray<FString> SeenPins;
	/** Parameters the search found (rows that are not headers). */
	int32 Found = 0;

	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SWidgetSwitcher> Body;
	TSharedPtr<SListView<TSharedPtr<FOSDebugRow>>> List;
	TArray<TSharedPtr<FOSDebugRow>> Rows;
	TSet<FString> OpenGroups;
	/** The group whose header has scrolled off the top: its copy stays pinned there. */
	TSharedPtr<FOSDebugRow> Sticky;
	TSharedPtr<SBox> StickyBox;
	TSharedPtr<SVerticalBox> PresetBox;
	TSharedPtr<SVerticalBox> PinnedBox;
	TArray<FString> PinnedShown;
	TSharedPtr<SVerticalBox> SkippedBox;
	int32 SeenSkipped = -1;
	FString StatusSeen;
	FString StatusShown;
	double StatusAt = 0.0;

	FString EditKey;
	FString EditBefore;
	double EditAt = 0.0;
	bool bDragging = false;
	bool bFocusPending = false;

	/** The viewport's DPI scale and height in pixels, from the last tick. */
	float ViewScale = 1.0f;
	float ViewHeight = 720.0f;
};
