#include "OSAttachMenu.h"
#include "OSLens.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "Kismet/GameplayStatics.h"
#include "MindKore.h"
#include "OSLaserSight.h"
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
	PreviousSelected = Selected;
	SelectedAt = -100.0;
	State = EState::Opening;
	OpenAt = GetWorld()->GetTimeSeconds();
	// Input counts from the next device ticks on: a request is taken at once, the open runs on the clock.
	StateTick = UOSSuitProjectorComponent::TickAt(OpenAt) + 1;
	LastTick = MIN_int64;
	Boxes.Reset();
	bLaidOut = false;
	bAppliedAny = false;
	AppliedId = NAME_None;
	AppliedMount = INDEX_NONE;
	BusyAt = -100.0;
	bCursorPlaced = false;
	bCursorMoved = false;
	bWheelChose = false;
	bHovering = false;
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
	if (State == EState::Opening || State == EState::Open)
	{
		BeginClose();
	}
}

void UOSAttachMenu::ApplyNow()
{
	if (State != EState::Open || !Guns)
	{
		return;
	}
	// A click on nothing does nothing: the line under the cursor, or the wheel's choice while the mouse is still.
	if (!bHovering && !bWheelChose)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Guns->IsSeating() || Guns->GetReloadRemaining() > 0.0f)
	{
		BusyAt = Now;
		return;
	}
	if (!Rows.IsValidIndex(Selected) || Rows[Selected].bEmpty)
	{
		return;
	}
	const FName Id = Rows[Selected].Id;
	const int32 Mount = Rows[Selected].Mount;
	if (!ApplySelected())
	{
		BusyAt = Now;
		return;
	}
	bAppliedAny = true;
	LastOutcome = EOSAttachMenuOutcome::Applied;
	AppliedId = Id;
	AppliedMount = Mount;
	AppliedAt = Now;
	AppliedSeconds = FMath::Max3(Guns->GetSeatRemaining(), Guns->GetReloadRemaining(), 0.05f);
	// The rows again (on / in the pouch changed), the same line selected; the boxes stay unless their lines changed.
	TArray<int32> Before;
	for (const FPlaceBox& Each : Boxes)
	{
		Before.Add(Each.Lines.Num());
	}
	ReadRows();
	const int32 Again = Rows.IndexOfByPredicate([Id, Mount](const FRow& Each) { return Each.Id == Id && Each.Mount == Mount; });
	Selected = Again != INDEX_NONE ? Again : FMath::Max(Rows.IndexOfByPredicate([](const FRow& Each) { return !Each.bEmpty; }), 0);
	PreviousSelected = Selected;
	TArray<int32> After;
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		if (Index == 0 || Rows[Index].Mount != Rows[Index - 1].Mount)
		{
			After.Add(0);
		}
		++After.Last();
	}
	if (After != Before)
	{
		bLaidOut = false;
	}
	else
	{
		// The same boxes: their line indices again (the order inside a place is the catalog's either way).
		int32 Next = 0;
		for (FPlaceBox& Each : Boxes)
		{
			for (int32& Line : Each.Lines)
			{
				Line = Next++;
			}
		}
	}
	UE_LOG(LogMindKore, Log, TEXT("Attach menu: applied %s on place %d, %.2f s; the menu stays up"), *Id.ToString(), Mount, AppliedSeconds);
}

void UOSAttachMenu::Select(int32 Index)
{
	if (!Rows.IsValidIndex(Index) || Rows[Index].bEmpty || Index == Selected)
	{
		return;
	}
	PreviousSelected = Selected;
	Selected = Index;
	SelectedAt = GetWorld()->GetTimeSeconds();
	if (const FOSAttachment* Chosen = Guns ? Guns->FindAttachment(Rows[Selected].Id) : nullptr)
	{
		if (Chosen->bHasSwitch)
		{
			Guns->NoteSwitchChoice(Chosen->Id);
		}
		// The gun nods as the hand shows the place (the pose springs); one click of the switch.
		if (Guns->Scope && Guns->Scope->Aims.IsValidIndex(Guns->CurrentWeapon))
		{
			const FOSGunMenuPose& Pose = Guns->Scope->Aims[Guns->CurrentWeapon].Menu;
			Guns->KickPose(Pose.RowKickMove * 0.5f, Pose.RowKickTurn * 0.5f);
		}
		if (Chosen->SwitchSound)
		{
			UGameplayStatics::PlaySound2D(this, Chosen->SwitchSound, 0.25f, 1.5f);
		}
	}
}

void UOSAttachMenu::Step(int32 Direction)
{
	if (State != EState::Open || Direction == 0 || Rows.IsEmpty())
	{
		return;
	}
	bWheelChose = true;
	if (Rows.FilterByPredicate([](const FRow& Each) { return !Each.bEmpty; }).Num() < 2)
	{
		return;
	}
	const int32 Way = Direction > 0 ? 1 : -1;
	int32 Next = Selected;
	do
	{
		Next = (Next + Way + Rows.Num()) % Rows.Num();
	} while (Rows[Next].bEmpty);
	Select(Next);
}

void UOSAttachMenu::MoveCursor(const FVector2D& Degrees)
{
	if (!TakesMouse())
	{
		return;
	}
	// The view's pixels per degree at its centre: a turn of the view and a move of the cursor read the same.
	const float FieldOfView = Camera ? FMath::Clamp(Camera->FieldOfView, 5.0f, 170.0f) : 90.0f;
	const double PerDegree = LastClip.X * 0.5 / FMath::Tan(FMath::DegreesToRadians(FieldOfView * 0.5f)) * (UE_DOUBLE_PI / 180.0);
	const double Scale = CursorGain * PerDegree;
	Cursor.X = FMath::Clamp(Cursor.X + Degrees.X * Scale, 0.0, LastClip.X);
	Cursor.Y = FMath::Clamp(Cursor.Y - Degrees.Y * Scale, 0.0, LastClip.Y);
	if (!Degrees.IsNearlyZero())
	{
		bCursorMoved = true;
		bWheelChose = false;
	}
}

void UOSAttachMenu::BeginClose()
{
	State = EState::Closing;
	ClosingAt = GetWorld()->GetTimeSeconds();
	StateTick = UOSSuitProjectorComponent::TickAt(ClosingAt);
	LastTick = MIN_int64;
	LastOutcome = bAppliedAny ? EOSAttachMenuOutcome::Applied : EOSAttachMenuOutcome::Cancelled;
	UE_LOG(LogMindKore, Log, TEXT("Attach menu: closing, %s"), bAppliedAny ? TEXT("something applied") : TEXT("nothing applied"));
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

void UOSAttachMenu::Finish(EOSAttachMenuOutcome Outcome)
{
	const bool bWasLit = State != EState::Closed;
	State = EState::Closed;
	ClosedAt = GetWorld()->GetTimeSeconds();
	LastOutcome = Outcome;
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
	// The laser on the gun: its colour as a line under it (owner 23.10: «выбор цвета лазера»).
	const UOSLaserSightComponent* Laser = Guns->LaserSight;
	const FOSLaserTint* Tint = Laser ? Laser->GetTint() : nullptr;
	for (int32 Index = 0; Tint && Index < Rows.Num(); ++Index)
	{
		const FOSAttachment* Each = Guns->FindAttachment(Rows[Index].Id);
		if (Rows[Index].bOn && Each && Each->bLaser)
		{
			FRow Row;
			Row.Id = TEXT("LaserTint");
			Row.Name = FText::Format(LOCTEXT("TintRow", "ЦВЕТ: {0}"), Tint->Name);
			Row.Description = Tint->Description;
			Row.Mount = Rows[Index].Mount;
			Row.bTint = true;
			Rows.Insert(Row, Index + 1);
			break;
		}
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
	Rows.StableSort([](const FRow& A, const FRow& B) { return A.Mount < B.Mount; });
}

FText UOSAttachMenu::Verb() const
{
	if (!Rows.IsValidIndex(Selected) || Rows[Selected].bEmpty)
	{
		return FText::GetEmpty();
	}
	const FRow& Row = Rows[Selected];
	if (Row.bTint)
	{
		return LOCTEXT("NextTint", "СЛЕДУЮЩИЙ ЦВЕТ");
	}
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
	if (Row.bTint)
	{
		// The next colour at once (the diode's switch clicks); the line reads it again.
		if (!Guns->LaserSight)
		{
			return false;
		}
		Guns->LaserSight->SetTint(Guns->LaserSight->Tint + 1);
		if (const FOSAttachment* Laser = Guns->GetShownOn(EOSMountType::Rail); Laser && Laser->SwitchSound)
		{
			UGameplayStatics::PlaySound2D(this, Laser->SwitchSound, 0.35f, 1.3f);
		}
		return true;
	}
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
	// The bodycam lens (OSLens.h) bends the picture after it is drawn; the HUD is not bent: bend the point.
	const FVector2D Clip(Canvas.ClipX, Canvas.ClipY);
	Out = OSLensScreenUV(Out / Clip, OSLensK(*Camera)) * Clip;
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

void OSGunScreenPoints(const UOSWeaponsComponent& Guns, const UCameraComponent* Camera, const UCanvas& Canvas, TArray<FVector2D>& Out)
{
	// The gun's mesh and what hangs on it (the suppressor, a module, the long magazine): their boxes' corners.
	TArray<const UStaticMeshComponent*, TInlineAllocator<8>> Parts;
	Parts.Add(Guns.ViewModel);
	for (const UStaticMeshComponent* Module : Guns.ModulesInView)
	{
		Parts.Add(Module);
	}
	Parts.Add(Guns.MagInView);
	for (const UStaticMeshComponent* Part : Parts)
	{
		if (!Part || !Part->IsVisible() || !Part->GetStaticMesh())
		{
			continue;
		}
		const FBox Local = Part->GetStaticMesh()->GetBoundingBox();
		const FTransform ToWorld = Part->GetComponentTransform();
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Point((Corner & 1) ? Local.Max.X : Local.Min.X, (Corner & 2) ? Local.Max.Y : Local.Min.Y, (Corner & 4) ? Local.Max.Z : Local.Min.Z);
			FVector2D Pixel;
			if (OSProjectToHud(Camera, Canvas, ToWorld.TransformPosition(Point), true, Pixel))
			{
				Out.Add(Pixel);
			}
		}
	}
}

FBox2D UOSAttachMenu::GunOnScreen(const UCanvas& Canvas) const
{
	FBox2D Gun(ForceInit);
	if (!Guns)
	{
		return Gun;
	}
	TArray<FVector2D> Points;
	OSGunScreenPoints(*Guns, Camera, Canvas, Points);
	for (const FVector2D& Each : Points)
	{
		Gun += Each;
	}
	return Gun;
}

void UOSAttachMenu::LayOut(const UCanvas& Canvas, float Cell)
{
	const FOSWeapon* Weapon = Guns ? Guns->GetWeapon() : nullptr;
	TMap<int32, FVector2D> PreviousAt;
	for (const FPlaceBox& Each : Boxes)
	{
		PreviousAt.Add(Each.Mount, Each.At);
	}
	const bool bAgain = bLaidOut;
	Boxes.Reset();
	if (!Weapon || Rows.IsEmpty())
	{
		return;
	}
	const auto Snap = [Cell](double Value) { return FMath::RoundToDouble(Value / Cell) * Cell; };
	const auto Width = [Cell](const FString& Line) { return OSProjectorTextCells(Line) * Cell; };
	const double Gap = GapCells * Cell;
	const FVector2D Screen(Canvas.ClipX, Canvas.ClipY);
	// Nothing over the keys at the bottom.
	const double Floor = Canvas.ClipY - 150.0 - 18.0 * Cell;
	FBox2D Gun = GunOnScreen(Canvas);
	if (!Gun.bIsValid)
	{
		Gun = FBox2D(Screen * FVector2D(0.55, 0.45), Screen * FVector2D(0.8, 0.85));
	}
	const FVector2D GunCentre = Gun.GetCenter();
	const FVector2D GunHalf = FVector2D::Max(Gun.GetExtent(), FVector2D(1.0, 1.0));
	const FBox2D KeepOut(Gun.Min - FVector2D(Gap * 0.5), Gun.Max + FVector2D(Gap * 0.5));
	const FString OnWord = LOCTEXT("StateOn", "НАДЕТ").ToString();
	const FString EmptyWord = LOCTEXT("EmptyWord", "ПУСТО").ToString();

	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		if (Boxes.IsEmpty() || Boxes.Last().Mount != Rows[Index].Mount)
		{
			Boxes.AddDefaulted_GetRef().Mount = Rows[Index].Mount;
		}
		Boxes.Last().Lines.Add(Index);
	}
	for (int32 Index = 0; Index < Boxes.Num(); ++Index)
	{
		FPlaceBox& Box = Boxes[Index];
		// The grid: 6 cells of margin, the header 14, 3 between, 14 a line, 4 below; room for «НАДЕТ» on every line.
		double Inner = Width(OSMountName(Weapon->Mounts[Box.Mount].Type).ToString()) + 10.0 * Cell + Width(TEXT("00"));
		for (const int32 Line : Box.Lines)
		{
			const FRow& Row = Rows[Line];
			Inner = FMath::Max(Inner, Row.bEmpty ? double(Width(EmptyWord)) : Width(Row.Name.ToString()) + 10.0 * Cell + Width(OnWord));
		}
		Box.Size = FVector2D(Snap(FMath::Max(Inner + 12.0 * Cell, 70.0 * Cell)), Snap(14.0 * Cell + 3.0 * Cell + Box.Lines.Num() * 14.0 * Cell + 4.0 * Cell));

		FVector Anchor;
		FVector2D Node = GunCentre;
		if (MountAnchor(Box.Mount, Anchor))
		{
			ProjectGun(Canvas, Anchor, Node);
		}
		// The side of the gun the node is on (the screen is wide: left and right first), then the others if it does not fit.
		const FVector2D Off = (Node - GunCentre) / GunHalf;
		const int32 Preferred = FMath::Abs(Off.X) >= FMath::Abs(Off.Y) * 0.6 ? (Off.X >= 0.0 ? 0 : 1) : (Off.Y < 0.0 ? 2 : 3);
		static const int32 Order[4][4] = { { 0, 1, 2, 3 }, { 1, 0, 2, 3 }, { 2, 3, 1, 0 }, { 3, 2, 1, 0 } };
		const auto Place = [&](int32 Side)
		{
			FVector2D At;
			switch (Side)
			{
			case 0: At = FVector2D(Gun.Max.X + Gap, Node.Y - 7.0 * Cell); break;
			case 1: At = FVector2D(Gun.Min.X - Gap - Box.Size.X, Node.Y - 7.0 * Cell); break;
			case 2: At = FVector2D(Node.X - Box.Size.X * 0.5, Gun.Min.Y - Gap - Box.Size.Y); break;
			default: At = FVector2D(Node.X - Box.Size.X * 0.5, Gun.Max.Y + Gap); break;
			}
			const FVector2D High(FMath::Max(Screen.X - Box.Size.X - Gap, Gap), FMath::Max(Floor - Box.Size.Y, Gap));
			return FVector2D(FMath::Clamp(At.X, Gap, High.X), FMath::Clamp(At.Y, Gap, High.Y));
		};
		Box.Side = Preferred;
		Box.At = Place(Preferred);
		for (const int32 Side : Order[Preferred])
		{
			const FVector2D At = Place(Side);
			if (!KeepOut.Intersect(FBox2D(At, At + Box.Size)))
			{
				Box.Side = Side;
				Box.At = At;
				break;
			}
		}
		// Boxes on one side stack apart (down for the sides, across for the top and the bottom).
		for (int32 Other = 0; Other < Index; ++Other)
		{
			const FPlaceBox& Before = Boxes[Other];
			const FBox2D Mine(Box.At - FVector2D(Cell * 4.0), Box.At + Box.Size + FVector2D(Cell * 4.0));
			if (!Mine.Intersect(FBox2D(Before.At, Before.At + Before.Size)))
			{
				continue;
			}
			if (Box.Side <= 1)
			{
				Box.At.Y = Before.At.Y + Before.Size.Y + Cell * 6.0;
			}
			else
			{
				Box.At.X = Before.At.X + Before.Size.X + Cell * 6.0;
			}
		}
		Box.At = FVector2D(Snap(Box.At.X), Snap(Box.At.Y));
	}
	for (FPlaceBox& Box : Boxes)
	{
		const FVector2D* Was = PreviousAt.Find(Box.Mount);
		Box.From = bAgain && Was ? *Was : Box.At;
	}
	bLaidOut = true;
	if (bAgain)
	{
		MovedAt = GetWorld()->GetTimeSeconds();
	}
	else
	{
		LaidOutAt = GetWorld()->GetTimeSeconds();
	}
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
	LastClip = FVector2D(Canvas.ClipX, Canvas.ClipY);
	// The font's cell: 2 px at 1080 lines - the device's own look at any resolution; everything sits on its grid.
	const float Cell = FMath::Max(1.0f, FMath::RoundToFloat(Canvas.ClipY / 540.0f));
	const float Fade = State == EState::Closing ? 1.0f - FMath::Clamp(float(Now - ClosingAt) / float(CloseTicks / OSProjectorClockHz), 0.0f, 1.0f) : 1.0f;
	const float Since = float(Now - OpenAt);
	const auto Light = [this, Fade](float Level)
	{
		return FLinearColor(HudColour.R, HudColour.G, HudColour.B, FMath::Clamp(Level, 0.0f, 1.0f) * Fade);
	};
	const FLinearColor Ink(PlateColour.R, PlateColour.G, PlateColour.B, Fade);
	const auto Snap = [Cell](double Value) { return float(FMath::RoundToDouble(Value / Cell) * Cell); };
	const auto Rect = [&Hud](const FLinearColor& Tint, float X, float Y, float W, float H)
	{
		if (W > 0.0f && H > 0.0f && Tint.A > 0.0f)
		{
			Hud.DrawRect(Tint, X, Y, W, H);
		}
	};
	const auto Text = [&Hud, Cell](FStringView Line, float X, float Y, const FLinearColor& Tint)
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
		Rect(Tint, X, Y, Arm, Cell);
		Rect(Tint, X, Y + Cell, Cell, Arm - Cell);
		Rect(Tint, X + W - Arm, Y, Arm, Cell);
		Rect(Tint, X + W - Cell, Y + Cell, Cell, Arm - Cell);
		Rect(Tint, X, Y + H - Cell, Arm, Cell);
		Rect(Tint, X, Y + H - Arm, Cell, Arm - Cell);
		Rect(Tint, X + W - Arm, Y + H - Cell, Arm, Cell);
		Rect(Tint, X + W - Cell, Y + H - Arm, Cell, Arm - Cell);
	};
	// The text decodes into place: each sign a few frames of the generator's noise, left to right, then itself.
	const auto Decode = [Now](const FString& Line, double Start, int32 Seed)
	{
		static const TCHAR Noise[] = TEXT("АБВГДЕЖЗИКЛМНОПРСТУФХЦЧШЭЮЯ0123456789");
		constexpr int32 NoiseCount = UE_ARRAY_COUNT(Noise) - 1;
		FString Out = Line;
		const int64 Frame = int64(Now * 30.0);
		for (int32 Index = 0; Index < Out.Len(); ++Index)
		{
			if (Out[Index] == TEXT(' '))
			{
				continue;
			}
			const double Settle = Start + Index * 0.014 + 0.07;
			if (Now < Start + Index * 0.014)
			{
				Out[Index] = TEXT(' ');
			}
			else if (Now < Settle)
			{
				Out[Index] = Noise[FMath::Min(int32(OSProjectorHash(Frame, Seed * 131 + Index) * NoiseCount), NoiseCount - 1)];
			}
		}
		return Out;
	};
	if (!bCursorPlaced)
	{
		Cursor = FVector2D(Canvas.ClipX * 0.5, Canvas.ClipY * 0.6);
		bCursorPlaced = true;
	}

	// The layout, once the gun has turned into the menu's pose; from then the boxes stay put on the screen.
	if (!bLaidOut && !Rows.IsEmpty())
	{
		const float Pose = Guns->Scope ? Guns->Scope->GetMenuAlpha() : 1.0f;
		if (Pose >= LayoutAtPose || Since >= LayoutLatestSeconds)
		{
			LayOut(Canvas, Cell);
		}
	}
	// Laid out, the boxes stay put; but if the gun comes to sit under one for a moment (the pose settled late, a
	// suppressor went on), lay out again and slide them over.
	if (bLaidOut)
	{
		const FBox2D Gun = GunOnScreen(Canvas);
		const FBox2D Core = Gun.bIsValid ? FBox2D(Gun.Min + FVector2D(2.0f * Cell), Gun.Max - FVector2D(2.0f * Cell)) : Gun;
		const bool bUnder = Core.bIsValid && Boxes.ContainsByPredicate([&Core](const FPlaceBox& Each) { return Core.Intersect(FBox2D(Each.At, Each.At + Each.Size)); });
		if (!bUnder)
		{
			OverlapSince = -1.0;
		}
		else if (OverlapSince < 0.0)
		{
			OverlapSince = Now;
		}
		else if (Now - OverlapSince > 0.15)
		{
			LayOut(Canvas, Cell);
			OverlapSince = -1.0;
		}
	}
	const float Laid = bLaidOut ? float(Now - LaidOutAt) : -1.0f;
	const int32 SelectedMount = Rows.IsValidIndex(Selected) ? Rows[Selected].Mount : INDEX_NONE;
	const float Slide = FMath::SmoothStep(0.0f, 1.0f, float(Now - SelectedAt) / 0.09f);

	// The nodes: a square on the place, a flash ring on the open, a slow pulse round the selected place.
	TArray<int32> Places;
	for (const FRow& Row : Rows)
	{
		Places.AddUnique(Row.Mount);
	}
	TMap<int32, FVector2D> Nodes;
	for (const int32 Mount : Places)
	{
		FVector Anchor;
		FVector2D Pixel;
		if (!MountAnchor(Mount, Anchor) || !ProjectGun(Canvas, Anchor, Pixel))
		{
			continue;
		}
		const FVector2D Node(Snap(Pixel.X), Snap(Pixel.Y));
		Nodes.Add(Mount, Node);
		const bool bHere = Mount == SelectedMount;
		const float Appear = FMath::Clamp(Since / 0.1f, 0.0f, 1.0f);
		Rect(Light((bHere ? 1.0f : 0.7f) * Appear), Node.X - Cell, Node.Y - Cell, 3.0f * Cell, 3.0f * Cell);
		if (Since < 0.35f)
		{
			const float Flash = Since / 0.35f;
			const float Side = Snap((3.0f + 18.0f * Flash) * Cell);
			Outline(Node.X + Cell * 0.5f - Side * 0.5f, Node.Y + Cell * 0.5f - Side * 0.5f, Side, Side, Light(0.8f * (1.0f - Flash)));
		}
		if (bHere)
		{
			const float Phase = FMath::Frac(float(Now - SelectedAt) / 1.1f);
			const float Side = Snap((7.0f + 10.0f * Phase) * Cell);
			Outline(Node.X + Cell * 0.5f - Side * 0.5f, Node.Y + Cell * 0.5f - Side * 0.5f, Side, Side, Light(0.55f * (1.0f - Phase)));
			Corners(Node.X - 3.0f * Cell, Node.Y - 3.0f * Cell, 7.0f * Cell, 7.0f * Cell, 2.0f * Cell, Light(0.9f));
		}
	}

	const FString EmptyWord = LOCTEXT("EmptyWord", "ПУСТО").ToString();
	// The boxes: leader out of the node (a 45 degree run, then square to the box), the frame unfolds, the text decodes.
	int32 Hovered = INDEX_NONE;
	for (int32 Index = 0; bLaidOut && Index < Boxes.Num(); ++Index)
	{
		const FPlaceBox& Box = Boxes[Index];
		const FVector2D* NodeAt = Nodes.Find(Box.Mount);
		const bool bHere = Box.Mount == SelectedMount;
		const FVector2D Shown = FMath::Lerp(Box.From, Box.At, double(FMath::SmoothStep(0.0f, 1.0f, float(Now - MovedAt) / 0.2f)));
		const float X = Snap(Shown.X);
		const float Y = Snap(Shown.Y);
		const float W = float(Box.Size.X);
		if (NodeAt)
		{
			const FVector2D Node = *NodeAt + FVector2D(Cell, Cell); // the square's middle
			FVector2D End;
			switch (Box.Side)
			{
			case 0: End = FVector2D(X - Cell, Y + 7.0f * Cell); break;
			case 1: End = FVector2D(X + W, Y + 7.0f * Cell); break;
			case 2: End = FVector2D(FMath::Clamp(float(Node.X), X + 6.0f * Cell, X + W - 6.0f * Cell), Y + Box.Size.Y); break;
			default: End = FVector2D(FMath::Clamp(float(Node.X), X + 6.0f * Cell, X + W - 6.0f * Cell), Y - Cell); break;
			}
			End = FVector2D(Snap(End.X), Snap(End.Y));
			const float Grow = FMath::Clamp((Laid - Index * 0.03f) / 0.14f, 0.0f, 1.0f);
			const FLinearColor Tint = Light(bHere ? 0.85f : 0.4f);
			// The run: diagonal first (as far as both ways allow), then straight on to the box.
			const FVector2D Way = End - Node;
			const bool bAcross = Box.Side <= 1;
			const float Main = float(bAcross ? FMath::Abs(Way.X) : FMath::Abs(Way.Y));
			const float Cross = float(bAcross ? FMath::Abs(Way.Y) : FMath::Abs(Way.X));
			const float Diagonal = FMath::Min(Cross, FMath::Max(Main - 6.0f * Cell, 0.0f));
			const FVector2D Sign(Way.X >= 0.0 ? 1.0 : -1.0, Way.Y >= 0.0 ? 1.0 : -1.0);
			const FVector2D Bend = Node + Sign * Diagonal;
			const FVector2D Knee = bAcross ? FVector2D(Bend.X, End.Y) : FVector2D(End.X, Bend.Y);
			const float Legs[3] = { Diagonal * 1.4142f, float(FVector2D::Distance(Bend, Knee)), float(FVector2D::Distance(Knee, End)) };
			float Left = (Legs[0] + Legs[1] + Legs[2]) * Grow;
			// The diagonal in cell steps (the device has no slanted strokes).
			const int32 Steps = FMath::FloorToInt32(FMath::Min(Left / 1.4142f, Diagonal) / Cell);
			for (int32 Stair = 1; Stair <= Steps; ++Stair)
			{
				Rect(Tint, Snap(Node.X + Sign.X * Stair * Cell), Snap(Node.Y + Sign.Y * Stair * Cell), Cell, Cell);
			}
			Left -= Legs[0];
			const FVector2D Joints[3] = { Bend, Knee, End };
			for (int32 Leg = 0; Leg < 2 && Left > 0.0f; ++Leg)
			{
				const FVector2D From = Joints[Leg];
				const FVector2D To = Joints[Leg + 1];
				const float Length = FMath::Min(float(FVector2D::Distance(From, To)), Left);
				Left -= Length;
				const FVector2D Reach = From + (To - From).GetSafeNormal() * Length;
				Rect(Tint, float(FMath::Min(From.X, Reach.X)), float(FMath::Min(From.Y, Reach.Y)), float(FMath::Abs(Reach.X - From.X)) + Cell, float(FMath::Abs(Reach.Y - From.Y)) + Cell);
			}
		}
		const float Unfold = FMath::SmoothStep(0.0f, 1.0f, (Laid - 0.1f - Index * 0.03f) / 0.14f);
		if (Unfold <= 0.0f)
		{
			continue;
		}
		// The frame opens out of its header's line: width first, then height.
		const float Widen = FMath::Clamp(Unfold * 2.0f, 0.0f, 1.0f);
		const float Drop = FMath::Clamp(Unfold * 2.0f - 1.0f, 0.0f, 1.0f);
		const float FrameW = Snap(W * Widen);
		const float FrameX = Box.Side == 1 ? X + W - FrameW : X;
		const float FrameH = Snap(FMath::Max(Cell * 2.0f, float(Box.Size.Y) * Drop));
		Rect(FLinearColor(PlateColour.R, PlateColour.G, PlateColour.B, PlateColour.A * Fade), FrameX, Y, FrameW, FrameH);
		// Scan lines over the plate, every other cell row (the suit's display).
		for (float ScanY = Y + Cell; ScanY < Y + FrameH - Cell; ScanY += 2.0f * Cell)
		{
			Rect(FLinearColor(0.0f, 0.0f, 0.0f, 0.22f * Fade), FrameX + Cell, ScanY, FrameW - 2.0f * Cell, Cell);
		}
		Outline(FrameX, Y, FrameW, FrameH, Light(bHere ? 0.4f : 0.2f));
		Corners(FrameX, Y, FrameW, FrameH, 5.0f * Cell, Light(bHere ? 1.0f : 0.5f));
		if (Drop < 1.0f)
		{
			continue;
		}
		// The header: a lit strip, the place's name, its number.
		Rect(Light(bHere ? 0.2f : 0.1f), X + 2.0f * Cell, Y + 2.0f * Cell, W - 4.0f * Cell, 12.0f * Cell);
		const FString Head = OSMountName(Weapon->Mounts[Box.Mount].Type).ToString();
		Text(Decode(Head, LaidOutAt + 0.4 + Index * 0.03, Box.Mount * 7 + 1), X + 6.0f * Cell, Y + 3.0f * Cell, Light(bHere ? 1.0f : 0.65f));
		const FString Number = FString::Printf(TEXT("%02d"), Index + 1);
		Text(Number, X + W - 6.0f * Cell - Width(Number), Y + 3.0f * Cell, Light(0.35f));
		for (int32 Line = 0; Line < Box.Lines.Num(); ++Line)
		{
			const int32 RowIndex = Box.Lines[Line];
			const FRow& Row = Rows[RowIndex];
			const float Top = Y + 17.0f * Cell + Line * 14.0f * Cell;
			if (Row.bEmpty)
			{
				Text(Decode(EmptyWord, LaidOutAt + 0.45, RowIndex * 13 + 5), X + 6.0f * Cell, Top + 2.0f * Cell, Light(0.3f));
				continue;
			}
			if (Cursor.X >= X && Cursor.X < X + W && Cursor.Y >= Top && Cursor.Y < Top + 14.0f * Cell)
			{
				Hovered = RowIndex;
			}
			const bool bSelected = RowIndex == Selected;
			bool bInk = false;
			if (bSelected)
			{
				// The bar: slides from the line it left in the same box, else wipes in from the left.
				const int32 From = Box.Lines.IndexOfByKey(PreviousSelected);
				const float FromTop = From != INDEX_NONE ? Y + 17.0f * Cell + From * 14.0f * Cell : Top;
				const float BarTop = Snap(FMath::Lerp(FromTop, Top, Slide));
				const float BarW = From != INDEX_NONE ? W - 4.0f * Cell : Snap((W - 4.0f * Cell) * Slide);
				Rect(Light(0.88f), X + 2.0f * Cell, BarTop, BarW, 13.0f * Cell);
				bInk = From != INDEX_NONE ? Slide > 0.5f : Slide > 0.4f;
			}
			else if (RowIndex == Hovered)
			{
				Outline(X + 2.0f * Cell, Top, W - 4.0f * Cell, 13.0f * Cell, Light(0.35f));
			}
			const FLinearColor Tint = bInk ? Ink : Light(0.7f);
			Text(Decode(Row.Name.ToString(), LaidOutAt + 0.45 + Line * 0.05, RowIndex * 13 + 7), X + 6.0f * Cell, Top + 2.0f * Cell, Tint);
			const bool bSeating = Row.Id == AppliedId && Row.Mount == AppliedMount && Guns->IsSeating();
			if (bSeating)
			{
				// On its way: the share done as a line under it, a blinking mark where «НАДЕТ» goes.
				const float Remaining = FMath::Max(Guns->GetSeatRemaining(), Guns->GetReloadRemaining());
				const float Done = FMath::Clamp(1.0f - Remaining / FMath::Max(AppliedSeconds, 0.05f), 0.0f, 1.0f);
				Rect(bInk ? Ink : Light(0.9f), X + 6.0f * Cell, Top + 11.0f * Cell, Snap((W - 12.0f * Cell) * Done), Cell);
				if (FMath::Frac(Now * 4.0) < 0.5)
				{
					Rect(bInk ? Ink : Light(0.9f), X + W - 9.0f * Cell, Top + 4.0f * Cell, 3.0f * Cell, 3.0f * Cell);
				}
			}
			else if (Row.bTint)
			{
				// A swatch of the colour in use, where «НАДЕТ» goes.
				const FOSLaserTint* Swatch = Guns->LaserSight ? Guns->LaserSight->GetTint() : nullptr;
				if (Swatch)
				{
					Rect(bInk ? Ink : Light(0.5f), X + W - 13.0f * Cell, Top + 2.0f * Cell, 7.0f * Cell, 9.0f * Cell);
					Rect(FLinearColor(Swatch->Dot.R, Swatch->Dot.G, Swatch->Dot.B, Fade), X + W - 12.0f * Cell, Top + 3.0f * Cell, 5.0f * Cell, 7.0f * Cell);
				}
			}
			else if (Row.bOn)
			{
				const FString OnWord = LOCTEXT("StateOn", "НАДЕТ").ToString();
				Text(OnWord, X + W - 6.0f * Cell - Width(OnWord), Top + 2.0f * Cell, bInk ? Ink : Light(0.5f));
			}
		}
	}
	bHovering = Hovered != INDEX_NONE;
	if (bCursorMoved)
	{
		bCursorMoved = false;
		if (Hovered != INDEX_NONE && IsOpen())
		{
			Select(Hovered);
		}
	}

	// The keys, centred low: the selected thing, then what the mouse does.
	const float FootY = Snap(Canvas.ClipY - 150.0);
	if (Rows.IsEmpty())
	{
		const FString None = LOCTEXT("NoneRow", "ОБВЕСА НЕТ").ToString();
		Text(Decode(None, OpenAt + 0.15, 3), Snap((Canvas.ClipX - Width(None)) * 0.5), Snap(Canvas.ClipY * 0.35), Light(0.7f));
	}
	if (Laid >= 0.3f || Rows.IsEmpty())
	{
		if (Rows.IsValidIndex(Selected) && !Rows[Selected].bEmpty)
		{
			const FString About = FString::Printf(TEXT("%s — %s"), *Rows[Selected].Name.ToString(), *Rows[Selected].Description.ToString());
			Text(Decode(About, FMath::Max(SelectedAt, LaidOutAt + 0.3), Selected * 17 + 3), Snap((Canvas.ClipX - Width(About)) * 0.5), FootY - 14.0f * Cell, Light(0.55f));
		}
		const bool bBusy = Now - BusyAt < 0.8;
		const FString Keys = Rows.IsEmpty() ? LOCTEXT("KeyClose", "ОТПУСТИТЬ TAB — ЗАКРЫТЬ").ToString()
			: bBusy ? LOCTEXT("KeyBusy", "ЖДИ — ИДЕТ УСТАНОВКА").ToString()
			: FString::Printf(TEXT("ЛКМ — %s    КОЛЕСО — ВЫБОР    ОТПУСТИТЬ TAB — ЗАКРЫТЬ"), *Verb().ToString());
		if (!bBusy || FMath::Frac((Now - BusyAt) * 5.0) < 0.6)
		{
			Text(Keys, Snap((Canvas.ClipX - Width(Keys)) * 0.5), FootY, Light(0.9f));
		}
	}

	// The cursor: a cross of the device's strokes, its middle open; brackets round it over a line.
	if (TakesMouse() || State == EState::Closing)
	{
		const FLinearColor Tint(0.9f, 0.95f, 1.0f, 0.95f * Fade);
		const float CX = Snap(Cursor.X);
		const float CY = Snap(Cursor.Y);
		const float Arm = (bHovering ? 2.0f : 3.0f) * Cell;
		const float Hole = 2.0f * Cell;
		Rect(Tint, CX - Hole - Arm, CY, Arm, Cell);
		Rect(Tint, CX + Cell + Hole, CY, Arm, Cell);
		Rect(Tint, CX, CY - Hole - Arm, Cell, Arm);
		Rect(Tint, CX, CY + Cell + Hole, Cell, Arm);
		if (bHovering)
		{
			Corners(CX - 4.0f * Cell, CY - 4.0f * Cell, 9.0f * Cell, 9.0f * Cell, 2.0f * Cell, Tint);
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
	if (Tick < StateTick || !bDeviceTick)
	{
		return;
	}
	const int64 Into = Tick - StateTick;
	if (State == EState::Opening && Into >= OpenTicks - 1)
	{
		State = EState::Open;
	}
	else if (State == EState::Closing && Into >= CloseTicks)
	{
		Finish(LastOutcome);
	}
	// The picture is the HUD's, every frame (DrawHud).
}


#undef LOCTEXT_NAMESPACE
