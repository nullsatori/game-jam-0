#pragma once

#include "CoreMinimal.h"
#include "JamDebug.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class ITableRow;
class SSearchBox;
class STableViewBase;
class SVerticalBox;
class SWidgetSwitcher;
template <typename ItemType> class SListView;

/** A line of the parameter list: a group header or a parameter. */
struct FJamDebugRow
{
	/** Not empty: a group header (Param.Owner is the group's owner, for its reset). */
	FString Header;
	FString Label;
	FJamDebugParam Param;
};

/**
 * The debug menu's widget (Docs/DebugMenu.md, «Раскладка»), one per game viewport: the panel (F6),
 * the pinned overlay (Shift+F6) and the watermark. It only draws and forwards: every value is read
 * from and written through UJamDebugSubsystem.
 */
class SJamDebugMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SJamDebugMenu) {}
		SLATE_ARGUMENT(UJamDebugSubsystem*, Debug)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	TSharedPtr<SWidget> GetFocusTarget() const;
	void FocusSearch();
	void OnModeChanged();
	void ShowTab(const FString& Name);
	void SetSearch(const FString& Text);
	void ToggleChangedOnly();

private:
	TSharedRef<SWidget> MakePanel();
	TSharedRef<SWidget> MakePinned();
	TSharedRef<SWidget> MakeWatermark();
	TSharedRef<SWidget> MakeSystemTab();
	TSharedRef<SWidget> MakeTabButton(const FString& Name);
	/** A parameter's editor: a spin box for numbers, a check box for bools, text for the rest; the changed mark and the star. */
	TSharedRef<SWidget> MakeParamLine(const FJamDebugParam& Param, const FString& Label);
	TSharedRef<SWidget> MakeKeyLine(const FString& Key, const FString& Label);
	TSharedRef<ITableRow> MakeRow(TSharedPtr<FJamDebugRow> Row, const TSharedRef<STableViewBase>& Table);
	void Rebuild();
	void RebuildPresets();
	void RebuildPinned();
	/** A spin box change is written at once; its undo step is recorded when the edit settles. */
	void EditNumber(const FString& Key, const FString& Value);
	void FlushEdit();

	TWeakObjectPtr<UJamDebugSubsystem> Debug;
	FString Tab = TEXT("Система");
	FString Search;
	bool bChangedOnly = false;
	int32 SeenGeneration = -1;
	int32 SeenChanged = -1;
	TArray<FString> SeenPins;

	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SWidgetSwitcher> Body;
	TSharedPtr<SListView<TSharedPtr<FJamDebugRow>>> List;
	TArray<TSharedPtr<FJamDebugRow>> Rows;
	TSharedPtr<SVerticalBox> PresetBox;
	TSharedPtr<SVerticalBox> PinnedBox;
	TArray<FString> PinnedShown;
	TSharedPtr<SVerticalBox> SkippedBox;
	int32 SeenSkipped = -1;

	FString EditKey;
	FString EditBefore;
	double EditAt = 0.0;
	bool bDragging = false;
	bool bFocusPending = false;
};
