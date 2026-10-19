#include "JamDebugMenu.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "JamGravity.h"
#include "JamShip.h"
#include "JamZeroGPawn.h"
#include "Misc/App.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
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
	// The suit projector's noir blue.
	const FLinearColor MenuBack(0.006f, 0.014f, 0.035f, 0.93f);
	const FLinearColor MenuPinnedBack(0.006f, 0.014f, 0.035f, 0.55f);
	const FLinearColor MenuText(0.80f, 0.88f, 1.0f);
	const FLinearColor MenuDim(0.48f, 0.58f, 0.75f);
	const FLinearColor MenuAccent(0.38f, 0.70f, 1.0f);
	const FLinearColor MenuChanged(1.0f, 0.64f, 0.22f);
	const FLinearColor MenuError(1.0f, 0.32f, 0.26f);

	const TArray<FString> MenuTabs = { TEXT("Система"), TEXT("Мир"), TEXT("Игрок"), TEXT("Оружие"), TEXT("Враги"), TEXT("Закреплено"), TEXT("Пресеты") };

	FSlateFontInfo MenuFont(int32 Size = 10) { return FCoreStyle::GetDefaultFontStyle("Regular", Size + 1); }
	FSlateFontInfo MenuBold(int32 Size = 10) { return FCoreStyle::GetDefaultFontStyle("Bold", Size + 1); }
	FSlateFontInfo MenuMono(int32 Size = 9) { return FCoreStyle::GetDefaultFontStyle("Mono", Size + 1); }
	const FSlateBrush* MenuWhite() { return FCoreStyle::Get().GetBrush("WhiteBrush"); }
	FText MenuText_(const FString& Text) { return FText::FromString(Text); }

	FString MenuNumberText(double Value, bool bInteger)
	{
		return bInteger ? FString::Printf(TEXT("%lld"), int64(FMath::RoundToDouble(Value))) : FString::SanitizeFloat(Value);
	}

	FString MenuMapName(const UWorld* World)
	{
		return World ? UWorld::RemovePIEPrefix(World->GetMapName()) : FString(TEXT("-"));
	}

	TSharedRef<SWidget> MenuButton(const FString& Label, TFunction<void()> Action, TAttribute<bool> Enabled = true)
	{
		return SNew(SBox).Padding(1.0f)
		[
			SNew(SButton)
			.IsEnabled(Enabled)
			.OnClicked_Lambda([Action]() { Action(); return FReply::Handled(); })
			[
				SNew(STextBlock).Font(MenuFont(9)).Text(MenuText_(Label))
			]
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
			return FVector2D(300.0f, 64.0f);
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out,
			int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override
		{
			const FVector2f Size = FVector2f(Geometry.GetLocalSize());
			FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(), MenuWhite(), ESlateDrawEffect::None, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
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
			for (const float Reference : { 1.0f / 30.0f, 1.0f / 60.0f })
			{
				FSlateDrawElement::MakeLines(Out, Layer + 1, Geometry.ToPaintGeometry(), TArray<FVector2f>{ FVector2f(0.0f, Y(Reference)), FVector2f(Size.X, Y(Reference)) },
					ESlateDrawEffect::None, FLinearColor(1.0f, 1.0f, 1.0f, 0.18f), true, 1.0f);
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
				FSlateDrawElement::MakeLines(Out, Layer + 2, Geometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None, MenuAccent, true, 1.5f);
			}
			return Layer + 2;
		}

	private:
		TWeakObjectPtr<UJamDebugSubsystem> Debug;
	};
}

void SJamDebugMenu::Construct(const FArguments& InArgs)
{
	Debug = InArgs._Debug;
	const auto ModeIs = [this](EJamDebugMode Mode, EVisibility Shown)
	{
		return [this, Mode, Shown]()
		{
			const UJamDebugSubsystem* DebugMenu = Debug.Get();
			return DebugMenu && DebugMenu->GetMode() == Mode ? Shown : EVisibility::Collapsed;
		};
	};
	ChildSlot
	[
		SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Fill)
		[
			SNew(SBox).WidthOverride(440.0f).Visibility_Lambda(ModeIs(EJamDebugMode::Open, EVisibility::Visible))
			[
				MakePanel()
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(12.0f)
		[
			// Pinned: translucent over the game, input stays with the game; left of the crosshair, under the HUD's counters.
			SNew(SBox).WidthOverride(380.0f).Visibility_Lambda(ModeIs(EJamDebugMode::Pinned, EVisibility::HitTestInvisible))
			[
				MakePinned()
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(12.0f)
		[
			MakeWatermark()
		]
	];
	Rebuild();
	RebuildPinned();
}

TSharedRef<SWidget> SJamDebugMenu::MakePanel()
{
	TSharedRef<SVerticalBox> Tabs = SNew(SVerticalBox);
	for (const FString& Name : MenuTabs)
	{
		Tabs->AddSlot().AutoHeight()[MakeTabButton(Name)];
	}
	return SNew(SBorder)
		.BorderImage(MenuWhite())
		.BorderBackgroundColor(MenuBack)
		.Padding(8.0f)
		[
			SNew(SVerticalBox)
			// The config and how far it is from the original.
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Font(MenuBold(11)).ColorAndOpacity_Lambda([this]()
				{
					const UJamDebugSubsystem* DebugMenu = Debug.Get();
					return FSlateColor(DebugMenu && DebugMenu->GetChangedCount() > 0 ? MenuChanged : MenuText);
				})
				.Text_Lambda([this]()
				{
					const UJamDebugSubsystem* DebugMenu = Debug.Get();
					return MenuText_(DebugMenu ? FString::Printf(TEXT("ТЕСТ: %s · изменено %d"),
						DebugMenu->GetConfigName().IsEmpty() ? TEXT("без имени") : *DebugMenu->GetConfigName(), DebugMenu->GetChangedCount()) : FString());
				})
			]
			// The last session's config: offered, never applied by itself.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
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
					SNew(STextBlock).Font(MenuFont(9)).Text_Lambda([this]()
					{
						const UJamDebugSubsystem* DebugMenu = Debug.Get();
						return MenuText_(FString::Printf(TEXT("вернуть прошлый конфиг (изменений: %d)"), DebugMenu ? DebugMenu->GetLastSessionChanges() : 0));
					})
				]
			]
			// Search first: six hundred parameters are not scrolled through.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 2.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SAssignNew(SearchBox, SSearchBox)
					.HintText(MenuText_(TEXT("поиск (Ctrl+F)")))
					.OnTextChanged_Lambda([this](const FText& Text)
					{
						Search = Text.ToString();
						Rebuild();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return bChangedOnly ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
					{
						bChangedOnly = State == ECheckBoxState::Checked;
						Rebuild();
					})
					[
						SNew(STextBlock).Font(MenuFont(9)).ColorAndOpacity(MenuText).Text(MenuText_(TEXT("только изменённые")))
					]
				]
			]
			// The config's name and its operations.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(STextBlock).Font(MenuFont(9)).ColorAndOpacity(MenuDim).Text(MenuText_(TEXT("имя")))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SEditableTextBox)
					.Font(MenuFont(9))
					.HintText(MenuText_(TEXT("имя конфига для экспорта")))
					.Text_Lambda([this]() { const UJamDebugSubsystem* DebugMenu = Debug.Get(); return MenuText_(DebugMenu ? DebugMenu->GetConfigName() : FString()); })
					.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type)
					{
						if (UJamDebugSubsystem* DebugMenu = Debug.Get())
						{
							DebugMenu->SetConfigName(Text.ToString());
						}
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SWrapBox).UseAllottedSize(true)
				+ SWrapBox::Slot()[MenuButton(TEXT("Экспорт"), [this]() { if (UJamDebugSubsystem* D = Debug.Get()) { D->ExportToFile(FString()); } })]
				+ SWrapBox::Slot()[MenuButton(TEXT("Импорт из буфера"), [this]() { if (UJamDebugSubsystem* D = Debug.Get()) { D->Import(TEXT("clipboard")); } })]
				+ SWrapBox::Slot()[MenuButton(TEXT("Сохранить в ассет"), [this]() { FString Message; if (UJamDebugSubsystem* D = Debug.Get()) { D->SaveToAsset(FString(), Message); } })]
				+ SWrapBox::Slot()[MenuButton(TEXT("Сбросить всё"), [this]() { if (UJamDebugSubsystem* D = Debug.Get()) { D->Reset(TEXT("all")); } },
					TAttribute<bool>::CreateLambda([this]() { const UJamDebugSubsystem* D = Debug.Get(); return D && D->GetChangedCount() > 0; }))]
				+ SWrapBox::Slot()[MenuButton(TEXT("Отменить"), [this]() { if (UJamDebugSubsystem* D = Debug.Get()) { D->Undo(); } },
					TAttribute<bool>::CreateLambda([this]() { const UJamDebugSubsystem* D = Debug.Get(); return D && D->CanUndo(); }))]
				+ SWrapBox::Slot()[MenuButton(TEXT("Повторить"), [this]() { if (UJamDebugSubsystem* D = Debug.Get()) { D->Redo(); } },
					TAttribute<bool>::CreateLambda([this]() { const UJamDebugSubsystem* D = Debug.Get(); return D && D->CanRedo(); }))]
			]
			// The last action's result; an import's skipped keys under it.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Font(MenuFont(9))
				.AutoWrapText(true)
				.ColorAndOpacity_Lambda([this]() { const UJamDebugSubsystem* D = Debug.Get(); return FSlateColor(D && D->bStatusError ? MenuError : MenuDim); })
				.Text_Lambda([this]() { const UJamDebugSubsystem* D = Debug.Get(); return MenuText_(D ? D->Status : FString()); })
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(SkippedBox, SVerticalBox)
			]
			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(0.0f, 6.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					Tabs
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SAssignNew(Body, SWidgetSwitcher)
					.WidgetIndex_Lambda([this]()
					{
						return !Search.IsEmpty() ? 1 : Tab == TEXT("Система") ? 0 : Tab == TEXT("Пресеты") ? 2 : 1;
					})
					+ SWidgetSwitcher::Slot()
					[
						SNew(SScrollBox) + SScrollBox::Slot()[MakeSystemTab()]
					]
					+ SWidgetSwitcher::Slot()
					[
						SNew(SVerticalBox)
						// The World tab: the gravity the pawn gets now, and from what.
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
						[
							SNew(STextBlock)
							.Font(MenuMono(8))
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
						+ SVerticalBox::Slot().FillHeight(1.0f)
						[
							SAssignNew(List, SListView<TSharedPtr<FJamDebugRow>>)
							.ListItemsSource(&Rows)
							.SelectionMode(ESelectionMode::None)
							.OnGenerateRow(this, &SJamDebugMenu::MakeRow)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SBox)
							.Visibility_Lambda([this]() { return Search.IsEmpty() && Tab == TEXT("Закреплено") ? EVisibility::Visible : EVisibility::Collapsed; })
							[
								MenuButton(TEXT("Закрепить по умолчанию"), [this]() { if (UJamDebugSubsystem* D = Debug.Get()) { D->RestoreDefaultPins(); } })
							]
						]
					]
					+ SWidgetSwitcher::Slot()
					[
						SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(PresetBox, SVerticalBox)]
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Font(MenuMono(8)).ColorAndOpacity(MenuDim).Text_Lambda([this]()
				{
					const UJamDebugSubsystem* D = Debug.Get();
					float Fps = 0.0f, Ms = 0.0f, MaxMs = 0.0f;
					if (D)
					{
						D->Measure(1.0f, Fps, Ms, MaxMs);
					}
					return MenuText_(FString::Printf(TEXT("%s · %s · %.1f FPS · Разработка"), *UJamDebugSubsystem::GetBuildHash(),
						*MenuMapName(D ? D->GetGameInstance()->GetWorld() : nullptr), Fps));
				})
			]
		];
}

TSharedRef<SWidget> SJamDebugMenu::MakeTabButton(const FString& Name)
{
	return SNew(SBox).WidthOverride(108.0f).Padding(0.0f, 1.0f)
	[
		SNew(SButton)
		.ButtonColorAndOpacity_Lambda([this, Name]() { return FSlateColor(Search.IsEmpty() && Tab == Name ? MenuAccent : FLinearColor(0.25f, 0.3f, 0.4f)); })
		.OnClicked_Lambda([this, Name]() { ShowTab(Name); return FReply::Handled(); })
		[
			SNew(STextBlock).Font(MenuFont(8)).Text(MenuText_(Name))
		]
	];
}

TSharedRef<SWidget> SJamDebugMenu::MakeSystemTab()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	const TWeakObjectPtr<UJamDebugSubsystem> Weak = Debug;
	const auto Section = [&Box](const FString& Title)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock).Font(MenuBold(10)).ColorAndOpacity(MenuAccent).Text(MenuText_(Title))
		];
	};
	// What is measured, next to what is set (principle 8).
	const auto Note = [&Box](TFunction<FString()> Text)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
		[
			SNew(STextBlock).Font(MenuMono(8)).ColorAndOpacity(MenuDim).AutoWrapText(true).Text_Lambda([Text]() { return MenuText_(Text()); })
		];
	};
	const auto Choices = [&Box, Weak](const FString& Key, const TArray<TPair<FString, double>>& Options)
	{
		TSharedRef<SWrapBox> Row = SNew(SWrapBox).UseAllottedSize(true);
		for (const TPair<FString, double>& Option : Options)
		{
			const double Value = Option.Value;
			Row->AddSlot()
			[
				SNew(SBox).Padding(1.0f)
				[
					SNew(SButton)
					.ButtonColorAndOpacity_Lambda([Weak, Key, Value]()
					{
						double Now = -1.0;
						return FSlateColor(Weak.IsValid() && Weak->GetNumber(Key, Now) && FMath::IsNearlyEqual(Now, Value) ? MenuAccent : FLinearColor(0.25f, 0.3f, 0.4f));
					})
					.OnClicked_Lambda([Weak, Key, Value]()
					{
						if (UJamDebugSubsystem* D = Weak.Get())
						{
							D->SetValue(Key, FString::SanitizeFloat(Value));
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock).Font(MenuFont(9)).Text(MenuText_(Option.Key))
					]
				]
			];
		}
		Box->AddSlot().AutoHeight()[Row];
	};
	const auto Line = [this, &Box](const FString& Key, const FString& Label)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 1.0f)[MakeKeyLine(Key, Label)];
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
		TSharedRef<SWrapBox> Row = SNew(SWrapBox).UseAllottedSize(true);
		Row->AddSlot()
		[
			SNew(SBox).Padding(1.0f)
			[
				SNew(SButton)
				.OnClicked_Lambda([Weak]() { if (UJamDebugSubsystem* D = Weak.Get()) { D->SetPaused(!D->IsPaused()); } return FReply::Handled(); })
				[
					SNew(STextBlock).Font(MenuFont(9)).Text_Lambda([Weak]() { return MenuText_(Weak.IsValid() && Weak->IsPaused() ? TEXT("продолжить") : TEXT("пауза")); })
				]
			]
		];
		Row->AddSlot()[MenuButton(TEXT("шаг на кадр"), [Weak]() { if (UJamDebugSubsystem* D = Weak.Get()) { D->StepFrame(); } },
			TAttribute<bool>::CreateLambda([Weak]() { return Weak.IsValid() && Weak->IsPaused(); }))];
		Box->AddSlot().AutoHeight()[Row];
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
	Box->AddSlot().AutoHeight().Padding(0.0f, 2.0f)[SNew(SJamDtGraph).Debug(Weak)];
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
	return SNew(STextBlock).Font(MenuFont(9)).ColorAndOpacity(MenuError).Text(MenuText_(TEXT("нет параметра ") + Key));
}

TSharedRef<SWidget> SJamDebugMenu::MakeParamLine(const FJamDebugParam& Param, const FString& Label)
{
	const FString Key = Param.Key;
	const TWeakObjectPtr<UJamDebugSubsystem> Weak = Debug;
	TSharedPtr<SWidget> Editor;
	const FNumericProperty* Numeric = CastField<FNumericProperty>(Param.Property);
	if (Numeric && !Numeric->IsEnum())
	{
		// A number: drag or type; with a known range the box is a slider.
		const bool bInteger = Numeric->IsInteger();
		Editor = SNew(SSpinBox<double>)
			.Font(MenuMono(9))
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
		Editor = SNew(SCheckBox)
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
			});
	}
	else
	{
		// Enums, vectors, colours: their UE text.
		Editor = SNew(SEditableTextBox)
			.Font(MenuMono(9))
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
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.55f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(STextBlock)
			.Font(MenuFont(9))
			.Text(MenuText_(Label))
			.ToolTipText(MenuText_(Tip))
			.ColorAndOpacity_Lambda([Weak, Key]() { return FSlateColor(Weak.IsValid() && Weak->IsChanged(Key) ? MenuChanged : MenuText); })
		]
		+ SHorizontalBox::Slot().FillWidth(0.45f).VAlign(VAlign_Center)
		[
			Editor.ToSharedRef()
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock).Font(MenuFont(8)).ColorAndOpacity(MenuDim).Text(MenuText_(Param.Units))
		]
		// The changed mark: the tooltip tells the original, a click puts it back.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
			.Visibility_Lambda([Weak, Key]() { return Weak.IsValid() && Weak->IsChanged(Key) ? EVisibility::Visible : EVisibility::Hidden; })
			.ToolTipText_Lambda([Weak, Key]()
			{
				const FString* Original = Weak.IsValid() ? Weak->GetOriginal(Key) : nullptr;
				return MenuText_(FString::Printf(TEXT("было: %s - клик вернёт"), Original ? **Original : TEXT("?")));
			})
			.OnClicked_Lambda([Weak, Key]() { if (UJamDebugSubsystem* D = Weak.Get()) { D->Reset(Key); } return FReply::Handled(); })
			[
				SNew(STextBlock).Font(MenuBold(10)).ColorAndOpacity(MenuChanged).Text(MenuText_(TEXT("●")))
			]
		]
		// The star pins it to the compact panel (Shift+F6).
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
			.ToolTipText(MenuText_(TEXT("закрепить (Shift+F6)")))
			.OnClicked_Lambda([Weak, Key]() { if (UJamDebugSubsystem* D = Weak.Get()) { D->SetPinned(Key, !D->IsPinned(Key)); } return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Font(MenuBold(10))
				.ColorAndOpacity_Lambda([Weak, Key]() { return FSlateColor(Weak.IsValid() && Weak->IsPinned(Key) ? MenuAccent : MenuDim); })
				.Text_Lambda([Weak, Key]() { return MenuText_(Weak.IsValid() && Weak->IsPinned(Key) ? TEXT("★") : TEXT("☆")); })
			]
		];
}

TSharedRef<ITableRow> SJamDebugMenu::MakeRow(TSharedPtr<FJamDebugRow> Row, const TSharedRef<STableViewBase>& Table)
{
	if (!Row->Header.IsEmpty())
	{
		const FString Owner = Row->Param.Owner;
		const TWeakObjectPtr<UJamDebugSubsystem> Weak = Debug;
		return SNew(STableRow<TSharedPtr<FJamDebugRow>>, Table)
			.Padding(FMargin(0.0f, 6.0f, 0.0f, 1.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(MenuBold(9)).ColorAndOpacity(MenuAccent).Text(MenuText_(Row->Header))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).Visibility_Lambda([Weak, Owner]()
					{
						if (const UJamDebugSubsystem* D = Weak.Get())
						{
							for (const TPair<FString, FString>& Value : D->GetChanged())
							{
								if (Value.Key.StartsWith(Owner + TEXT(".")))
								{
									return EVisibility::Visible;
								}
							}
						}
						return EVisibility::Collapsed;
					})
					[
						MenuButton(TEXT("сбросить ") + Owner, [Weak, Owner]() { if (UJamDebugSubsystem* D = Weak.Get()) { D->Reset(Owner); } })
					]
				]
			];
	}
	return SNew(STableRow<TSharedPtr<FJamDebugRow>>, Table)
		.Padding(FMargin(0.0f, 1.0f))
		[
			MakeParamLine(Row->Param, Row->Label)
		];
}

void SJamDebugMenu::Rebuild()
{
	Rows.Reset();
	UJamDebugSubsystem* DebugMenu = Debug.Get();
	if (DebugMenu)
	{
		TArray<FJamDebugParam> Params;
		DebugMenu->ListParams(Params);
		const auto InTab = [this, DebugMenu](const FJamDebugParam& Param)
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
		const auto Add = [this](const FJamDebugParam& Param, const FString& Label)
		{
			const TSharedPtr<FJamDebugRow> Row = MakeShared<FJamDebugRow>();
			Row->Param = Param;
			Row->Label = Label;
			Rows.Add(Row);
		};
		if (Search.IsEmpty() && Tab == TEXT("Закреплено"))
		{
			// In pin order, with the full keys.
			for (const FString& Pin : DebugMenu->GetPins())
			{
				const FJamDebugParam* Param = Params.FindByPredicate([&Pin](const FJamDebugParam& Each) { return Each.Key == Pin; });
				if (Param && (!bChangedOnly || DebugMenu->IsChanged(Pin)))
				{
					Add(*Param, Pin);
				}
			}
		}
		else
		{
			FString Group;
			for (const FJamDebugParam& Param : Params)
			{
				const bool bShown = Search.IsEmpty() ? InTab(Param) : (Param.Key.Contains(Search) || Param.Group.Contains(Search));
				if (!bShown || (bChangedOnly && !DebugMenu->IsChanged(Param.Key)))
				{
					continue;
				}
				if (Param.Group != Group)
				{
					Group = Param.Group;
					const TSharedPtr<FJamDebugRow> Header = MakeShared<FJamDebugRow>();
					Header->Header = Group;
					Header->Param.Owner = Param.Owner;
					Rows.Add(Header);
				}
				Add(Param, Param.Property->GetName());
			}
		}
		SeenGeneration = DebugMenu->GetGeneration();
		SeenChanged = DebugMenu->GetChangedCount();
		SeenPins = DebugMenu->GetPins();
	}
	if (List.IsValid())
	{
		List->RequestListRefresh();
	}
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
	PresetBox->AddSlot().AutoHeight()
	[
		SNew(STextBlock).Font(MenuFont(9)).ColorAndOpacity(MenuDim).AutoWrapText(true)
		.Text(MenuText_(TEXT("Встроенные - ассеты в /Game/Jam/Debug/Presets, свои - JSON в Saved/JamDebug/Presets. «Применить» заменяет текущий конфиг, «поверх» добавляет к нему.")))
	];
	PresetBox->AddSlot().AutoHeight()[MenuButton(TEXT("обновить список"), [this]() { RebuildPresets(); })];
	TArray<FJamDebugPreset> Presets;
	DebugMenu->ListPresets(Presets);
	for (const FJamDebugPreset& Preset : Presets)
	{
		const FString Name = Preset.Name;
		PresetBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(MenuFont(9)).ColorAndOpacity(MenuText).ToolTipText(MenuText_(Preset.Source))
				.Text(MenuText_(FString::Printf(TEXT("%s · %s"), *Name, Preset.bAsset ? TEXT("ассет") : TEXT("JSON"))))
			]
			+ SHorizontalBox::Slot().AutoWidth()[MenuButton(TEXT("применить"), [Weak, Name]() { if (UJamDebugSubsystem* D = Weak.Get()) { D->Import(Name, true); } })]
			+ SHorizontalBox::Slot().AutoWidth()[MenuButton(TEXT("поверх"), [Weak, Name]() { if (UJamDebugSubsystem* D = Weak.Get()) { D->Import(Name, false); } })]
		];
	}
}

TSharedRef<SWidget> SJamDebugMenu::MakePinned()
{
	const TWeakObjectPtr<UJamDebugSubsystem> Weak = Debug;
	return SNew(SBorder)
		.BorderImage(MenuWhite())
		.BorderBackgroundColor(MenuPinnedBack)
		.Padding(8.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Font(MenuMono(9)).ColorAndOpacity(MenuText).Text_Lambda([Weak]()
				{
					float Fps = 0.0f, Ms = 0.0f, MaxMs = 0.0f;
					const UWorld* World = Weak.IsValid() ? Weak->GetGameInstance()->GetWorld() : nullptr;
					const AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
					if (Weak.IsValid())
					{
						Weak->Measure(1.0f, Fps, Ms, MaxMs);
					}
					return MenuText_(FString::Printf(TEXT("%.1f FPS · %.2f мс · время × %.2f%s"), Fps, Ms, Settings ? Settings->GetEffectiveTimeDilation() : 1.0f,
						Weak.IsValid() && Weak->IsPaused() ? TEXT(" · пауза") : TEXT("")));
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SAssignNew(PinnedBox, SVerticalBox)
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
	for (const FString& Key : DebugMenu->GetPins())
	{
		PinnedBox->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.68f)
			[
				SNew(STextBlock).Font(MenuFont(9)).Text(MenuText_(Key))
				.ColorAndOpacity_Lambda([Weak, Key]() { return FSlateColor(Weak.IsValid() && Weak->IsChanged(Key) ? MenuChanged : MenuText); })
			]
			+ SHorizontalBox::Slot().FillWidth(0.32f)
			[
				SNew(STextBlock).Font(MenuMono(9)).ColorAndOpacity(MenuText).Text_Lambda([Weak, Key]()
				{
					double Number = 0.0;
					FString Value;
					if (!Weak.IsValid())
					{
						return FText::GetEmpty();
					}
					if (Weak->GetNumber(Key, Number))
					{
						return MenuText_(FString::Printf(TEXT("%g"), Number));
					}
					return MenuText_(Weak->GetValue(Key, Value) ? Value : FString(TEXT("нет")));
				})
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
		.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.45f))
		.Padding(FMargin(8.0f, 3.0f))
		.Visibility_Lambda([Weak]() { return Weak.IsValid() && Weak->GetChangedCount() > 0 ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		[
			SNew(STextBlock).Font(MenuMono(10)).ColorAndOpacity(MenuChanged).Text_Lambda([Weak]()
			{
				return Weak.IsValid() ? MenuText_(FString::Printf(TEXT("Тест: %s · изменено %d · %s"),
					Weak->GetConfigName().IsEmpty() ? TEXT("без имени") : *Weak->GetConfigName(), Weak->GetChangedCount(), *UJamDebugSubsystem::GetBuildHash())) : FText::GetEmpty();
			})
		];
}

void SJamDebugMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	UJamDebugSubsystem* DebugMenu = Debug.Get();
	if (!DebugMenu)
	{
		return;
	}
	if (DebugMenu->GetGeneration() != SeenGeneration || (bChangedOnly && DebugMenu->GetChangedCount() != SeenChanged)
		|| (Tab == TEXT("Закреплено") && DebugMenu->GetPins() != SeenPins))
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
	if (SkippedBox.IsValid() && DebugMenu->LastSkipped.Num() != SeenSkipped)
	{
		SeenSkipped = DebugMenu->LastSkipped.Num();
		SkippedBox->ClearChildren();
		if (SeenSkipped > 0)
		{
			SkippedBox->AddSlot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(MenuBold(9)).ColorAndOpacity(MenuError).Text(MenuText_(FString::Printf(TEXT("пропущено при импорте: %d"), SeenSkipped)))
				]
				+ SHorizontalBox::Slot().AutoWidth()[MenuButton(TEXT("скрыть"), [this]() { if (UJamDebugSubsystem* D = Debug.Get()) { D->LastSkipped.Reset(); } })]
			];
			for (const FString& Skipped : DebugMenu->LastSkipped)
			{
				SkippedBox->AddSlot().AutoHeight()
				[
					SNew(STextBlock).Font(MenuMono(8)).ColorAndOpacity(MenuError).AutoWrapText(true).Text(MenuText_(Skipped))
				];
			}
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
	Tab = Name;
	SetSearch(FString());
	if (Tab == TEXT("Пресеты"))
	{
		RebuildPresets();
	}
	Rebuild();
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
