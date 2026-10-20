#include "JamDebugMenu.h"

#include "Algo/BinarySearch.h"
#include "Algo/StableSort.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "JamGravity.h"
#include "JamShip.h"
#include "JamZeroGPawn.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
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
	// blue (JamWeldProjector: HaloColour 0.045/0.065/0.15 for the hairlines, CoreColour 0.62/0.74/1.0 for what is
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

	// The game HUD is Canvas, in pixels, at the left (AJamZeroGHUD, AJamShipHUD): the ship's lamps, cargo and air
	// from y 24 to about 125; O2 and HP from ClipY - 82 down; with a hull tool in hand up to three more lines from
	// ClipY - 204. The panel keeps between them. Under MenuToolLinesMinPx of height (720p) it may cover the tool lines
	// (key hints that do nothing while the menu has the input, and the breach readout): O2 and HP stay clear always.
	constexpr float MenuHudTopPx = 132.0f;
	constexpr float MenuHudBottomPx = 212.0f;
	constexpr float MenuVitalsBottomPx = 100.0f;
	constexpr float MenuToolLinesMinPx = 900.0f;
	constexpr float MenuLeftPx = 16.0f;
	// The engine's DPI curve gives 0.67 at 720p: the panel's text was 9 px there (KNOWN_ISSUES B6). The menu never
	// goes under 1: its small type keeps its pixel size at 720p and grows with the curve above 1080p.
	constexpr float MenuMinScale = 1.0f;

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
	const FSlateBrush* MenuWhite() { return FCoreStyle::Get().GetBrush("WhiteBrush"); }
	FText MenuText_(const FString& Text) { return FText::FromString(Text); }

	/** The widget styles, built once: hairline outlines, square corners, quiet hover, no gloss. */
	struct FJamMenuLook
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

		static const FJamMenuLook& Get()
		{
			static const FJamMenuLook Look;
			return Look;
		}

		FJamMenuLook()
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
			BareField.Padding = FMargin(4.0f, 2.0f);
			BareField.ForegroundColor = MenuCore;

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

	// MindKore's Hash01 (as in JamWeldProjector.cpp): the same for the same pair, unrelated for neighbours.
	float MenuHash(int64 A, int64 B)
	{
		uint32 H = uint32(A) * 0x9E3779B1u ^ uint32(B) * 0x85EBCA6Bu;
		H ^= H >> 15;
		H *= 0x2C1B3C6Du;
		H ^= H >> 12;
		return float(H & 0xFFFF) / 65535.0f;
	}

	/** The projector's device clock (JamDeviceClockHz in JamWeldProjector.cpp): the dashes and letters change on its ticks only. */
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
	class SJamDash : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SJamDash) : _Color(MenuAccent.CopyWithNewOpacity(0.55f)) {}
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
	class SJamFrame : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SJamFrame) : _Corner(12.0f), _bDashes(true) {}
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

	// MK Pribor N, the projector's character generator (the same table as JamWeldProjector.cpp, MIT, see CREDITS):
	// ten rows from the top, bit 0 leftmost; Advance in cells.
	struct FJamMenuGlyph
	{
		uint16 Code;
		uint8 Advance;
		uint16 Rows[10];
	};

	const FJamMenuGlyph MenuGlyphs[] = {
#include "DeviceFont/MKDeviceFontData.inl"
	};

	/** A word in MK Pribor N, cell by cell on whole pixels (a cell never smears between pixels), tracked out like the projector's readout. */
	class SJamPribor : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SJamPribor) : _Cell(1.6f), _Track(1), _Color(MenuCore) {}
			SLATE_ARGUMENT(FString, Text)
			SLATE_ARGUMENT(float, Cell)
			SLATE_ARGUMENT(int32, Track)
			SLATE_ARGUMENT(FLinearColor, Color)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Cell = InArgs._Cell;
			Track = InArgs._Track;
			Color = InArgs._Color;
			for (const TCHAR Char : InArgs._Text)
			{
				const int32 Index = Algo::BinarySearchBy(MenuGlyphs, uint16(Char), &FJamMenuGlyph::Code);
				Glyphs.Add(Index == INDEX_NONE ? nullptr : &MenuGlyphs[Index]);
				Cells += (Index == INDEX_NONE ? 3 : MenuGlyphs[Index].Advance) + Track;
			}
			Cells = FMath::Max(Cells - Track - 1, 0);
			ForceVolatile(true);
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			// The cell as painted last frame (snapped to whole pixels), so the word never runs past its box.
			const float Unit = FMath::Max(1.0f, FMath::CeilToFloat(Cell * PaintScale - 0.05f)) / PaintScale;
			return FVector2D(Cells * Unit, Rows * Unit);
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer,
			const FWidgetStyle&, bool) const override
		{
			const float Scale = FMath::Max(Geometry.Scale, 0.01f);
			PaintScale = Scale;
			const float Unit = FMath::Max(1.0f, FMath::CeilToFloat(Cell * Scale - 0.05f)) / Scale;
			const int64 Tick = MenuClockTick(Args);
			int32 Pen = 0;
			int32 Run = 0;
			for (const FJamMenuGlyph* Glyph : Glyphs)
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
							FSlateLayoutTransform(FVector2f((Pen + Bit) * Unit, Row * Unit))), MenuWhite(), ESlateDrawEffect::None, Color.CopyWithNewOpacity(Color.A * Level));
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
		TArray<const FJamMenuGlyph*> Glyphs;
		int32 Cells = 0;
		float Cell = 1.7f;
		int32 Track = 1;
		FLinearColor Color;
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
	struct FJamMenuName
	{
		const TCHAR* Key;
		const TCHAR* Section;
		const TCHAR* Label;
		/** "×" goes before the number (×0.5); anything else after it. */
		const TCHAR* Unit;
	};

	// The default pins (UJamDebugSubsystem::GetDefaultPins), in the overlay's sections. Docs/DebugMenu.md, «Оформление».
	const FJamMenuName MenuNames[] = {
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

	const FJamMenuName* MenuNameOf(const FString& Key)
	{
		for (const FJamMenuName& Name : MenuNames)
		{
			if (Key == Name.Key)
			{
				return &Name;
			}
		}
		return nullptr;
	}

	/** A pin outside the table: its section by owner. */
	FString MenuSectionOf(const FJamDebugParam& Param)
	{
		if (const FJamMenuName* Name = MenuNameOf(Param.Key))
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

	/**
	 * A readable name and unit: the table's, else the editor's DisplayName, else the property name split into words with
	 * its unit suffix turned into a unit (ArmPullCmS -> "Arm Pull", "см/с").
	 */
	void MenuHumanName(const FJamDebugParam& Param, FString& OutLabel, FString& OutUnit)
	{
		if (const FJamMenuName* Name = MenuNameOf(Param.Key))
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
		OutLabel = FName::NameToDisplayString(Param.Property->GetName(), Param.Property->IsA<FBoolProperty>());
		static const TPair<const TCHAR*, const TCHAR*> Suffixes[] = {
			{ TEXT(" Cm S"), TEXT("см/с") }, { TEXT(" Cm"), TEXT("см") }, { TEXT(" Deg"), TEXT("°") }, { TEXT(" Seconds"), TEXT("с") },
			{ TEXT(" Ms"), TEXT("мс") }, { TEXT(" Kg"), TEXT("кг") }, { TEXT(" Hz"), TEXT("Гц") },
		};
		for (const TPair<const TCHAR*, const TCHAR*>& Suffix : Suffixes)
		{
			if (OutLabel.EndsWith(Suffix.Key, ESearchCase::CaseSensitive))
			{
				OutLabel.LeftChopInline(FCString::Strlen(Suffix.Key));
				OutUnit = Suffix.Value;
				break;
			}
		}
	}

	// Category words of the groups in Russian ("Pawn · ZeroG|Arm" reads «Рука»), in the order the tabs list them: the
	// most tuned first (arm, movement, view; a gun's recoil). Anything else keeps its word and goes after.
	const TPair<const TCHAR*, const TCHAR*> MenuGroupWords[] = {
		{ TEXT("Arm"), TEXT("Рука") }, { TEXT("Grab"), TEXT("Хват") }, { TEXT("Walk"), TEXT("Ходьба") }, { TEXT("Thrusters"), TEXT("Маневровые") },
		{ TEXT("ZeroG"), TEXT("Невесомость") }, { TEXT("View"), TEXT("Взгляд") }, { TEXT("Hands"), TEXT("Руки") }, { TEXT("Push"), TEXT("Толчок") },
		{ TEXT("Grapple"), TEXT("Кошка") }, { TEXT("Tether"), TEXT("Трос") }, { TEXT("Body"), TEXT("Тело") }, { TEXT("Crash"), TEXT("Удары") },
		{ TEXT("Oxygen"), TEXT("Кислород") }, { TEXT("Vitals"), TEXT("Здоровье") }, { TEXT("Recoil"), TEXT("Отдача") }, { TEXT("Weapon"), TEXT("Ствол") },
		{ TEXT("Action"), TEXT("Цикл") }, { TEXT("Reload"), TEXT("Перезарядка") }, { TEXT("Switch"), TEXT("Смена") }, { TEXT("SlideLock"), TEXT("Затворная задержка") },
		{ TEXT("Pose"), TEXT("Поза") }, { TEXT("Parts"), TEXT("Детали") }, { TEXT("Tracer"), TEXT("Трассеры") }, { TEXT("Weapons"), TEXT("Стволы") },
		{ TEXT("Scope"), TEXT("Прицел") }, { TEXT("Aim"), TEXT("Наведение") }, { TEXT("Sights"), TEXT("Мушка") }, { TEXT("Motion"), TEXT("Качание") },
		{ TEXT("Hunt"), TEXT("Охота") }, { TEXT("Flight"), TEXT("Полёт") }, { TEXT("Flock"), TEXT("Стая") }, { TEXT("Hearing"), TEXT("Слух") },
		{ TEXT("Hit"), TEXT("Попадания") }, { TEXT("Roost"), TEXT("Насест") }, { TEXT("Look"), TEXT("Вид") }, { TEXT("Ladder"), TEXT("Лестница") },
		{ TEXT("Prediction"), TEXT("Прогноз") }, { TEXT("Input"), TEXT("Ввод") }, { TEXT("BulletTime"), TEXT("Замедление") }, { TEXT("Sound"), TEXT("Звук") },
		{ TEXT("Strap"), TEXT("Стяжка") }, { TEXT("Welder"), TEXT("Сварка") }, { TEXT("Projector"), TEXT("Проектор") }, { TEXT("Ship"), TEXT("Корабль") },
		{ TEXT("Jam"), TEXT("Общее") },
	};

	/** The group's last word: "Pawn · ZeroG|Arm" -> "Arm", "Flyer" -> "Flyer". */
	FString MenuGroupWord(const FString& Group)
	{
		int32 At = INDEX_NONE;
		Group.FindLastChar(TEXT('|'), At);
		if (At != INDEX_NONE)
		{
			return Group.Mid(At + 1);
		}
		const int32 Dot = Group.Find(TEXT(" · "), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		return Dot == INDEX_NONE ? Group : Group.Mid(Dot + 3);
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

	/** «Рука», «Пистолет · Отдача», «Насест Flyers_Belt · Стая», «Зона Gravity_Cabin». */
	FString MenuGroupTitle(const FString& Group, const FString& Owner)
	{
		const FString Word = MenuGroupWord(Group);
		FString Title = Word;
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
		else if (Owner == TEXT("World"))
		{
			Who = TEXT("Мир");
		}
		else if (Owner == TEXT("Flyer") && Group == Owner)
		{
			Who = TEXT("Летун");
		}
		if (Who.IsEmpty() || Group == Owner)
		{
			return Who.IsEmpty() ? Title : Who;
		}
		return Who + TEXT(" · ") + Title;
	}

	// --- small pieces --------------------------------------------------------------------------------------------

	TSharedRef<SWidget> MenuHairline(const FLinearColor& Color = MenuHair)
	{
		return SNew(SBox).HeightOverride(1.0f)[SNew(SImage).Image(MenuWhite()).ColorAndOpacity(Color)];
	}

	/** A section's label (capitals, tracked out) with a hairline under it; Right is an optional action at its end. */
	TSharedRef<SWidget> MenuSection(const FString& Title, TSharedPtr<SWidget> Right = nullptr)
	{
		TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Bottom).Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				// FText::ToUpper, not FString's: that one leaves Cyrillic as it is.
				SNew(STextBlock).Font(MenuLabel()).ColorAndOpacity(MenuAccent).AutoWrapText(true).Text(MenuText_(Title).ToUpper())
			];
		if (Right.IsValid())
		{
			Line->AddSlot().AutoWidth().VAlign(VAlign_Bottom).Padding(8.0f, 0.0f, 0.0f, 2.0f)[Right.ToSharedRef()];
		}
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Line]
			+ SVerticalBox::Slot().AutoHeight()[MenuHairline()];
	}

	TSharedRef<SWidget> MenuButton(const FString& Label, TFunction<void()> Action, TAttribute<bool> Enabled = true)
	{
		return SNew(SButton)
			.ButtonStyle(&FJamMenuLook::Get().Button)
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
			.ButtonStyle(&FJamMenuLook::Get().Ghost)
			.ContentPadding(FMargin(3.5f, 3.0f))
			.IsEnabled(Enabled)
			.ToolTipText(MenuText_(Label + TEXT(": ") + Tip))
			.OnClicked_Lambda([Action]() { Action(); return FReply::Handled(); })
			[
				SNew(STextBlock).Font(MenuFont()).ColorAndOpacity(FSlateColor::UseForeground()).Text(MenuText_(Label))
			];
	}

	/** The game's dt over the last 5 s: one column per pixel (its longest frame), lines at 30 and 60 FPS. */
	class SJamDtGraph : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SJamDtGraph) {}
			SLATE_ARGUMENT(TWeakObjectPtr<UJamDebugSubsystem>, Debug)
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
			const UJamDebugSubsystem* DebugMenu = Debug.Get();
			if (!DebugMenu || DebugMenu->GetFrames().IsEmpty() || Size.X < 2.0f)
			{
				return Layer;
			}
			const TArray<UJamDebugSubsystem::FFrameSample>& Frames = DebugMenu->GetFrames();
			const double Start = Frames.Last().At - 5.0;
			const int32 Columns = int32(Size.X);
			TArray<float> Peak;
			Peak.Init(-1.0f, Columns);
			float Max = 1.0f / 30.0f;
			for (const UJamDebugSubsystem::FFrameSample& Frame : Frames)
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
		TWeakObjectPtr<UJamDebugSubsystem> Debug;
	};

	FString MenuOpenGroupsFile()
	{
		return FPaths::ProjectSavedDir() / TEXT("JamDebug/OpenGroups.txt");
	}
}

void SJamDebugMenu::Construct(const FArguments& InArgs)
{
	Debug = InArgs._Debug;
	TArray<FString> Saved;
	if (FFileHelper::LoadFileToStringArray(Saved, *MenuOpenGroupsFile()))
	{
		OpenGroups.Append(Saved);
	}
	const auto ModeIs = [this](EJamDebugMode Mode, EVisibility Shown)
	{
		return [this, Mode, Shown]()
		{
			const UJamDebugSubsystem* DebugMenu = Debug.Get();
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
				SNew(SBox).WidthOverride(420.0f).Visibility_Lambda(ModeIs(EJamDebugMode::Open, EVisibility::Visible))
				[
					MakePanel()
				]
			]
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(Clearance)
			[
				// Pinned: translucent over the game, input stays with the game; left of the crosshair, between the HUD's corners.
				SNew(SBox).Visibility_Lambda(ModeIs(EJamDebugMode::Pinned, EVisibility::HitTestInvisible))
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

float SJamDebugMenu::MenuScale() const
{
	return FMath::Max(1.0f, MenuMinScale / FMath::Max(ViewScale, 0.01f));
}

FMargin SJamDebugMenu::HudClearance() const
{
	const float Units = 1.0f / FMath::Max(ViewScale * MenuScale(), 0.01f);
	// In a small view (a PIE window) the lamps at the top give way too: O2 and HP never do.
	const float Top = ViewHeight - MenuHudTopPx - MenuVitalsBottomPx >= ViewHeight * 0.5f ? MenuHudTopPx : MenuLeftPx;
	const float Bottom = ViewHeight >= MenuToolLinesMinPx ? MenuHudBottomPx : MenuVitalsBottomPx;
	return FMargin(MenuLeftPx * Units, Top * Units, 0.0f, Bottom * Units);
}

TSharedRef<SWidget> SJamDebugMenu::MakePanel()
{
	const FJamMenuLook& Look = FJamMenuLook::Get();
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
			.Padding(FMargin(14.0f, 12.0f, 14.0f, 9.0f))
			[
				SNew(SVerticalBox)
				// One line: the instrument's name in the projector's letters, «ТЕСТ» and the config's name (typed in place,
				// it exports with it), how far the config is from the original.
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SJamPribor).Text(TEXT("ОТЛАДКА")).Color(MenuCore)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(12.0f, 2.0f, 10.0f, 2.0f)
					[
						SNew(SBox).WidthOverride(1.0f)[SNew(SImage).Image(MenuWhite()).ColorAndOpacity(MenuHair)]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 2.0f, 0.0f)
					[
						SNew(STextBlock).Font(MenuLabel()).ColorAndOpacity(MenuDim).Text(MenuText_(TEXT("ТЕСТ")))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(SEditableTextBox)
						.Style(&Look.BareField)
						.Font(MenuMedium(TypeHeading))
						.HintText(MenuText_(TEXT("без имени")))
						.ToolTipText(MenuText_(TEXT("имя конфига для экспорта")))
						.Text_Lambda([this]() { const UJamDebugSubsystem* DebugMenu = Debug.Get(); return MenuText_(DebugMenu ? DebugMenu->GetConfigName() : FString()); })
						.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type)
						{
							if (UJamDebugSubsystem* DebugMenu = Debug.Get())
							{
								DebugMenu->SetConfigName(Text.ToString());
							}
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock).Font(MenuFont(TypeCaption))
						.ColorAndOpacity_Lambda([this]()
						{
							const UJamDebugSubsystem* DebugMenu = Debug.Get();
							return FSlateColor(DebugMenu && DebugMenu->GetChangedCount() > 0 ? MenuChanged : MenuDim);
						})
						.Text_Lambda([this]()
						{
							const UJamDebugSubsystem* DebugMenu = Debug.Get();
							return MenuText_(FString::Printf(TEXT("изменено %d"), DebugMenu ? DebugMenu->GetChangedCount() : 0));
						})
					]
				]
				// The last session's config: offered, never applied by itself.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f).HAlign(HAlign_Left)
				[
					SNew(SButton)
					.ButtonStyle(&Look.Button)
					.Visibility_Lambda([this]()
					{
						const UJamDebugSubsystem* DebugMenu = Debug.Get();
						return DebugMenu && DebugMenu->GetLastSessionChanges() > 0 && DebugMenu->GetChangedCount() == 0 ? EVisibility::Visible : EVisibility::Collapsed;
					})
					.OnClicked_Lambda([this]()
					{
						if (UJamDebugSubsystem* DebugMenu = Debug.Get())
						{
							DebugMenu->RestoreLastSession();
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock).Font(MenuFont()).ColorAndOpacity(FSlateColor::UseForeground()).Text_Lambda([this]()
						{
							const UJamDebugSubsystem* DebugMenu = Debug.Get();
							return MenuText_(FString::Printf(TEXT("вернуть прошлый конфиг (изменений: %d)"), DebugMenu ? DebugMenu->GetLastSessionChanges() : 0));
						})
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 9.0f)
				[
					SNew(SJamDash)
				]
				// Search first: six hundred parameters are not scrolled through. It looks through every tab.
				+ SVerticalBox::Slot().AutoHeight()
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
				// The config's operations, then the edits': one quiet toolbar (the design's [Экспорт] [Импорт] [Сохранить] [Сбросить]).
				+ SVerticalBox::Slot().AutoHeight().Padding(-4.0f, 5.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[MenuTool(TEXT("Экспорт"), TEXT("в Saved/JamDebug/Presets и в буфер"), [this]() { if (UJamDebugSubsystem* D = Debug.Get()) { D->ExportToFile(FString()); } })]
					+ SHorizontalBox::Slot().AutoWidth()[MenuTool(TEXT("Импорт"), TEXT("из буфера"), [this]() { if (UJamDebugSubsystem* D = Debug.Get()) { D->Import(TEXT("clipboard")); } })]
					+ SHorizontalBox::Slot().AutoWidth()[MenuTool(TEXT("Сохранить"), TEXT("в ассет /Game/Jam/Debug/Presets (в сборке - экспорт)"), [this]() { FString Message; if (UJamDebugSubsystem* D = Debug.Get()) { D->SaveToAsset(FString(), Message); } })]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill).Padding(4.0f, 5.0f)
					[
						SNew(SBox).WidthOverride(1.0f)[SNew(SImage).Image(MenuWhite()).ColorAndOpacity(MenuHair)]
					]
					+ SHorizontalBox::Slot().AutoWidth()[MenuTool(TEXT("Сбросить"), TEXT("всё"), [this]() { if (UJamDebugSubsystem* D = Debug.Get()) { D->Reset(TEXT("all")); } },
						TAttribute<bool>::CreateLambda([this]() { const UJamDebugSubsystem* D = Debug.Get(); return D && D->GetChangedCount() > 0; }))]
					+ SHorizontalBox::Slot().AutoWidth()[MenuTool(TEXT("Отменить"), TEXT("Ctrl+Z"), [this]() { if (UJamDebugSubsystem* D = Debug.Get()) { D->Undo(); } },
						TAttribute<bool>::CreateLambda([this]() { const UJamDebugSubsystem* D = Debug.Get(); return D && D->CanUndo(); }))]
					+ SHorizontalBox::Slot().AutoWidth()[MenuTool(TEXT("Повторить"), TEXT("Ctrl+Y"), [this]() { if (UJamDebugSubsystem* D = Debug.Get()) { D->Redo(); } },
						TAttribute<bool>::CreateLambda([this]() { const UJamDebugSubsystem* D = Debug.Get(); return D && D->CanRedo(); }))]
				]
				// The last action's result; an import's skipped keys under it.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Font(MenuFont())
					.AutoWrapText(true)
					.Visibility_Lambda([this]() { const UJamDebugSubsystem* D = Debug.Get(); return D && !D->Status.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
					.ColorAndOpacity_Lambda([this]() { const UJamDebugSubsystem* D = Debug.Get(); return FSlateColor(D && D->bStatusError ? MenuError : MenuDim); })
					.Text_Lambda([this]() { const UJamDebugSubsystem* D = Debug.Get(); return MenuText_(D ? D->Status : FString()); })
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(SkippedBox, SVerticalBox)
				]
				// The tabs: a row of words over the body, the open one lit and underlined.
				+ SVerticalBox::Slot().AutoHeight().Padding(-6.0f, 10.0f, 0.0f, 0.0f)
				[
					Tabs
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					MenuHairline()
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
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
						[
							SNew(STextBlock)
							.Font(MenuFont(TypeCaption))
							.ColorAndOpacity(MenuDim)
							.AutoWrapText(true)
							.Visibility_Lambda([this]() { return Search.IsEmpty() && Tab == TEXT("Мир") ? EVisibility::Visible : EVisibility::Collapsed; })
							.Text_Lambda([this]()
							{
								const UJamDebugSubsystem* D = Debug.Get();
								const UWorld* World = D ? D->GetGameInstance()->GetWorld() : nullptr;
								const AJamZeroGPawn* Pawn = nullptr;
								if (World)
								{
									for (TActorIterator<AJamZeroGPawn> It(World); It && !Pawn; ++It)
									{
										Pawn = *It;
									}
								}
								if (!Pawn)
								{
									return MenuText_(TEXT("нет пешки"));
								}
								const FJamGravity Gravity = JamGravityAt(World, Pawn->GetActorLocation());
								return MenuText_(Gravity.Zone
									? FString::Printf(TEXT("у пешки: %.0f см/с² = зона %s %.0f × %.2f"), Gravity.Acceleration.Size(),
										*Gravity.Zone->GetActorNameOrLabel(), Gravity.Zone->GravityCm, JamGetGravityScale())
									: FString::Printf(TEXT("у пешки: невесомость (вне зон), множитель × %.2f"), JamGetGravityScale()));
							})
						]
						// The search: what it looks through and how much it found.
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
						[
							SNew(STextBlock)
							.Font(MenuLabel())
							.ColorAndOpacity(MenuAccent)
							.Visibility_Lambda([this]() { return !Search.IsEmpty() && Found > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
							.Text_Lambda([this]() { return MenuText_(FString::Printf(TEXT("ПОИСК ПО ВСЕМ ВКЛАДКАМ · НАЙДЕНО %d"), Found)); })
						]
						+ SVerticalBox::Slot().FillHeight(1.0f)
						[
							SNew(SOverlay)
							+ SOverlay::Slot()
							[
								SAssignNew(List, SListView<TSharedPtr<FJamDebugRow>>)
								.ListItemsSource(&Rows)
								.SelectionMode(ESelectionMode::None)
								.ScrollBarStyle(&Look.Scroll)
								.OnGenerateRow(this, &SJamDebugMenu::MakeRow)
							]
							// The open group's header stays at the top while its rows scroll under it.
							+ SOverlay::Slot().VAlign(VAlign_Top).Padding(0.0f, 0.0f, 6.0f, 0.0f)
							[
								SAssignNew(StickyBox, SBox).Visibility(EVisibility::Collapsed)
							]
							// Nothing to show: say why, and what the search looks through.
							+ SOverlay::Slot().VAlign(VAlign_Top).Padding(0.0f, 12.0f, 8.0f, 0.0f)
							[
								SNew(STextBlock)
								.Font(MenuFont())
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
							SNew(SBox)
							// Only when the pins differ from the default list.
							.Visibility_Lambda([this]()
							{
								const UJamDebugSubsystem* D = Debug.Get();
								return Search.IsEmpty() && Tab == TEXT("Главное") && D && D->GetPins() != UJamDebugSubsystem::GetDefaultPins() ? EVisibility::Visible : EVisibility::Collapsed;
							})
							[
								MenuButton(TEXT("Закрепить по умолчанию"), [this]() { if (UJamDebugSubsystem* D = Debug.Get()) { D->RestoreDefaultPins(); } })
							]
						]
					]
					+ SWidgetSwitcher::Slot()
					[
						SNew(SScrollBox).ScrollBarStyle(&Look.Scroll).ScrollBarThickness(FVector2f(3.0f, 3.0f))
						+ SScrollBox::Slot().Padding(0.0f, 0.0f, 8.0f, 0.0f)[SAssignNew(PresetBox, SVerticalBox)]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 5.0f)
				[
					MenuHairline()
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(MenuFont(TypeCaption)).ColorAndOpacity(MenuDim).Text_Lambda([this]()
					{
						const UJamDebugSubsystem* D = Debug.Get();
						float Fps = 0.0f, Ms = 0.0f, MaxMs = 0.0f;
						if (D)
						{
							D->Measure(1.0f, Fps, Ms, MaxMs);
						}
						return MenuText_(FString::Printf(TEXT("%s  ·  %s  ·  %.1f FPS  ·  Разработка"), *UJamDebugSubsystem::GetBuildHash(),
							*MenuMapName(D ? D->GetGameInstance()->GetWorld() : nullptr), Fps));
					})
				]
			]
		]
		+ SOverlay::Slot()
		[
			SNew(SJamFrame)
		];
}

TSharedRef<SWidget> SJamDebugMenu::MakeTabButton(const FString& Name)
{
	const auto IsOpen = [this, Name]() { return Search.IsEmpty() && Tab == Name; };
	return SNew(SCheckBox)
		.Style(&FJamMenuLook::Get().Tab)
		.IsChecked_Lambda([IsOpen]() { return IsOpen() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([this, Name](ECheckBoxState) { ShowTab(Name); })
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(6.0f, 4.0f, 6.0f, 5.0f)
			[
				SNew(STextBlock).Font(MenuMedium()).ColorAndOpacity(FSlateColor::UseForeground()).Text(MenuText_(Name))
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

TSharedRef<SWidget> SJamDebugMenu::MakeChoices(const FString& Key, const TArray<TPair<FString, double>>& Options)
{
	const TWeakObjectPtr<UJamDebugSubsystem> Weak = Debug;
	TSharedRef<SWrapBox> Row = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4.0f, 4.0f));
	for (const TPair<FString, double>& Option : Options)
	{
		const double Value = Option.Value;
		Row->AddSlot()
		[
			SNew(SCheckBox)
			.Style(&FJamMenuLook::Get().Chip)
			.IsChecked_Lambda([Weak, Key, Value]()
			{
				double Now = -1.0;
				return Weak.IsValid() && Weak->GetNumber(Key, Now) && FMath::IsNearlyEqual(Now, Value) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([Weak, Key, Value](ECheckBoxState)
			{
				if (UJamDebugSubsystem* D = Weak.Get())
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
	return Row;
}

TSharedRef<SWidget> SJamDebugMenu::MakeQuickControls()
{
	const TWeakObjectPtr<UJamDebugSubsystem> Weak = Debug;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[MenuSection(TEXT("Система"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
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
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)[MakeKeyLine(TEXT("System.Vsync"), TEXT("Vsync"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)[MakeKeyLine(TEXT("System.TimeScale"), TEXT("Скорость игры"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(&FJamMenuLook::Get().Button)
				.OnClicked_Lambda([Weak]() { if (UJamDebugSubsystem* D = Weak.Get()) { D->SetPaused(!D->IsPaused()); } return FReply::Handled(); })
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
		];
}

TSharedRef<SWidget> SJamDebugMenu::MakeSystemTab()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	const TWeakObjectPtr<UJamDebugSubsystem> Weak = Debug;
	const auto Section = [&Box](const FString& Title)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, Box->NumSlots() == 0 ? 0.0f : 14.0f, 0.0f, 6.0f)[MenuSection(Title)];
	};
	// What is measured, next to what is set (principle 8).
	const auto Note = [&Box](TFunction<FString()> Text)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 3.0f)
		[
			SNew(STextBlock).Font(MenuFont(TypeCaption)).ColorAndOpacity(MenuDim).AutoWrapText(true).Text_Lambda([Text]() { return MenuText_(Text()); })
		];
	};
	const auto Choices = [this, &Box](const FString& Key, const TArray<TPair<FString, double>>& Options)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 4.0f)[MakeChoices(Key, Options)];
	};
	const auto Line = [this, &Box](const FString& Key, const FString& Label)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 2.0f)[MakeKeyLine(Key, Label)];
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
			.ButtonStyle(&FJamMenuLook::Get().Button)
			.OnClicked_Lambda([Weak]() { if (UJamDebugSubsystem* D = Weak.Get()) { D->SetPaused(!D->IsPaused()); } return FReply::Handled(); })
			[
				SNew(STextBlock).Font(MenuFont()).ColorAndOpacity(FSlateColor::UseForeground())
				.Text_Lambda([Weak]() { return MenuText_(Weak.IsValid() && Weak->IsPaused() ? TEXT("продолжить") : TEXT("пауза")); })
			]
		];
		Row->AddSlot()[MenuButton(TEXT("шаг на кадр"), [Weak]() { if (UJamDebugSubsystem* D = Weak.Get()) { D->StepFrame(); } },
			TAttribute<bool>::CreateLambda([Weak]() { return Weak.IsValid() && Weak->IsPaused(); }))];
		Box->AddSlot().AutoHeight().Padding(0.0f, 4.0f)[Row];
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
	Box->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 8.0f)[SNew(SJamDtGraph).Debug(Weak)];
	return Box;
}

TSharedRef<SWidget> SJamDebugMenu::MakeKeyLine(const FString& Key, const FString& Label)
{
	const UJamDebugSubsystem* DebugMenu = Debug.Get();
	TArray<FJamDebugParam> Params;
	if (DebugMenu)
	{
		DebugMenu->ListParams(Params);
	}
	for (const FJamDebugParam& Param : Params)
	{
		if (Param.Key == Key)
		{
			return MakeParamLine(Param, Label);
		}
	}
	return SNew(STextBlock).Font(MenuFont()).ColorAndOpacity(MenuError).Text(MenuText_(TEXT("нет параметра ") + Key));
}

TSharedRef<SWidget> SJamDebugMenu::MakeParamLine(const FJamDebugParam& Param, const FString& Label)
{
	const FJamMenuLook& Look = FJamMenuLook::Get();
	const FString Key = Param.Key;
	const TWeakObjectPtr<UJamDebugSubsystem> Weak = Debug;
	TSharedPtr<SWidget> Editor;
	const FNumericProperty* Numeric = CastField<FNumericProperty>(Param.Property);
	if (Numeric && !Numeric->IsEnum())
	{
		// A number: drag or type; with a known range the box is a slider.
		const bool bInteger = Numeric->IsInteger();
		Editor = SNew(SSpinBox<double>)
			.Style(&Look.Spin)
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
					if (UJamDebugSubsystem* D = Weak.Get())
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
				UJamDebugSubsystem* D = Weak.Get();
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
		Tip += FString::Printf(TEXT("\n[%s .. %s]"), Param.Min.IsSet() ? *FString::SanitizeFloat(Param.Min.GetValue()) : TEXT(""),
			Param.Max.IsSet() ? *FString::SanitizeFloat(Param.Max.GetValue()) : TEXT(""));
	}
	FString Unit = Param.Units;
	if (const FJamMenuName* Name = MenuNameOf(Key))
	{
		Unit = Name->Unit;
	}
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.6f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(STextBlock)
			.Font(MenuFont())
			.Text(MenuText_(Label))
			.ToolTipText(MenuText_(Tip))
			.HighlightText_Lambda([this]() { return MenuText_(Search); })
			.HighlightShape(&FJamMenuLook::Get().Highlight).HighlightColor(MenuCore)
			.ColorAndOpacity_Lambda([Weak, Key]() { return FSlateColor(Weak.IsValid() && Weak->IsChanged(Key) ? MenuChanged : MenuText); })
		]
		+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)
		[
			Editor.ToSharedRef()
		]
		// Units at the body size: label, value and unit share one baseline.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox).WidthOverride(46.0f)
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
			.OnClicked_Lambda([Weak, Key]() { if (UJamDebugSubsystem* D = Weak.Get()) { D->Reset(Key); } return FReply::Handled(); })
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
			.OnClicked_Lambda([Weak, Key]() { if (UJamDebugSubsystem* D = Weak.Get()) { D->SetPinned(Key, !D->IsPinned(Key)); } return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Font(MenuFont())
				.ColorAndOpacity_Lambda([Weak, Key]() { return FSlateColor(Weak.IsValid() && Weak->IsPinned(Key) ? MenuCore : MenuDim.CopyWithNewOpacity(0.7f)); })
				.Text_Lambda([Weak, Key]() { return MenuText_(Weak.IsValid() && Weak->IsPinned(Key) ? TEXT("★") : TEXT("☆")); })
			]
		];
}

TSharedRef<SWidget> SJamDebugMenu::MakeGroupHeader(TSharedPtr<FJamDebugRow> Row)
{
	const FJamMenuLook& Look = FJamMenuLook::Get();
	const FString Group = Row->Header;
	const FString Owner = Row->Param.Owner;
	const TWeakObjectPtr<UJamDebugSubsystem> Weak = Debug;
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
			const int32 At = Rows.IndexOfByPredicate([&Group](const TSharedPtr<FJamDebugRow>& Each) { return Each->Kind == FJamDebugRow::EKind::Group && Each->Header == Group; });
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
			.Padding(FMargin(0.0f, 7.0f, 0.0f, 0.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 5.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SJamPribor).Text(bOpen ? TEXT("▾") : TEXT("▸")).Cell(1.3f).Color(bOpen ? MenuAccent : MenuDim)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Font(MenuMedium(TypeHeading)).ColorAndOpacity(bOpen ? MenuCore : MenuText)
						.Text(MenuText_(MenuGroupTitle(Group, Owner)))
						.HighlightText_Lambda([this]() { return MenuText_(Search); })
			.HighlightShape(&FJamMenuLook::Get().Highlight).HighlightColor(MenuCore)
					]
					// The owner path, muted: where this lives in the code and the config keys.
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(8.0f, 1.0f, 6.0f, 0.0f)
					[
						SNew(STextBlock).Font(MenuFont(TypeCaption)).ColorAndOpacity(MenuDim).Clipping(EWidgetClipping::ClipToBounds)
						.Text(MenuText_(Group.Replace(TEXT("|"), TEXT(" / "))))
						.HighlightText_Lambda([this]() { return MenuText_(Search); })
			.HighlightShape(&FJamMenuLook::Get().Highlight).HighlightColor(MenuCore)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(STextBlock).Font(MenuMono(TypeCaption)).ColorAndOpacity(MenuChanged)
						.Visibility_Lambda([ChangedIn]() { return ChangedIn() > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
						.Text_Lambda([ChangedIn]() { return MenuText_(FString::Printf(TEXT("● %d"), ChangedIn())); })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(STextBlock).Font(MenuMono(TypeCaption)).ColorAndOpacity(MenuDim).Text(MenuText_(FString::FromInt(Row->Count)))
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
									if (UJamDebugSubsystem* D = Weak.Get(); D && D->IsChanged(Key))
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

TSharedRef<ITableRow> SJamDebugMenu::MakeRow(TSharedPtr<FJamDebugRow> Row, const TSharedRef<STableViewBase>& Table)
{
	const FJamMenuLook& Look = FJamMenuLook::Get();
	if (Row->Kind == FJamDebugRow::EKind::Group)
	{
		return SNew(STableRow<TSharedPtr<FJamDebugRow>>, Table)
			.Style(&Look.Row)
			.Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
			[
				MakeGroupHeader(Row)
			];
	}
	if (Row->Kind == FJamDebugRow::EKind::Section)
	{
		return SNew(STableRow<TSharedPtr<FJamDebugRow>>, Table)
			.Style(&Look.Row)
			.Padding(FMargin(0.0f, 12.0f, 6.0f, 5.0f))
			[
				MenuSection(Row->Header)
			];
	}
	// Rows of an open group sit in from its header.
	const bool bInGroup = Search.IsEmpty() ? Tab != TEXT("Главное") : true;
	return SNew(STableRow<TSharedPtr<FJamDebugRow>>, Table)
		.Style(&Look.Row)
		.Padding(FMargin(bInGroup ? 14.0f : 0.0f, 2.0f, 6.0f, 2.0f))
		[
			MakeParamLine(Row->Param, Row->Label)
		];
}

void SJamDebugMenu::Rebuild()
{
	Rows.Reset();
	Found = 0;
	UJamDebugSubsystem* DebugMenu = Debug.Get();
	if (DebugMenu)
	{
		TArray<FJamDebugParam> Params;
		DebugMenu->ListParams(Params);
		const auto InTab = [this](const FJamDebugParam& Param)
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
		const auto Label = [](const FJamDebugParam& Param)
		{
			FString Name, Unit;
			MenuHumanName(Param, Name, Unit);
			return MenuNameOf(Param.Key) ? Name : Param.Property->GetName();
		};
		const auto Add = [this](const FJamDebugParam& Param, const FString& Text)
		{
			const TSharedPtr<FJamDebugRow> Row = MakeShared<FJamDebugRow>();
			Row->Param = Param;
			Row->Label = Text;
			Rows.Add(Row);
		};
		if (Search.IsEmpty() && Tab == TEXT("Главное"))
		{
			// The pins by section (Мир, Взгляд, Движение...), in pin order inside one, with human names.
			TArray<const FJamDebugParam*> Pinned;
			for (const FString& Pin : DebugMenu->GetPins())
			{
				const FJamDebugParam* Param = Params.FindByPredicate([&Pin](const FJamDebugParam& Each) { return Each.Key == Pin; });
				if (Param && (!bChangedOnly || DebugMenu->IsChanged(Pin)))
				{
					Pinned.Add(Param);
				}
			}
			Algo::StableSortBy(Pinned, [](const FJamDebugParam* Param) { return MenuSectionRank(MenuSectionOf(*Param)); });
			FString Section;
			for (const FJamDebugParam* Param : Pinned)
			{
				if (MenuSectionOf(*Param) != Section)
				{
					Section = MenuSectionOf(*Param);
					const TSharedPtr<FJamDebugRow> Header = MakeShared<FJamDebugRow>();
					Header->Kind = FJamDebugRow::EKind::Section;
					Header->Header = Section;
					Rows.Add(Header);
				}
				FString Name, Unit;
				MenuHumanName(*Param, Name, Unit);
				Add(*Param, Name);
			}
		}
		else
		{
			// Groups in the curated order: by owner as listed (the pawn, its parts, each gun, the flyers, the map's
			// actors), inside an owner the most tuned first; a gun's recoil leads its rows.
			struct FGroup
			{
				FString Key;
				FString Owner;
				int32 OwnerOrder = 0;
				int32 Rank = 0;
				TArray<const FJamDebugParam*> Params;
				int32 Total = 0;
				TArray<FString> Keys;
			};
			TArray<FGroup> Groups;
			TArray<FString> Owners;
			for (const FJamDebugParam& Param : Params)
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
				const TSharedPtr<FJamDebugRow> Header = MakeShared<FJamDebugRow>();
				Header->Kind = FJamDebugRow::EKind::Group;
				Header->Header = Group.Key;
				Header->Param.Owner = Group.Params[0]->Owner;
				Header->Count = Group.Total;
				Header->Keys = Group.Keys;
				Rows.Add(Header);
				Found += Group.Params.Num();
				if (!Search.IsEmpty() || OpenGroups.Contains(Group.Key))
				{
					Algo::StableSortBy(Group.Params, [](const FJamDebugParam* Param) { return Param->Property->GetName().Contains(TEXT("Recoil")) ? 0 : 1; });
					for (const FJamDebugParam* Param : Group.Params)
					{
						Add(*Param, Label(*Param));
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

void SJamDebugMenu::ToggleGroup(const FString& Group)
{
	if (OpenGroups.Remove(Group) == 0)
	{
		OpenGroups.Add(Group);
	}
	SaveGroups();
	Rebuild();
}

void SJamDebugMenu::SetGroups(const FString& Text)
{
	if (Text == TEXT("none"))
	{
		OpenGroups.Reset();
	}
	else
	{
		for (const TSharedPtr<FJamDebugRow>& Row : Rows)
		{
			if (Row->Kind == FJamDebugRow::EKind::Group
				&& (Text == TEXT("all") || Row->Header.Contains(Text) || MenuGroupTitle(Row->Header, Row->Param.Owner).Contains(Text)))
			{
				OpenGroups.Add(Row->Header);
			}
		}
	}
	SaveGroups();
	Rebuild();
}

void SJamDebugMenu::SaveGroups() const
{
	FFileHelper::SaveStringArrayToFile(OpenGroups.Array(), *MenuOpenGroupsFile(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void SJamDebugMenu::RebuildPresets()
{
	UJamDebugSubsystem* DebugMenu = Debug.Get();
	if (!PresetBox.IsValid() || !DebugMenu)
	{
		return;
	}
	PresetBox->ClearChildren();
	const TWeakObjectPtr<UJamDebugSubsystem> Weak = Debug;
	PresetBox->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
	[
		MenuSection(TEXT("Пресеты"), MenuTool(TEXT("обновить"), TEXT("перечитать список"), [this]() { RebuildPresets(); }))
	];
	PresetBox->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(STextBlock).Font(MenuFont(TypeCaption)).ColorAndOpacity(MenuDim).AutoWrapText(true)
		.Text(MenuText_(TEXT("Встроенные - ассеты в /Game/Jam/Debug/Presets, свои - JSON в Saved/JamDebug/Presets. «Применить» заменяет текущий конфиг, «поверх» добавляет к нему.")))
	];
	TArray<FJamDebugPreset> Presets;
	DebugMenu->ListPresets(Presets);
	for (const FJamDebugPreset& Preset : Presets)
	{
		const FString Name = Preset.Name;
		PresetBox->AddSlot().AutoHeight().Padding(0.0f, 3.0f)
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
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)[MenuButton(TEXT("применить"), [Weak, Name]() { if (UJamDebugSubsystem* D = Weak.Get()) { D->Import(Name, true); } })]
			+ SHorizontalBox::Slot().AutoWidth()[MenuButton(TEXT("поверх"), [Weak, Name]() { if (UJamDebugSubsystem* D = Weak.Get()) { D->Import(Name, false); } })]
		];
	}
}

TSharedRef<SWidget> SJamDebugMenu::MakePinned()
{
	const TWeakObjectPtr<UJamDebugSubsystem> Weak = Debug;
	return SNew(SBox).MinDesiredWidth(232.0f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage(MenuWhite())
				.BorderBackgroundColor(MenuInkPinned)
				.Padding(FMargin(12.0f, 9.0f, 12.0f, 9.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(MenuMono()).ColorAndOpacity(MenuCore).Text_Lambda([Weak]()
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
						})
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 7.0f, 0.0f, 2.0f)
					[
						SNew(SJamDash)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SAssignNew(PinnedBox, SVerticalBox)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock).Font(MenuFont(TypeCaption)).ColorAndOpacity(MenuDim).Text(MenuText_(TEXT("F6 — меню  ·  Shift+F6 — скрыть")))
					]
				]
			]
			+ SOverlay::Slot()
			[
				SNew(SJamFrame).Corner(8.0f).bDashes(false)
			]
		];
}

void SJamDebugMenu::RebuildPinned()
{
	UJamDebugSubsystem* DebugMenu = Debug.Get();
	if (!PinnedBox.IsValid() || !DebugMenu)
	{
		return;
	}
	PinnedBox->ClearChildren();
	const TWeakObjectPtr<UJamDebugSubsystem> Weak = Debug;
	TArray<FJamDebugParam> Params;
	DebugMenu->ListParams(Params);
	// Human names in short sections (Мир, Взгляд, Движение, Рука, Отдача, Выживание, Летуны); the keys are the menu's.
	TArray<FJamDebugParam> Pinned;
	for (const FString& Key : DebugMenu->GetPins())
	{
		if (const FJamDebugParam* Param = Params.FindByPredicate([&Key](const FJamDebugParam& Each) { return Each.Key == Key; }))
		{
			Pinned.Add(*Param);
		}
	}
	Algo::StableSortBy(Pinned, [](const FJamDebugParam& Param) { return MenuSectionRank(MenuSectionOf(Param)); });
	FString Section;
	for (const FJamDebugParam& Param : Pinned)
	{
		const FString Key = Param.Key;
		if (MenuSectionOf(Param) != Section)
		{
			Section = MenuSectionOf(Param);
			PinnedBox->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 1.0f)
			[
				SNew(STextBlock).Font(MenuLabel()).ColorAndOpacity(MenuAccent).Text(MenuText_(Section).ToUpper())
			];
		}
		FString Label, Unit;
		MenuHumanName(Param, Label, Unit);
		const bool bTimes = Unit == TEXT("×");
		const auto Changed = [Weak, Key]() { return Weak.IsValid() && Weak->IsChanged(Key); };
		PinnedBox->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
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
						return MenuText_(bTimes ? FString::Printf(TEXT("×%g"), Number) : FString::Printf(TEXT("%g"), Number));
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
		];
	}
	PinnedShown = DebugMenu->GetPins();
}

TSharedRef<SWidget> SJamDebugMenu::MakeWatermark()
{
	// On every frame while the config differs from the original: it gets into screenshots and clips.
	const TWeakObjectPtr<UJamDebugSubsystem> Weak = Debug;
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
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.0f, 6.0f, 8.0f, 6.0f)
			[
				SNew(SJamPribor).Text(TEXT("ТЕСТ")).Color(MenuChanged)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 6.0f, 12.0f, 6.0f)
			[
				SNew(STextBlock).Font(MenuMono()).ColorAndOpacity(MenuChanged).Text_Lambda([Weak]()
				{
					return Weak.IsValid() ? MenuText_(FString::Printf(TEXT("%s · изменено %d · %s"),
						Weak->GetConfigName().IsEmpty() ? TEXT("без имени") : *Weak->GetConfigName(), Weak->GetChangedCount(), *UJamDebugSubsystem::GetBuildHash())) : FText::GetEmpty();
				})
			]
		];
}

void SJamDebugMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	ViewScale = AllottedGeometry.Scale;
	ViewHeight = AllottedGeometry.GetLocalSize().Y * AllottedGeometry.Scale;
	UJamDebugSubsystem* DebugMenu = Debug.Get();
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
	// The sticky header: the last group header at or above the top row, once it has scrolled off.
	if (List.IsValid() && StickyBox.IsValid())
	{
		const double Offset = List->GetScrollOffset();
		int32 Header = INDEX_NONE;
		for (int32 Index = FMath::Min(int32(Offset), Rows.Num() - 1); Index >= 0; --Index)
		{
			if (Rows[Index]->Kind == FJamDebugRow::EKind::Group)
			{
				Header = Index;
				break;
			}
		}
		const TSharedPtr<FJamDebugRow> Now = Header != INDEX_NONE && Offset > Header + 0.05 && Header + 1 < Rows.Num()
			&& Rows[Header + 1]->Kind == FJamDebugRow::EKind::Param ? Rows[Header] : nullptr;
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
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Font(MenuLabel()).ColorAndOpacity(MenuError).Text(MenuText_(FString::Printf(TEXT("ПРОПУЩЕНО ПРИ ИМПОРТЕ · %d"), SeenSkipped)))
					]
					+ SHorizontalBox::Slot().AutoWidth()[MenuTool(TEXT("скрыть"), TEXT("убрать список"), [this]() { if (UJamDebugSubsystem* D = Debug.Get()) { D->LastSkipped.Reset(); } })]
				];
			TSharedRef<SVerticalBox> Keys = SNew(SVerticalBox);
			for (const FString& Skipped : DebugMenu->LastSkipped)
			{
				Keys->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
				[
					SNew(STextBlock).Font(MenuFont(TypeCaption)).ColorAndOpacity(MenuError.CopyWithNewOpacity(0.9f)).AutoWrapText(true).Text(MenuText_(Skipped))
				];
			}
			// A long list scrolls in place and leaves the parameters their room.
			Lines->AddSlot().AutoHeight()
			[
				SNew(SBox).MaxDesiredHeight(64.0f)
				[
					SNew(SScrollBox).ScrollBarStyle(&FJamMenuLook::Get().Scroll).ScrollBarThickness(FVector2f(3.0f, 3.0f)) + SScrollBox::Slot()[Keys]
				]
			];
			SkippedBox->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				SNew(SBorder).BorderImage(MenuWhite()).BorderBackgroundColor(MenuError.CopyWithNewOpacity(0.07f)).Padding(FMargin(10.0f, 6.0f, 6.0f, 8.0f))
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

void SJamDebugMenu::EditNumber(const FString& Key, const FString& Value)
{
	UJamDebugSubsystem* DebugMenu = Debug.Get();
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

void SJamDebugMenu::FlushEdit()
{
	UJamDebugSubsystem* DebugMenu = Debug.Get();
	FString Now;
	if (DebugMenu && !EditKey.IsEmpty() && DebugMenu->GetValue(EditKey, Now) && Now != EditBefore)
	{
		DebugMenu->SetValue(EditKey, Now, nullptr, /*bRecord=*/true, &EditBefore);
	}
	EditKey.Reset();
}

TSharedPtr<SWidget> SJamDebugMenu::GetFocusTarget() const
{
	return SearchBox;
}

void SJamDebugMenu::FocusSearch()
{
	if (SearchBox.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(SearchBox, EFocusCause::SetDirectly);
	}
}

void SJamDebugMenu::OnModeChanged()
{
	// The panel was collapsed until now: the search gets the keys once it is laid out (next tick).
	bFocusPending = Debug.IsValid() && Debug->GetMode() == EJamDebugMode::Open;
	RebuildPinned();
	if (Tab == TEXT("Пресеты"))
	{
		RebuildPresets();
	}
}

void SJamDebugMenu::ShowTab(const FString& Name)
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

void SJamDebugMenu::SetSearch(const FString& Text)
{
	Search = Text;
	if (SearchBox.IsValid())
	{
		SearchBox->SetText(MenuText_(Text));
	}
	Rebuild();
}

void SJamDebugMenu::ToggleChangedOnly()
{
	bChangedOnly = !bChangedOnly;
	Rebuild();
}
