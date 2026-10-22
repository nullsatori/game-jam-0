#include "OSDebugMenu.h"

#include "Algo/BinarySearch.h"
#include "Algo/StableSort.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "OSGravity.h"
#include "OSShip.h"
#include "OSWeapons.h"
#include "OSZeroGPawn.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Fonts/FontMeasure.h"
#include "Internationalization/FastDecimalFormat.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/Input/NumericTypeInterface.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

namespace
{
	// The look (Docs/DebugMenu.md, «Оформление»): a dark instrument of the suit. One accent, the weld projector's
	// blue (OSWeldProjector: HaloColour 0.045/0.065/0.15 for the hairlines, CoreColour 0.62/0.74/1.0 for what is
	// active); amber only for "changed", red only for errors. Linear colours.
	const FLinearColor MenuInk(0.0030f, 0.0042f, 0.0090f, 0.94f);
	const FLinearColor MenuInkPinned(0.0030f, 0.0042f, 0.0090f, 0.70f);
	const FLinearColor MenuWell(0.0055f, 0.0075f, 0.0160f);
	const FLinearColor MenuHair(0.045f, 0.065f, 0.15f);
	const FLinearColor MenuText(0.56f, 0.63f, 0.80f);
	const FLinearColor MenuDim(0.17f, 0.21f, 0.33f);
	const FLinearColor MenuAccent(0.16f, 0.26f, 0.85f);
	const FLinearColor MenuCore(0.62f, 0.74f, 1.0f);
	const FLinearColor MenuChanged(0.85f, 0.42f, 0.10f);
	const FLinearColor MenuError(0.80f, 0.12f, 0.08f);
	const FLinearColor MenuClear(0.0f, 0.0f, 0.0f, 0.0f);

	// The game HUD is Canvas, in pixels, at the left (AOSZeroGHUD, AOSShipHUD): the ship's lamps, cargo and air
	// from y 24 to about 125; O2 and HP from ClipY - 82 down; with a hull tool in hand up to three more lines from
	// ClipY - 204. The panel keeps between them. Under MenuToolLinesMinPx of height (720p) it may cover the tool lines
	// (key hints that do nothing while the menu has the input, and the breach readout): O2 and HP stay clear always.
	constexpr float MenuHudTopPx = 132.0f;
	constexpr float MenuHudBottomPx = 212.0f;
	constexpr float MenuVitalsBottomPx = 100.0f;
	constexpr float MenuToolLinesMinPx = 900.0f;
	constexpr float MenuLeftPx = 16.0f;
	// The engine's DPI curve gives 0.67 at 720p: the panel's text was 9 px there (KNOWN_ISSUES B6). The menu never
	// goes under 1: its small type keeps its pixel size at 720p and grows with the curve above 1080p, in quarter steps
	// (1, 1.25, 1.5...) so that the 4-unit grid below stays on whole pixels.
	constexpr float MenuMinScale = 1.0f;

	// The vertical rhythm (Docs/DebugMenu.md, «Оформление»): every line box, row and gap is a whole number of 4-unit
	// steps; text sits on a baseline a fixed distance above its box's bottom, whatever its size.
	constexpr float GridRow = 24.0f;      // a parameter, a chip row, the search, the toolbar, a button row
	constexpr float GridRowTwo = 36.0f;   // a search result: the name and the property under it
	constexpr float GridLine = 16.0f;     // a line of body text, wrapped ones too; the overlay's rows
	constexpr float GridCaption = 12.0f;  // a line of caption text
	constexpr float GridHeader = 24.0f;   // the header line
	constexpr float GridTab = 28.0f;      // the tab bar, its underline at the bottom
	constexpr float GridSection = 24.0f;  // a section label with its hairline
	constexpr float GridGroup = 32.0f;    // a group header with its hairline
	constexpr float GridBaseline = 8.0f;  // the header's and the group's baseline above the box's bottom

	// «Главное» is the landing view: the quick system controls and the pinned parameters («Закреплено» is its old name).
	const TArray<FString> MenuTabs = { TEXT("Главное"), TEXT("Система"), TEXT("Мир"), TEXT("Игрок"), TEXT("Оружие"), TEXT("Враги"), TEXT("Пресеты") };

	// The type scale (Docs/DebugMenu.md, «Оформление»): one base, x1.2 a step, three sizes; weight, tracking and colour
	// carry the rest. Roboto from the engine's default font (Engine/Content/Slate/Fonts); its mono face (DroidSansMono)
	// for numbers only.
	constexpr float TypeCaption = 7.5f;
	constexpr float TypeBody = 9.0f;
	constexpr float TypeHeading = 10.8f;
	FSlateFontInfo MenuFont(float Size = TypeBody) { return FCoreStyle::GetDefaultFontStyle("Regular", Size); }
	FSlateFontInfo MenuMedium(float Size = TypeBody) { return FCoreStyle::GetDefaultFontStyle("Medium", Size); }
	FSlateFontInfo MenuMono(float Size = TypeBody) { return FCoreStyle::GetDefaultFontStyle("Mono", Size); }
	/** Section labels: capitals at caption size, bold, tracked out. */
	FSlateFontInfo MenuLabel()
	{
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", TypeCaption);
		Font.LetterSpacing = 200;
		return Font;
	}
	/**
	 * How far under Font's baseline its line box ends, in layout units: text of different sizes in one row shares a
	 * baseline when the smaller one is lifted by the difference. Measured at x100 so the font's own rounding does not show.
	 */
	float MenuDescent(const FSlateFontInfo& Font)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return 0.0f;
		}
		return -float(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->GetBaseline(Font, 100.0f)) / 100.0f;
	}

	/** Line height for wrapped text in Font: exactly Units a line (a percentage of the font's own line height). */
	float MenuLineHeight(const FSlateFontInfo& Font, float Units)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return 1.0f;
		}
		const float Own = float(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->GetMaxCharacterHeight(Font, 100.0f)) / 100.0f;
		return Own > 0.0f ? Units / Own : 1.0f;
	}

	/** Text in a line box Height units tall, its baseline Baseline units above the box's bottom. */
	TSharedRef<SWidget> MenuOnLine(float Height, float Baseline, const FSlateFontInfo& Font, const TSharedRef<SWidget>& Text)
	{
		return SNew(SBox).HeightOverride(Height).VAlign(VAlign_Bottom).Padding(FMargin(0.0f, 0.0f, 0.0f, Baseline - MenuDescent(Font)))[Text];
	}

	/** Any row: a fixed height, its content centred on the row line. */
	TSharedRef<SWidget> MenuRow(float Height, const TSharedRef<SWidget>& Content)
	{
		return SNew(SBox).HeightOverride(Height).VAlign(VAlign_Center)[Content];
	}

	const FSlateBrush* MenuWhite() { return FCoreStyle::Get().GetBrush("WhiteBrush"); }
	FText MenuText_(const FString& Text) { return FText::FromString(Text); }

	/**
	 * A number as the menu shows it everywhere: a point, whatever the culture (as the exports and the console), and no
	 * trailing zeros (90, 0.7, 2500). Six decimals at most, so a shown value commits back unchanged.
	 */
	FString MenuNumber(double Value)
	{
		const FNumberFormattingOptions Options = FNumberFormattingOptions().SetUseGrouping(false).SetMinimumFractionalDigits(0).SetMaximumFractionalDigits(6);
		return FastDecimalFormat::NumberToString(Value, FastDecimalFormat::GetCultureAgnosticFormattingRules(), Options);
	}

	/** The spin boxes' numbers: shown by MenuNumber; typed with a point or a comma, either way (and still as an expression). */
	class FOSMenuNumbers : public TDefaultNumericTypeInterface<double>
	{
	public:
		virtual FString ToString(const double& Value) const override
		{
			return MenuNumber(Value);
		}

		virtual TOptional<double> FromString(const FString& InString, const double& InExistingValue) override
		{
			return TDefaultNumericTypeInterface<double>::FromString(InString.Replace(TEXT(","), TEXT(".")), InExistingValue);
		}
	};

	/** The widget styles, built once: hairline outlines, square corners, quiet hover, no gloss. */
	struct FOSMenuLook
	{
		FButtonStyle Button;
		FButtonStyle Ghost;
		FCheckBoxStyle Check;
		FCheckBoxStyle Chip;
		FCheckBoxStyle Tab;
		FEditableTextBoxStyle Field;
		FEditableTextBoxStyle BareField;
		FSearchBoxStyle Search;
		FSpinBoxStyle Spin;
		FScrollBarStyle Scroll;
		FTableRowStyle Row;
		FSlateBrush Highlight;

		static const FOSMenuLook& Get()
		{
			static const FOSMenuLook Look;
			return Look;
		}

		FOSMenuLook()
		{
			const auto Box = [](const FLinearColor& Fill, const FLinearColor& Line, const FVector2f& Size = FVector2f::ZeroVector) -> FSlateBrush
			{
				return FSlateRoundedBoxBrush(Fill, 1.0f, Line, 1.0f, Size);
			};
			const auto Flat = [](const FLinearColor& Fill) -> FSlateBrush { return FSlateColorBrush(Fill); };
			const auto Tint = [](const FLinearColor& Color, float Alpha) { return Color.CopyWithNewOpacity(Alpha); };
			const FSlateBrush None = FSlateNoResource();
			const ISlateStyle& Core = FCoreStyle::Get();

			Button = Core.GetWidgetStyle<FButtonStyle>("Button");
			Button.Normal = Box(MenuClear, MenuHair);
			Button.Hovered = Box(Tint(MenuAccent, 0.10f), Tint(MenuAccent, 0.70f));
			Button.Pressed = Box(Tint(MenuAccent, 0.22f), MenuAccent);
			Button.Disabled = Box(MenuClear, Tint(MenuHair, 0.45f));
			Button.NormalForeground = MenuText;
			Button.HoveredForeground = MenuCore;
			Button.PressedForeground = MenuCore;
			Button.DisabledForeground = Tint(MenuDim, 0.6f);
			Button.NormalPadding = FMargin(9.0f, 2.0f);
			Button.PressedPadding = FMargin(9.0f, 2.0f);

			Ghost = Button;
			Ghost.Normal = None;
			Ghost.Hovered = Flat(Tint(MenuAccent, 0.14f));
			Ghost.Pressed = Flat(Tint(MenuAccent, 0.24f));
			Ghost.Disabled = None;
			Ghost.DisabledForeground = Tint(MenuDim, 0.55f);
			Ghost.NormalPadding = FMargin(3.0f, 0.0f);
			Ghost.PressedPadding = FMargin(3.0f, 0.0f);

			// A bool: a small square lamp, lit when on.
			Check = Core.GetWidgetStyle<FCheckBoxStyle>("Checkbox");
			const FVector2f Lamp(11.0f, 11.0f);
			Check.UncheckedImage = Box(MenuWell, Tint(MenuDim, 0.9f), Lamp);
			Check.UncheckedHoveredImage = Box(MenuWell, MenuAccent, Lamp);
			Check.UncheckedPressedImage = Box(Tint(MenuAccent, 0.3f), MenuAccent, Lamp);
			Check.CheckedImage = Box(MenuAccent, MenuAccent, Lamp);
			Check.CheckedHoveredImage = Box(MenuAccent, MenuCore, Lamp);
			Check.CheckedPressedImage = Box(Tint(MenuAccent, 0.6f), MenuCore, Lamp);
			Check.UndeterminedImage = Check.UncheckedImage;
			Check.UndeterminedHoveredImage = Check.UncheckedHoveredImage;
			Check.UndeterminedPressedImage = Check.UncheckedPressedImage;
			Check.Padding = FMargin(6.0f, 0.0f, 0.0f, 0.0f);
			Check.ForegroundColor = MenuText;
			Check.HoveredForeground = MenuCore;
			Check.PressedForeground = MenuCore;
			Check.CheckedForeground = MenuText;
			Check.CheckedHoveredForeground = MenuCore;
			Check.CheckedPressedForeground = MenuCore;

			// A choice of several (FPS, fixed step) and the "changed only" filter: an outlined chip, filled when on.
			Chip = Core.GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox");
			Chip.CheckBoxType = ESlateCheckBoxType::ToggleButton;
			Chip.UncheckedImage = Button.Normal;
			Chip.UncheckedHoveredImage = Button.Hovered;
			Chip.UncheckedPressedImage = Button.Pressed;
			Chip.CheckedImage = Box(Tint(MenuAccent, 0.26f), Tint(MenuAccent, 0.9f));
			Chip.CheckedHoveredImage = Box(Tint(MenuAccent, 0.34f), MenuAccent);
			Chip.CheckedPressedImage = Chip.CheckedHoveredImage;
			Chip.UndeterminedImage = Chip.UncheckedImage;
			Chip.UndeterminedHoveredImage = Chip.UncheckedHoveredImage;
			Chip.UndeterminedPressedImage = Chip.UncheckedPressedImage;
			Chip.BackgroundImage = None;
			Chip.BackgroundHoveredImage = None;
			Chip.BackgroundPressedImage = None;
			Chip.Padding = FMargin(8.0f, 3.0f);
			Chip.ForegroundColor = MenuText;
			Chip.HoveredForeground = MenuCore;
			Chip.PressedForeground = MenuCore;
			Chip.CheckedForeground = MenuCore;
			Chip.CheckedHoveredForeground = MenuCore;
			Chip.CheckedPressedForeground = MenuCore;
			Chip.UndeterminedForeground = MenuText;

			// A tab: no outline; the open one is a faint blue band with a bar at its edge (the bar is in the widget).
			Tab = Chip;
			Tab.UncheckedImage = None;
			Tab.UncheckedHoveredImage = Flat(Tint(MenuAccent, 0.07f));
			Tab.UncheckedPressedImage = Flat(Tint(MenuAccent, 0.14f));
			Tab.CheckedImage = Flat(Tint(MenuAccent, 0.13f));
			Tab.CheckedHoveredImage = Flat(Tint(MenuAccent, 0.18f));
			Tab.CheckedPressedImage = Tab.CheckedHoveredImage;
			Tab.UndeterminedImage = None;
			Tab.UndeterminedHoveredImage = Tab.UncheckedHoveredImage;
			Tab.UndeterminedPressedImage = Tab.UncheckedPressedImage;
			Tab.Padding = FMargin(0.0f);
			Tab.ForegroundColor = MenuDim;
			Tab.HoveredForeground = MenuText;
			Tab.PressedForeground = MenuText;
			Tab.UndeterminedForeground = MenuDim;

			Scroll = Core.GetWidgetStyle<FScrollBarStyle>("ScrollBar");
			Scroll.HorizontalBackgroundImage = None;
			Scroll.VerticalBackgroundImage = None;
			Scroll.HorizontalTopSlotImage = None;
			Scroll.VerticalTopSlotImage = None;
			Scroll.HorizontalBottomSlotImage = None;
			Scroll.VerticalBottomSlotImage = None;
			Scroll.NormalThumbImage = Flat(MenuHair);
			Scroll.HoveredThumbImage = Flat(Tint(MenuAccent, 0.7f));
			Scroll.DraggedThumbImage = Flat(MenuAccent);
			Scroll.Thickness = 3.0f;

			Field = Core.GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
			Field.BackgroundImageNormal = Box(MenuWell, Tint(MenuHair, 0.9f));
			Field.BackgroundImageHovered = Box(MenuWell, Tint(MenuAccent, 0.55f));
			Field.BackgroundImageFocused = Box(MenuWell, MenuAccent);
			Field.BackgroundImageReadOnly = Field.BackgroundImageNormal;
			Field.Padding = FMargin(7.0f, 4.0f);
			Field.ForegroundColor = MenuText;
			Field.FocusedForegroundColor = MenuCore;
			Field.ReadOnlyForegroundColor = MenuDim;
			Field.BackgroundColor = FLinearColor::White;
			Field.ScrollBarStyle = Scroll;

			// The config's name in the header: plain text until hovered or typed in.
			BareField = Field;
			BareField.BackgroundImageNormal = None;
			BareField.BackgroundImageHovered = Flat(Tint(MenuAccent, 0.07f));
			BareField.BackgroundImageReadOnly = None;
			// No vertical padding: its text sits on the header's baseline (MakePanel).
			BareField.Padding = FMargin(4.0f, 0.0f);
			// Muted: the name is a caption to the title; its hint («без имени») comes out at about Dim.
			BareField.ForegroundColor = MenuText.CopyWithNewOpacity(0.7f);

			Search = Core.GetWidgetStyle<FSearchBoxStyle>("SearchBox");
			Search.TextBoxStyle = Field;
			Search.TextBoxStyle.TextStyle.Font = MenuFont();
			Search.ActiveFontInfo = MenuFont();
			Search.GlassImage.TintColor = MenuDim;
			Search.ClearImage.TintColor = MenuDim;

			Spin = Core.GetWidgetStyle<FSpinBoxStyle>("SpinBox");
			Spin.BackgroundBrush = Box(MenuWell, Tint(MenuHair, 0.9f));
			Spin.HoveredBackgroundBrush = Box(MenuWell, Tint(MenuAccent, 0.55f));
			Spin.ActiveBackgroundBrush = Box(MenuWell, MenuAccent);
			Spin.InactiveFillBrush = Flat(Tint(MenuAccent, 0.16f));
			Spin.HoveredFillBrush = Flat(Tint(MenuAccent, 0.24f));
			Spin.ActiveFillBrush = Flat(Tint(MenuAccent, 0.30f));
			Spin.ArrowsImage = None;
			Spin.ForegroundColor = MenuText;
			Spin.TextPadding = FMargin(7.0f, 2.0f);

			Row = Core.GetWidgetStyle<FTableRowStyle>("TableView.Row");
			Row.SelectorFocusedBrush = None;
			Row.ActiveBrush = None;
			Row.InactiveBrush = None;
			Row.ActiveHoveredBrush = Flat(Tint(MenuAccent, 0.06f));
			Row.InactiveHoveredBrush = Row.ActiveHoveredBrush;
			Row.ActiveHighlightedBrush = None;
			Row.InactiveHighlightedBrush = None;
			Row.EvenRowBackgroundBrush = None;
			Row.OddRowBackgroundBrush = None;
			Row.EvenRowBackgroundHoveredBrush = Row.ActiveHoveredBrush;
			Row.OddRowBackgroundHoveredBrush = Row.ActiveHoveredBrush;
			Row.bUseParentRowBrush = false;
			Row.TextColor = MenuText;
			Row.SelectedTextColor = MenuCore;

			// A search match: a quiet blue block under the letters, not the engine's green.
			Highlight = Box(Tint(MenuAccent, 0.45f), MenuClear);
		}
	};

	// MindKore's Hash01 (as in OSWeldProjector.cpp): the same for the same pair, unrelated for neighbours.
	float MenuHash(int64 A, int64 B)
	{
		uint32 H = uint32(A) * 0x9E3779B1u ^ uint32(B) * 0x85EBCA6Bu;
		H ^= H >> 15;
		H *= 0x2C1B3C6Du;
		H ^= H >> 12;
		return float(H & 0xFFFF) / 65535.0f;
	}

	/** The projector's device clock (OSDeviceClockHz in OSWeldProjector.cpp): the dashes and letters change on its ticks only. */
	int64 MenuClockTick(const FPaintArgs& Args)
	{
		return int64(Args.GetCurrentTime() * 12.0);
	}

	/**
	 * A one-pixel dashed line in the projector's hand (dash : gap = 3 : 2.2, its DashCm / DashGapCm): each dash a
	 * little brighter or dimmer on every clock tick, one in twenty-five out for a tick. Along is (1, 0) or (0, 1).
	 */
	void MenuPaintDashes(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, int64 Tick, const FVector2f& From, const FVector2f& Along,
		float Length, const FLinearColor& Color)
	{
		const float Pixel = 1.0f / FMath::Max(Geometry.Scale, 0.01f);
		const FVector2f Across = Along.X != 0.0f ? FVector2f(0.0f, Pixel) : FVector2f(Pixel, 0.0f);
		constexpr float Dash = 6.0f;
		constexpr float Gap = 4.4f;
		int32 Index = 0;
		for (float At = 0.0f; At < Length; At += Dash + Gap, ++Index)
		{
			const float Hash = MenuHash(Tick, Index);
			if (Hash < 0.04f)
			{
				continue;
			}
			const float Size = FMath::Min(Dash, Length - At);
			FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(Along * Size + Across, FSlateLayoutTransform(From + Along * At)), MenuWhite(),
				ESlateDrawEffect::None, Color.CopyWithNewOpacity(Color.A * (0.6f + 0.4f * Hash)));
		}
	}

	/** A horizontal dashed hairline across the width. */
	class SOSDash : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SOSDash) : _Color(MenuAccent.CopyWithNewOpacity(0.55f)) {}
			SLATE_ARGUMENT(FLinearColor, Color)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Color = InArgs._Color;
			// Repainted every frame for the clock, also under global invalidation.
			ForceVolatile(true);
		}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(8.0f, 1.0f); }

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer,
			const FWidgetStyle&, bool) const override
		{
			MenuPaintDashes(Out, Layer, Geometry, MenuClockTick(Args), FVector2f::ZeroVector, FVector2f(1.0f, 0.0f), float(Geometry.GetLocalSize().X), Color);
			return Layer;
		}

	private:
		FLinearColor Color;
	};

	/** The device frame over a panel: solid corner ticks and dashed hairline edges between them. */
	class SOSFrame : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SOSFrame) : _Corner(12.0f), _bDashes(true) {}
			SLATE_ARGUMENT(float, Corner)
			SLATE_ARGUMENT(bool, bDashes)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Corner = InArgs._Corner;
			bDashes = InArgs._bDashes;
			SetVisibility(EVisibility::HitTestInvisible);
			ForceVolatile(bDashes);
		}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer,
			const FWidgetStyle&, bool) const override
		{
			const FVector2f Size = FVector2f(Geometry.GetLocalSize());
			const float Pixel = 1.0f / FMath::Max(Geometry.Scale, 0.01f);
			const auto Bar = [&](float X, float Y, float W, float H)
			{
				FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y))), MenuWhite(),
					ESlateDrawEffect::None, MenuAccent);
			};
			Bar(0.0f, 0.0f, Corner, Pixel);
			Bar(0.0f, 0.0f, Pixel, Corner);
			Bar(Size.X - Corner, 0.0f, Corner, Pixel);
			Bar(Size.X - Pixel, 0.0f, Pixel, Corner);
			Bar(0.0f, Size.Y - Pixel, Corner, Pixel);
			Bar(0.0f, Size.Y - Corner, Pixel, Corner);
			Bar(Size.X - Corner, Size.Y - Pixel, Corner, Pixel);
			Bar(Size.X - Pixel, Size.Y - Corner, Pixel, Corner);
			if (bDashes)
			{
				const int64 Tick = MenuClockTick(Args);
				const float Inset = Corner + 6.0f;
				const FLinearColor Dashes = MenuAccent.CopyWithNewOpacity(0.38f);
				MenuPaintDashes(Out, Layer, Geometry, Tick, FVector2f(Inset, 0.0f), FVector2f(1.0f, 0.0f), Size.X - 2.0f * Inset, Dashes);
				MenuPaintDashes(Out, Layer, Geometry, Tick + 101, FVector2f(Inset, Size.Y - Pixel), FVector2f(1.0f, 0.0f), Size.X - 2.0f * Inset, Dashes);
				MenuPaintDashes(Out, Layer, Geometry, Tick + 202, FVector2f(0.0f, Inset), FVector2f(0.0f, 1.0f), Size.Y - 2.0f * Inset, Dashes);
				MenuPaintDashes(Out, Layer, Geometry, Tick + 303, FVector2f(Size.X - Pixel, Inset), FVector2f(0.0f, 1.0f), Size.Y - 2.0f * Inset, Dashes);
			}
			return Layer;
		}

	private:
		float Corner = 12.0f;
		bool bDashes = true;
	};

	// MK Pribor N, the projector's character generator (the same table as OSWeldProjector.cpp, MIT, see CREDITS):
	// ten rows from the top, bit 0 leftmost; Advance in cells.
	struct FOSMenuGlyph
	{
		uint16 Code;
		uint8 Advance;
		uint16 Rows[10];
	};

	const FOSMenuGlyph MenuGlyphs[] = {
#include "DeviceFont/MKDeviceFontData.inl"
	};

	/**
	 * A word in MK Pribor N, cell by cell on whole pixels, tracked out a whole cell like the projector's readout. A cell is
	 * 2 px up to menu scale 1.25, then 2 px a whole step (3 at 1.5, 4 at 2): never a fraction, never smeared. With a
	 * LineHeight it fills that line box and puts the capitals' bottom Baseline units above the box's bottom, on a pixel.
	 */
	class SOSPribor : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SOSPribor) : _Track(1), _Color(MenuCore), _LineHeight(0.0f), _Baseline(0.0f) {}
			SLATE_ARGUMENT(FString, Text)
			SLATE_ARGUMENT(int32, Track)
			SLATE_ARGUMENT(FLinearColor, Color)
			SLATE_ARGUMENT(float, LineHeight)
			SLATE_ARGUMENT(float, Baseline)
		SLATE_END_ARGS()

		static int32 CellPixels(float Scale)
		{
			return FMath::Max(2, FMath::FloorToInt32(Scale * 4.0f + 0.01f) / 2);
		}

		void Construct(const FArguments& InArgs)
		{
			Track = InArgs._Track;
			Color = InArgs._Color;
			LineHeight = InArgs._LineHeight;
			Baseline = InArgs._Baseline;
			for (const TCHAR Char : InArgs._Text)
			{
				const int32 Index = Algo::BinarySearchBy(MenuGlyphs, uint16(Char), &FOSMenuGlyph::Code);
				Glyphs.Add(Index == INDEX_NONE ? nullptr : &MenuGlyphs[Index]);
				Cells += (Index == INDEX_NONE ? 3 : MenuGlyphs[Index].Advance) + Track;
			}
			Cells = FMath::Max(Cells - Track - 1, 0);
			ForceVolatile(true);
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			// The cell as painted last frame, so the word never runs past its box.
			const float Unit = CellPixels(PaintScale) / PaintScale;
			return FVector2D(Cells * Unit, LineHeight > 0.0f ? LineHeight : Rows * Unit);
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer,
			const FWidgetStyle&, bool) const override
		{
			const float Scale = FMath::Max(Geometry.Scale, 0.01f);
			PaintScale = Scale;
			const float Unit = CellPixels(Scale) / Scale;
			// The capitals are rows 1..7 of the table; row 8 is the descender (Д, Щ).
			const float CapsBottom = LineHeight > 0.0f ? FMath::RoundToFloat((LineHeight - Baseline) * Scale) / Scale : Caps * Unit;
			const float Top = CapsBottom - Caps * Unit;
			const int64 Tick = MenuClockTick(Args);
			int32 Pen = 0;
			int32 Run = 0;
			for (const FOSMenuGlyph* Glyph : Glyphs)
			{
				for (int32 Row = 0; Glyph && Row < Rows; ++Row)
				{
					const uint16 Bits = Glyph->Rows[Row + 1];
					for (int32 Bit = 0; Bit < 16;)
					{
						if (!(Bits >> Bit & 1))
						{
							++Bit;
							continue;
						}
						int32 End = Bit;
						while (End < 16 && (Bits >> End & 1))
						{
							++End;
						}
						// The projector's shimmer, kept faint: a run of cells 10 % dimmer or not, per tick.
						const float Level = 0.9f + 0.1f * MenuHash(Tick, ++Run);
						FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2f((End - Bit) * Unit, Unit),
							FSlateLayoutTransform(FVector2f((Pen + Bit) * Unit, Top + Row * Unit))), MenuWhite(), ESlateDrawEffect::None, Color.CopyWithNewOpacity(Color.A * Level));
						Bit = End;
					}
				}
				Pen += (Glyph ? Glyph->Advance : 3) + Track;
			}
			return Layer;
		}

	private:
		// Rows 1..8 of the ten: the first is the line gap, the last is under the lowest descender.
		static constexpr int32 Rows = 8;
		static constexpr int32 Caps = 7;
		TArray<const FOSMenuGlyph*> Glyphs;
		int32 Cells = 0;
		int32 Track = 1;
		FLinearColor Color;
		float LineHeight = 0.0f;
		float Baseline = 0.0f;
		mutable float PaintScale = 1.0f;
	};

	FString MenuNumberText(double Value, bool bInteger)
	{
		return bInteger ? FString::Printf(TEXT("%lld"), int64(FMath::RoundToDouble(Value))) : FString::SanitizeFloat(Value);
	}

	FString MenuMapName(const UWorld* World)
	{
		return World ? UWorld::RemovePIEPrefix(World->GetMapName()) : FString(TEXT("-"));
	}

	// --- human names ---------------------------------------------------------------------------------------------

	/** A parameter as a person reads it: the overlay and «Главное» show these, the key stays in tooltips and exports. */
	struct FOSMenuName
	{
		const TCHAR* Key;
		const TCHAR* Section;
		const TCHAR* Label;
		/** "×" goes before the number (×0.5); anything else after it. */
		const TCHAR* Unit;
	};

	// The default pins (UOSDebugSubsystem::GetDefaultPins), in the overlay's sections. Docs/DebugMenu.md, «Оформление».
	const FOSMenuName MenuNames[] = {
		{ TEXT("World.GravityScale"), TEXT("Мир"), TEXT("Гравитация"), TEXT("×") },
		{ TEXT("Pawn.LookSensitivity"), TEXT("Взгляд"), TEXT("Чувствительность"), TEXT("") },
		{ TEXT("Pawn.CameraFOV"), TEXT("Взгляд"), TEXT("FOV"), TEXT("°") },
		{ TEXT("Pawn.ThrustAccelCm"), TEXT("Движение"), TEXT("Маневровые"), TEXT("см/с²") },
		{ TEXT("Pawn.WalkSpeedCm"), TEXT("Движение"), TEXT("Ходьба"), TEXT("см/с") },
		{ TEXT("Pawn.JumpSpeedCm"), TEXT("Движение"), TEXT("Прыжок"), TEXT("см/с") },
		{ TEXT("Pawn.ArmLengthCm"), TEXT("Рука"), TEXT("Длина руки"), TEXT("см") },
		{ TEXT("Pawn.ArmPullCmS"), TEXT("Рука"), TEXT("Подтягивание"), TEXT("см/с") },
		{ TEXT("Pawn.SwingAccelCm"), TEXT("Рука"), TEXT("Раскачка"), TEXT("см/с²") },
		{ TEXT("Weapon[Пистолет].RecoilImpulse"), TEXT("Отдача"), TEXT("Пистолет"), TEXT("кг·см/с") },
		{ TEXT("Weapon[Дробовик].RecoilImpulse"), TEXT("Отдача"), TEXT("Дробовик"), TEXT("кг·см/с") },
		{ TEXT("Weapon[Снапа].RecoilImpulse"), TEXT("Отдача"), TEXT("СВ-98"), TEXT("кг·см/с") },
		{ TEXT("Pawn.Oxygen.CapacitySeconds"), TEXT("Выживание"), TEXT("Кислород"), TEXT("с") },
		{ TEXT("Flyer.DiveSpeedCm"), TEXT("Летуны"), TEXT("Пике"), TEXT("см/с") },
		{ TEXT("Flyer.BiteDamage"), TEXT("Летуны"), TEXT("Укус"), TEXT("") },
	};
	const TCHAR* MenuSections[] = { TEXT("Мир"), TEXT("Взгляд"), TEXT("Движение"), TEXT("Рука"), TEXT("Отдача"), TEXT("Выживание"), TEXT("Летуны"), TEXT("Оружие"), TEXT("Прочее") };

	const FOSMenuName* MenuNameOf(const FString& Key)
	{
		for (const FOSMenuName& Name : MenuNames)
		{
			if (Key == Name.Key)
			{
				return &Name;
			}
		}
		return nullptr;
	}

	/** A pin outside the table: its section by owner. */
	FString MenuSectionOf(const FOSDebugParam& Param)
	{
		if (const FOSMenuName* Name = MenuNameOf(Param.Key))
		{
			return Name->Section;
		}
		const FString& Owner = Param.Owner;
		return Owner == TEXT("World") || Owner.StartsWith(TEXT("Zone[")) ? TEXT("Мир")
			: Owner == TEXT("Flyer") || Owner.StartsWith(TEXT("Habitat[")) ? TEXT("Летуны")
			: Owner.StartsWith(TEXT("Weapon[")) || Owner == TEXT("Pawn.Guns") ? TEXT("Оружие")
			: TEXT("Прочее");
	}

	int32 MenuSectionRank(const FString& Section)
	{
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(MenuSections); ++Index)
		{
			if (Section == MenuSections[Index])
			{
				return Index;
			}
		}
		return UE_ARRAY_COUNT(MenuSections);
	}

	// Short Russian names of the fields people tune most, by owner kind ("Weapon" stands for every Weapon[...]):
	// the arm, grab, walk, thrusters and view of the pawn, every gun's recoil and kick, the flyer's flight and hunt.
	// Docs/DebugMenu.md, «Оформление». The rest falls back to MenuHumanName's rule.
	const FOSMenuName MenuFieldNames[] = {
		{ TEXT("Pawn.LookSensitivity"), TEXT(""), TEXT("Чувствительность мыши"), TEXT("") },
		{ TEXT("Pawn.ArmLengthCm"), TEXT(""), TEXT("Длина руки"), TEXT("см") },
		{ TEXT("Pawn.ArmMinCm"), TEXT(""), TEXT("Рука, подтянут до"), TEXT("см") },
		{ TEXT("Pawn.ArmStiffness"), TEXT(""), TEXT("Жёсткость руки"), TEXT("рад/с") },
		{ TEXT("Pawn.ArmDampingRatio"), TEXT(""), TEXT("Демпфирование руки"), TEXT("") },
		{ TEXT("Pawn.ArmPullCmS"), TEXT(""), TEXT("Подтягивание"), TEXT("см/с") },
		{ TEXT("Pawn.ArmRelaxCmS"), TEXT(""), TEXT("Возврат руки"), TEXT("см/с") },
		{ TEXT("Pawn.SwingAccelCm"), TEXT(""), TEXT("Раскачка"), TEXT("см/с²") },
		{ TEXT("Pawn.SwingMaxSpeedCm"), TEXT(""), TEXT("Раскачка, предел"), TEXT("см/с") },
		{ TEXT("Pawn.SwingDampAccelCm"), TEXT(""), TEXT("Гашение раскачки"), TEXT("см/с²") },
		{ TEXT("Pawn.SwingDampRate"), TEXT(""), TEXT("Гашение раскачки, темп"), TEXT("1/с") },
		{ TEXT("Pawn.HoldSteerStiffness"), TEXT(""), TEXT("Тело в руке, жёсткость"), TEXT("") },
		{ TEXT("Pawn.ArmTugMinAccelCm"), TEXT(""), TEXT("Рывок руки, порог"), TEXT("см/с²") },
		{ TEXT("Pawn.ArmTugCmPer1000"), TEXT(""), TEXT("Рывок руки, тряска"), TEXT("см/1000") },
		{ TEXT("Pawn.HeadClearanceCm"), TEXT(""), TEXT("Зазор головы"), TEXT("см") },
		{ TEXT("Pawn.GrabReachCm"), TEXT(""), TEXT("Дальность хвата"), TEXT("см") },
		{ TEXT("Pawn.MaxGrabMassKg"), TEXT(""), TEXT("Хват, макс. масса"), TEXT("кг") },
		{ TEXT("Pawn.HoldDistanceCm"), TEXT(""), TEXT("Держать на расстоянии"), TEXT("см") },
		{ TEXT("Pawn.bHoldInFront"), TEXT(""), TEXT("Держать перед собой"), TEXT("") },
		{ TEXT("Pawn.ThrowImpulse"), TEXT(""), TEXT("Бросок, импульс"), TEXT("кг·см/с") },
		{ TEXT("Pawn.MaxThrowSpeedCm"), TEXT(""), TEXT("Бросок, предел"), TEXT("см/с") },
		{ TEXT("Pawn.bThrustersEnabled"), TEXT(""), TEXT("Маневровые включены"), TEXT("") },
		{ TEXT("Pawn.ThrustAccelCm"), TEXT(""), TEXT("Маневровые, разгон"), TEXT("см/с²") },
		{ TEXT("Pawn.ThrustMaxSpeedCm"), TEXT(""), TEXT("Маневровые, предел"), TEXT("см/с") },
		{ TEXT("Pawn.StabiliserAccelCm"), TEXT(""), TEXT("Стабилизатор"), TEXT("см/с²") },
		{ TEXT("Pawn.StabiliserRate"), TEXT(""), TEXT("Стабилизатор, темп"), TEXT("1/с") },
		{ TEXT("Pawn.WalkSpeedCm"), TEXT(""), TEXT("Ходьба"), TEXT("см/с") },
		{ TEXT("Pawn.WalkAccelCm"), TEXT(""), TEXT("Ходьба, разгон"), TEXT("см/с²") },
		{ TEXT("Pawn.WalkBrakingCm"), TEXT(""), TEXT("Ходьба, торможение"), TEXT("см/с²") },
		{ TEXT("Pawn.AirAccelCm"), TEXT(""), TEXT("Управление в воздухе"), TEXT("см/с²") },
		{ TEXT("Pawn.LaunchThresholdCm"), TEXT(""), TEXT("Порог отрыва от пола"), TEXT("см/с") },
		{ TEXT("Pawn.JumpSpeedCm"), TEXT(""), TEXT("Прыжок"), TEXT("см/с") },
		{ TEXT("Pawn.JetExitSpeedCm"), TEXT(""), TEXT("Прыжок с планетоида"), TEXT("см/с") },
		{ TEXT("Pawn.GroundProbeCm"), TEXT(""), TEXT("Поиск пола"), TEXT("см") },
		{ TEXT("Pawn.MaxGroundAngleDeg"), TEXT(""), TEXT("Пол, макс. уклон"), TEXT("°") },
		{ TEXT("Pawn.EyeHeightCm"), TEXT(""), TEXT("Высота глаз"), TEXT("см") },
		{ TEXT("Pawn.EyeRiseRate"), TEXT(""), TEXT("Подъём глаз, темп"), TEXT("1/с") },
		{ TEXT("Pawn.GroundFeedbackMinSpeedCm"), TEXT(""), TEXT("Удар о пол от"), TEXT("см/с") },
		{ TEXT("Pawn.StepCm"), TEXT(""), TEXT("Шаг"), TEXT("см") },
		{ TEXT("Pawn.StepVolume"), TEXT(""), TEXT("Громкость шага"), TEXT("") },
		{ TEXT("Pawn.FleshPitchRange"), TEXT(""), TEXT("Шаг по плоти, тон"), TEXT("") },
		{ TEXT("Pawn.bFreeBodyRotation"), TEXT(""), TEXT("Свободный поворот"), TEXT("") },
		{ TEXT("Pawn.CameraFOV"), TEXT(""), TEXT("FOV"), TEXT("°") },
		{ TEXT("Pawn.bLensDistortion"), TEXT(""), TEXT("Линза бодикама"), TEXT("") },
		{ TEXT("Pawn.DustCount"), TEXT(""), TEXT("Пыль, пылинок"), TEXT("") },
		{ TEXT("Pawn.DustCubeCm"), TEXT(""), TEXT("Пыль, куб"), TEXT("см") },
		{ TEXT("Pawn.DustSpeckCm"), TEXT(""), TEXT("Пыль, пылинка"), TEXT("см") },
		{ TEXT("Pawn.EyeRadiusCm"), TEXT(""), TEXT("Радиус глаза"), TEXT("см") },
		{ TEXT("Pawn.RollRateDegPerSec"), TEXT(""), TEXT("Крен Q/E"), TEXT("°/с") },
		{ TEXT("Pawn.RecoilShakeCmPer10000"), TEXT(""), TEXT("Тряска от отдачи"), TEXT("см/10000") },
		{ TEXT("Weapon.Name"), TEXT(""), TEXT("Имя"), TEXT("") },
		{ TEXT("Weapon.RecoilImpulse"), TEXT(""), TEXT("Отдача, импульс"), TEXT("кг·см/с") },
		{ TEXT("Weapon.KickPitchDeg"), TEXT(""), TEXT("Подброс вверх"), TEXT("°") },
		{ TEXT("Weapon.KickYawDeg"), TEXT(""), TEXT("Увод вбок"), TEXT("°") },
		{ TEXT("Weapon.ViewKickBackCm"), TEXT(""), TEXT("Ствол в кадре назад"), TEXT("см") },
		{ TEXT("Weapon.ViewKickRiseDeg"), TEXT(""), TEXT("Ствол в кадре вверх"), TEXT("°") },
		{ TEXT("Weapon.Damage"), TEXT(""), TEXT("Урон"), TEXT("") },
		{ TEXT("Weapon.FireInterval"), TEXT(""), TEXT("Интервал выстрела"), TEXT("с") },
		{ TEXT("Weapon.MagazineSize"), TEXT(""), TEXT("Магазин"), TEXT("") },
		{ TEXT("Weapon.Reserve"), TEXT(""), TEXT("Запас"), TEXT("") },
		{ TEXT("Weapon.ReloadSeconds"), TEXT(""), TEXT("Перезарядка"), TEXT("с") },
		{ TEXT("Weapon.Pellets"), TEXT(""), TEXT("Дробин"), TEXT("") },
		{ TEXT("Weapon.SpreadDeg"), TEXT(""), TEXT("Разброс"), TEXT("°") },
		{ TEXT("Weapon.HitImpulse"), TEXT(""), TEXT("Толчок попадания"), TEXT("кг·см/с") },
		{ TEXT("Weapon.RangeCm"), TEXT(""), TEXT("Дальность"), TEXT("см") },
		{ TEXT("Weapon.bAutomatic"), TEXT(""), TEXT("Автоматический"), TEXT("") },
		{ TEXT("Weapon.ProjectileSpeed"), TEXT(""), TEXT("Скорость пули"), TEXT("см/с") },
		{ TEXT("Weapon.bUnlocked"), TEXT(""), TEXT("Доступен"), TEXT("") },
		{ TEXT("Weapon.Ammo"), TEXT(""), TEXT("Патронов в магазине"), TEXT("") },
		{ TEXT("Pawn.Guns.RecoilAttackSeconds"), TEXT(""), TEXT("Отдача, нарастание"), TEXT("с") },
		{ TEXT("Pawn.Guns.RecoilSettleFraction"), TEXT(""), TEXT("Отдача, доля возврата"), TEXT("") },
		{ TEXT("Pawn.Guns.RecoilRecoverDegPerSec"), TEXT(""), TEXT("Отдача, скорость возврата"), TEXT("°/с") },
		{ TEXT("Pawn.Guns.ViewKickReturnRate"), TEXT(""), TEXT("Возврат ствола в кадре"), TEXT("1/с") },
		{ TEXT("Flyer.MassKg"), TEXT(""), TEXT("Масса"), TEXT("кг") },
		{ TEXT("Flyer.CruiseSpeedCm"), TEXT(""), TEXT("Крейсерская"), TEXT("см/с") },
		{ TEXT("Flyer.CircleSpeedCm"), TEXT(""), TEXT("Кружение"), TEXT("см/с") },
		{ TEXT("Flyer.DiveSpeedCm"), TEXT(""), TEXT("Пике"), TEXT("см/с") },
		{ TEXT("Flyer.WanderAccelCm"), TEXT(""), TEXT("Блуждание, разгон"), TEXT("см/с²") },
		{ TEXT("Flyer.HuntAccelCm"), TEXT(""), TEXT("Охота, разгон"), TEXT("см/с²") },
		{ TEXT("Flyer.DiveAccelScale"), TEXT(""), TEXT("Пике, разгон"), TEXT("×") },
		{ TEXT("Flyer.SteerSeconds"), TEXT(""), TEXT("Поворот за"), TEXT("с") },
		{ TEXT("Flyer.WanderArriveCm"), TEXT(""), TEXT("Блуждание, цель достигнута"), TEXT("см") },
		{ TEXT("Flyer.WanderRetargetSeconds"), TEXT(""), TEXT("Блуждание, новая цель"), TEXT("с") },
		{ TEXT("Flyer.WanderReachCm"), TEXT(""), TEXT("Блуждание, дальность цели"), TEXT("см") },
		{ TEXT("Flyer.ShellClearanceCm"), TEXT(""), TEXT("Отступ от стенок"), TEXT("см") },
		{ TEXT("Flyer.SwoopChance"), TEXT(""), TEXT("Доля бросков"), TEXT("") },
		{ TEXT("Flyer.SwoopSpeedScale"), TEXT(""), TEXT("Бросок, скорость"), TEXT("×") },
		{ TEXT("Flyer.WanderSpeedScale"), TEXT(""), TEXT("Блуждание, скорость"), TEXT("×") },
		{ TEXT("Flyer.SentrySpeedScale"), TEXT(""), TEXT("Часовые, скорость"), TEXT("×") },
		{ TEXT("Flyer.MaxWanderTurnDeg"), TEXT(""), TEXT("Блуждание, поворот"), TEXT("°") },
		{ TEXT("Flyer.WanderBobCm"), TEXT(""), TEXT("Блуждание, покачивание"), TEXT("см/с") },
		{ TEXT("Flyer.AvoidSeconds"), TEXT(""), TEXT("Обход, упреждение"), TEXT("с") },
		{ TEXT("Flyer.AvoidRadiusCm"), TEXT(""), TEXT("Обход, радиус"), TEXT("см") },
		{ TEXT("Flyer.AvoidExtraCm"), TEXT(""), TEXT("Обход, запас"), TEXT("см") },
		{ TEXT("Flyer.SeparationCm"), TEXT(""), TEXT("Дистанция в стае"), TEXT("см") },
		{ TEXT("Flyer.SeparationPushCm"), TEXT(""), TEXT("Расталкивание"), TEXT("см/с") },
		{ TEXT("Flyer.SightRangeCm"), TEXT(""), TEXT("Дальность зрения"), TEXT("см") },
		{ TEXT("Flyer.SightHalfAngleDeg"), TEXT(""), TEXT("Угол зрения"), TEXT("°") },
		{ TEXT("Flyer.SenseSeconds"), TEXT(""), TEXT("Проверка зрения"), TEXT("с") },
		{ TEXT("Flyer.LoseSightSeconds"), TEXT(""), TEXT("Потеря цели"), TEXT("с") },
		{ TEXT("Flyer.CalmSeconds"), TEXT(""), TEXT("Успокоение"), TEXT("с") },
		{ TEXT("Flyer.AimProvokeSeconds"), TEXT(""), TEXT("Злит прицел через"), TEXT("с") },
		{ TEXT("Flyer.AimConeDeg"), TEXT(""), TEXT("Конус прицела"), TEXT("°") },
		{ TEXT("Flyer.RejoinQuietSeconds"), TEXT(""), TEXT("Тишина до возврата"), TEXT("с") },
		{ TEXT("Flyer.CircleRadiusCm"), TEXT(""), TEXT("Кружение, радиус"), TEXT("см") },
		{ TEXT("Flyer.CircleTightRadiusCm"), TEXT(""), TEXT("Петля, радиус"), TEXT("см") },
		{ TEXT("Flyer.CircleTightenSeconds"), TEXT(""), TEXT("Петля, за"), TEXT("с") },
		{ TEXT("Flyer.CircleRadiusJitter"), TEXT(""), TEXT("Кружение, разброс радиуса"), TEXT("") },
		{ TEXT("Flyer.CircleHeightCm"), TEXT(""), TEXT("Кружение, высота"), TEXT("см") },
		{ TEXT("Flyer.CircleGain"), TEXT(""), TEXT("Кружение, притяжение"), TEXT("1/с") },
		{ TEXT("Flyer.CircleBobCm"), TEXT(""), TEXT("Кружение, покачивание"), TEXT("см") },
		{ TEXT("Flyer.CircleSeconds"), TEXT(""), TEXT("Кружение до атаки"), TEXT("с") },
		{ TEXT("Flyer.CircleAgainSeconds"), TEXT(""), TEXT("Ещё круг"), TEXT("с") },
		{ TEXT("Flyer.MaxDiveSeconds"), TEXT(""), TEXT("Пике, не дольше"), TEXT("с") },
		{ TEXT("Flyer.DiveLeadSeconds"), TEXT(""), TEXT("Пике, упреждение"), TEXT("с") },
		{ TEXT("Flyer.OvershootCm"), TEXT(""), TEXT("Пролёт мимо"), TEXT("см") },
		{ TEXT("Flyer.RetreatSeconds"), TEXT(""), TEXT("Отход"), TEXT("с") },
		{ TEXT("Flyer.RetreatSpeedScale"), TEXT(""), TEXT("Отход, скорость"), TEXT("×") },
		{ TEXT("Flyer.RetreatClimb"), TEXT(""), TEXT("Отход, набор высоты"), TEXT("") },
		{ TEXT("Flyer.FeintChance"), TEXT(""), TEXT("Доля ложных атак"), TEXT("") },
		{ TEXT("Flyer.FeintMissCm"), TEXT(""), TEXT("Ложная атака, мимо на"), TEXT("см") },
		{ TEXT("Flyer.FeintSpeedCm"), TEXT(""), TEXT("Ложная атака, скорость"), TEXT("см/с") },
		{ TEXT("Flyer.BiteReachCm"), TEXT(""), TEXT("Укус, дальность"), TEXT("см") },
		{ TEXT("Flyer.BiteDamage"), TEXT(""), TEXT("Укус, урон"), TEXT("") },
		{ TEXT("Flyer.BiteImpulse"), TEXT(""), TEXT("Укус, толчок"), TEXT("кг·см/с") },
		{ TEXT("Flyer.BiteBounceBackCm"), TEXT(""), TEXT("Отскок назад"), TEXT("см/с") },
		{ TEXT("Flyer.BiteBounceUpCm"), TEXT(""), TEXT("Отскок вверх"), TEXT("см/с") },
		{ TEXT("Flyer.MaxHealth"), TEXT(""), TEXT("Здоровье"), TEXT("") },
		{ TEXT("Flyer.CorpseLinearDamping"), TEXT(""), TEXT("Труп, торможение"), TEXT("") },
		{ TEXT("Flyer.CorpseAngularDamping"), TEXT(""), TEXT("Труп, вращение"), TEXT("") },
		{ TEXT("Flyer.BiteShakeCm"), TEXT(""), TEXT("Укус, тряска камеры"), TEXT("см") },
		{ TEXT("Flyer.BiteVisorBlood"), TEXT(""), TEXT("Укус, кровь на визоре"), TEXT("") },
	};

	/** The field table's name: "Weapon[Пистолет].KickPitchDeg" is looked up as "Weapon.KickPitchDeg". */
	const FOSMenuName* MenuFieldOf(const FOSDebugParam& Param)
	{
		if (!Param.Property)
		{
			return nullptr;
		}
		const FString Kind = Param.Owner.StartsWith(TEXT("Weapon[")) ? FString(TEXT("Weapon")) : Param.Owner;
		const FString Key = Kind + TEXT(".") + Param.Property->GetName();
		for (const FOSMenuName& Name : MenuFieldNames)
		{
			if (Key == Name.Key)
			{
				return &Name;
			}
		}
		return nullptr;
	}

	/**
	 * A readable name and unit. Pinned (the overlay, «Главное»): the pin table first («Пистолет» under «Отдача»).
	 * Then the field table, then the editor's DisplayName, then the rule: the property name split into words, its unit
	 * suffix turned into a unit (Deg -> °, Cm -> см, a speed or an acceleration in Cm -> см/с or см/с², CmS -> см/с,
	 * Seconds -> с, Kg -> кг; Per1000 / Per10000 stay in the unit as "/1000").
	 */
	void MenuHumanName(const FOSDebugParam& Param, bool bPinned, FString& OutLabel, FString& OutUnit)
	{
		const FOSMenuName* Name = bPinned ? MenuNameOf(Param.Key) : nullptr;
		Name = Name ? Name : MenuFieldOf(Param);
		if (Name)
		{
			OutLabel = Name->Label;
			OutUnit = Name->Unit;
			return;
		}
		OutUnit = Param.Units;
		if (!Param.Property)
		{
			OutLabel = Param.Key;
			return;
		}
#if WITH_METADATA
		if (Param.Property->HasMetaData(TEXT("DisplayName")))
		{
			OutLabel = Param.Property->GetMetaData(TEXT("DisplayName"));
			return;
		}
#endif
		FString Words = Param.Property->GetName();
		Words.RemoveFromStart(TEXT("b"), ESearchCase::CaseSensitive);
		// "Per10000": a rate per so much of something - it stays in the unit, honestly.
		FString Per;
		const int32 PerAt = Words.Find(TEXT("Per"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (PerAt != INDEX_NONE && Words.Len() > PerAt + 3 && FChar::IsDigit(Words[PerAt + 3]))
		{
			Per = TEXT("/") + Words.Mid(PerAt + 3);
			Words.LeftInline(PerAt);
		}
		static const TPair<const TCHAR*, const TCHAR*> Suffixes[] = {
			{ TEXT("DegPerSec"), TEXT("°/с") }, { TEXT("CmS"), TEXT("см/с") }, { TEXT("Cm"), TEXT("см") }, { TEXT("Deg"), TEXT("°") },
			{ TEXT("Seconds"), TEXT("с") }, { TEXT("Ms"), TEXT("мс") }, { TEXT("Kg"), TEXT("кг") }, { TEXT("Hz"), TEXT("Гц") },
		};
		for (const TPair<const TCHAR*, const TCHAR*>& Suffix : Suffixes)
		{
			if (Words.EndsWith(Suffix.Key, ESearchCase::CaseSensitive) && Words.Len() > FCString::Strlen(Suffix.Key))
			{
				Words.LeftChopInline(FCString::Strlen(Suffix.Key));
				OutUnit = Suffix.Value;
				// The project writes speeds and accelerations in "Cm" too.
				if (OutUnit == TEXT("см") && Per.IsEmpty())
				{
					OutUnit = Words.EndsWith(TEXT("Accel")) ? TEXT("см/с²") : Words.EndsWith(TEXT("Speed")) ? TEXT("см/с") : OutUnit;
				}
				break;
			}
		}
		OutUnit += Per;
		OutLabel = FName::NameToDisplayString(Words, false);
	}

	// Category words of the groups in Russian ("Pawn · ZeroG|Arm" reads «Рука»), every word the project uses, in the
	// order the tabs list them: the most tuned first (arm, movement, view; a gun's recoil). Pawn components' names
	// (Pawn.Oxygen) are looked up here too.
	const TPair<const TCHAR*, const TCHAR*> MenuGroupWords[] = {
		{ TEXT("Arm"), TEXT("Рука") }, { TEXT("Grab"), TEXT("Хват") }, { TEXT("Walk"), TEXT("Ходьба") }, { TEXT("Thrusters"), TEXT("Маневровые") },
		{ TEXT("ZeroG"), TEXT("Невесомость") }, { TEXT("View"), TEXT("Взгляд") }, { TEXT("Input"), TEXT("Ввод") }, { TEXT("Hands"), TEXT("Руки") },
		{ TEXT("Push"), TEXT("Толчок") }, { TEXT("Grapple"), TEXT("Кошка") }, { TEXT("Tether"), TEXT("Трос") }, { TEXT("Body"), TEXT("Тело") },
		{ TEXT("Crash"), TEXT("Удары") }, { TEXT("Oxygen"), TEXT("Кислород") }, { TEXT("Vitals"), TEXT("Здоровье") },
		{ TEXT("Recoil"), TEXT("Отдача") }, { TEXT("Weapon"), TEXT("Ствол") }, { TEXT("Action"), TEXT("Цикл") }, { TEXT("Reload"), TEXT("Перезарядка") },
		{ TEXT("Switch"), TEXT("Смена") }, { TEXT("SlideLock"), TEXT("Затворная задержка") }, { TEXT("Pose"), TEXT("Поза") }, { TEXT("Parts"), TEXT("Детали") },
		{ TEXT("Tracer"), TEXT("Трассеры") }, { TEXT("Weapons"), TEXT("Стволы") }, { TEXT("Guns"), TEXT("Стволы") }, { TEXT("Fire"), TEXT("Огонь") },
		{ TEXT("MuzzleFx"), TEXT("Вспышка") }, { TEXT("Scope"), TEXT("Прицел") }, { TEXT("Aim"), TEXT("Наведение") }, { TEXT("Sights"), TEXT("Мушка") },
		{ TEXT("Motion"), TEXT("Качание") },
		{ TEXT("Flight"), TEXT("Полёт") }, { TEXT("Hunt"), TEXT("Охота") }, { TEXT("Flock"), TEXT("Стая") }, { TEXT("Hearing"), TEXT("Слух") },
		{ TEXT("Hit"), TEXT("Попадания") }, { TEXT("Flinch"), TEXT("Вздрагивание") }, { TEXT("Roost"), TEXT("Насест") }, { TEXT("Look"), TEXT("Вид") },
		{ TEXT("Ladder"), TEXT("Лестница") }, { TEXT("Noise"), TEXT("Шум") },
		{ TEXT("Interaction"), TEXT("Взаимодействие") }, { TEXT("Keyring"), TEXT("Ключи") }, { TEXT("Keys"), TEXT("Ключи") }, { TEXT("Key"), TEXT("Ключ") },
		{ TEXT("Camera"), TEXT("Камера") }, { TEXT("Dust"), TEXT("Пыль") }, { TEXT("VisorBlood"), TEXT("Кровь на визоре") },
		{ TEXT("HullTools"), TEXT("Инструменты ремонта") }, { TEXT("Strap"), TEXT("Стяжка") }, { TEXT("Welder"), TEXT("Сварка") }, { TEXT("Weld"), TEXT("Сварка") },
		{ TEXT("WeldProjector"), TEXT("Проектор сварки") }, { TEXT("Projector"), TEXT("Проектор") }, { TEXT("Appear"), TEXT("Появление") },
		{ TEXT("Dash"), TEXT("Пунктир") }, { TEXT("Plate"), TEXT("Лист") }, { TEXT("Text"), TEXT("Надпись") },
		{ TEXT("Prediction"), TEXT("Прогноз") }, { TEXT("BulletTime"), TEXT("Замедление") }, { TEXT("Sound"), TEXT("Звук") }, { TEXT("Audio"), TEXT("Звук") },
		{ TEXT("Volume"), TEXT("Громкость") }, { TEXT("Limiter"), TEXT("Лимитер") }, { TEXT("Feedback"), TEXT("Отклик") }, { TEXT("Visual"), TEXT("Вид") },
		{ TEXT("Ship"), TEXT("Корабль") }, { TEXT("Walker"), TEXT("Пешеход") }, { TEXT("World"), TEXT("Мир") }, { TEXT("System"), TEXT("Система") },
		{ TEXT("LivingWall"), TEXT("Живая стена") }, { TEXT("Regrow"), TEXT("Отрастание") }, { TEXT("Nerves"), TEXT("Нервы") }, { TEXT("Tear"), TEXT("Разрыв") },
		{ TEXT("ThinSpots"), TEXT("Тонкие места") }, { TEXT("Squeeze"), TEXT("Сжатие") }, { TEXT("Rock"), TEXT("Камень") },
		{ TEXT("Glass"), TEXT("Стекло") }, { TEXT("Debris"), TEXT("Обломки") }, { TEXT("Impact"), TEXT("Удар") }, { TEXT("HitZones"), TEXT("Зоны попадания") },
		{ TEXT("HitZone"), TEXT("Зона попадания") }, { TEXT("Targets"), TEXT("Цели") }, { TEXT("Target"), TEXT("Мишень") }, { TEXT("Dummy"), TEXT("Манекен") },
		{ TEXT("Collision"), TEXT("Коллизия") }, { TEXT("Shape"), TEXT("Форма") }, { TEXT("Prop"), TEXT("Предмет") }, { TEXT("Carry"), TEXT("Переноска") },
		{ TEXT("Handle"), TEXT("Ручка") }, { TEXT("Pickup"), TEXT("Подбор") }, { TEXT("Dock"), TEXT("Док") }, { TEXT("Cart"), TEXT("Тележка") },
		{ TEXT("Door"), TEXT("Дверь") }, { TEXT("Gate"), TEXT("Ворота") }, { TEXT("Lift"), TEXT("Лифт") }, { TEXT("Shelf"), TEXT("Полка") },
		{ TEXT("Fastener"), TEXT("Крепёж") }, { TEXT("Appliance"), TEXT("Прибор") }, { TEXT("Access"), TEXT("Доступ") }, { TEXT("Lamp"), TEXT("Лампа") },
		{ TEXT("Indicator"), TEXT("Индикатор") }, { TEXT("Light"), TEXT("Свет") }, { TEXT("Debug"), TEXT("Отладка") },
		{ TEXT("OS"), TEXT("Общее") }, { TEXT("MindKore"), TEXT("Общее") },
	};

	/** The group's last word: "Pawn · ZeroG|Arm" -> "Arm", "Pawn.Oxygen" -> "Oxygen", "Flyer" -> "Flyer". */
	FString MenuGroupWord(const FString& Group)
	{
		int32 At = INDEX_NONE;
		Group.FindLastChar(TEXT('|'), At);
		if (At != INDEX_NONE)
		{
			return Group.Mid(At + 1);
		}
		const int32 Dot = Group.Find(TEXT(" · "), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		FString Word = Dot == INDEX_NONE ? Group : Group.Mid(Dot + 3);
		Word.RemoveFromStart(TEXT("Pawn."));
		return Word;
	}

	int32 MenuGroupRank(const FString& Group)
	{
		const FString Word = MenuGroupWord(Group);
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(MenuGroupWords); ++Index)
		{
			if (Word == MenuGroupWords[Index].Key)
			{
				return Index;
			}
		}
		return UE_ARRAY_COUNT(MenuGroupWords);
	}

	/** «Рука», «Пистолет · Отдача», «Насест Flyers_Belt · Стая», «Зона Gravity_Cabin», «Кислород». */
	FString MenuGroupTitle(const FString& Group, const FString& Owner)
	{
		const FString Word = MenuGroupWord(Group);
		// A word the table lacks still reads as words, not as an identifier.
		FString Title = FName::NameToDisplayString(Word, false);
		for (const TPair<const TCHAR*, const TCHAR*>& Pair : MenuGroupWords)
		{
			if (Word == Pair.Key)
			{
				Title = Pair.Value;
			}
		}
		FString Who;
		if (Owner.StartsWith(TEXT("Weapon[")))
		{
			Who = Owner.Mid(7).LeftChop(1);
		}
		else if (Owner.StartsWith(TEXT("Habitat[")))
		{
			Who = TEXT("Насест ") + Owner.Mid(8).LeftChop(1);
		}
		else if (Owner.StartsWith(TEXT("Zone[")))
		{
			Who = TEXT("Зона ") + Owner.Mid(5).LeftChop(1);
		}
		else if (Owner == TEXT("World") && Group != Owner)
		{
			Who = TEXT("Мир");
		}
		else if (Owner == TEXT("Flyer") && Group == Owner)
		{
			Who = TEXT("Летун");
		}
		else if (Owner == TEXT("Pawn") && Group == Owner)
		{
			Who = TEXT("Пешка");
		}
		if (Who.IsEmpty() || Group == Owner)
		{
			return Who.IsEmpty() ? Title : Who;
		}
		return Who + TEXT(" · ") + Title;
	}

	/** A value as the rows show it: 90.000000 -> 90, True -> вкл. */
	FString MenuValueText(const FString& Value)
	{
		if (Value == TEXT("True") || Value == TEXT("False"))
		{
			return Value == TEXT("True") ? TEXT("вкл") : TEXT("выкл");
		}
		return Value.IsNumeric() ? MenuNumber(FCString::Atod(*Value)) : Value;
	}

	// --- small pieces --------------------------------------------------------------------------------------------

	TSharedRef<SWidget> MenuHairline(const FLinearColor& Color = MenuHair)
	{
		return SNew(SBox).HeightOverride(1.0f)[SNew(SImage).Image(MenuWhite()).ColorAndOpacity(Color)];
	}

	/** A section's label (capitals, tracked out) on its baseline, a hairline under it: GridSection units in all. */
	TSharedRef<SWidget> MenuSection(const FString& Title, TSharedPtr<SWidget> Right = nullptr)
	{
		TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				// FText::ToUpper, not FString's: that one leaves Cyrillic as it is.
				MenuOnLine(GridSection - 1.0f, 6.0f, MenuLabel(), SNew(STextBlock).Font(MenuLabel()).ColorAndOpacity(MenuAccent).Text(MenuText_(Title).ToUpper()))
			];
		if (Right.IsValid())
		{
			Line->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)[Right.ToSharedRef()];
		}
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(GridSection - 1.0f)[Line]]
			+ SVerticalBox::Slot().AutoHeight()[MenuHairline()];
	}

	TSharedRef<SWidget> MenuButton(const FString& Label, TFunction<void()> Action, TAttribute<bool> Enabled = true)
	{
		return SNew(SButton)
			.ButtonStyle(&FOSMenuLook::Get().Button)
			.IsEnabled(Enabled)
			.OnClicked_Lambda([Action]() { Action(); return FReply::Handled(); })
			[
				SNew(STextBlock).Font(MenuFont()).ColorAndOpacity(FSlateColor::UseForeground()).Text(MenuText_(Label))
			];
	}

	/** A toolbar action: text only until hovered; the tooltip says what the short label leaves out. */
	TSharedRef<SWidget> MenuTool(const FString& Label, const FString& Tip, TFunction<void()> Action, TAttribute<bool> Enabled = true)
	{
		return SNew(SButton)
			.ButtonStyle(&FOSMenuLook::Get().Ghost)
			.ContentPadding(FMargin(3.5f, 3.0f))
			.IsEnabled(Enabled)
			.ToolTipText(MenuText_(Label + TEXT(": ") + Tip))
			.OnClicked_Lambda([Action]() { Action(); return FReply::Handled(); })
			[
				SNew(STextBlock).Font(MenuFont()).ColorAndOpacity(FSlateColor::UseForeground()).Text(MenuText_(Label))
			];
	}

	/** An Actions row's button: its label and lit state read on paint. */
	TSharedRef<SWidget> MenuActionButton(const FOSDebugRow::FAction& Action)
	{
		const TFunction<FString()> Label = Action.Label;
		const TFunction<bool()> IsOn = Action.IsOn;
		const TFunction<void()> Run = Action.Run;
		return SNew(SButton)
			.ButtonStyle(&FOSMenuLook::Get().Button)
			.ToolTipText(MenuText_(Action.Tip))
			.OnClicked_Lambda([Run]() { if (Run) { Run(); } return FReply::Handled(); })
			[
				SNew(STextBlock).Font(MenuFont())
				.ColorAndOpacity_Lambda([IsOn]() { return FSlateColor(IsOn && IsOn() ? MenuCore : MenuDim); })
				.Text_Lambda([Label]() { return MenuText_(Label ? Label() : FString()); })
			];
	}

	/** The player's guns now (the zero-G pawn's or a walker's), or null. */
	UOSWeaponsComponent* MenuPlayerGuns(const TWeakObjectPtr<UOSDebugSubsystem>& Debug)
	{
		UWorld* World = Debug.IsValid() ? Debug->GetGameWorld() : nullptr;
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		return Pawn ? Pawn->FindComponentByClass<UOSWeaponsComponent>() : nullptr;
	}

	void MenuExec(const TWeakObjectPtr<UOSDebugSubsystem>& Debug, const FString& Command)
	{
		UWorld* World = Debug.IsValid() ? Debug->GetGameWorld() : nullptr;
		if (GEngine && World)
		{
			GEngine->Exec(World, *Command);
		}
	}

	/**
	 * «Снаряжение» at the head of «Игрок» (Docs/DebugMenu.md): the guns open or closed (the pump and the RMB-93 swapped in
	 * slot 2), each attachment none -> in the pouch -> on a gun -> none, all or nothing. Every button runs an os.Guns /
	 * os.Attach command (Docs/Console.md), so what it does is logged and can be typed.
	 */
	void MenuGearRows(const TWeakObjectPtr<UOSDebugSubsystem>& Debug, TArray<TSharedPtr<FOSDebugRow>>& Rows)
	{
		const UOSWeaponsComponent* Guns = MenuPlayerGuns(Debug);
		if (!Guns)
		{
			return;
		}
		const TSharedPtr<FOSDebugRow> Header = MakeShared<FOSDebugRow>();
		Header->Kind = FOSDebugRow::EKind::Section;
		Header->Header = TEXT("Снаряжение");
		Rows.Add(Header);

		const TSharedPtr<FOSDebugRow> GunsRow = MakeShared<FOSDebugRow>();
		GunsRow->Kind = FOSDebugRow::EKind::Actions;
		GunsRow->Label = TEXT("Стволы");
		for (int32 Gun = 0; Gun < Guns->Weapons.Num(); ++Gun)
		{
			FOSDebugRow::FAction& Action = GunsRow->Actions.AddDefaulted_GetRef();
			Action.Label = [Debug, Gun]()
			{
				const UOSWeaponsComponent* G = MenuPlayerGuns(Debug);
				return G && G->Weapons.IsValidIndex(Gun) ? G->Weapons[Gun].Name.ToString() : FString(TEXT("—"));
			};
			Action.IsOn = [Debug, Gun]() { const UOSWeaponsComponent* G = MenuPlayerGuns(Debug); return G && G->IsWeaponUnlocked(Gun); };
			Action.Run = [Debug, Gun]()
			{
				const UOSWeaponsComponent* G = MenuPlayerGuns(Debug);
				MenuExec(Debug, FString::Printf(TEXT("%s %d"), G && G->IsWeaponUnlocked(Gun) ? TEXT("os.Guns.Take") : TEXT("os.Guns.Give"), Gun + 1));
			};
			Action.Tip = FString::Printf(TEXT("os.Guns.Give / os.Guns.Take %d: открыть или закрыть"), Gun + 1);
		}
		if (Guns->Weapons.Num() > 1)
		{
			FOSDebugRow::FAction& Swap = GunsRow->Actions.AddDefaulted_GetRef();
			Swap.Label = [Debug]()
			{
				const UOSWeaponsComponent* G = MenuPlayerGuns(Debug);
				return G && G->Weapons.IsValidIndex(1) && G->Weapons[1].Id == FName(TEXT("Rmb93")) ? FString(TEXT("→ дробовик")) : FString(TEXT("→ Рысь"));
			};
			Swap.Run = [Debug]()
			{
				const UOSWeaponsComponent* G = MenuPlayerGuns(Debug);
				const bool bRmb = G && G->Weapons.IsValidIndex(1) && G->Weapons[1].Id == FName(TEXT("Rmb93"));
				MenuExec(Debug, bRmb ? TEXT("os.Guns.Swap 2 Slot") : TEXT("os.Guns.Swap 2 Rmb93"));
			};
			Swap.Tip = TEXT("os.Guns.Swap 2 Rmb93 / Slot: в слот 2 Рысь или дробовик");
		}
		Rows.Add(GunsRow);

		for (const FOSAttachment& Each : Guns->Attachments)
		{
			const FName Id = Each.Id;
			const TSharedPtr<FOSDebugRow> Row = MakeShared<FOSDebugRow>();
			Row->Kind = FOSDebugRow::EKind::Actions;
			Row->Label = Each.Name.ToString();
			FOSDebugRow::FAction& Action = Row->Actions.AddDefaulted_GetRef();
			Action.Label = [Debug, Id]()
			{
				const UOSWeaponsComponent* G = MenuPlayerGuns(Debug);
				int32 Gun = INDEX_NONE, Mount = INDEX_NONE;
				if (G && G->FindMounted(Id, Gun, Mount))
				{
					return FString::Printf(TEXT("на стволе: %s"), *G->Weapons[Gun].Name.ToString());
				}
				return FString(G && G->HasAttachment(Id) ? TEXT("в сумке") : TEXT("нет"));
			};
			Action.IsOn = [Debug, Id]() { const UOSWeaponsComponent* G = MenuPlayerGuns(Debug); return G && G->HasAttachment(Id); };
			Action.Run = [Debug, Id]()
			{
				// none -> the pouch -> on the gun in hand (or the first that takes it) -> none.
				const UOSWeaponsComponent* G = MenuPlayerGuns(Debug);
				if (!G)
				{
					return;
				}
				int32 Gun = INDEX_NONE, Mount = INDEX_NONE;
				if (G->FindMounted(Id, Gun, Mount))
				{
					MenuExec(Debug, TEXT("os.Attach.Take ") + Id.ToString());
					return;
				}
				if (!G->HasAttachment(Id))
				{
					MenuExec(Debug, TEXT("os.Attach.Give ") + Id.ToString() + TEXT(" bag"));
					return;
				}
				const FOSAttachment* Attachment = G->FindAttachment(Id);
				int32 Target = Attachment && G->FindMountFor(G->CurrentWeapon, *Attachment) != INDEX_NONE ? G->CurrentWeapon : INDEX_NONE;
				for (int32 Other = 0; Target == INDEX_NONE && Attachment && Other < G->Weapons.Num(); ++Other)
				{
					Target = G->FindMountFor(Other, *Attachment) != INDEX_NONE ? Other : INDEX_NONE;
				}
				MenuExec(Debug, Target == INDEX_NONE ? TEXT("os.Attach.Take ") + Id.ToString()
					: FString::Printf(TEXT("os.Attach.Mount %s %d"), *Id.ToString(), Target + 1));
			};
			Action.Tip = FString::Printf(TEXT("%s: нет → в сумке → на стволе → нет (os.Attach.Give %s bag / Mount / Take)"), *Id.ToString(), *Id.ToString());
			Rows.Add(Row);
		}

		const TSharedPtr<FOSDebugRow> AllRow = MakeShared<FOSDebugRow>();
		AllRow->Kind = FOSDebugRow::EKind::Actions;
		AllRow->Label = TEXT("Разом");
		FOSDebugRow::FAction& All = AllRow->Actions.AddDefaulted_GetRef();
		All.Label = []() { return FString(TEXT("Всё")); };
		All.Run = [Debug]() { MenuExec(Debug, TEXT("os.Guns.Give all")); MenuExec(Debug, TEXT("os.Attach.Give all bag")); };
		All.Tip = TEXT("os.Guns.Give all, os.Attach.Give all bag");
		FOSDebugRow::FAction& Nothing = AllRow->Actions.AddDefaulted_GetRef();
		Nothing.Label = []() { return FString(TEXT("Ничего")); };
		Nothing.Run = [Debug]() { MenuExec(Debug, TEXT("os.Attach.Take all")); MenuExec(Debug, TEXT("os.Guns.Take all")); };
		Nothing.Tip = TEXT("os.Attach.Take all, os.Guns.Take all");
		Rows.Add(AllRow);
	}

	/** The game's dt over the last 5 s: one column per pixel (its longest frame), lines at 30 and 60 FPS. */
	class SOSDtGraph : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SOSDtGraph) {}
			SLATE_ARGUMENT(TWeakObjectPtr<UOSDebugSubsystem>, Debug)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Debug = InArgs._Debug;
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return FVector2D(280.0f, 56.0f);
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out,
			int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override
		{
			const FVector2f Size = FVector2f(Geometry.GetLocalSize());
			FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(), MenuWhite(), ESlateDrawEffect::None, MenuWell);
			const UOSDebugSubsystem* DebugMenu = Debug.Get();
			if (!DebugMenu || DebugMenu->GetFrames().IsEmpty() || Size.X < 2.0f)
			{
				return Layer;
			}
			const TArray<UOSDebugSubsystem::FFrameSample>& Frames = DebugMenu->GetFrames();
			const double Start = Frames.Last().At - 5.0;
			const int32 Columns = int32(Size.X);
			TArray<float> Peak;
			Peak.Init(-1.0f, Columns);
			float Max = 1.0f / 30.0f;
			for (const UOSDebugSubsystem::FFrameSample& Frame : Frames)
			{
				if (Frame.At >= Start)
				{
					const int32 Column = FMath::Clamp(int32((Frame.At - Start) / 5.0 * Columns), 0, Columns - 1);
					Peak[Column] = FMath::Max(Peak[Column], Frame.DeltaSeconds);
					Max = FMath::Max(Max, Frame.DeltaSeconds);
				}
			}
			const float Top = Max * 1.15f;
			const auto Y = [&Size, Top](float Seconds) { return Size.Y * (1.0f - Seconds / Top); };
			const FSlateFontInfo LabelFont = MenuMono(TypeCaption);
			for (const int32 Fps : { 30, 60 })
			{
				const float At = Y(1.0f / Fps);
				MenuPaintDashes(Out, Layer + 1, Geometry, 0, FVector2f(0.0f, At), FVector2f(1.0f, 0.0f), Size.X, MenuHair);
				FSlateDrawElement::MakeText(Out, Layer + 1, Geometry.ToPaintGeometry(FVector2f(24.0f, 10.0f), FSlateLayoutTransform(FVector2f(Size.X - 16.0f, At - 12.0f))),
					FString::FromInt(Fps), LabelFont, ESlateDrawEffect::None, MenuDim);
			}
			TArray<FVector2f> Points;
			for (int32 Column = 0; Column < Columns; ++Column)
			{
				if (Peak[Column] >= 0.0f)
				{
					Points.Add(FVector2f(float(Column), Y(Peak[Column])));
				}
			}
			if (Points.Num() > 1)
			{
				FSlateDrawElement::MakeLines(Out, Layer + 2, Geometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None, MenuCore.CopyWithNewOpacity(0.85f), true, 1.25f);
			}
			return Layer + 2;
		}

	private:
		TWeakObjectPtr<UOSDebugSubsystem> Debug;
	};

	FString MenuOpenGroupsFile()
	{
		return FPaths::ProjectSavedDir() / TEXT("OSDebug/OpenGroups.txt");
	}
}

void SOSDebugMenu::Construct(const FArguments& InArgs)
{
	Debug = InArgs._Debug;
	TArray<FString> Saved;
	if (FFileHelper::LoadFileToStringArray(Saved, *MenuOpenGroupsFile()))
	{
		OpenGroups.Append(Saved);
	}
	const auto ModeIs = [this](EOSDebugMode Mode, EVisibility Shown)
	{
		return [this, Mode, Shown]()
		{
			const UOSDebugSubsystem* DebugMenu = Debug.Get();
			return DebugMenu && DebugMenu->GetMode() == Mode ? Shown : EVisibility::Collapsed;
		};
	};
	const TAttribute<FMargin> Clearance = TAttribute<FMargin>::CreateLambda([this]() { return HudClearance(); });
	ChildSlot
	[
		SNew(SDPIScaler)
		.Visibility(EVisibility::SelfHitTestInvisible)
		.DPIScale_Lambda([this]() { return MenuScale(); })
		[
			SNew(SOverlay)
			.Visibility(EVisibility::SelfHitTestInvisible)
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Fill).Padding(Clearance)
			[
				SNew(SBox).WidthOverride(420.0f).Visibility_Lambda(ModeIs(EOSDebugMode::Open, EVisibility::Visible))
				[
					MakePanel()
				]
			]
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(Clearance)
			[
				// Pinned: translucent over the game, input stays with the game; left of the crosshair, between the HUD's corners.
				SNew(SBox).Visibility_Lambda(ModeIs(EOSDebugMode::Pinned, EVisibility::HitTestInvisible))
				[
					MakePinned()
				]
			]
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(14.0f)
			[
				MakeWatermark()
			]
		]
	];
	Rebuild();
	RebuildPinned();
}

float SOSDebugMenu::StatusAlpha() const
{
	const UOSDebugSubsystem* D = Debug.Get();
	if (!D || StatusShown.IsEmpty())
	{
		return 0.0f;
	}
	// An error stays until the next message; the rest is a receipt: 4 s, then a second to fade.
	return D->bStatusError ? 1.0f : FMath::Clamp(5.0f - float(FPlatformTime::Seconds() - StatusAt), 0.0f, 1.0f);
}

FString SOSDebugMenu::MenuStatusText(const FString& Status) const
{
	// "Pawn.ArmLengthCm = 90.000000 (было 70.000000)" (os.Debug.Set / Get) reads «Длина руки → 90 см (было 70)».
	FString Key, Rest;
	const UOSDebugSubsystem* D = Debug.Get();
	if (!D || !Status.Split(TEXT(" = "), &Key, &Rest) || Key.Contains(TEXT(" ")))
	{
		return Status;
	}
	TArray<FOSDebugParam> Params;
	D->ListParams(Params);
	const FOSDebugParam* Param = Params.FindByPredicate([&Key](const FOSDebugParam& Each) { return Each.Key == Key; });
	if (!Param)
	{
		return Status;
	}
	FString Value = Rest, Was;
	if (Rest.Split(TEXT(" (было "), &Value, &Was))
	{
		Was.RemoveFromEnd(TEXT(")"));
	}
	FString Label, Unit;
	MenuHumanName(*Param, false, Label, Unit);
	const auto WithUnit = [&Unit](const FString& Text)
	{
		return Unit == TEXT("×") ? TEXT("×") + MenuValueText(Text) : Unit.IsEmpty() ? MenuValueText(Text) : MenuValueText(Text) + TEXT(" ") + Unit;
	};
	return FString::Printf(TEXT("%s → %s%s"), *Label, *WithUnit(Value), Was.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (было %s)"), *WithUnit(Was)));
}

float SOSDebugMenu::MenuScale() const
{
	const float View = FMath::Max(ViewScale, 0.01f);
	const float Menu = FMath::Max(MenuMinScale, FMath::FloorToFloat(View * 4.0f + 0.01f) / 4.0f);
	return Menu / View;
}

FMargin SOSDebugMenu::HudClearance() const
{
	const float Units = 1.0f / FMath::Max(ViewScale * MenuScale(), 0.01f);
	// In a small view (a PIE window) the lamps at the top give way too: O2 and HP never do.
	const float Top = ViewHeight - MenuHudTopPx - MenuVitalsBottomPx >= ViewHeight * 0.5f ? MenuHudTopPx : MenuLeftPx;
	const float Bottom = ViewHeight >= MenuToolLinesMinPx ? MenuHudBottomPx : MenuVitalsBottomPx;
	return FMargin(MenuLeftPx * Units, Top * Units, 0.0f, Bottom * Units);
}

TSharedRef<SWidget> SOSDebugMenu::MakePanel()
{
	const FOSMenuLook& Look = FOSMenuLook::Get();
	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	for (const FString& Name : MenuTabs)
	{
		Tabs->AddSlot().AutoWidth()[MakeTabButton(Name)];
	}
	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(MenuWhite())
			.BorderBackgroundColor(MenuInk)
			.Clipping(EWidgetClipping::ClipToBounds)
			.Padding(FMargin(14.0f, 12.0f, 14.0f, 12.0f))
			[
				SNew(SVerticalBox)
				// One line box on one baseline: the projector's letters «ОТЛАДКА» (the capitals' bottom is the baseline),
				// the config's name (typed in place, it exports with it; the watermark says «ТЕСТ»), and how far the config
				// is from the original, right-aligned.
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SOSPribor).Text(TEXT("ОТЛАДКА")).Color(MenuCore).LineHeight(GridHeader).Baseline(GridBaseline)
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(12.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SBox).HeightOverride(GridHeader).VAlign(VAlign_Bottom).Padding(FMargin(0.0f, 0.0f, 0.0f, GridBaseline - MenuDescent(MenuFont())))
						[
							SNew(SEditableTextBox)
							.Style(&Look.BareField)
							.Font(MenuFont())
							.HintText(MenuText_(TEXT("без имени")))
							.ToolTipText(MenuText_(TEXT("имя конфига для экспорта")))
							.Text_Lambda([this]() { const UOSDebugSubsystem* DebugMenu = Debug.Get(); return MenuText_(DebugMenu ? DebugMenu->GetConfigName() : FString()); })
							.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type)
							{
								if (UOSDebugSubsystem* DebugMenu = Debug.Get())
								{
									DebugMenu->SetConfigName(Text.ToString());
								}
							})
						]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						MenuOnLine(GridHeader, GridBaseline, MenuFont(TypeCaption), SNew(STextBlock).Font(MenuFont(TypeCaption))
						.ColorAndOpacity_Lambda([this]()
						{
							const UOSDebugSubsystem* DebugMenu = Debug.Get();
							return FSlateColor(DebugMenu && DebugMenu->GetChangedCount() > 0 ? MenuChanged : MenuDim);
						})
						.Text_Lambda([this]()
						{
							const UOSDebugSubsystem* DebugMenu = Debug.Get();
							return MenuText_(FString::Printf(TEXT("изменено %d"), DebugMenu ? DebugMenu->GetChangedCount() : 0));
						}))
					]
				]
				// The last session's config: offered, never applied by itself.
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
				[
					SNew(SButton)
					.ButtonStyle(&Look.Button)
					.Visibility_Lambda([this]()
					{
						const UOSDebugSubsystem* DebugMenu = Debug.Get();
						return DebugMenu && DebugMenu->GetLastSessionChanges() > 0 && DebugMenu->GetChangedCount() == 0 ? EVisibility::Visible : EVisibility::Collapsed;
					})
					.OnClicked_Lambda([this]()
					{
						if (UOSDebugSubsystem* DebugMenu = Debug.Get())
						{
							DebugMenu->RestoreLastSession();
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock).Font(MenuFont()).ColorAndOpacity(FSlateColor::UseForeground()).Text_Lambda([this]()
						{
							const UOSDebugSubsystem* DebugMenu = Debug.Get();
							return MenuText_(FString::Printf(TEXT("вернуть прошлый конфиг (изменений: %d)"), DebugMenu ? DebugMenu->GetLastSessionChanges() : 0));
						})
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MenuRow(GridLine, SNew(SOSDash))
				]
				// Search first: six hundred parameters are not scrolled through. It looks through every tab.
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).HeightOverride(GridRow)
					[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SAssignNew(SearchBox, SSearchBox)
						.Style(&Look.Search)
						.HintText(MenuText_(TEXT("поиск по всем вкладкам  ·  Ctrl+F")))
						.OnTextChanged_Lambda([this](const FText& Text)
						{
							Search = Text.ToString();
							Rebuild();
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill).Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SCheckBox)
						.Style(&Look.Chip)
						.ToolTipText(MenuText_(TEXT("только изменённые")))
						.IsChecked_Lambda([this]() { return bChangedOnly ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
						{
							bChangedOnly = State == ECheckBoxState::Checked;
							Rebuild();
						})
						[
							SNew(SBox).VAlign(VAlign_Center)
							[
								SNew(STextBlock).Font(MenuFont()).ColorAndOpacity(FSlateColor::UseForeground()).Text(MenuText_(TEXT("изменённые")))
							]
						]
					]
					]
				]
				// The config's operations at the left, the edits' at the right, a hairline between: one quiet toolbar
				// that fits the frame (the design's [Экспорт] [Импорт] [Сохранить] [Сбросить]); tooltips say the rest.
				+ SVerticalBox::Slot().AutoHeight().Padding(-4.0f, 4.0f, -4.0f, 0.0f)
				[
					SNew(SBox).HeightOverride(GridRow)
					[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[MenuTool(TEXT("Экспорт"), TEXT("в Saved/OSDebug/Presets и в буфер"), [this]() { if (UOSDebugSubsystem* D = Debug.Get()) { D->ExportToFile(FString()); } })]
					+ SHorizontalBox::Slot().AutoWidth()[MenuTool(TEXT("Импорт"), TEXT("из буфера"), [this]() { if (UOSDebugSubsystem* D = Debug.Get()) { D->Import(TEXT("clipboard")); } })]
					+ SHorizontalBox::Slot().AutoWidth()[MenuTool(TEXT("Сохранить"), TEXT("в ассет /Game/OS/Debug/Presets (в сборке - экспорт)"), [this]() { FString Message; if (UOSDebugSubsystem* D = Debug.Get()) { D->SaveToAsset(FString(), Message); } })]
					+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Center).VAlign(VAlign_Fill).Padding(0.0f, 5.0f)
					[
						SNew(SBox).WidthOverride(1.0f)[SNew(SImage).Image(MenuWhite()).ColorAndOpacity(MenuHair)]
					]
					+ SHorizontalBox::Slot().AutoWidth()[MenuTool(TEXT("Сброс"), TEXT("сбросить всё"), [this]() { if (UOSDebugSubsystem* D = Debug.Get()) { D->Reset(TEXT("all")); } },
						TAttribute<bool>::CreateLambda([this]() { const UOSDebugSubsystem* D = Debug.Get(); return D && D->GetChangedCount() > 0; }))]
					+ SHorizontalBox::Slot().AutoWidth()[MenuTool(TEXT("Отмена"), TEXT("отменить, Ctrl+Z"), [this]() { if (UOSDebugSubsystem* D = Debug.Get()) { D->Undo(); } },
						TAttribute<bool>::CreateLambda([this]() { const UOSDebugSubsystem* D = Debug.Get(); return D && D->CanUndo(); }))]
					+ SHorizontalBox::Slot().AutoWidth()[MenuTool(TEXT("Повтор"), TEXT("повторить, Ctrl+Y"), [this]() { if (UOSDebugSubsystem* D = Debug.Get()) { D->Redo(); } },
						TAttribute<bool>::CreateLambda([this]() { const UOSDebugSubsystem* D = Debug.Get(); return D && D->CanRedo(); }))]
					]
				]
				// The last action's result; an import's skipped keys under it.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Font(MenuFont())
					.LineHeightPercentage(MenuLineHeight(MenuFont(), GridLine))
					.AutoWrapText(true)
					.Visibility_Lambda([this]() { return StatusAlpha() > 0.0f ? EVisibility::Visible : EVisibility::Collapsed; })
					.ColorAndOpacity_Lambda([this]()
					{
						const UOSDebugSubsystem* D = Debug.Get();
						return FSlateColor((D && D->bStatusError ? MenuError : MenuDim).CopyWithNewOpacity(StatusAlpha()));
					})
					.Text_Lambda([this]() { return MenuText_(StatusShown); })
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(SkippedBox, SVerticalBox)
				]
				// The tabs: a row of words over the body, the open one lit and underlined on the bar's hairline.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 8.0f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot().VAlign(VAlign_Bottom)[MenuHairline()]
					+ SOverlay::Slot().Padding(-6.0f, 0.0f, 0.0f, 0.0f)[Tabs]
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SAssignNew(Body, SWidgetSwitcher)
					.WidgetIndex_Lambda([this]()
					{
						return !Search.IsEmpty() ? 1 : Tab == TEXT("Система") ? 0 : Tab == TEXT("Пресеты") ? 2 : 1;
					})
					+ SWidgetSwitcher::Slot()
					[
						SNew(SScrollBox).ScrollBarStyle(&Look.Scroll).ScrollBarThickness(FVector2f(3.0f, 3.0f))
						+ SScrollBox::Slot().Padding(0.0f, 0.0f, 8.0f, 0.0f)[MakeSystemTab()]
					]
					+ SWidgetSwitcher::Slot()
					[
						SNew(SVerticalBox)
						// «Главное»: the quick controls above the pins.
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 8.0f, 4.0f)
						[
							SNew(SBox)
							.Visibility_Lambda([this]() { return Search.IsEmpty() && Tab == TEXT("Главное") ? EVisibility::Visible : EVisibility::Collapsed; })
							[
								MakeQuickControls()
							]
						]
						// The World tab: the gravity the pawn gets now, and from what.
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
						[
							SNew(STextBlock)
							.Font(MenuFont(TypeCaption))
							.LineHeightPercentage(MenuLineHeight(MenuFont(TypeCaption), GridCaption))
							.ColorAndOpacity(MenuDim)
							.AutoWrapText(true)
							.Visibility_Lambda([this]() { return Search.IsEmpty() && Tab == TEXT("Мир") ? EVisibility::Visible : EVisibility::Collapsed; })
							.Text_Lambda([this]()
							{
								const UOSDebugSubsystem* D = Debug.Get();
								const UWorld* World = D ? D->GetGameInstance()->GetWorld() : nullptr;
								const AOSZeroGPawn* Pawn = nullptr;
								if (World)
								{
									for (TActorIterator<AOSZeroGPawn> It(World); It && !Pawn; ++It)
									{
										Pawn = *It;
									}
								}
								if (!Pawn)
								{
									return MenuText_(TEXT("нет пешки"));
								}
								const FOSGravity Gravity = OSGravityAt(World, Pawn->GetActorLocation());
								return MenuText_(Gravity.Zone
									? FString::Printf(TEXT("у пешки: %.0f см/с² = зона %s %.0f × %.2f"), Gravity.Acceleration.Size(),
										*Gravity.Zone->GetActorNameOrLabel(), Gravity.Zone->GravityCm, OSGetGravityScale())
									: FString::Printf(TEXT("у пешки: невесомость (вне зон), множитель × %.2f"), OSGetGravityScale()));
							})
						]
						// The search: what it looks through and how much it found.
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SBox)
							.Visibility_Lambda([this]() { return !Search.IsEmpty() && Found > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
							[
								MenuOnLine(GridLine, 4.0f, MenuLabel(), SNew(STextBlock).Font(MenuLabel()).ColorAndOpacity(MenuAccent)
								.Text_Lambda([this]() { return MenuText_(FString::Printf(TEXT("ПОИСК ПО ВСЕМ ВКЛАДКАМ · НАЙДЕНО %d"), Found)); }))
							]
						]
						+ SVerticalBox::Slot().FillHeight(1.0f)
						[
							SNew(SOverlay)
							+ SOverlay::Slot()
							[
								SAssignNew(List, SListView<TSharedPtr<FOSDebugRow>>)
								.ListItemsSource(&Rows)
								.SelectionMode(ESelectionMode::None)
								.ScrollBarStyle(&Look.Scroll)
								.OnGenerateRow(this, &SOSDebugMenu::MakeRow)
							]
							// The open group's header stays at the top while its rows scroll under it.
							+ SOverlay::Slot().VAlign(VAlign_Top).Padding(0.0f, 0.0f, 6.0f, 0.0f)
							[
								SAssignNew(StickyBox, SBox).Visibility(EVisibility::Collapsed)
							]
							// Nothing to show: say why, and what the search looks through.
							+ SOverlay::Slot().VAlign(VAlign_Top).Padding(0.0f, 8.0f, 8.0f, 0.0f)
							[
								SNew(STextBlock)
								.Font(MenuFont())
								.LineHeightPercentage(MenuLineHeight(MenuFont(), GridLine))
								.ColorAndOpacity(MenuDim)
								.AutoWrapText(true)
								.Visibility_Lambda([this]() { return Rows.IsEmpty() && Tab != TEXT("Главное") ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
								.Text_Lambda([this]()
								{
									return MenuText_(!Search.IsEmpty()
										? FString::Printf(TEXT("Ничего не найдено по «%s».\nПоиск смотрит имена параметров и групп во всех вкладках%s."), *Search,
											bChangedOnly ? TEXT(" - и только среди изменённых") : TEXT(""))
										: bChangedOnly ? FString(TEXT("Здесь ничего не изменено.")) : FString(TEXT("Здесь нет параметров.")));
								})
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f).HAlign(HAlign_Left)
						[
							SNew(SBox).HeightOverride(GridRow).VAlign(VAlign_Center)
							// Only when the pins differ from the default list.
							.Visibility_Lambda([this]()
							{
								const UOSDebugSubsystem* D = Debug.Get();
								return Search.IsEmpty() && Tab == TEXT("Главное") && D && D->GetPins() != UOSDebugSubsystem::GetDefaultPins() ? EVisibility::Visible : EVisibility::Collapsed;
							})
							[
								MenuButton(TEXT("Закрепить по умолчанию"), [this]() { if (UOSDebugSubsystem* D = Debug.Get()) { D->RestoreDefaultPins(); } })
							]
						]
					]
					+ SWidgetSwitcher::Slot()
					[
						SNew(SScrollBox).ScrollBarStyle(&Look.Scroll).ScrollBarThickness(FVector2f(3.0f, 3.0f))
						+ SScrollBox::Slot().Padding(0.0f, 0.0f, 8.0f, 0.0f)[SAssignNew(PresetBox, SVerticalBox)]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
				[
					MenuHairline()
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MenuOnLine(GridLine - 1.0f, 3.0f, MenuFont(TypeCaption), SNew(STextBlock).Font(MenuFont(TypeCaption)).ColorAndOpacity(MenuDim).Text_Lambda([this]()
					{
						const UOSDebugSubsystem* D = Debug.Get();
						float Fps = 0.0f, Ms = 0.0f, MaxMs = 0.0f;
						if (D)
						{
							D->Measure(1.0f, Fps, Ms, MaxMs);
						}
						return MenuText_(FString::Printf(TEXT("%s  ·  %s  ·  %.1f FPS  ·  Разработка"), *UOSDebugSubsystem::GetBuildHash(),
							*MenuMapName(D ? D->GetGameInstance()->GetWorld() : nullptr), Fps));
					}))
				]
			]
		]
		+ SOverlay::Slot()
		[
			SNew(SOSFrame)
		];
}

TSharedRef<SWidget> SOSDebugMenu::MakeTabButton(const FString& Name)
{
	const auto IsOpen = [this, Name]() { return Search.IsEmpty() && Tab == Name; };
	return SNew(SCheckBox)
		.Style(&FOSMenuLook::Get().Tab)
		.IsChecked_Lambda([IsOpen]() { return IsOpen() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([this, Name](ECheckBoxState) { ShowTab(Name); })
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(6.0f, 0.0f)
			[
				MenuOnLine(GridTab - 2.0f, GridBaseline, MenuMedium(), SNew(STextBlock).Font(MenuMedium()).ColorAndOpacity(FSlateColor::UseForeground()).Text(MenuText_(Name)))
			]
			// The open tab's underline, on the bar's hairline.
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).HeightOverride(2.0f)
				[
					SNew(SImage).Image(MenuWhite()).ColorAndOpacity_Lambda([IsOpen]() { return FSlateColor(IsOpen() ? MenuAccent : MenuClear); })
				]
			]
		];
}

TSharedRef<SWidget> SOSDebugMenu::MakeChoices(const FString& Key, const TArray<TPair<FString, double>>& Options)
{
	const TWeakObjectPtr<UOSDebugSubsystem> Weak = Debug;
	TSharedRef<SWrapBox> Row = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4.0f, 4.0f));
	for (const TPair<FString, double>& Option : Options)
	{
		const double Value = Option.Value;
		Row->AddSlot().VAlign(VAlign_Center)
		[
			SNew(SCheckBox)
			.Style(&FOSMenuLook::Get().Chip)
			.IsChecked_Lambda([Weak, Key, Value]()
			{
				double Now = -1.0;
				return Weak.IsValid() && Weak->GetNumber(Key, Now) && FMath::IsNearlyEqual(Now, Value) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([Weak, Key, Value](ECheckBoxState)
			{
				if (UOSDebugSubsystem* D = Weak.Get())
				{
					D->SetValue(Key, FString::SanitizeFloat(Value));
				}
			})
			[
				SNew(SBox).MinDesiredWidth(24.0f).HAlign(HAlign_Center)
				[
					SNew(STextBlock).Font(FMath::IsNearlyZero(Value) && Option.Key != TEXT("0") ? MenuFont() : MenuMono()).ColorAndOpacity(FSlateColor::UseForeground()).Text(MenuText_(Option.Key))
				]
			]
		];
	}
	return MenuRow(GridRow, Row);
}

TSharedRef<SWidget> SOSDebugMenu::MakeQuickControls()
{
	const TWeakObjectPtr<UOSDebugSubsystem> Weak = Debug;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)[MenuSection(TEXT("Система"))]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.42f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(MenuFont()).ColorAndOpacity(MenuText).Text(MenuText_(TEXT("Лимит FPS")))
			]
			+ SHorizontalBox::Slot().FillWidth(0.58f)
			[
				MakeChoices(TEXT("System.FpsLimit"), { { TEXT("30"), 30.0 }, { TEXT("60"), 60.0 }, { TEXT("144"), 144.0 }, { TEXT("нет"), 0.0 } })
			]
		]
		+ SVerticalBox::Slot().AutoHeight()[MakeKeyLine(TEXT("System.Vsync"), TEXT("Vsync"))]
		+ SVerticalBox::Slot().AutoHeight()[MakeKeyLine(TEXT("System.TimeScale"), TEXT("Скорость игры"))]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).HeightOverride(GridRow)
			[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(&FOSMenuLook::Get().Button)
				.OnClicked_Lambda([Weak]() { if (UOSDebugSubsystem* D = Weak.Get()) { D->SetPaused(!D->IsPaused()); } return FReply::Handled(); })
				[
					SNew(STextBlock).Font(MenuFont()).ColorAndOpacity(FSlateColor::UseForeground())
					.Text_Lambda([Weak]() { return MenuText_(Weak.IsValid() && Weak->IsPaused() ? TEXT("продолжить") : TEXT("пауза")); })
				]
			]
			// What is measured, next to what is set (principle 8).
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(10.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock).Font(MenuMono(TypeCaption)).ColorAndOpacity(MenuDim).Text_Lambda([Weak]()
				{
					float Fps = 0.0f, Ms = 0.0f, MaxMs = 0.0f;
					const UWorld* World = Weak.IsValid() ? Weak->GetGameInstance()->GetWorld() : nullptr;
					const AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
					if (Weak.IsValid())
					{
						Weak->Measure(1.0f, Fps, Ms, MaxMs);
					}
					return MenuText_(FString::Printf(TEXT("%.0f FPS · %.1f мс · время ×%.2f"), Fps, Ms, Settings ? Settings->GetEffectiveTimeDilation() : 1.0f));
				})
			]
			]
		];
}

TSharedRef<SWidget> SOSDebugMenu::MakeSystemTab()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	const TWeakObjectPtr<UOSDebugSubsystem> Weak = Debug;
	const auto Section = [&Box](const FString& Title)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, Box->NumSlots() == 0 ? 0.0f : 16.0f, 0.0f, 4.0f)[MenuSection(Title)];
	};
	// What is measured, next to what is set (principle 8).
	const auto Note = [&Box](TFunction<FString()> Text)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock).Font(MenuFont(TypeCaption)).LineHeightPercentage(MenuLineHeight(MenuFont(TypeCaption), GridCaption))
			.ColorAndOpacity(MenuDim).AutoWrapText(true).Text_Lambda([Text]() { return MenuText_(Text()); })
		];
	};
	const auto Choices = [this, &Box](const FString& Key, const TArray<TPair<FString, double>>& Options)
	{
		Box->AddSlot().AutoHeight()[MakeChoices(Key, Options)];
	};
	const auto Line = [this, &Box](const FString& Key, const FString& Label)
	{
		Box->AddSlot().AutoHeight()[MakeKeyLine(Key, Label)];
	};
	const auto CVar = [](const TCHAR* Name) { const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name); return Variable ? Variable->GetFloat() : -1.0f; };

	Section(TEXT("Лимит кадров"));
	Choices(TEXT("System.FpsLimit"), { { TEXT("30"), 30.0 }, { TEXT("60"), 60.0 }, { TEXT("120"), 120.0 }, { TEXT("144"), 144.0 }, { TEXT("нет"), 0.0 } });
	Line(TEXT("System.FpsLimit"), TEXT("своё, FPS (0 - нет)"));
	Note([Weak, CVar]()
	{
		float Fps = 0.0f, Ms = 0.0f, MaxMs = 0.0f;
		if (Weak.IsValid())
		{
			Weak->Measure(1.0f, Fps, Ms, MaxMs);
		}
		return FString::Printf(TEXT("t.MaxFPS %g · измерено %.1f FPS · %.2f мс, макс. %.1f мс"), CVar(TEXT("t.MaxFPS")), Fps, Ms, MaxMs);
	});
	Section(TEXT("Vsync"));
	Line(TEXT("System.Vsync"), TEXT("vsync"));
	Note([CVar]()
	{
		return FString::Printf(TEXT("r.VSync %g%s"), CVar(TEXT("r.VSync")),
			GIsEditor ? *FString::Printf(TEXT(" · в PIE кадром управляет редактор: r.VSyncEditor %g"), CVar(TEXT("r.VSyncEditor"))) : TEXT(""));
	});
	Section(TEXT("Шаг симуляции"));
	Choices(TEXT("System.FixedStep"), { { TEXT("выкл"), 0.0 }, { TEXT("30"), 30.0 }, { TEXT("60"), 60.0 }, { TEXT("144"), 144.0 } });
	Line(TEXT("System.FixedStep"), TEXT("фикс. шаг, Гц"));
	Note([]()
	{
		return FString::Printf(TEXT("FApp: %s · dt сейчас %.2f мс · для проверки и повтора, не для игры: не успевает машина - игра замедляется"),
			FApp::UseFixedTimeStep() ? *FString::Printf(TEXT("шаг %.2f мс"), FApp::GetFixedDeltaTime() * 1000.0) : TEXT("свободный шаг"), FApp::GetDeltaTime() * 1000.0);
	});
	Line(TEXT("System.HitchMs"), TEXT("хитч, мс"));
	Line(TEXT("System.HitchEverySeconds"), TEXT("каждые, с"));
	Note([Weak]()
	{
		float Fps = 0.0f, Ms = 0.0f, MaxMs = 0.0f;
		if (Weak.IsValid())
		{
			Weak->Measure(5.0f, Fps, Ms, MaxMs);
		}
		return FString::Printf(TEXT("самый длинный кадр за 5 с: %.0f мс"), MaxMs);
	});
	Section(TEXT("Время"));
	Line(TEXT("System.TimeScale"), TEXT("масштаб"));
	{
		TSharedRef<SWrapBox> Row = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4.0f, 4.0f));
		Row->AddSlot()
		[
			SNew(SButton)
			.ButtonStyle(&FOSMenuLook::Get().Button)
			.OnClicked_Lambda([Weak]() { if (UOSDebugSubsystem* D = Weak.Get()) { D->SetPaused(!D->IsPaused()); } return FReply::Handled(); })
			[
				SNew(STextBlock).Font(MenuFont()).ColorAndOpacity(FSlateColor::UseForeground())
				.Text_Lambda([Weak]() { return MenuText_(Weak.IsValid() && Weak->IsPaused() ? TEXT("продолжить") : TEXT("пауза")); })
			]
		];
		Row->AddSlot()[MenuButton(TEXT("шаг на кадр"), [Weak]() { if (UOSDebugSubsystem* D = Weak.Get()) { D->StepFrame(); } },
			TAttribute<bool>::CreateLambda([Weak]() { return Weak.IsValid() && Weak->IsPaused(); }))];
		Box->AddSlot().AutoHeight()[MenuRow(GridRow, Row)];
	}
	Note([Weak]()
	{
		const UWorld* World = Weak.IsValid() ? Weak->GetGameInstance()->GetWorld() : nullptr;
		const AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
		return FString::Printf(TEXT("скорость игры сейчас × %.2f (с замедлением V)%s"), Settings ? Settings->GetEffectiveTimeDilation() : 1.0f,
			Weak.IsValid() && Weak->IsPaused() ? TEXT(" · пауза") : TEXT(""));
	});
	Section(TEXT("Оверлей"));
	Note([Weak]()
	{
		float Fps = 0.0f, Ms = 0.0f, MaxMs = 0.0f;
		if (Weak.IsValid())
		{
			Weak->Measure(1.0f, Fps, Ms, MaxMs);
		}
		return FString::Printf(TEXT("%.1f FPS · кадр %.2f мс · dt игры за 5 с:"), Fps, Ms);
	});
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[SNew(SOSDtGraph).Debug(Weak)];
	return Box;
}

TSharedRef<SWidget> SOSDebugMenu::MakeKeyLine(const FString& Key, const FString& Label)
{
	const UOSDebugSubsystem* DebugMenu = Debug.Get();
	TArray<FOSDebugParam> Params;
	if (DebugMenu)
	{
		DebugMenu->ListParams(Params);
	}
	for (const FOSDebugParam& Param : Params)
	{
		if (Param.Key == Key)
		{
			return MakeParamLine(Param, Label);
		}
	}
	return SNew(STextBlock).Font(MenuFont()).ColorAndOpacity(MenuError).Text(MenuText_(TEXT("нет параметра ") + Key));
}

TSharedRef<SWidget> SOSDebugMenu::MakeParamLine(const FOSDebugParam& Param, const FString& Label, const FString& InUnit)
{
	const FOSMenuLook& Look = FOSMenuLook::Get();
	const FString Key = Param.Key;
	const TWeakObjectPtr<UOSDebugSubsystem> Weak = Debug;
	TSharedPtr<SWidget> Editor;
	const FNumericProperty* Numeric = CastField<FNumericProperty>(Param.Property);
	if (Numeric && !Numeric->IsEnum())
	{
		// A number: drag or type; with a known range the box is a slider.
		const bool bInteger = Numeric->IsInteger();
		static const TSharedPtr<INumericTypeInterface<double>> Numbers = MakeShared<FOSMenuNumbers>();
		Editor = SNew(SSpinBox<double>)
			.Style(&Look.Spin)
			.TypeInterface(Numbers)
			.Font(MenuMono())
			.MinValue(Param.Min)
			.MaxValue(Param.Max)
			.MinSliderValue(Param.SliderMin)
			.MaxSliderValue(Param.SliderMax)
			.Delta(bInteger ? 1.0 : 0.0)
			.Value_Lambda([Weak, Key]()
			{
				double Value = 0.0;
				if (Weak.IsValid())
				{
					Weak->GetNumber(Key, Value);
				}
				return Value;
			})
			.OnBeginSliderMovement_Lambda([this]() { bDragging = true; })
			.OnValueChanged_Lambda([this, Key, bInteger](double Value) { EditNumber(Key, MenuNumberText(Value, bInteger)); })
			.OnEndSliderMovement_Lambda([this](double) { bDragging = false; FlushEdit(); })
			.OnValueCommitted_Lambda([this, Key, bInteger](double Value, ETextCommit::Type)
			{
				EditNumber(Key, MenuNumberText(Value, bInteger));
				bDragging = false;
				FlushEdit();
			});
	}
	else if (Param.Property && Param.Property->IsA<FBoolProperty>())
	{
		Editor = SNew(SBox).HAlign(HAlign_Left)
			[
				SNew(SCheckBox)
				.Style(&Look.Check)
				.IsChecked_Lambda([Weak, Key]()
				{
					FString Value;
					return Weak.IsValid() && Weak->GetValue(Key, Value) && Value == TEXT("True") ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([Weak, Key](ECheckBoxState State)
				{
					if (UOSDebugSubsystem* D = Weak.Get())
					{
						D->SetValue(Key, State == ECheckBoxState::Checked ? TEXT("True") : TEXT("False"));
					}
				})
			];
	}
	else
	{
		// Enums, vectors, colours: their UE text.
		Editor = SNew(SEditableTextBox)
			.Style(&Look.Field)
			.Font(MenuMono())
			.Text_Lambda([Weak, Key]()
			{
				FString Value;
				if (Weak.IsValid())
				{
					Weak->GetValue(Key, Value);
				}
				return MenuText_(Value);
			})
			.OnTextCommitted_Lambda([Weak, Key](const FText& Text, ETextCommit::Type Commit)
			{
				UOSDebugSubsystem* D = Weak.Get();
				FString Error;
				if (D && Commit != ETextCommit::OnCleared && !D->SetValue(Key, Text.ToString(), &Error))
				{
					D->Say(Key + TEXT(": ") + Error, true);
				}
			});
	}
	// The key stays one hover away: the exports and the console speak in keys.
	FString Tip = Key;
	if (!Param.Tooltip.IsEmpty())
	{
		Tip += TEXT("\n") + Param.Tooltip;
	}
	if (Param.Min.IsSet() || Param.Max.IsSet())
	{
		Tip += FString::Printf(TEXT("\n[%s .. %s]"), Param.Min.IsSet() ? *MenuNumber(Param.Min.GetValue()) : TEXT(""),
			Param.Max.IsSet() ? *MenuNumber(Param.Max.GetValue()) : TEXT(""));
	}
	const FString Unit = InUnit.IsEmpty() ? Param.Units : InUnit;
	const FText Highlight = MenuText_(Search);
	return SNew(SBox).HeightOverride(Search.IsEmpty() ? GridRow : GridRowTwo)
	[
	SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.6f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Font(MenuFont())
				.Text(MenuText_(Label))
				.ToolTipText(MenuText_(Tip))
				.HighlightText(Highlight)
				.HighlightShape(&FOSMenuLook::Get().Highlight).HighlightColor(MenuCore)
				.ColorAndOpacity_Lambda([Weak, Key]() { return FSlateColor(Weak.IsValid() && Weak->IsChanged(Key) ? MenuChanged : MenuText); })
			]
			// A search also matches keys: the property name (the key without its owner, which heads the group), muted, under the name.
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Visibility(Search.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				.Font(MenuFont(TypeCaption))
				.ColorAndOpacity(MenuDim)
				.Text(MenuText_(Param.Property ? Param.Property->GetName() : Key))
				.HighlightText(Highlight)
				.HighlightShape(&FOSMenuLook::Get().Highlight).HighlightColor(MenuCore)
			]
		]
		+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)
		[
			Editor.ToSharedRef()
		]
		// Units at the body size: label, value and unit share one baseline.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox).WidthOverride(56.0f)
			[
				SNew(STextBlock).Font(MenuFont()).ColorAndOpacity(MenuDim).Text(MenuText_(Unit))
			]
		]
		// The changed mark: the tooltip tells the original, a click puts it back.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.ButtonStyle(&Look.Ghost)
			.Visibility_Lambda([Weak, Key]() { return Weak.IsValid() && Weak->IsChanged(Key) ? EVisibility::Visible : EVisibility::Hidden; })
			.ToolTipText_Lambda([Weak, Key]()
			{
				const FString* Original = Weak.IsValid() ? Weak->GetOriginal(Key) : nullptr;
				return MenuText_(FString::Printf(TEXT("было: %s - клик вернёт"), Original ? **Original : TEXT("?")));
			})
			.OnClicked_Lambda([Weak, Key]() { if (UOSDebugSubsystem* D = Weak.Get()) { D->Reset(Key); } return FReply::Handled(); })
			[
				SNew(STextBlock).Font(MenuFont(TypeCaption)).ColorAndOpacity(MenuChanged).Text(MenuText_(TEXT("●")))
			]
		]
		// The star pins it to the compact panel (Shift+F6).
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(&Look.Ghost)
			.ToolTipText(MenuText_(TEXT("закрепить (Shift+F6)")))
			.OnClicked_Lambda([Weak, Key]() { if (UOSDebugSubsystem* D = Weak.Get()) { D->SetPinned(Key, !D->IsPinned(Key)); } return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Font(MenuFont())
				.ColorAndOpacity_Lambda([Weak, Key]() { return FSlateColor(Weak.IsValid() && Weak->IsPinned(Key) ? MenuCore : MenuDim.CopyWithNewOpacity(0.7f)); })
				.Text_Lambda([Weak, Key]() { return MenuText_(Weak.IsValid() && Weak->IsPinned(Key) ? TEXT("★") : TEXT("☆")); })
			]
		]
	];
}

TSharedRef<SWidget> SOSDebugMenu::MakeGroupHeader(TSharedPtr<FOSDebugRow> Row)
{
	const FOSMenuLook& Look = FOSMenuLook::Get();
	const FString Group = Row->Header;
	const FString Owner = Row->Param.Owner;
	const TWeakObjectPtr<UOSDebugSubsystem> Weak = Debug;
	// A search shows every match: its groups are open and do not fold.
	const bool bOpen = !Search.IsEmpty() || OpenGroups.Contains(Group);
	const TArray<FString> Keys = Row->Keys;
	const auto ChangedIn = [Weak, Keys]()
	{
		int32 Changed = 0;
		for (const FString& Key : Keys)
		{
			Changed += Weak.IsValid() && Weak->IsChanged(Key) ? 1 : 0;
		}
		return Changed;
	};
	return SNew(SButton)
		.ButtonStyle(&Look.Ghost)
		.ContentPadding(FMargin(0.0f))
		.ToolTipText(MenuText_(Group))
		.OnClicked_Lambda([this, Group]()
		{
			if (!Search.IsEmpty())
			{
				return FReply::Handled();
			}
			const int32 At = Rows.IndexOfByPredicate([&Group](const TSharedPtr<FOSDebugRow>& Each) { return Each->Kind == FOSDebugRow::EKind::Group && Each->Header == Group; });
			ToggleGroup(Group);
			if (At != INDEX_NONE && List.IsValid() && List->GetScrollOffset() > At)
			{
				List->SetScrollOffset(At);
			}
			return FReply::Handled();
		})
		[
			SNew(SBorder)
			.BorderImage(MenuWhite())
			.BorderBackgroundColor(MenuInk.CopyWithNewOpacity(1.0f))
			.Padding(0.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					// The arrow's cells (rows 2-6, 7 px over their own baseline) sit at the lower-case letters' middle: its baseline 3 units under the text's.
					+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SOSPribor).Text(bOpen ? TEXT("▾") : TEXT("▸")).Color(bOpen ? MenuAccent : MenuDim).LineHeight(GridGroup - 1.0f).Baseline(GridBaseline - 3.0f)
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						MenuOnLine(GridGroup - 1.0f, GridBaseline, MenuMedium(TypeHeading), SNew(STextBlock).Font(MenuMedium(TypeHeading)).ColorAndOpacity(bOpen ? MenuCore : MenuText)
						.Text(MenuText_(MenuGroupTitle(Group, Owner)))
						.HighlightText_Lambda([this]() { return MenuText_(Search); })
						.HighlightShape(&FOSMenuLook::Get().Highlight).HighlightColor(MenuCore))
					]
					// The owner path, muted: where this lives in the code and the config keys.
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(8.0f, 0.0f, 6.0f, 0.0f)
					[
						MenuOnLine(GridGroup - 1.0f, GridBaseline, MenuFont(TypeCaption), SNew(STextBlock).Font(MenuFont(TypeCaption)).ColorAndOpacity(MenuDim).Clipping(EWidgetClipping::ClipToBounds)
						.Text(MenuText_(Group.Replace(TEXT("|"), TEXT(" / "))))
						.HighlightText_Lambda([this]() { return MenuText_(Search); })
						.HighlightShape(&FOSMenuLook::Get().Highlight).HighlightColor(MenuCore))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(SBox)
						.Visibility_Lambda([ChangedIn]() { return ChangedIn() > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
						[
							MenuOnLine(GridGroup - 1.0f, GridBaseline, MenuMono(TypeCaption), SNew(STextBlock).Font(MenuMono(TypeCaption)).ColorAndOpacity(MenuChanged)
							.Text_Lambda([ChangedIn]() { return MenuText_(FString::Printf(TEXT("● %d"), ChangedIn())); }))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						MenuOnLine(GridGroup - 1.0f, GridBaseline, MenuMono(TypeCaption), SNew(STextBlock).Font(MenuMono(TypeCaption)).ColorAndOpacity(MenuDim).Text(MenuText_(FString::FromInt(Row->Count))))
					]
					// Only a group with changes offers its reset, and it resets that group only.
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox)
						.Visibility_Lambda([ChangedIn]() { return ChangedIn() > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
						[
							MenuTool(TEXT("сбросить"), TEXT("правки этой группы"), [Weak, Keys]()
							{
								for (const FString& Key : Keys)
								{
									if (UOSDebugSubsystem* D = Weak.Get(); D && D->IsChanged(Key))
									{
										D->Reset(Key);
									}
								}
							})
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight()[MenuHairline(bOpen ? MenuAccent.CopyWithNewOpacity(0.6f) : MenuHair)]
			]
		];
}

TSharedRef<ITableRow> SOSDebugMenu::MakeRow(TSharedPtr<FOSDebugRow> Row, const TSharedRef<STableViewBase>& Table)
{
	const FOSMenuLook& Look = FOSMenuLook::Get();
	if (Row->Kind == FOSDebugRow::EKind::Group)
	{
		return SNew(STableRow<TSharedPtr<FOSDebugRow>>, Table)
			.Style(&Look.Row)
			.Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
			[
				MakeGroupHeader(Row)
			];
	}
	if (Row->Kind == FOSDebugRow::EKind::Section)
	{
		return SNew(STableRow<TSharedPtr<FOSDebugRow>>, Table)
			.Style(&Look.Row)
			.Padding(FMargin(0.0f, 8.0f, 6.0f, 0.0f))
			[
				MenuSection(Row->Header)
			];
	}
	if (Row->Kind == FOSDebugRow::EKind::Actions)
	{
		TSharedRef<SWrapBox> Buttons = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4.0f, 2.0f));
		for (const FOSDebugRow::FAction& Action : Row->Actions)
		{
			Buttons->AddSlot()[MenuActionButton(Action)];
		}
		return SNew(STableRow<TSharedPtr<FOSDebugRow>>, Table)
			.Style(&Look.Row)
			.Padding(FMargin(0.0f, 1.0f, 6.0f, 1.0f))
			[
				SNew(SBox).MinDesiredHeight(GridRow)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(0.3f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(STextBlock).Font(MenuFont()).ColorAndOpacity(MenuText).Text(MenuText_(Row->Label))
					]
					+ SHorizontalBox::Slot().FillWidth(0.7f).VAlign(VAlign_Center)
					[
						Buttons
					]
				]
			];
	}
	// Rows of an open group sit in from its header.
	const bool bInGroup = Search.IsEmpty() ? Tab != TEXT("Главное") : true;
	return SNew(STableRow<TSharedPtr<FOSDebugRow>>, Table)
		.Style(&Look.Row)
		.Padding(FMargin(bInGroup ? 14.0f : 0.0f, 0.0f, 6.0f, 0.0f))
		[
			MakeParamLine(Row->Param, Row->Label, Row->Unit)
		];
}

void SOSDebugMenu::Rebuild()
{
	Rows.Reset();
	Found = 0;
	UOSDebugSubsystem* DebugMenu = Debug.Get();
	if (DebugMenu)
	{
		TArray<FOSDebugParam> Params;
		DebugMenu->ListParams(Params);
		const auto InTab = [this](const FOSDebugParam& Param)
		{
			const FString& Owner = Param.Owner;
			if (Tab == TEXT("Мир"))
			{
				return Owner == TEXT("World") || Owner.StartsWith(TEXT("Zone["));
			}
			if (Tab == TEXT("Игрок"))
			{
				return (Owner == TEXT("Pawn") || Owner.StartsWith(TEXT("Pawn."))) && Owner != TEXT("Pawn.Guns");
			}
			if (Tab == TEXT("Оружие"))
			{
				return Owner.StartsWith(TEXT("Weapon[")) || Owner == TEXT("Pawn.Guns");
			}
			if (Tab == TEXT("Враги"))
			{
				return Owner == TEXT("Flyer") || Owner.StartsWith(TEXT("Habitat["));
			}
			return false;
		};
		const auto Label = [](const FOSDebugParam& Param)
		{
			FString Name, Unit;
			MenuHumanName(Param, false, Name, Unit);
			return Name;
		};
		const auto Add = [this](const FOSDebugParam& Param, bool bPinned)
		{
			const TSharedPtr<FOSDebugRow> Row = MakeShared<FOSDebugRow>();
			Row->Param = Param;
			MenuHumanName(Param, bPinned, Row->Label, Row->Unit);
			Rows.Add(Row);
		};
		if (Search.IsEmpty() && Tab == TEXT("Главное"))
		{
			// The pins by section (Мир, Взгляд, Движение...), in pin order inside one, with human names.
			TArray<const FOSDebugParam*> Pinned;
			for (const FString& Pin : DebugMenu->GetPins())
			{
				const FOSDebugParam* Param = Params.FindByPredicate([&Pin](const FOSDebugParam& Each) { return Each.Key == Pin; });
				if (Param && (!bChangedOnly || DebugMenu->IsChanged(Pin)))
				{
					Pinned.Add(Param);
				}
			}
			Algo::StableSortBy(Pinned, [](const FOSDebugParam* Param) { return MenuSectionRank(MenuSectionOf(*Param)); });
			FString Section;
			for (const FOSDebugParam* Param : Pinned)
			{
				if (MenuSectionOf(*Param) != Section)
				{
					Section = MenuSectionOf(*Param);
					const TSharedPtr<FOSDebugRow> Header = MakeShared<FOSDebugRow>();
					Header->Kind = FOSDebugRow::EKind::Section;
					Header->Header = Section;
					Rows.Add(Header);
				}
				Add(*Param, true);
			}
		}
		else
		{
			// «Игрок» opens with the gear (guns, attachments): buttons, not parameters, so a search skips it.
			if (Search.IsEmpty() && Tab == TEXT("Игрок") && !bChangedOnly)
			{
				MenuGearRows(Debug, Rows);
			}
			// Groups in the curated order: by owner as listed (the pawn, its parts, each gun, the flyers, the map's
			// actors), inside an owner the most tuned first; a gun's recoil leads its rows.
			struct FGroup
			{
				FString Key;
				FString Owner;
				int32 OwnerOrder = 0;
				int32 Rank = 0;
				TArray<const FOSDebugParam*> Params;
				int32 Total = 0;
				TArray<FString> Keys;
			};
			TArray<FGroup> Groups;
			TArray<FString> Owners;
			for (const FOSDebugParam& Param : Params)
			{
				const bool bShown = Search.IsEmpty() ? InTab(Param)
					: (Param.Key.Contains(Search) || Param.Group.Contains(Search) || MenuGroupTitle(Param.Group, Param.Owner).Contains(Search) || Label(Param).Contains(Search));
				if (!bShown)
				{
					continue;
				}
				FGroup* Group = Groups.FindByPredicate([&Param](const FGroup& Each) { return Each.Key == Param.Group; });
				if (!Group)
				{
					Group = &Groups.AddDefaulted_GetRef();
					Group->Key = Param.Group;
					Group->Owner = Param.Owner;
					Group->OwnerOrder = Owners.AddUnique(Param.Owner);
					Group->Rank = Param.Group == Param.Owner ? -1 : MenuGroupRank(Param.Group);
				}
				++Group->Total;
				Group->Keys.Add(Param.Key);
				if (!bChangedOnly || DebugMenu->IsChanged(Param.Key))
				{
					Group->Params.Add(&Param);
				}
			}
			Groups.RemoveAll([](const FGroup& Each) { return Each.Params.IsEmpty(); });
			// The shared gun settings (Pawn.Guns) come after the guns themselves.
			Algo::StableSortBy(Groups, [](const FGroup& Each) { return (Each.Owner == TEXT("Pawn.Guns") ? 500 : Each.OwnerOrder) * 1000 + Each.Rank; });
			for (FGroup& Group : Groups)
			{
				const TSharedPtr<FOSDebugRow> Header = MakeShared<FOSDebugRow>();
				Header->Kind = FOSDebugRow::EKind::Group;
				Header->Header = Group.Key;
				Header->Param.Owner = Group.Params[0]->Owner;
				Header->Count = Group.Total;
				Header->Keys = Group.Keys;
				Rows.Add(Header);
				Found += Group.Params.Num();
				if (!Search.IsEmpty() || OpenGroups.Contains(Group.Key))
				{
					Algo::StableSortBy(Group.Params, [](const FOSDebugParam* Param) { return Param->Property->GetName().Contains(TEXT("Recoil")) ? 0 : 1; });
					for (const FOSDebugParam* Param : Group.Params)
					{
						Add(*Param, false);
					}
				}
			}
		}
		SeenGeneration = DebugMenu->GetGeneration();
		SeenChanged = DebugMenu->GetChangedCount();
		SeenPins = DebugMenu->GetPins();
	}
	Sticky.Reset();
	if (StickyBox.IsValid())
	{
		StickyBox->SetVisibility(EVisibility::Collapsed);
	}
	if (List.IsValid())
	{
		List->RequestListRefresh();
	}
}

void SOSDebugMenu::ToggleGroup(const FString& Group)
{
	if (OpenGroups.Remove(Group) == 0)
	{
		OpenGroups.Add(Group);
	}
	SaveGroups();
	Rebuild();
}

void SOSDebugMenu::SetGroups(const FString& Text)
{
	if (Text == TEXT("none"))
	{
		OpenGroups.Reset();
	}
	else
	{
		for (const TSharedPtr<FOSDebugRow>& Row : Rows)
		{
			if (Row->Kind == FOSDebugRow::EKind::Group
				&& (Text == TEXT("all") || Row->Header.Contains(Text) || MenuGroupTitle(Row->Header, Row->Param.Owner).Contains(Text)))
			{
				OpenGroups.Add(Row->Header);
			}
		}
	}
	SaveGroups();
	Rebuild();
}

void SOSDebugMenu::SaveGroups() const
{
	FFileHelper::SaveStringArrayToFile(OpenGroups.Array(), *MenuOpenGroupsFile(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void SOSDebugMenu::RebuildPresets()
{
	UOSDebugSubsystem* DebugMenu = Debug.Get();
	if (!PresetBox.IsValid() || !DebugMenu)
	{
		return;
	}
	PresetBox->ClearChildren();
	const TWeakObjectPtr<UOSDebugSubsystem> Weak = Debug;
	PresetBox->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
	[
		MenuSection(TEXT("Пресеты"), MenuTool(TEXT("обновить"), TEXT("перечитать список"), [this]() { RebuildPresets(); }))
	];
	PresetBox->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(STextBlock).Font(MenuFont(TypeCaption)).LineHeightPercentage(MenuLineHeight(MenuFont(TypeCaption), GridCaption)).ColorAndOpacity(MenuDim).AutoWrapText(true)
		.Text(MenuText_(TEXT("Встроенные - ассеты в /Game/OS/Debug/Presets, свои - JSON в Saved/OSDebug/Presets. «Применить» заменяет текущий конфиг, «поверх» добавляет к нему.")))
	];
	TArray<FOSDebugPreset> Presets;
	DebugMenu->ListPresets(Presets);
	for (const FOSDebugPreset& Preset : Presets)
	{
		const FString Name = Preset.Name;
		PresetBox->AddSlot().AutoHeight()
		[
			SNew(SBox).HeightOverride(GridRow)
			[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(MenuFont()).ColorAndOpacity(MenuText).ToolTipText(MenuText_(Preset.Source)).Text(MenuText_(Name))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0.0f)
			[
				SNew(STextBlock).Font(MenuFont(TypeCaption)).ColorAndOpacity(MenuDim).Text(MenuText_(Preset.bAsset ? TEXT("ассет") : TEXT("JSON")))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)[MenuButton(TEXT("применить"), [Weak, Name]() { if (UOSDebugSubsystem* D = Weak.Get()) { D->Import(Name, true); } })]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MenuButton(TEXT("поверх"), [Weak, Name]() { if (UOSDebugSubsystem* D = Weak.Get()) { D->Import(Name, false); } })]
			]
		];
	}
}

TSharedRef<SWidget> SOSDebugMenu::MakePinned()
{
	const TWeakObjectPtr<UOSDebugSubsystem> Weak = Debug;
	return SNew(SBox).MinDesiredWidth(232.0f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage(MenuWhite())
				.BorderBackgroundColor(MenuInkPinned)
				.Padding(FMargin(12.0f, 8.0f, 12.0f, 8.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						MenuRow(GridLine, SNew(STextBlock).Font(MenuMono()).ColorAndOpacity(MenuCore).Text_Lambda([Weak]()
						{
							float Fps = 0.0f, Ms = 0.0f, MaxMs = 0.0f;
							const UWorld* World = Weak.IsValid() ? Weak->GetGameInstance()->GetWorld() : nullptr;
							const AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
							if (Weak.IsValid())
							{
								Weak->Measure(1.0f, Fps, Ms, MaxMs);
							}
							return MenuText_(FString::Printf(TEXT("%.0f FPS  %.1f мс  ×%.2f%s"), Fps, Ms, Settings ? Settings->GetEffectiveTimeDilation() : 1.0f,
								Weak.IsValid() && Weak->IsPaused() ? TEXT("  пауза") : TEXT("")));
						}))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						MenuRow(GridCaption, SNew(SOSDash))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SAssignNew(PinnedBox, SVerticalBox)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						MenuOnLine(GridLine, 4.0f, MenuFont(TypeCaption), SNew(STextBlock).Font(MenuFont(TypeCaption)).ColorAndOpacity(MenuDim).Text(MenuText_(TEXT("F6 — меню  ·  Shift+F6 — скрыть"))))
					]
				]
			]
			+ SOverlay::Slot()
			[
				SNew(SOSFrame).Corner(8.0f).bDashes(false)
			]
		];
}

void SOSDebugMenu::RebuildPinned()
{
	UOSDebugSubsystem* DebugMenu = Debug.Get();
	if (!PinnedBox.IsValid() || !DebugMenu)
	{
		return;
	}
	PinnedBox->ClearChildren();
	const TWeakObjectPtr<UOSDebugSubsystem> Weak = Debug;
	TArray<FOSDebugParam> Params;
	DebugMenu->ListParams(Params);
	// Human names in short sections (Мир, Взгляд, Движение, Рука, Отдача, Выживание, Летуны); the keys are the menu's.
	TArray<FOSDebugParam> Pinned;
	for (const FString& Key : DebugMenu->GetPins())
	{
		if (const FOSDebugParam* Param = Params.FindByPredicate([&Key](const FOSDebugParam& Each) { return Each.Key == Key; }))
		{
			Pinned.Add(*Param);
		}
	}
	Algo::StableSortBy(Pinned, [](const FOSDebugParam& Param) { return MenuSectionRank(MenuSectionOf(Param)); });
	FString Section;
	for (const FOSDebugParam& Param : Pinned)
	{
		const FString Key = Param.Key;
		if (MenuSectionOf(Param) != Section)
		{
			Section = MenuSectionOf(Param);
			PinnedBox->AddSlot().AutoHeight()
			[
				MenuOnLine(20.0f, 4.0f, MenuLabel(), SNew(STextBlock).Font(MenuLabel()).ColorAndOpacity(MenuAccent).Text(MenuText_(Section).ToUpper()))
			];
		}
		FString Label, Unit;
		MenuHumanName(Param, true, Label, Unit);
		const bool bTimes = Unit == TEXT("×");
		const auto Changed = [Weak, Key]() { return Weak.IsValid() && Weak->IsChanged(Key); };
		PinnedBox->AddSlot().AutoHeight()
		[
			SNew(SBox).HeightOverride(GridLine)
			[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(MenuFont()).Text(MenuText_(Label))
				.ColorAndOpacity_Lambda([Changed]() { return FSlateColor(Changed() ? MenuChanged : MenuText); })
			]
			// Numbers right-aligned in mono, the unit after them in a column of its own.
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock).Font(MenuMono()).Justification(ETextJustify::Right)
				.ColorAndOpacity_Lambda([Changed]() { return FSlateColor(Changed() ? MenuChanged : MenuCore); })
				.Text_Lambda([Weak, Key, bTimes]()
				{
					double Number = 0.0;
					FString Value;
					if (!Weak.IsValid())
					{
						return FText::GetEmpty();
					}
					if (Weak->GetNumber(Key, Number))
					{
						return MenuText_(bTimes ? TEXT("×") + MenuNumber(Number) : MenuNumber(Number));
					}
					return MenuText_(Weak->GetValue(Key, Value) ? Value : FString(TEXT("нет")));
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBox).WidthOverride(46.0f)
				[
					SNew(STextBlock).Font(MenuFont()).ColorAndOpacity(MenuDim).Text(MenuText_(bTimes ? FString() : Unit))
				]
			]
			]
		];
	}
	PinnedShown = DebugMenu->GetPins();
}

TSharedRef<SWidget> SOSDebugMenu::MakeWatermark()
{
	// On every frame while the config differs from the original: it gets into screenshots and clips.
	const TWeakObjectPtr<UOSDebugSubsystem> Weak = Debug;
	return SNew(SBorder)
		.BorderImage(MenuWhite())
		.BorderBackgroundColor(MenuInk.CopyWithNewOpacity(0.72f))
		.Padding(0.0f)
		.Visibility_Lambda([Weak]() { return Weak.IsValid() && Weak->GetChangedCount() > 0 ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(2.0f)[SNew(SImage).Image(MenuWhite()).ColorAndOpacity(MenuChanged)]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(10.0f, 4.0f, 8.0f, 4.0f)
			[
				SNew(SOSPribor).Text(TEXT("ТЕСТ")).Color(MenuChanged).LineHeight(GridHeader).Baseline(GridBaseline)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 4.0f, 12.0f, 4.0f)
			[
				MenuOnLine(GridHeader, GridBaseline, MenuMono(), SNew(STextBlock).Font(MenuMono()).ColorAndOpacity(MenuChanged).Text_Lambda([Weak]()
				{
					return Weak.IsValid() ? MenuText_(FString::Printf(TEXT("%s · изменено %d · %s"),
						Weak->GetConfigName().IsEmpty() ? TEXT("без имени") : *Weak->GetConfigName(), Weak->GetChangedCount(), *UOSDebugSubsystem::GetBuildHash())) : FText::GetEmpty();
				}))
			]
		];
}

void SOSDebugMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	ViewScale = AllottedGeometry.Scale;
	ViewHeight = AllottedGeometry.GetLocalSize().Y * AllottedGeometry.Scale;
	UOSDebugSubsystem* DebugMenu = Debug.Get();
	if (!DebugMenu)
	{
		return;
	}
	if (DebugMenu->GetGeneration() != SeenGeneration || (bChangedOnly && DebugMenu->GetChangedCount() != SeenChanged)
		|| (Tab == TEXT("Главное") && DebugMenu->GetPins() != SeenPins))
	{
		Rebuild();
	}
	if (bFocusPending)
	{
		bFocusPending = false;
		FocusSearch();
	}
	if (DebugMenu->GetPins() != PinnedShown)
	{
		RebuildPinned();
	}
	if (DebugMenu->Status != StatusSeen)
	{
		StatusSeen = DebugMenu->Status;
		StatusShown = MenuStatusText(StatusSeen);
		StatusAt = FPlatformTime::Seconds();
	}
	// The sticky header: the last group header at or above the top row, once it has scrolled off.
	if (List.IsValid() && StickyBox.IsValid())
	{
		const double Offset = List->GetScrollOffset();
		int32 Header = INDEX_NONE;
		for (int32 Index = FMath::Min(int32(Offset), Rows.Num() - 1); Index >= 0; --Index)
		{
			if (Rows[Index]->Kind == FOSDebugRow::EKind::Group)
			{
				Header = Index;
				break;
			}
		}
		const TSharedPtr<FOSDebugRow> Now = Header != INDEX_NONE && Offset > Header + 0.05 && Header + 1 < Rows.Num()
			&& Rows[Header + 1]->Kind == FOSDebugRow::EKind::Param ? Rows[Header] : nullptr;
		if (Now != Sticky)
		{
			Sticky = Now;
			if (Sticky.IsValid())
			{
				StickyBox->SetContent(MakeGroupHeader(Sticky));
			}
			StickyBox->SetVisibility(Sticky.IsValid() ? EVisibility::Visible : EVisibility::Collapsed);
		}
	}
	if (SkippedBox.IsValid() && DebugMenu->LastSkipped.Num() != SeenSkipped)
	{
		SeenSkipped = DebugMenu->LastSkipped.Num();
		SkippedBox->ClearChildren();
		if (SeenSkipped > 0)
		{
			TSharedRef<SVerticalBox> Lines = SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).HeightOverride(GridRow)
					[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Font(MenuLabel()).ColorAndOpacity(MenuError).Text(MenuText_(FString::Printf(TEXT("ПРОПУЩЕНО ПРИ ИМПОРТЕ · %d"), SeenSkipped)))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MenuTool(TEXT("скрыть"), TEXT("убрать список"), [this]() { if (UOSDebugSubsystem* D = Debug.Get()) { D->LastSkipped.Reset(); } })]
					]
				];
			TSharedRef<SVerticalBox> Keys = SNew(SVerticalBox);
			for (const FString& Skipped : DebugMenu->LastSkipped)
			{
				Keys->AddSlot().AutoHeight()
				[
					SNew(STextBlock).Font(MenuFont(TypeCaption)).LineHeightPercentage(MenuLineHeight(MenuFont(TypeCaption), GridCaption))
					.ColorAndOpacity(MenuError.CopyWithNewOpacity(0.9f)).AutoWrapText(true).Text(MenuText_(Skipped))
				];
			}
			// A long list scrolls in place and leaves the parameters their room.
			Lines->AddSlot().AutoHeight()
			[
				SNew(SBox).MaxDesiredHeight(5.0f * GridCaption)
				[
					SNew(SScrollBox).ScrollBarStyle(&FOSMenuLook::Get().Scroll).ScrollBarThickness(FVector2f(3.0f, 3.0f)) + SScrollBox::Slot()[Keys]
				]
			];
			SkippedBox->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				SNew(SBorder).BorderImage(MenuWhite()).BorderBackgroundColor(MenuError.CopyWithNewOpacity(0.07f)).Padding(FMargin(10.0f, 0.0f, 6.0f, 8.0f))
				[
					Lines
				]
			];
		}
	}
	// A wheel or a drag on a number has no single "done" event: its undo step is recorded once it settles.
	if (!EditKey.IsEmpty() && !bDragging && FPlatformTime::Seconds() - EditAt > 0.5)
	{
		FlushEdit();
	}
}

void SOSDebugMenu::EditNumber(const FString& Key, const FString& Value)
{
	UOSDebugSubsystem* DebugMenu = Debug.Get();
	if (!DebugMenu)
	{
		return;
	}
	if (EditKey != Key)
	{
		FlushEdit();
		EditKey = Key;
		DebugMenu->GetValue(Key, EditBefore);
	}
	FString Error;
	if (!DebugMenu->SetValue(Key, Value, &Error, /*bRecord=*/false))
	{
		DebugMenu->Say(Key + TEXT(": ") + Error, true);
	}
	EditAt = FPlatformTime::Seconds();
}

void SOSDebugMenu::FlushEdit()
{
	UOSDebugSubsystem* DebugMenu = Debug.Get();
	FString Now;
	if (DebugMenu && !EditKey.IsEmpty() && DebugMenu->GetValue(EditKey, Now) && Now != EditBefore)
	{
		DebugMenu->SetValue(EditKey, Now, nullptr, /*bRecord=*/true, &EditBefore);
	}
	EditKey.Reset();
}

TSharedPtr<SWidget> SOSDebugMenu::GetFocusTarget() const
{
	return SearchBox;
}

void SOSDebugMenu::FocusSearch()
{
	if (SearchBox.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(SearchBox, EFocusCause::SetDirectly);
	}
}

void SOSDebugMenu::OnModeChanged()
{
	// The panel was collapsed until now: the search gets the keys once it is laid out (next tick).
	bFocusPending = Debug.IsValid() && Debug->GetMode() == EOSDebugMode::Open;
	RebuildPinned();
	if (Tab == TEXT("Пресеты"))
	{
		RebuildPresets();
	}
}

void SOSDebugMenu::ShowTab(const FString& Name)
{
	// «Закреплено» became «Главное» (the pins are its body).
	Tab = Name == TEXT("Закреплено") ? FString(TEXT("Главное")) : Name;
	SetSearch(FString());
	if (Tab == TEXT("Пресеты"))
	{
		RebuildPresets();
	}
	Rebuild();
	if (List.IsValid())
	{
		List->ScrollToTop();
	}
}

void SOSDebugMenu::SetSearch(const FString& Text)
{
	Search = Text;
	if (SearchBox.IsValid())
	{
		SearchBox->SetText(MenuText_(Text));
	}
	Rebuild();
}

void SOSDebugMenu::ToggleChangedOnly()
{
	bChangedOnly = !bChangedOnly;
	Rebuild();
}
