#include "OSAttachMenu.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "MindKore.h"
#include "OSScope.h"
#include "OSWeapons.h"
#include "OSWeldProjector.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "OSAttachMenu"

namespace
{
	// The picture's layout, raster pixels (Docs/Attachments.md, «Картинка»).
	constexpr int32 PicLeft = 24;
	constexpr int32 PicRight = 824;
	constexpr int32 HeaderY = 16;
	constexpr int32 RuleY = 48;
	constexpr int32 RowsY = 96;
	constexpr int32 RowStep = 72;
	constexpr int32 RowHeight = 50;
	constexpr int32 MoreY = 400;
	constexpr int32 FootRuleY = 432;
	constexpr int32 KeysY = 446;
	constexpr int32 MidY = UOSSuitProjectorComponent::RasterHeight / 2;
}

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
	Projector = GetOwner()->FindComponentByClass<UOSSuitProjectorComponent>();
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
	Selected = 0;
	PendingStep = 0;
	State = EState::Opening;
	OpenAt = GetWorld()->GetTimeSeconds();
	// The first picture on the next device tick: a request is taken at once, the light changes on the clock.
	StateTick = UOSSuitProjectorComponent::TickAt(OpenAt) + 1;
	LastTick = MIN_int64;
	BracketMount = INDEX_NONE;
	BracketSince = -100.0;
	if (Projector)
	{
		Projector->Acquire(EOSProjectorClient::Menu);
	}
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
	if (Projector)
	{
		Projector->Release(EOSProjectorClient::Menu);
	}
	Finish(EOSAttachMenuOutcome::Killed);
}

void UOSAttachMenu::BeginClose(bool bApply)
{
	State = EState::Closing;
	StateTick = UOSSuitProjectorComponent::TickAt(GetWorld()->GetTimeSeconds());
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
	if (Projector)
	{
		Projector->Release(EOSProjectorClient::Menu);
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
	// No wall within reach: the selected row and its verb, one line in the same font (§2, «Прерывания»).
	if (State != EState::Open || (Projector && Projector->HasWall() && Projector->HasPictureFunction()))
	{
		return FString();
	}
	if (!Rows.IsValidIndex(Selected))
	{
		return LOCTEXT("NoneHud", "ОБВЕСА НЕТ").ToString();
	}
	const FRow& Row = Rows[Selected];
	return FString::Printf(TEXT("%s · %s ▸ TAB — %s"), *Row.Name.ToString(), Row.bOn ? TEXT("НАДЕТ") : TEXT("В СУМКЕ"), *Verb().ToString());
}

bool UOSAttachMenu::Bracket(double Now, FVector2D& OutCentre, float& OutSize)
{
	const FOSWeapon* Weapon = Guns ? Guns->GetWeapon() : nullptr;
	const int32 Mount = Rows.IsValidIndex(Selected) ? Rows[Selected].Mount
		: (Weapon && !Weapon->Mounts.IsEmpty() ? 0 : INDEX_NONE);
	if (!Weapon || !Projector || !Guns->ViewModel || !Weapon->Mounts.IsValidIndex(Mount) || Weapon->Mounts[Mount].Type == EOSMountType::Magazine)
	{
		return false;
	}
	const FTransform Frame = Guns->GetMountFrame(Weapon->Mounts[Mount]) * Guns->ViewModel->GetComponentTransform();
	FVector2D Pixel;
	if (!Projector->RasterOf(Frame.GetLocation(), Pixel))
	{
		return false;
	}
	const float Size = Projector->PixelsAt(BracketCm, float(FVector::Dist(Frame.GetLocation(), Projector->GetComponentLocation())));
	if (Mount != BracketMount)
	{
		// The jump to another place flies (the weld frame's corners' curve); the first one only appears.
		BracketFromPx = BracketMount == INDEX_NONE ? Pixel : BracketToPx;
		BracketFromSize = BracketMount == INDEX_NONE ? Size : BracketFromSize;
		BracketMount = Mount;
		BracketSince = Now;
	}
	BracketToPx = Pixel;
	const float Flight = UOSWeldProjectorComponent::FlightCurve(float((Now - BracketSince) / BracketFlightSeconds), 0.0f, 0.08f);
	OutCentre = FMath::Lerp(BracketFromPx, Pixel, Flight);
	OutSize = FMath::Lerp(BracketFromSize, Size, Flight);
	BracketFromSize = Flight >= 1.0f ? Size : BracketFromSize;
	return true;
}

void UOSAttachMenu::Draw(TArray<FOSWeldBox>& Boxes, double Now, float Level) const
{
	const FOSProjectorPaint Paint{ Boxes };
	const FOSWeapon* Weapon = Guns ? Guns->GetWeapon() : nullptr;
	if (!Weapon)
	{
		return;
	}
	// The header: the gun's name typed at TypeSignsPerSecond, the selected row's place on the right.
	const FString Title = Weapon->Name.ToString().ToUpper();
	const int32 Typed = FMath::Clamp(FMath::FloorToInt32(float(Now - OpenAt) * TypeSignsPerSecond), 0, Title.Len());
	Paint.Text(FStringView(*Title, Typed), PicLeft, HeaderY, 2, 0.8f * Level);
	if (Rows.IsValidIndex(Selected) && Weapon->Mounts.IsValidIndex(Rows[Selected].Mount))
	{
		Paint.TextRight(OSMountName(Weapon->Mounts[Rows[Selected].Mount].Type).ToString(), PicRight, HeaderY, 2, 0.8f * Level);
	}
	Paint.Dotted(PicLeft, PicRight, RuleY, 0.75f * Level);
	Paint.Dotted(PicLeft, PicRight, FootRuleY, 0.75f * Level);
	if (Rows.IsEmpty())
	{
		const FString None = LOCTEXT("NoneRow", "ОБВЕСА НЕТ").ToString();
		Paint.Text(None, (PicLeft + PicRight - FOSProjectorPaint::TextWidth(None, 2)) / 2, MidY - 10, 2, 0.45f * Level);
		Paint.TextRight(LOCTEXT("KeyRelease", "▸ ОТПУСТИТЬ TAB").ToString(), PicRight, KeysY, 2, 1.0f * Level);
		return;
	}
	// The rows: a window of VisibleRows round the selected one.
	const int32 First = FMath::Clamp(Selected - VisibleRows + 1, 0, FMath::Max(Rows.Num() - VisibleRows, 0));
	for (int32 Index = First; Index < FMath::Min(First + VisibleRows, Rows.Num()); ++Index)
	{
		const FRow& Row = Rows[Index];
		const int32 Y = RowsY + (Index - First) * RowStep;
		if (Row.bEmpty)
		{
			Paint.Text(Row.Name.ToString(), PicLeft + 16, Y + 8, 2, 0.45f * Level);
			continue;
		}
		const bool bSelected = Index == Selected;
		if (bSelected)
		{
			// The corners light with the core laser on the tick they jumped (one pale stroke, as the weld ring's flash).
			Paint.Corners(PicLeft, Y, PicRight - PicLeft, RowHeight, 10, 3, 1.0f * Level, bJumped ? 0.8f * Level : 0.0f);
		}
		const int32 Cell = bSelected ? 3 : 2;
		Paint.Text(Row.Name.ToString(), PicLeft + 16, Y + (bSelected ? 6 : 8), Cell, (bSelected ? 1.0f : 0.8f) * Level);
		Paint.Text(Row.Description.ToString(), PicLeft + 16, Y + 34, 2, 0.45f * Level);
		// The state: the word cut out of a plate (on) or in its outline (in the pouch).
		const FString StateWord = Row.bOn ? LOCTEXT("StateOn", "НАДЕТ").ToString() : LOCTEXT("StatePouch", "В СУМКЕ").ToString();
		const int32 Width = FOSProjectorPaint::TextWidth(StateWord, 2) + 12;
		const int32 X = PicRight - 16 - Width;
		if (Row.bOn)
		{
			Paint.Box(X, Y + 4, Width, 28, 0.6f * Level);
			Paint.Text(StateWord, X + 6, Y + 8, 2, 0.0f, 0.0f, /*bCut=*/true);
		}
		else
		{
			Paint.Outline(X, Y + 4, Width, 28, 2, 0.6f * Level);
			Paint.Text(StateWord, X + 6, Y + 8, 2, 0.8f * Level);
		}
	}
	if (Rows.Num() > First + VisibleRows)
	{
		Paint.Text(FString::Printf(TEXT("▾ ЕЩЁ %d"), Rows.Num() - First - VisibleRows), PicLeft + 16, MoreY, 2, 0.75f * Level);
	}
	Paint.Text(FString::Printf(TEXT("▸ ОТПУСТИТЬ TAB — %s"), *Verb().ToString()), PicLeft, KeysY, 2, 1.0f * Level);
	if (Rows.FilterByPredicate([](const FRow& Each) { return !Each.bEmpty; }).Num() > 1)
	{
		Paint.TextRight(LOCTEXT("KeyWheel", "КОЛЕСО — ВЫБОР").ToString(), PicRight, KeysY, 2, 0.75f * Level);
	}
}

void UOSAttachMenu::Compose(int64 Tick, double Now)
{
	TArray<FOSWeldBox> Boxes;
	const FOSProjectorPaint Paint{ Boxes };
	const int64 Into = Tick - StateTick; // device ticks into the state, from 0
	const bool bWall = Projector && Projector->HasWall();
	const auto Point = [&Paint]() { Paint.Box(422, MidY - 2, 4, 4, 1.0f); };
	const auto Line = [&Paint]() { Paint.Box(PicLeft, MidY - 1, PicRight - PicLeft, 2, 0.8f); };
	if (bWall)
	{
		if (State == EState::Opening)
		{
			if (Into == 0) { Point(); }
			else if (Into == 1) { Line(); }
			else if (Into == 2)
			{
				// The mirror's sweep: the line gone up to the top rule, leaving it behind.
				Paint.Dotted(PicLeft, PicRight, RuleY, 0.75f);
				Paint.Box(PicLeft, MidY - 96, PicRight - PicLeft, 2, 0.8f);
			}
			else
			{
				Draw(Boxes, Now, 1.0f);
				if (Into == 3)
				{
					// The squeezed frame: everything at 0.4 of its height about the centre.
					for (FOSWeldBox& Box : Boxes)
					{
						Box.Y = int16(MidY + FMath::RoundToInt32((Box.Y - MidY) * 0.4f));
						Box.H = int16(FMath::Max(1, FMath::RoundToInt32(Box.H * 0.4f)));
					}
				}
			}
		}
		else if (State == EState::Open)
		{
			Draw(Boxes, Now, 1.0f);
		}
		else if (State == EState::Closing)
		{
			if (Into == 0) { Draw(Boxes, Now, 0.45f); }
			else if (Into == 1) { Line(); }
			else if (Into == 2) { Point(); }
		}
	}
	// The bracket on the gun: lit in every state but the last closing ticks, with or without a wall.
	FVector2D At;
	float Size = 0.0f;
	if ((State == EState::Open || State == EState::Opening || (State == EState::Closing && Into == 0)) && Bracket(Now, At, Size))
	{
		const int32 Side = FMath::Max(FMath::RoundToInt32(Size), 6);
		Paint.Corners(FMath::RoundToInt32(At.X) - Side / 2, FMath::RoundToInt32(At.Y) - Side / 2, Side, Side, FMath::Max(Side / 3, 2), 2,
			State == EState::Closing ? 0.45f : 0.9f);
	}
	if (Projector)
	{
		Projector->SetPicture(MoveTemp(Boxes));
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
	bJumped = false;
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
			bJumped = true;
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
	// The picture on the device's ticks; in the open and the close the bracket follows the moving gun every frame.
	const bool bAnimating = State != EState::Open || Now - BracketSince < BracketFlightSeconds;
	if (bDeviceTick || bAnimating)
	{
		Compose(Tick, Now);
	}
}

#undef LOCTEXT_NAMESPACE
