#include "OSAttachMenu.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "Kismet/GameplayStatics.h"
#include "MindKore.h"
#include "OSScope.h"
#include "OSWeapons.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "OSAttachMenu"

UOSAttachMenu::UOSAttachMenu()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Closed it costs nothing: no tick (Press turns it on).
	PrimaryComponentTick.bStartWithTickEnabled = false;
	static ConstructorHelpers::FObjectFinder<USoundBase> Relay(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_Stow_Pistol.SW_OS_Stow_Pistol"));
	RelaySound = Relay.Object;
}

void UOSAttachMenu::BeginPlay()
{
	Super::BeginPlay();
	Guns = GetOwner()->FindComponentByClass<UOSWeaponsComponent>();
	Camera = GetOwner()->FindComponentByClass<UCameraComponent>();
	SetComponentTickEnabled(false);
}

void UOSAttachMenu::Press()
{
	if (!Guns || IsActive())
	{
		return;
	}
	const FOSWeapon* Weapon = Guns->GetWeapon();
	// No gun in hand (a tool, a body, none yet): Tab does nothing and the HUD stays out of it.
	if (!Weapon || Guns->GetShownWeapon() != Guns->CurrentWeapon || Guns->IsSeating())
	{
		return;
	}
	// The magazine in flight takes both hands: no menu (a quiet word on the HUD).
	if (Guns->GetReloadRemaining() > 0.0f)
	{
		Refusal = LOCTEXT("Reloading", "перезарядка").ToString();
		RefusalUntil = GetWorld()->GetTimeSeconds() + 1.0;
		LastOutcome = EOSAttachMenuOutcome::Refused;
		UE_LOG(LogMindKore, Log, TEXT("Attach menu: refused, reloading"));
		return;
	}
	// The shot's cycle (slide, pump, bolt): it opens when the cycle is through, if Tab is still down.
	if (Weapon->SinceShot < Weapon->FireReadySince)
	{
		bWaiting = true;
		SetComponentTickEnabled(true);
		UE_LOG(LogMindKore, Log, TEXT("Attach menu: waiting for the cycle"));
		return;
	}
	Open();
}

void UOSAttachMenu::Open()
{
	bWaiting = false;
	ReadRows();
	// The first place's first thing (the rows go place by place; an empty place is never selected).
	Selected = FMath::Max(Rows.IndexOfByPredicate([](const FRow& Each) { return !Each.bEmpty; }), 0);
	PendingStep = 0;
	State = EState::Opening;
	OpenAt = GetWorld()->GetTimeSeconds();
	// The first picture on the next device tick: a request is taken at once, the light changes on the clock.
	StateTick = UOSSuitProjectorComponent::TickAt(OpenAt) + 1;
	LastTick = MIN_int64;
	BoxAt.Reset();
	if (Guns->Scope)
	{
		Guns->Scope->SetMenuHeld(true);
	}
	// The relay in the suit, quieter than the holster it stands in for.
	if (RelaySound)
	{
		UGameplayStatics::PlaySound2D(this, RelaySound, 0.3f, 1.4f);
	}
	SetComponentTickEnabled(true);
	UE_LOG(LogMindKore, Log, TEXT("Attach menu: open, %d rows: %s"), Rows.Num(), *DescribeRows());
}

void UOSAttachMenu::Release()
{
	if (bWaiting)
	{
		bWaiting = false;
		Finish(EOSAttachMenuOutcome::Cancelled);
		return;
	}
	if (State == EState::Opening)
	{
		BeginClose(false);
	}
	else if (State == EState::Open)
	{
		BeginClose(true);
	}
}

void UOSAttachMenu::ApplyNow()
{
	if (State == EState::Open)
	{
		BeginClose(true);
	}
}

void UOSAttachMenu::Step(int32 Direction)
{
	if (State == EState::Open && Rows.FilterByPredicate([](const FRow& Each) { return !Each.bEmpty; }).Num() > 1 && Direction != 0)
	{
		PendingStep += Direction > 0 ? 1 : -1;
	}
}

void UOSAttachMenu::Kill()
{
	if (!IsActive())
	{
		return;
	}
	bWaiting = false;
	Finish(EOSAttachMenuOutcome::Killed);
}

void UOSAttachMenu::BeginClose(bool bApply)
{
	State = EState::Closing;
	ClosingAt = GetWorld()->GetTimeSeconds();
	StateTick = UOSSuitProjectorComponent::TickAt(ClosingAt);
	LastTick = MIN_int64;
	// The Apply on the first closing tick - now: the module leaves the pouch as the frame dims.
	bApplyOnClose = bApply && ApplySelected();
	LastOutcome = bApplyOnClose ? EOSAttachMenuOutcome::Applied : EOSAttachMenuOutcome::Cancelled;
	UE_LOG(LogMindKore, Log, TEXT("Attach menu: closing, %s"), bApplyOnClose ? TEXT("applied") : TEXT("nothing applied"));
}

void UOSAttachMenu::Finish(EOSAttachMenuOutcome Outcome)
{
	const bool bWasLit = State != EState::Closed;
	State = EState::Closed;
	ClosedAt = GetWorld()->GetTimeSeconds();
	LastOutcome = Outcome;
	bApplyOnClose = false;
	if (RelaySound && bWasLit)
	{
		UGameplayStatics::PlaySound2D(this, RelaySound, 0.25f, 1.25f);
	}
	if (Guns && Guns->Scope)
	{
		Guns->Scope->SetMenuHeld(WantsMenuPose());
	}
	UE_LOG(LogMindKore, Log, TEXT("Attach menu: closed (%s)"), *UEnum::GetValueAsString(LastOutcome));
}

bool UOSAttachMenu::WantsMenuPose() const
{
	return (State != EState::Closed) || (Guns && Guns->IsSeating());
}

bool UOSAttachMenu::IsGunFree() const
{
	if (State != EState::Closed || bWaiting || !Guns || Guns->IsSeating())
	{
		return false;
	}
	const UWorld* World = GetWorld();
	return (!World || World->GetTimeSeconds() - ClosedAt >= ReturnSeconds) && (!Guns->Scope || Guns->Scope->GetMenuAlpha() < 0.05f);
}

void UOSAttachMenu::ReadRows()
{
	Rows.Reset();
	const FOSWeapon* Weapon = Guns ? Guns->GetWeapon() : nullptr;
	if (!Weapon)
	{
		return;
	}
	// What fits the gun in hand, on it or in the pouch (one carried for another gun shows too: Apply moves it), in the catalog's order.
	for (const FOSAttachment& Each : Guns->Attachments)
	{
		if (!Each.FitsGun(Weapon->Id) || !Guns->HasAttachment(Each.Id))
		{
			continue;
		}
		FRow Row;
		Row.Id = Each.Id;
		Row.Name = Each.Name;
		Row.Description = Each.Description;
		Row.bMagazine = Each.MountType == EOSMountType::Magazine;
		for (int32 Mount = 0; Mount < Weapon->Mounts.Num(); ++Mount)
		{
			if (Weapon->Mounts[Mount].Mounted == Each.Id)
			{
				Row.Mount = Mount;
				Row.bOn = true;
			}
		}
		if (Row.Mount == INDEX_NONE)
		{
			Row.Mount = Guns->FindMountFor(Guns->CurrentWeapon, Each);
		}
		if (Row.Mount != INDEX_NONE)
		{
			Rows.Add(Row);
		}
	}
	if (Rows.IsEmpty())
	{
		return; // «ОБВЕСА НЕТ»
	}
	// Place by place (the wheel walks the gun's places in order), the catalog's order inside a place.
	Rows.StableSort([](const FRow& A, const FRow& B) { return A.Mount < B.Mount; });
	// The places nothing carried goes on, dim at the end (the RMB-93's top).
	for (int32 Mount = 0; Mount < Weapon->Mounts.Num(); ++Mount)
	{
		if (Weapon->Mounts[Mount].Type != EOSMountType::Magazine && !Rows.ContainsByPredicate([Mount](const FRow& Each) { return Each.Mount == Mount; }))
		{
			FRow Row;
			Row.Name = FText::Format(LOCTEXT("EmptyPlace", "{0} — ПУСТО"), OSMountName(Weapon->Mounts[Mount].Type));
			Row.Mount = Mount;
			Row.bEmpty = true;
			Rows.Add(Row);
		}
	}
	Rows.StableSort([](const FRow& A, const FRow& B) { return A.Mount < B.Mount; });
}

FText UOSAttachMenu::Verb() const
{
	if (!Rows.IsValidIndex(Selected) || Rows[Selected].bEmpty)
	{
		return FText::GetEmpty();
	}
	const FRow& Row = Rows[Selected];
	if (Row.bMagazine)
	{
		return Row.bOn ? LOCTEXT("StockBack", "ВЕРНУТЬ ШТАТНЫЙ") : LOCTEXT("Change", "СМЕНИТЬ");
	}
	return Row.bOn ? LOCTEXT("Off", "СНЯТЬ") : LOCTEXT("On", "НАДЕТЬ");
}

bool UOSAttachMenu::ApplySelected()
{
	if (!Guns || !Rows.IsValidIndex(Selected) || Rows[Selected].bEmpty)
	{
		return false;
	}
	const FRow& Row = Rows[Selected];
	const FOSAttachment* Attachment = Guns->FindAttachment(Row.Id);
	if (Attachment && Attachment->bHasSwitch && !Row.bOn)
	{
		Guns->NoteSwitchChoice(Row.Id);
	}
	return Guns->ApplyAttachment(Row.Mount, Row.bOn ? NAME_None : Row.Id);
}

FString UOSAttachMenu::DescribeRows() const
{
	if (Rows.IsEmpty())
	{
		return LOCTEXT("None", "ОБВЕСА НЕТ").ToString();
	}
	FString Text;
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		Text += FString::Printf(TEXT("%s%s%s [%s]"), Index ? TEXT(", ") : TEXT(""), Index == Selected ? TEXT(">") : TEXT(""), *Rows[Index].Name.ToString(),
			Rows[Index].bEmpty ? TEXT("-") : Rows[Index].bOn ? TEXT("НАДЕТ") : TEXT("В СУМКЕ"));
	}
	return Text;
}

FString UOSAttachMenu::GetHudLine() const
{
	const UWorld* World = GetWorld();
	if (World && World->GetTimeSeconds() < RefusalUntil)
	{
		return Refusal;
	}
	return FString();
}

bool OSProjectToHud(const UCameraComponent* Camera, const UCanvas& Canvas, const FVector& World, bool bFirstPerson, FVector2D& Out)
{
	if (!Camera)
	{
		return false;
	}
	// The first-person scale toward the eye does not move a point on screen; only the field of view does.
	const FVector Local = Camera->GetComponentTransform().InverseTransformPositionNoScale(World);
	if (Local.X < 1.0f)
	{
		return false;
	}
	const float FieldOfView = bFirstPerson && Camera->bEnableFirstPersonFieldOfView ? Camera->FirstPersonFieldOfView : Camera->FieldOfView;
	const float Half = Canvas.ClipX * 0.5f;
	const float Scale = Half / FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(FieldOfView, 5.0f, 170.0f) * 0.5f));
	Out = FVector2D(Half + Local.Y / Local.X * Scale, Canvas.ClipY * 0.5f - Local.Z / Local.X * Scale);
	return true;
}

bool UOSAttachMenu::ProjectGun(const UCanvas& Canvas, const FVector& World, FVector2D& Out) const
{
	return OSProjectToHud(Camera, Canvas, World, true, Out);
}

bool UOSAttachMenu::MountAnchor(int32 Mount, FVector& Out) const
{
	const FOSWeapon* Weapon = Guns ? Guns->GetWeapon() : nullptr;
	if (!Weapon || !Guns->ViewModel || !Weapon->Mounts.IsValidIndex(Mount))
	{
		return false;
	}
	const FOSWeaponMount& Place = Weapon->Mounts[Mount];
	if (Place.Type == EOSMountType::Magazine)
	{
		// The well: the magazine's foot (its mesh's lowest face, a little up), else the gun's lowest point.
		const UStaticMeshComponent* Mag = Guns->MagInView;
		if (Mag && Mag->GetStaticMesh() && Mag->IsVisible())
		{
			const FBox Box = Mag->GetStaticMesh()->GetBoundingBox();
			const FVector Foot(Box.GetCenter().X, Box.GetCenter().Y, Box.Min.Z + Box.GetSize().Z * 0.12f);
			Out = Mag->GetComponentTransform().TransformPosition(Foot);
			return true;
		}
		const FBox Box = Guns->ViewModel->GetStaticMesh()->GetBoundingBox();
		Out = Guns->ViewModel->GetComponentTransform().TransformPosition(FVector(Box.GetCenter().X, Box.GetCenter().Y, Box.Min.Z));
		return true;
	}
	Out = (Guns->GetMountFrame(Place) * Guns->ViewModel->GetComponentTransform()).GetLocation();
	return true;
}

void UOSAttachMenu::DrawHud(AHUD& Hud, UCanvas& Canvas)
{
	const UWorld* World = GetWorld();
	const FOSWeapon* Weapon = Guns ? Guns->GetWeapon() : nullptr;
	if (!World || State == EState::Closed || !Weapon || !Guns->ViewModel || !Guns->ViewModel->GetStaticMesh())
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	const float DeltaTime = float(FMath::Clamp(Now - LastDrawAt, 0.0, 0.1));
	const bool bFirst = Now - LastDrawAt > 0.2;
	LastDrawAt = Now;

	// The open: the leaders run out over the first 60 % of the opening ticks, then the boxes type in; the close fades.
	const float OpenSpan = float(OpenTicks / OSProjectorClockHz);
	const float CloseSpan = float(CloseTicks / OSProjectorClockHz);
	const float Grow = State == EState::Opening ? FMath::Clamp(float(Now - OpenAt) / OpenSpan, 0.0f, 1.0f) : 1.0f;
	const float Fade = State == EState::Closing ? 1.0f - FMath::Clamp(float(Now - ClosingAt) / CloseSpan, 0.0f, 1.0f) : 1.0f;
	const float LeaderShare = FMath::Clamp(Grow / 0.6f, 0.0f, 1.0f);
	const bool bBoxes = Grow >= 0.6f;
	const int32 Typed = FMath::Max(0, FMath::FloorToInt32((float(Now - OpenAt) - 0.6f * OpenSpan) * TypeSignsPerSecond * 1.5f));

	// The font's cell: 2 px at 1080 lines, the device's own look at any resolution.
	const float Cell = FMath::Max(1.0f, FMath::RoundToFloat(Canvas.ClipY / 540.0f));
	const float Gap = GapCells * Cell;
	const auto Colour = [this, Fade](float Level)
	{
		return FLinearColor(HudColour.R, HudColour.G, HudColour.B, FMath::Clamp(Level, 0.0f, 1.0f) * Fade);
	};
	const auto Snap = [Cell](float Value) { return FMath::RoundToFloat(Value / Cell) * Cell; };
	const auto Rect = [&Hud](const FLinearColor& Tint, float X, float Y, float W, float H)
	{
		if (W > 0.0f && H > 0.0f)
		{
			Hud.DrawRect(Tint, X, Y, W, H);
		}
	};
	const auto Text = [&](FStringView Line, float X, float Y, const FLinearColor& Tint)
	{
		OSProjectorTextRuns(Line, [&](int32 RunX, int32 RunY, int32 Length)
		{
			Hud.DrawRect(Tint, X + RunX * Cell, Y + RunY * Cell, Length * Cell, Cell);
		});
	};
	const auto Width = [Cell](FStringView Line) { return OSProjectorTextCells(Line) * Cell; };
	const auto Outline = [&](float X, float Y, float W, float H, const FLinearColor& Tint)
	{
		Rect(Tint, X, Y, W, Cell);
		Rect(Tint, X, Y + H - Cell, W, Cell);
		Rect(Tint, X, Y + Cell, Cell, H - 2.0f * Cell);
		Rect(Tint, X + W - Cell, Y + Cell, Cell, H - 2.0f * Cell);
	};
	const auto Corners = [&](float X, float Y, float W, float H, float Arm, const FLinearColor& Tint)
	{
		for (const float CX : { X, X + W - Cell })
		{
			for (const float CY : { Y, Y + H - Cell })
			{
				Rect(Tint, CX == X ? X : X + W - Arm, CY, Arm, Cell);
				Rect(Tint, CX, CY == Y ? Y : Y + H - Arm, Cell, Arm);
			}
		}
	};

	// The keys, centred low: the selected thing's line, then what the release does.
	const float FootY = Snap(Canvas.ClipY - 150.0f);
	const FString VerbWord = Verb().ToString();
	const FString Keys = Rows.IsEmpty() ? LOCTEXT("KeyRelease", "ОТПУСТИТЬ TAB").ToString()
		: Rows.FilterByPredicate([](const FRow& Each) { return !Each.bEmpty; }).Num() > 1
			? FString::Printf(TEXT("КОЛЕСО — ВЫБОР    ОТПУСТИТЬ TAB — %s"), *VerbWord)
			: FString::Printf(TEXT("ОТПУСТИТЬ TAB — %s"), *VerbWord);
	if (bBoxes)
	{
		if (Rows.IsValidIndex(Selected) && !Rows[Selected].bEmpty)
		{
			const FString About = FString::Printf(TEXT("%s — %s"), *Rows[Selected].Name.ToString(), *Rows[Selected].Description.ToString());
			Text(About, Snap((Canvas.ClipX - Width(About)) * 0.5f), FootY - 14.0f * Cell, Colour(0.6f));
		}
		Text(Keys, Snap((Canvas.ClipX - Width(Keys)) * 0.5f), FootY, Colour(0.9f));
	}
	if (Rows.IsEmpty())
	{
		const FString None = LOCTEXT("NoneRow", "ОБВЕСА НЕТ").ToString();
		Text(None, Snap((Canvas.ClipX - Width(None)) * 0.5f), Snap(Canvas.ClipY * 0.35f), Colour(0.6f * Grow));
		return;
	}

	// The gun's outline on screen: its mesh's box, corner by corner. The boxes keep out of it.
	FBox2D Gun(ForceInit);
	{
		const FBox Local = Guns->ViewModel->GetStaticMesh()->GetBoundingBox();
		const FTransform ToWorld = Guns->ViewModel->GetComponentTransform();
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Point((Corner & 1) ? Local.Max.X : Local.Min.X, (Corner & 2) ? Local.Max.Y : Local.Min.Y, (Corner & 4) ? Local.Max.Z : Local.Min.Z);
			FVector2D Pixel;
			if (ProjectGun(Canvas, ToWorld.TransformPosition(Point), Pixel))
			{
				Gun += Pixel;
			}
		}
	}
	if (!Gun.bIsValid)
	{
		return;
	}
	const FVector2D GunCentre = Gun.GetCenter();
	const FVector2D GunHalf = FVector2D::Max(Gun.GetExtent(), FVector2D(1.0f, 1.0f));
	const FBox2D GunKeepOut(Gun.Min - FVector2D(Gap * 0.5f), Gun.Max + FVector2D(Gap * 0.5f));

	// The places in the rows' order, each with its lines.
	struct FSlot
	{
		int32 Mount = INDEX_NONE;
		TArray<int32> Lines;
		FVector2D Node = FVector2D::ZeroVector;
		bool bNode = false;
		FVector2D Size = FVector2D::ZeroVector;
		FVector2D At = FVector2D::ZeroVector;
		int32 Side = 0; // 0 right, 1 left, 2 up, 3 down
	};
	TArray<FSlot> Slots;
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		if (Slots.IsEmpty() || Slots.Last().Mount != Rows[Index].Mount)
		{
			Slots.AddDefaulted_GetRef().Mount = Rows[Index].Mount;
		}
		Slots.Last().Lines.Add(Index);
	}
	const FString OnWord = LOCTEXT("StateOn", "НАДЕТ").ToString();
	const FString EmptyWord = LOCTEXT("EmptyWord", "ПУСТО").ToString();
	const float LineStep = 12.0f * Cell;
	const float Pad = 4.0f * Cell;
	const float HeadH = 14.0f * Cell;
	const FVector2D Screen(Canvas.ClipX, Canvas.ClipY);
	for (FSlot& Slot : Slots)
	{
		FVector Anchor;
		Slot.bNode = MountAnchor(Slot.Mount, Anchor) && ProjectGun(Canvas, Anchor, Slot.Node);
		if (!Slot.bNode)
		{
			Slot.Node = GunCentre;
		}
		float Inner = Width(OSMountName(Weapon->Mounts[Slot.Mount].Type).ToString());
		for (const int32 Line : Slot.Lines)
		{
			const FRow& Row = Rows[Line];
			Inner = FMath::Max(Inner, Row.bEmpty ? Width(EmptyWord) + 6.0f * Cell : 6.0f * Cell + Width(Row.Name.ToString()) + (Row.bOn ? 6.0f * Cell + Width(OnWord) + 4.0f * Cell : 0.0f));
		}
		Slot.Size = FVector2D(Snap(Inner + 2.0f * Pad), Snap(HeadH + Slot.Lines.Num() * LineStep + Pad));

		// The side of the gun the node is on (the screen is wide: left and right first), then the others if it does not fit.
		const FVector2D Off = (Slot.Node - GunCentre) / GunHalf;
		const int32 Preferred = FMath::Abs(Off.X) >= FMath::Abs(Off.Y) * 0.6f ? (Off.X >= 0.0f ? 0 : 1) : (Off.Y < 0.0f ? 2 : 3);
		const int32 Order[4][4] = { { 0, 1, 2, 3 }, { 1, 0, 2, 3 }, { 2, 3, 1, 0 }, { 3, 2, 1, 0 } };
		const auto Place = [&](int32 Side)
		{
			FVector2D At;
			switch (Side)
			{
			case 0: At = FVector2D(Gun.Max.X + Gap, Slot.Node.Y - Slot.Size.Y * 0.5f); break;
			case 1: At = FVector2D(Gun.Min.X - Gap - Slot.Size.X, Slot.Node.Y - Slot.Size.Y * 0.5f); break;
			case 2: At = FVector2D(Slot.Node.X - Slot.Size.X * 0.5f, Gun.Min.Y - Gap - Slot.Size.Y); break;
			default: At = FVector2D(Slot.Node.X - Slot.Size.X * 0.5f, Gun.Max.Y + Gap); break;
			}
			const FVector2D High = FVector2D::Max(Screen - Slot.Size - FVector2D(Gap), FVector2D(Gap));
			return FVector2D(FMath::Clamp(At.X, double(Gap), High.X), FMath::Clamp(At.Y, double(Gap), High.Y));
		};
		Slot.Side = Preferred;
		Slot.At = Place(Preferred);
		for (const int32 Side : Order[Preferred])
		{
			const FVector2D At = Place(Side);
			if (!GunKeepOut.Intersect(FBox2D(At, At + Slot.Size)))
			{
				Slot.Side = Side;
				Slot.At = At;
				break;
			}
		}
	}
	// Boxes on one side stack apart (down for the sides, across for the top and the bottom).
	for (int32 Index = 1; Index < Slots.Num(); ++Index)
	{
		for (int32 Other = 0; Other < Index; ++Other)
		{
			FSlot& Slot = Slots[Index];
			const FSlot& Before = Slots[Other];
			const FBox2D Mine(Slot.At - FVector2D(Cell * 3.0f), Slot.At + Slot.Size + FVector2D(Cell * 3.0f));
			if (!Mine.Intersect(FBox2D(Before.At, Before.At + Before.Size)))
			{
				continue;
			}
			if (Slot.Side <= 1)
			{
				Slot.At.Y = Before.At.Y + Before.Size.Y + Cell * 4.0f;
			}
			else
			{
				Slot.At.X = Before.At.X + Before.Size.X + Cell * 4.0f;
			}
		}
	}
	// The boxes follow the gun's sway smoothed (the nodes ride the gun exactly), on the font's grid.
	if (bFirst || BoxAt.Num() != Slots.Num())
	{
		BoxAt.SetNum(Slots.Num());
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			BoxAt[Index] = Slots[Index].At;
		}
	}
	const float Follow = 1.0f - FMath::Exp(-FollowRate * DeltaTime);
	const int32 SelectedMount = Rows.IsValidIndex(Selected) ? Rows[Selected].Mount : INDEX_NONE;
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		BoxAt[Index] += (Slots[Index].At - BoxAt[Index]) * Follow;
		const FSlot& Slot = Slots[Index];
		const FVector2D At(Snap(BoxAt[Index].X), Snap(BoxAt[Index].Y));
		const bool bHere = Slot.Mount == SelectedMount;

		// The node on the gun: a dot, and the selected place's corners round it.
		const FVector2D Node(Snap(Slot.Node.X), Snap(Slot.Node.Y));
		Rect(Colour(bHere ? 1.0f : 0.7f), Node.X - Cell, Node.Y - Cell, 2.0f * Cell, 2.0f * Cell);
		if (bHere)
		{
			Corners(Node.X - 5.0f * Cell, Node.Y - 5.0f * Cell, 10.0f * Cell, 10.0f * Cell, 3.0f * Cell, Colour(0.9f));
		}

		// The leader: out of the node square to the box's near edge, an elbow on the pixel grid, grown on the open.
		FVector2D Edge;
		if (Slot.Side <= 1)
		{
			Edge = FVector2D(Slot.Side == 0 ? At.X : At.X + Slot.Size.X, FMath::Clamp(Node.Y, At.Y + Cell * 7.0f, At.Y + Slot.Size.Y - Cell * 7.0f));
		}
		else
		{
			Edge = FVector2D(FMath::Clamp(Node.X, At.X + Cell * 7.0f, At.X + Slot.Size.X - Cell * 7.0f), Slot.Side == 2 ? At.Y + Slot.Size.Y : At.Y);
		}
		Edge = FVector2D(Snap(Edge.X), Snap(Edge.Y));
		{
			const bool bAcrossFirst = Slot.Side <= 1;
			const FVector2D Bend = bAcrossFirst ? FVector2D(Snap((Node.X + Edge.X) * 0.5f), Node.Y) : FVector2D(Node.X, Snap((Node.Y + Edge.Y) * 0.5f));
			const FVector2D Corner = bAcrossFirst ? FVector2D(Bend.X, Edge.Y) : FVector2D(Edge.X, Bend.Y);
			const FVector2D Path[4] = { Node, Bend, Corner, Edge };
			float Total = 0.0f;
			for (int32 Leg = 0; Leg < 3; ++Leg)
			{
				Total += float(FVector2D::Distance(Path[Leg], Path[Leg + 1]));
			}
			float Left = Total * LeaderShare;
			const FLinearColor Tint = Colour(bHere ? 0.8f : 0.45f);
			for (int32 Leg = 0; Leg < 3 && Left > 0.0f; ++Leg)
			{
				const FVector2D From = Path[Leg];
				const FVector2D Way = Path[Leg + 1] - From;
				const float Length = FMath::Min(float(Way.Size()), Left);
				Left -= Length;
				if (Length <= 0.0f)
				{
					continue;
				}
				const FVector2D To = From + Way.GetSafeNormal() * Length;
				Rect(Tint, FMath::Min(From.X, To.X), FMath::Min(From.Y, To.Y), FMath::Abs(To.X - From.X) + Cell, FMath::Abs(To.Y - From.Y) + Cell);
			}
		}
		if (!bBoxes)
		{
			continue;
		}

		// The box: a dark plate, its frame, the place's name over a rule, its things one a line.
		Rect(FLinearColor(PlateColour.R, PlateColour.G, PlateColour.B, PlateColour.A * Fade), At.X, At.Y, Slot.Size.X, Slot.Size.Y);
		Outline(At.X, At.Y, Slot.Size.X, Slot.Size.Y, Colour(bHere ? 0.55f : 0.3f));
		if (bHere)
		{
			Corners(At.X, At.Y, Slot.Size.X, Slot.Size.Y, 4.0f * Cell, Colour(1.0f));
		}
		const FString Head = OSMountName(Weapon->Mounts[Slot.Mount].Type).ToString();
		Text(FStringView(*Head, FMath::Min(Typed, Head.Len())), At.X + Pad, At.Y + Pad, Colour(bHere ? 0.85f : 0.6f));
		for (float X = At.X + Pad; X < At.X + Slot.Size.X - Pad; X += 4.0f * Cell)
		{
			Rect(Colour(0.35f), X, At.Y + Pad + 11.0f * Cell, 2.0f * Cell, Cell);
		}
		for (int32 Line = 0; Line < Slot.Lines.Num(); ++Line)
		{
			const FRow& Row = Rows[Slot.Lines[Line]];
			const float Y = At.Y + HeadH + Line * LineStep + Cell;
			const bool bSelected = Slot.Lines[Line] == Selected;
			if (Row.bEmpty)
			{
				Text(FStringView(*EmptyWord, FMath::Min(Typed, EmptyWord.Len())), At.X + Pad + 6.0f * Cell, Y, Colour(0.35f));
				continue;
			}
			const FString Name = Row.Name.ToString();
			if (bSelected)
			{
				Text(TEXT("▸"), At.X + Pad, Y, Colour(1.0f));
			}
			Text(FStringView(*Name, FMath::Min(Typed, Name.Len())), At.X + Pad + 6.0f * Cell, Y, Colour(bSelected ? 1.0f : 0.6f));
			if (Row.bOn)
			{
				// On the gun: the word on a plate, dark out of the light.
				const float PlateW = Width(OnWord) + 4.0f * Cell;
				const float PlateX = At.X + Slot.Size.X - Pad - PlateW;
				Rect(Colour(bSelected ? 0.85f : 0.5f), PlateX, Y - Cell, PlateW, 10.0f * Cell);
				Text(OnWord, PlateX + 2.0f * Cell, Y, FLinearColor(PlateColour.R, PlateColour.G, PlateColour.B, Fade));
			}
		}
	}
}

void UOSAttachMenu::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const double Now = GetWorld()->GetTimeSeconds();
	if (bWaiting)
	{
		const FOSWeapon* Weapon = Guns ? Guns->GetWeapon() : nullptr;
		if (!Weapon)
		{
			Kill();
		}
		else if (Weapon->SinceShot >= Weapon->FireReadySince)
		{
			Open();
		}
		return;
	}
	if (State == EState::Closed)
	{
		// A module on its way keeps the gun in the menu's pose; then the tick sleeps.
		if (Guns && Guns->Scope)
		{
			Guns->Scope->SetMenuHeld(WantsMenuPose());
		}
		if (!WantsMenuPose())
		{
			SetComponentTickEnabled(false);
		}
		return;
	}
	// The gun left the hand or the view another way (a probe, a command): dark.
	if (!Guns || !Guns->GetWeapon() || Guns->GetShownWeapon() != Guns->CurrentWeapon)
	{
		Kill();
		return;
	}
	const int64 Tick = UOSSuitProjectorComponent::TickAt(Now);
	const bool bDeviceTick = Tick != LastTick;
	LastTick = Tick;
	if (Tick < StateTick)
	{
		return;
	}
	if (bDeviceTick)
	{
		const int64 Into = Tick - StateTick;
		if (State == EState::Opening && Into >= OpenTicks - 1)
		{
			State = EState::Open;
		}
		else if (State == EState::Closing && Into >= CloseTicks)
		{
			Finish(LastOutcome);
			return;
		}
		if (State == EState::Open && PendingStep != 0 && Rows.Num() > 1)
		{
			// One row a tick, round; the gun nods as the hand shows the place (the pose springs).
			const int32 Direction = PendingStep > 0 ? 1 : -1;
			PendingStep -= Direction;
			do
			{
				Selected = (Selected + Direction + Rows.Num()) % Rows.Num();
			} while (Rows[Selected].bEmpty);
			if (const FOSAttachment* Chosen = Guns->FindAttachment(Rows[Selected].Id); Chosen && Chosen->bHasSwitch)
			{
				Guns->NoteSwitchChoice(Chosen->Id);
			}
			if (Guns->Scope && Guns->Scope->Aims.IsValidIndex(Guns->CurrentWeapon))
			{
				const FOSGunMenuPose& Pose = Guns->Scope->Aims[Guns->CurrentWeapon].Menu;
				Guns->KickPose(Pose.RowKickMove, Pose.RowKickTurn);
			}
			// One click of the switch a tick.
			if (const FOSAttachment* Row = Guns->FindAttachment(Rows[Selected].Id); Row && Row->SwitchSound)
			{
				UGameplayStatics::PlaySound2D(this, Row->SwitchSound, 0.4f, 1.3f);
			}
		}
	}
	// The picture is the HUD's, every frame (DrawHud).
}

#undef LOCTEXT_NAMESPACE
