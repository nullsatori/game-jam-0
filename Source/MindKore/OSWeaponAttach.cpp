#include "OSWeaponAttach.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "MindKore.h"
#include "OSAttachMenu.h"
#include "OSLaserSight.h"
#include "OSScope.h"
#include "OSShip.h"
#include "OSWeapons.h"
#include "OSZeroGPawn.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "OSAttach"

namespace
{
	bool SeatCrossed(float Before, float Now, float At)
	{
		return Before < At && Now >= At;
	}

	float Smooth(float X)
	{
		return FMath::SmoothStep(0.0f, 1.0f, X);
	}

	/** A bump 0 -> 1 -> 0 over Width seconds starting at Start. */
	float Bump(float T, float Start, float Width)
	{
		const float U = (T - Start) / Width;
		return U > 0.0f && U < 1.0f ? FMath::Sin(PI * U) : 0.0f;
	}

	/** The hand's way between the pouch and the rail's mouth: an arc bowed down, the turn of the pouch fading over the last 5 cm. */
	void HandWay(const FOSAttachment& Attachment, const FVector& Pouch, const FVector& Mouth, float Share, FOSSeatPose& Out)
	{
		const float S = FMath::Clamp(Share, 0.0f, 1.0f);
		Out.Place = FMath::Lerp(Pouch, Mouth, S) + FVector(0.0f, 0.0f, -Attachment.ApproachArcCm * FMath::Sin(PI * S));
		const float Left = float((Mouth - Out.Place).Size());
		Out.Turn = Attachment.ApproachTurn * FMath::Clamp(Left / 5.0f, 0.0f, 1.0f);
	}

	void ToastGuns(const UObject* Context, const FText& Text)
	{
		UE_LOG(LogMindKore, Log, TEXT("Toast: %s"), *Text.ToString());
		if (AOSShipGameMode* Mode = Context && Context->GetWorld() ? Context->GetWorld()->GetAuthGameMode<AOSShipGameMode>() : nullptr)
		{
			Mode->Toast(Text);
		}
	}

	/** The magazine's stretch about its top (FOSWeapon::MagStretch along ViewMesh's Z), and its inverse for what rides inside. */
	FTransform MagStretchAbout(const FVector& Pivot, float Stretch)
	{
		const FVector Scale(1.0f, 1.0f, Stretch);
		return FTransform(FQuat::Identity, Pivot - Pivot * Scale, Scale);
	}
}

FText OSMountName(EOSMountType Type)
{
	switch (Type)
	{
	case EOSMountType::Rail: return LOCTEXT("Rail", "ПЛАНКА");
	case EOSMountType::Muzzle: return LOCTEXT("Muzzle", "ДУЛО");
	case EOSMountType::Top: return LOCTEXT("Top", "ВЕРХ");
	case EOSMountType::Magazine: return LOCTEXT("Magazine", "МАГАЗИН");
	}
	return FText::GetEmpty();
}

float OSSeatLength(const FOSAttachment& Attachment, EOSMountType Type, bool bOn)
{
	switch (Type)
	{
	case EOSMountType::Muzzle:
		return bOn ? Attachment.ApproachSeconds + Attachment.ThreadSeconds + 0.08f : Attachment.UnthreadSeconds + 0.2f;
	case EOSMountType::Magazine:
		return 0.0f; // the reload's
	default:
		return bOn ? Attachment.SeatSeconds : Attachment.UnseatSeconds;
	}
}

FOSSeatPose OSSeatPoseAt(const FOSAttachment& A, const FOSWeaponMount& Mount, bool bOn, float T, const FVector& Pouch)
{
	FOSSeatPose P;
	const float Length = OSSeatLength(A, Mount.Type, bOn);
	if (Mount.Type == EOSMountType::Magazine || T < 0.0f)
	{
		P.bDone = Mount.Type == EOSMountType::Magazine;
		return P;
	}
	if (T >= Length)
	{
		// On: seated at rest; off: back in the pouch, out of view.
		P.bShown = bOn;
		P.bLens = bOn;
		P.bDone = true;
		return P;
	}
	P.bShown = true;
	if (Mount.Type == EOSMountType::Muzzle)
	{
		// Screwed on along the bore (owner 22.10 14:11): 2.5 turns over 1.2 cm, slowing into the squeeze.
		const FVector Start(A.ThreadTravelCm, 0.0f, 0.0f);
		const float Turns = A.ThreadTurns * 360.0f;
		if (bOn)
		{
			if (T < A.ApproachSeconds)
			{
				HandWay(A, Pouch, Start, Smooth(T / A.ApproachSeconds), P);
				return P;
			}
			const float U = FMath::Clamp((T - A.ApproachSeconds) / A.ThreadSeconds, 0.0f, 1.0f);
			const float Left = FMath::Square(1.0f - U); // 1 - the eased share: slows to the end
			P.Place = Start * Left;
			P.Turn = FRotator(0.0f, 0.0f, -Turns * Left);
			return P;
		}
		if (T < A.UnthreadSeconds)
		{
			const float Out = Smooth(T / A.UnthreadSeconds);
			P.Place = Start * Out;
			P.Turn = FRotator(0.0f, 0.0f, -Turns * Out);
			return P;
		}
		const float Back = (T - A.UnthreadSeconds) / FMath::Max(Length - A.UnthreadSeconds, 0.01f);
		HandWay(A, Pouch, Start, 1.0f - Smooth(Back), P);
		return P;
	}

	// The rail: rolled on from the muzzle's end (owner 22.10 14:11). Side: the module comes from under the rail (or over the top one).
	const float Side = Mount.bUnder ? -1.0f : 1.0f;
	const FVector Mouth(A.RailMouthCm, 0.0f, Side * A.RailDropCm);
	if (bOn)
	{
		if (T < A.ApproachSeconds)
		{
			HandWay(A, Pouch, Mouth, Smooth(T / A.ApproachSeconds), P);
			return P;
		}
		// Arrived 4 mm past the mouth and back (the addendum's 1 / 0.96 / 1 keys read as that overshoot), rising into the slot meanwhile.
		const FVector Way = (Mouth - Pouch).GetSafeNormal();
		const float Over = 0.4f * Bump(T, A.ApproachSeconds, 0.08f);
		const float Rise = FMath::Clamp((T - A.ApproachSeconds) / A.RiseSeconds, 0.0f, 1.0f);
		const float Past = 0.1f * Bump(T, A.ApproachSeconds + A.RiseSeconds - 0.03f, 0.05f);
		const float Share = OSEvalCycle(A.RailTravel, T);
		float Z = Side * A.RailDropCm * (1.0f - Smooth(Rise)) - Side * Past;
		for (const float Detent : A.Detents)
		{
			Z += A.DetentJitterCm * Bump(T, Detent - 0.01f, 0.02f);
		}
		// The latch: it settles 0.3 mm in 30 ms, then the hand lets go and it sits at the seat.
		if (T >= A.LockSeconds)
		{
			const float Sink = FMath::Clamp((T - A.LockSeconds) / 0.03f, 0.0f, 1.0f);
			const float Back = Smooth((T - A.LockSeconds - 0.03f) / FMath::Max(A.SeatSeconds - A.LockSeconds - 0.03f, 0.01f));
			Z -= 0.03f * Sink * (1.0f - Back);
		}
		P.Place = FVector(A.RailMouthCm * (1.0f - FMath::Clamp(Share, 0.0f, 1.2f)), 0.0f, Z) + Way * Over;
		// The gun gives under the push with the roll on, and comes back off the stop.
		const float Release = 1.0f - Smooth((T - A.StopSeconds) / FMath::Max(A.SeatSeconds - A.StopSeconds, 0.01f));
		const float Give = FMath::Clamp(Share, 0.0f, 1.0f) * (T < A.StopSeconds ? 1.0f : Release);
		P.YieldCm = A.YieldCm * Give;
		P.YieldRollDeg = -A.YieldRollDeg * Give;
		P.bLens = T >= A.LensSeconds;
		return P;
	}
	// Off (0.55 s): unlatched and lifted, rolled back to the mouth over the detents, off the slot, the arc to the pouch.
	constexpr float RollFrom = 0.08f, RollTo = 0.30f, DropTo = 0.36f;
	const float Lift = 0.03f * FMath::Clamp(T / 0.03f, 0.0f, 1.0f);
	const float Roll = Smooth((T - RollFrom) / (RollTo - RollFrom));
	if (T < DropTo)
	{
		float Z = Lift + Side * A.RailDropCm * Smooth((T - RollTo) / (DropTo - RollTo));
		for (const float Detent : A.Detents)
		{
			Z += A.DetentJitterCm * Bump(T, Detent - (A.StopSeconds - RollTo) - 0.01f, 0.02f);
		}
		P.Place = FVector(A.RailMouthCm * Roll, 0.0f, Z);
		const float Give = Roll * (1.0f - Smooth((T - RollTo) / 0.15f));
		P.YieldCm = -A.UnseatYieldCm * Give;
		return P;
	}
	HandWay(A, Pouch, Mouth, 1.0f - Smooth((T - DropTo) / FMath::Max(Length - DropTo, 0.01f)), P);
	return P;
}

TArray<FOSAttachment> OSMakeAttachments()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> UspMag(TEXT("/Game/OSArt/Meshes/Weapons/SM_OS_USP_Mag.SM_OS_USP_Mag"));
	// Stand-ins until the Foley pass (the addendum): the USP's magazine for the stop, its slide stop for the rail's click,
	// the dry click for the detents and the switch, the holster's leather for the pouch, the hammer for the latch.
	static ConstructorHelpers::FObjectFinder<USoundBase> Pouch(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_Draw_Pistol.SW_OS_Draw_Pistol"));
	static ConstructorHelpers::FObjectFinder<USoundBase> Click(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_SlideRelease_Pistol.SW_OS_SlideRelease_Pistol"));
	static ConstructorHelpers::FObjectFinder<USoundBase> Tick(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_Empty_Pistol.SW_OS_Empty_Pistol"));
	static ConstructorHelpers::FObjectFinder<USoundBase> Knock(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_MagIn_Pistol.SW_OS_MagIn_Pistol"));
	static ConstructorHelpers::FObjectFinder<USoundBase> Latch(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_Hammer_Pistol.SW_OS_Hammer_Pistol"));

	FOSAttachment Rail;
	Rail.MountType = EOSMountType::Rail;
	Rail.Mesh = Cube.Object;
	Rail.bHasSwitch = true;
	Rail.PouchSound = Pouch.Object;
	Rail.RailClick = Click.Object;
	Rail.DetentTick = Tick.Object;
	Rail.StopKnock = Knock.Object;
	Rail.LockClick = Latch.Object;
	Rail.SwitchSound = Tick.Object;
	// The push (6 share/s off the mouth), the run, the stop at 0.52 arriving at 9 share/s, the 6% rebound, rest at 0.58.
	Rail.RailTravel = OSMakeCycleCurve({ { 0.30f, 0.0f, 0.0f, 6.0f }, { 0.40f, 0.55f, 4.6f, 4.6f }, { 0.52f, 1.0f, 9.0f, 0.0f },
		{ 0.545f, 0.94f, 0.0f, 0.0f }, { 0.58f, 1.0f, 0.0f, 0.0f } });
	Rail.SeatKick = { 0.2f, FVector(0.0f, 0.0f, -4.0f), FRotator::ZeroRotator };
	Rail.StopKick = { 0.52f, FVector(-25.0f, 0.0f, 0.0f), FRotator(0.0f, 0.0f, -40.0f) };
	Rail.LockKick = { 0.6f, FVector(0.0f, 0.0f, -6.0f), FRotator::ZeroRotator };

	// ЛЦУ (§6): a 3.2 x 1.6 x 1.4 cm box under the rail, its 4 mm lens at the front.
	FOSAttachment Laser = Rail;
	Laser.Id = TEXT("Laser");
	Laser.Name = LOCTEXT("LaserName", "ЛЦУ");
	Laser.Description = LOCTEXT("LaserText", "ТОЧКА ТУДА, КУДА СМОТРИШЬ");
	Laser.Fits = { TEXT("Pistol"), TEXT("Rmb93") };
	Laser.SeatOffset = FTransform(FQuat::Identity, FVector(0.0f, 0.0f, -0.7f), FVector(0.032f, 0.016f, 0.014f));
	Laser.bLaser = true;
	Laser.LensOffsetCm = FVector(1.62f, 0.0f, -0.7f);

	// Фонарь (§8): a 3 x 2 x 2 cm box under the rail, light 15 m ahead along the bore.
	FOSAttachment Torch = Rail;
	Torch.Id = TEXT("Torch");
	Torch.Name = LOCTEXT("TorchName", "ФОНАРЬ");
	Torch.Description = LOCTEXT("TorchText", "СВЕТ ВДОЛЬ СТВОЛА");
	Torch.Fits = { TEXT("Pistol"), TEXT("Shotgun"), TEXT("Rmb93") };
	Torch.SeatOffset = FTransform(FQuat::Identity, FVector(0.0f, 0.0f, -1.0f), FVector(0.03f, 0.02f, 0.02f));
	Torch.TorchCandelas = 6000.0f;
	Torch.TorchConeDeg = 16.0f;
	Torch.TorchRangeCm = 1500.0f;
	Torch.LensOffsetCm = FVector(1.52f, 0.0f, -1.0f);

	// Глушитель (§8): a 2.6 cm tube 12 cm long screwed onto the USP's muzzle, the muzzle moved out to its end;
	// the shot 12 dB down until its own sound, the flash x0.3, the smoke x1.5.
	FOSAttachment Suppressor = Rail;
	Suppressor.Id = TEXT("Suppressor");
	Suppressor.Name = LOCTEXT("SuppressorName", "ГЛУШИТЕЛЬ");
	Suppressor.Description = LOCTEXT("SuppressorText", "ТИШЕ, БЕЗ ВСПЫШКИ");
	Suppressor.MountType = EOSMountType::Muzzle;
	Suppressor.Mesh = Cylinder.Object;
	Suppressor.bHasSwitch = false;
	Suppressor.Fits = { TEXT("Pistol") };
	Suppressor.SeatOffset = FTransform(FRotator(90.0f, 0.0f, 0.0f), FVector(6.0f, 0.0f, 0.0f), FVector(0.026f, 0.026f, 0.12f));
	Suppressor.PouchOffsetCm = FVector(10.0f, 6.0f, -12.0f);
	Suppressor.TightenKick = { 0.0f, FVector(15.0f, 0.0f, 0.0f), FRotator(0.0f, 0.0f, 25.0f) };
	Suppressor.ShotSoundOverride = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_Shot_Pistol_Suppressed.SW_OS_Shot_Pistol_Suppressed")));
	Suppressor.ShotVolume = 0.25f;
	Suppressor.MuzzleFlashScale = 0.3f;
	Suppressor.MuzzleSmokeScale = 1.5f;
	Suppressor.MuzzleOffsetCm = 12.0f;

	// Магазин 20 (owner 22.10 14:16, 14:19): the stock mesh longer by 3 cm until the PC's; clear, the rounds inside.
	FOSAttachment BigMag;
	BigMag.Id = TEXT("BigMag");
	BigMag.Name = LOCTEXT("BigMagName", "МАГАЗИН 20");
	BigMag.Description = LOCTEXT("BigMagText", "ПРОЗРАЧНЫЙ, 20 ПАТРОНОВ");
	BigMag.MountType = EOSMountType::Magazine;
	BigMag.Fits = { TEXT("Pistol") };
	BigMag.MagazineSizeOverride = 20;
	BigMag.MagMeshOverride = UspMag.Object;
	BigMag.MagExtraCm = 3.0f;
	BigMag.bClearMagazine = true;
	BigMag.ClearMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/OSArt/Materials/Weapons/M_OS_ClearMag.M_OS_ClearMag")));
	return { Laser, Torch, Suppressor, BigMag };
}

// --- UOSWeaponsComponent: the catalog, the pouch and the mounts ---

void UOSWeaponsComponent::SetupAttachments()
{
	if (Weapons.Num() < 3)
	{
		return;
	}
	// The places, in each ViewMesh's space (X toward the muzzle along the rail: the USP sits turned 180, so yaw 180).
	// First numbers off the meshes' proportions; the PC's stills fix them (Docs/Attachments.md, «Ждёт ПК»).
	Weapons[0].Id = TEXT("Pistol");
	FOSWeaponMount UspRail;
	UspRail.Type = EOSMountType::Rail;
	UspRail.Transform = FTransform(FRotator(0.0f, 180.0f, 0.0f), FVector(-7.0f, -1.43f, 1.6f));
	FOSWeaponMount UspMuzzle;
	UspMuzzle.Type = EOSMountType::Muzzle;
	UspMuzzle.Socket = TEXT("Muzzle");
	UspMuzzle.Transform = FTransform(FRotator(0.0f, 180.0f, 0.0f), FVector::ZeroVector);
	FOSWeaponMount UspWell;
	UspWell.Type = EOSMountType::Magazine;
	Weapons[0].Mounts = { UspRail, UspMuzzle, UspWell };
	Weapons[1].Id = TEXT("Shotgun");
	FOSWeaponMount PumpRail;
	PumpRail.Type = EOSMountType::Rail;
	PumpRail.Transform = FTransform(FRotator::ZeroRotator, FVector(36.0f, 0.0f, 1.0f));
	Weapons[1].Mounts = { PumpRail };
	Weapons[2].Id = TEXT("Sniper");
	Attachments = OSMakeAttachments();
}

const FOSAttachment* UOSWeaponsComponent::FindAttachment(FName Id) const
{
	return Attachments.FindByPredicate([Id](const FOSAttachment& Each) { return Each.Id == Id; });
}

bool UOSWeaponsComponent::FindMounted(FName Id, int32& OutWeapon, int32& OutMount) const
{
	for (int32 Gun = 0; Gun < Weapons.Num(); ++Gun)
	{
		for (int32 Mount = 0; Mount < Weapons[Gun].Mounts.Num(); ++Mount)
		{
			if (!Id.IsNone() && Weapons[Gun].Mounts[Mount].Mounted == Id)
			{
				OutWeapon = Gun;
				OutMount = Mount;
				return true;
			}
		}
	}
	return false;
}

bool UOSWeaponsComponent::HasAttachment(FName Id) const
{
	int32 Gun, Mount;
	return Carried.Contains(Id) || FindMounted(Id, Gun, Mount);
}

int32 UOSWeaponsComponent::FindWeapon(const FString& Key) const
{
	if (Key.IsNumeric())
	{
		const int32 Slot = FCString::Atoi(*Key) - 1;
		return Weapons.IsValidIndex(Slot) ? Slot : INDEX_NONE;
	}
	for (int32 Gun = 0; Gun < Weapons.Num(); ++Gun)
	{
		if (Weapons[Gun].Id.ToString().Equals(Key, ESearchCase::IgnoreCase) || Weapons[Gun].Name.ToString().Equals(Key, ESearchCase::IgnoreCase))
		{
			return Gun;
		}
	}
	return INDEX_NONE;
}

int32 UOSWeaponsComponent::FindMountFor(int32 Weapon, const FOSAttachment& Attachment) const
{
	if (!Weapons.IsValidIndex(Weapon) || !Attachment.FitsGun(Weapons[Weapon].Id))
	{
		return INDEX_NONE;
	}
	int32 First = INDEX_NONE;
	for (int32 Mount = 0; Mount < Weapons[Weapon].Mounts.Num(); ++Mount)
	{
		const FOSWeaponMount& Place = Weapons[Weapon].Mounts[Mount];
		if (Place.Type == Attachment.MountType)
		{
			if (Place.Mounted.IsNone())
			{
				return Mount;
			}
			First = First == INDEX_NONE ? Mount : First;
		}
	}
	return First;
}

void UOSWeaponsComponent::LockWeapon(int32 Index)
{
	if (!Weapons.IsValidIndex(Index) || !Weapons[Index].bUnlocked)
	{
		return;
	}
	if (AttachMenu && Index == CurrentWeapon)
	{
		AttachMenu->Kill();
	}
	if (Index == SeatWeapon)
	{
		FinishSeat();
	}
	Weapons[Index].bUnlocked = false;
	if (Index == CurrentWeapon)
	{
		// The next gun open takes the hands; none: the gun leaves the view at once.
		int32 Next = INDEX_NONE;
		for (int32 Gun = 0; Gun < Weapons.Num(); ++Gun)
		{
			if (Weapons[Gun].bUnlocked)
			{
				Next = Gun;
				break;
			}
		}
		bReloadQueued = false;
		ReloadEndSeconds = 0.0;
		if (Next == INDEX_NONE)
		{
			ClearView();
		}
		else
		{
			SelectWeapon(Next);
		}
	}
	UE_LOG(LogMindKore, Log, TEXT("Weapon %s: %s locked, %d/%d kept"), *GetNameSafe(GetOwner()), *Weapons[Index].Name.ToString(), Weapons[Index].Ammo, Weapons[Index].Reserve);
}

bool UOSWeaponsComponent::GiveAttachment(FName Id, bool bBagOnly)
{
	const FOSAttachment* Attachment = FindAttachment(Id);
	if (!Attachment || HasAttachment(Id))
	{
		return false;
	}
	Carried.AddUnique(Id);
	UE_LOG(LogMindKore, Log, TEXT("Attach %s: %s into the pouch"), *GetNameSafe(GetOwner()), *Id.ToString());
	if (bBagOnly)
	{
		return true;
	}
	// The first meeting (§4): on the gun in hand at once, rolled on by the hand, when it has a free place that fits
	// (a magazine place is never free: the stock one is in it). Otherwise the pouch, and a word of where.
	const int32 Mount = GetWeapon() ? FindMountFor(CurrentWeapon, *Attachment) : INDEX_NONE;
	if (Mount != INDEX_NONE && Weapons[CurrentWeapon].Mounts[Mount].Mounted.IsNone() && Attachment->MountType != EOSMountType::Magazine)
	{
		if (!ApplyAttachment(Mount, Id))
		{
			MountAttachment(Id, CurrentWeapon, Mount);
		}
		return true;
	}
	ToastGuns(this, FText::Format(LOCTEXT("InPouch", "{0} — в сумке, Tab"), Attachment->Name));
	return true;
}

bool UOSWeaponsComponent::TakeAttachment(FName Id)
{
	if (!HasAttachment(Id))
	{
		return false;
	}
	UnmountAttachment(Id);
	Carried.Remove(Id);
	SwitchOff.Remove(Id);
	UE_LOG(LogMindKore, Log, TEXT("Attach %s: %s taken away"), *GetNameSafe(GetOwner()), *Id.ToString());
	return true;
}

bool UOSWeaponsComponent::MountAttachment(FName Id, int32 Weapon, int32 Mount)
{
	const FOSAttachment* Attachment = FindAttachment(Id);
	if (!Attachment || !Weapons.IsValidIndex(Weapon))
	{
		return false;
	}
	if (Mount == INDEX_NONE)
	{
		Mount = FindMountFor(Weapon, *Attachment);
	}
	if (!Weapons[Weapon].Mounts.IsValidIndex(Mount) || Weapons[Weapon].Mounts[Mount].Type != Attachment->MountType || !Attachment->FitsGun(Weapons[Weapon].Id))
	{
		return false;
	}
	if (Weapon == SeatWeapon)
	{
		FinishSeat();
	}
	// From wherever it is: another gun's place, or the pouch.
	if (Weapons[Weapon].Mounts[Mount].Mounted == Id)
	{
		return true;
	}
	if (!HasAttachment(Id))
	{
		Carried.AddUnique(Id);
	}
	UnmountAttachment(Id);
	FOSWeaponMount& Place = Weapons[Weapon].Mounts[Mount];
	if (!Place.Mounted.IsNone())
	{
		UnmountAttachment(Place.Mounted);
	}
	Carried.Remove(Id);
	Place.Mounted = Id;
	if (Place.Type == EOSMountType::Magazine)
	{
		SwapMagazine(Weapons[Weapon], Id);
	}
	UE_LOG(LogMindKore, Log, TEXT("Attach %s: %s on %s (%s)"), *GetNameSafe(GetOwner()), *Id.ToString(), *Weapons[Weapon].Name.ToString(), *OSMountName(Place.Type).ToString());
	return true;
}

bool UOSWeaponsComponent::UnmountAttachment(FName Id)
{
	int32 Gun, Mount;
	if (!FindMounted(Id, Gun, Mount))
	{
		return false;
	}
	if (Gun == SeatWeapon)
	{
		FinishSeat();
	}
	FOSWeaponMount& Place = Weapons[Gun].Mounts[Mount];
	Place.Mounted = NAME_None;
	if (Place.Type == EOSMountType::Magazine)
	{
		SwapMagazine(Weapons[Gun], NAME_None);
	}
	Carried.AddUnique(Id);
	UE_LOG(LogMindKore, Log, TEXT("Attach %s: %s off %s, into the pouch"), *GetNameSafe(GetOwner()), *Id.ToString(), *Weapons[Gun].Name.ToString());
	return true;
}

void UOSWeaponsComponent::SwapMagazine(FOSWeapon& Weapon, FName Id)
{
	const FOSAttachment* Attachment = FindAttachment(Id);
	if (!Weapon.StockMagMesh && Weapon.StockMagazineSize == 0)
	{
		Weapon.StockMagMesh = Weapon.MagMesh;
		Weapon.StockMagazineSize = Weapon.MagazineSize;
		Weapon.StockMagTravelCm = Weapon.MagTravelCm;
	}
	const int32 Size = Attachment && Attachment->MagazineSizeOverride > 0 ? Attachment->MagazineSizeOverride : Weapon.StockMagazineSize;
	Weapon.MagMesh = Attachment && Attachment->MagMeshOverride ? Attachment->MagMeshOverride.Get() : Weapon.StockMagMesh.Get();
	// Longer by MagExtraCm: the way out of the well too, and the mesh stretched along it until the PC's own mesh.
	const float Stock = Weapon.StockMagTravelCm.Size();
	const float Extra = Attachment ? Attachment->MagExtraCm : 0.0f;
	Weapon.MagTravelCm = Stock > 0.0f ? Weapon.StockMagTravelCm * ((Stock + Extra) / Stock) : Weapon.StockMagTravelCm;
	Weapon.MagStretch = Weapon.MagMesh && Attachment && Attachment->MagMeshOverride && Weapon.MagMesh == Weapon.StockMagMesh && Stock > 0.0f
		? (Stock + Extra) / Stock : 1.0f;
	// Off a reload (a command, a preset): the rounds stay, cut to the size, the rest into the reserve.
	if (!bMagSwap)
	{
		const int32 Kept = FMath::Min(Weapon.Ammo, Size);
		Weapon.Reserve += Weapon.Ammo - Kept;
		Weapon.Ammo = Kept;
	}
	Weapon.MagazineSize = Size;
	ClearMagRounds = -1;
	if (&Weapon == (Weapons.IsValidIndex(ShownWeapon) ? &Weapons[ShownWeapon] : nullptr))
	{
		UpdateCounterAmmo();
	}
}

bool UOSWeaponsComponent::ApplyAttachment(int32 MountIndex, FName Id)
{
	FOSWeapon* Weapon = GetWeapon() ? &Weapons[CurrentWeapon] : nullptr;
	if (!Weapon || !Weapon->Mounts.IsValidIndex(MountIndex) || IsSeating() || GetReloadRemaining() > 0.0f)
	{
		return false;
	}
	FOSWeaponMount& Place = Weapon->Mounts[MountIndex];
	const FName Was = Place.Mounted;
	if (Was == Id)
	{
		return false;
	}
	const FOSAttachment* Next = FindAttachment(Id);
	if (!Id.IsNone() && (!Next || Next->MountType != Place.Type || !Next->FitsGun(Weapon->Id)))
	{
		return false;
	}
	if (Place.Type == EOSMountType::Magazine)
	{
		// A magazine swap is a reload (owner 22.10 14:16): the stock one out along its path (its rounds kept, not
		// dropped: not a fight), the other in; the swap itself at the eject (UpdatePartEvents).
		if (!Was.IsNone())
		{
			Carried.AddUnique(Was);
		}
		if (!Id.IsNone())
		{
			// Off another gun's well, if it was there.
			UnmountAttachment(Id);
			Carried.Remove(Id);
		}
		Place.Mounted = Id;
		bMagSwap = true;
		MagSwapTo = Id;
		MagSwapKeep = Weapon->Ammo;
		SeatWeapon = CurrentWeapon;
		const float Seconds = StartReloadTimeline(*Weapon);
		UE_LOG(LogMindKore, Log, TEXT("Attach %s: magazine %s -> %s, %.2f s"), *GetNameSafe(GetOwner()), Was.IsNone() ? TEXT("stock") : *Was.ToString(),
			Id.IsNone() ? TEXT("stock") : *Id.ToString(), Seconds);
		return true;
	}
	SeatSteps.Reset();
	float At = 0.0f;
	const FOSAttachment* Old = FindAttachment(Was);
	if (Old)
	{
		// The old module off first, back into the pouch, on its way.
		SeatSteps.Add({ MountIndex, Was, false, At, OSSeatLength(*Old, Place.Type, false) });
		At += SeatSteps.Last().Length;
		Carried.AddUnique(Was);
	}
	if (Next)
	{
		UnmountAttachment(Id);
		Carried.Remove(Id);
		SeatSteps.Add({ MountIndex, Id, true, At, OSSeatLength(*Next, Place.Type, true) });
	}
	Place.Mounted = Id;
	SeatWeapon = CurrentWeapon;
	SeatStartSeconds = GetWorld()->GetTimeSeconds();
	SeatPrevTime = -KINDA_SMALL_NUMBER;
	UE_LOG(LogMindKore, Log, TEXT("Attach %s: %s %s -> %s, %.2f s"), *GetNameSafe(GetOwner()), *Weapon->Name.ToString(), *OSMountName(Place.Type).ToString(),
		Was.IsNone() ? TEXT("-") : *Was.ToString(), Id.IsNone() ? TEXT("-") : *Id.ToString(), GetSeatRemaining());
	return true;
}

bool UOSWeaponsComponent::IsSeating() const
{
	return !SeatSteps.IsEmpty() || (bMagSwap && ReloadEndSeconds > 0.0);
}

float UOSWeaponsComponent::GetSeatRemaining() const
{
	if (bMagSwap)
	{
		return GetReloadRemaining();
	}
	if (SeatSteps.IsEmpty())
	{
		return 0.0f;
	}
	const FSeatStep& Last = SeatSteps.Last();
	return FMath::Max(0.0f, Last.Start + Last.Length - float(GetWorld()->GetTimeSeconds() - SeatStartSeconds));
}

void UOSWeaponsComponent::FinishSeat()
{
	SeatSteps.Reset();
	SeatPrevTime = -1.0f;
	if (bMagSwap && Weapons.IsValidIndex(SeatWeapon))
	{
		// A swap cut short (a switch, a grab): it ends as if the magazine were seated - the rounds are not lost.
		FOSWeapon& Weapon = Weapons[SeatWeapon];
		const bool bEjected = Weapon.Ammo == 0 && MagSwapKeep > 0;
		bMagSwap = false;
		SwapMagazine(Weapon, MagSwapTo);
		if (bEjected)
		{
			Weapon.Ammo = FMath::Min(MagSwapKeep, Weapon.MagazineSize);
			Weapon.Reserve += MagSwapKeep - Weapon.Ammo;
		}
		UpdateCounterAmmo();
	}
	bMagSwap = false;
	SeatWeapon = INDEX_NONE;
}

bool UOSWeaponsComponent::ToggleSwitch()
{
	const FOSWeapon* Weapon = GetWeapon();
	if (!Weapon)
	{
		return false;
	}
	// The switched attachment on the gun chosen in the menu last (§8: two on one gun - the tap is that one's), else the first.
	const FOSAttachment* Target = nullptr;
	for (const FOSWeaponMount& Place : Weapon->Mounts)
	{
		const FOSAttachment* On = FindAttachment(Place.Mounted);
		if (On && On->bHasSwitch && (!Target || On->Id == LastSwitchChoice))
		{
			Target = On;
		}
	}
	if (!Target)
	{
		return false;
	}
	const bool bWasOn = IsSwitchOn(Target->Id);
	bWasOn ? (void)SwitchOff.Add(Target->Id) : (void)SwitchOff.Remove(Target->Id);
	PlayGunSound(Target->SwitchSound, 0.6f, bWasOn ? 0.9f : 1.1f);
	UE_LOG(LogMindKore, Log, TEXT("Attach %s: %s switched %s"), *GetNameSafe(GetOwner()), *Target->Id.ToString(), bWasOn ? TEXT("off") : TEXT("on"));
	return true;
}

const FOSAttachment* UOSWeaponsComponent::GetShownOn(EOSMountType Type) const
{
	if (!Weapons.IsValidIndex(ShownWeapon))
	{
		return nullptr;
	}
	for (const FOSWeaponMount& Place : Weapons[ShownWeapon].Mounts)
	{
		if (Place.Type == Type && !Place.Mounted.IsNone())
		{
			return FindAttachment(Place.Mounted);
		}
	}
	return nullptr;
}

const FOSAttachment* UOSWeaponsComponent::GetActiveOn(int32 Weapon, EOSMountType Type) const
{
	if (!Weapons.IsValidIndex(Weapon))
	{
		return nullptr;
	}
	const float Now = SeatSteps.IsEmpty() ? 0.0f : float(GetWorld()->GetTimeSeconds() - SeatStartSeconds);
	for (int32 Mount = 0; Mount < Weapons[Weapon].Mounts.Num(); ++Mount)
	{
		const FOSWeaponMount& Place = Weapons[Weapon].Mounts[Mount];
		if (Place.Type != Type || Place.Mounted.IsNone())
		{
			continue;
		}
		const FOSAttachment* On = FindAttachment(Place.Mounted);
		if (On && Weapon == SeatWeapon)
		{
			// Rolling on: it works from the latch (the lens lights at LensSeconds); the screw from its squeeze.
			for (const FSeatStep& Step : SeatSteps)
			{
				if (Step.Mount == Mount && Step.bOn && Now < Step.Start + (Type == EOSMountType::Rail ? On->LensSeconds : Step.Length))
				{
					return nullptr;
				}
			}
		}
		return On;
	}
	return nullptr;
}

FTransform UOSWeaponsComponent::GetMountFrame(const FOSWeaponMount& Mount) const
{
	if (!Mount.Socket.IsNone() && ViewModel && ViewModel->DoesSocketExist(Mount.Socket))
	{
		// The socket's place on the mesh; its turn is the mount's own (the bore's line, not the socket's axes).
		return Mount.Transform * FTransform(ViewModel->GetSocketTransform(Mount.Socket, RTS_Component).GetLocation());
	}
	return Mount.Transform;
}

bool UOSWeaponsComponent::GetLaserLens(FTransform& OutLens) const
{
	const FOSAttachment* Laser = GetActiveOn(ShownWeapon, EOSMountType::Rail);
	if (!Laser || !Laser->bLaser || !IsSwitchOn(Laser->Id) || !ViewModel || SwitchStage != ESwitch::Drawn)
	{
		return false;
	}
	for (int32 Mount = 0; Mount < Weapons[ShownWeapon].Mounts.Num(); ++Mount)
	{
		if (Weapons[ShownWeapon].Mounts[Mount].Mounted == Laser->Id && ModulesInView.IsValidIndex(Mount))
		{
			const FTransform Frame = GetMountFrame(Weapons[ShownWeapon].Mounts[Mount]) * ViewModel->GetComponentTransform();
			OutLens = FTransform(Frame.GetRotation(), Frame.TransformPosition(Laser->LensOffsetCm));
			return true;
		}
	}
	return false;
}

void UOSWeaponsComponent::KickPose(const FVector& Move, const FRotator& Turn)
{
	Pose.PlaceVelocity += Move;
	Pose.TurnVelocity += FVector(Turn.Pitch, Turn.Yaw, Turn.Roll);
}

void UOSWeaponsComponent::MakeAttachmentComponents()
{
	if (!ViewModel)
	{
		return;
	}
	const auto Make = [this](const TCHAR* Name, USceneComponent* Parent)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(GetOwner(), Name);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		Part->SetOnlyOwnerSee(true);
		Part->SetupAttachment(Parent);
		Part->RegisterComponent();
		Part->SetVisibility(false);
		return Part;
	};
	int32 MostMounts = 0;
	for (const FOSWeapon& Gun : Weapons)
	{
		MostMounts = FMath::Max(MostMounts, Gun.Mounts.Num());
	}
	// The RMB-93 comes later by a pickup (two places): room for four.
	for (int32 Index = 0; Index < FMath::Max(MostMounts, 4); ++Index)
	{
		ModulesInView.Add(Make(*FString::Printf(TEXT("GunModuleInView%d"), Index), ViewModel));
	}
	TorchInView = NewObject<USpotLightComponent>(GetOwner(), TEXT("GunTorchInView"));
	TorchInView->SetupAttachment(ViewModel);
	// The suit projector's lasers' reasons: no shadow (a light moving with the camera every frame), no MegaLights.
	TorchInView->CastShadows = false;
	TorchInView->bAllowMegaLights = false;
	TorchInView->IntensityUnits = ELightUnits::Candelas;
	TorchInView->bUseInverseSquaredFalloff = true;
	TorchInView->SetVisibleFlag(false);
	TorchInView->RegisterComponent();
	// The clear magazine's insides ride the magazine in view (its swing, its reload path).
	if (MagInView)
	{
		static const TCHAR* CylinderPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
		static const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
		UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, CylinderPath);
		RoundsInView = NewObject<UInstancedStaticMeshComponent>(GetOwner(), TEXT("GunRoundsInView"));
		RoundsInView->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RoundsInView->SetCastShadow(false);
		RoundsInView->SetOnlyOwnerSee(true);
		RoundsInView->SetupAttachment(MagInView);
		RoundsInView->SetStaticMesh(Cylinder);
		RoundsInView->RegisterComponent();
		RoundsInView->SetVisibility(false);
		FollowerInView = Make(TEXT("GunFollowerInView"), MagInView);
		FollowerInView->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, CubePath));
		SpringInView = Make(TEXT("GunSpringInView"), MagInView);
		SpringInView->SetStaticMesh(Cylinder);
		// The rounds in the brass of the guns' cases (SM_OS_Case_9mm's material); the bullets' copper waits for the PC's mesh.
		if (Weapons.IsValidIndex(0) && Weapons[0].CaseMesh)
		{
			RoundsInView->SetMaterial(0, Weapons[0].CaseMesh->GetMaterial(0));
		}
	}
}

void UOSWeaponsComponent::GetSeatYield(FVector& OutPlace, FRotator& OutTurn) const
{
	OutPlace = FVector::ZeroVector;
	OutTurn = FRotator::ZeroRotator;
	if (SeatSteps.IsEmpty() || SeatWeapon != ShownWeapon || !Weapons.IsValidIndex(SeatWeapon))
	{
		return;
	}
	const float Now = float(GetWorld()->GetTimeSeconds() - SeatStartSeconds);
	for (const FSeatStep& Step : SeatSteps)
	{
		const FOSAttachment* Attachment = FindAttachment(Step.Id);
		if (!Attachment || Now < Step.Start || Now >= Step.Start + Step.Length)
		{
			continue;
		}
		const FOSSeatPose Seat = OSSeatPoseAt(*Attachment, Weapons[SeatWeapon].Mounts[Step.Mount], Step.bOn, Now - Step.Start, FVector::ZeroVector);
		// Back along the bore is toward the eye: the camera's -X.
		OutPlace = FVector(-Seat.YieldCm, 0.0f, 0.0f);
		OutTurn = FRotator(0.0f, 0.0f, Seat.YieldRollDeg);
	}
}

void UOSWeaponsComponent::UpdateSeatEvents()
{
	if (SeatSteps.IsEmpty())
	{
		return;
	}
	if (SeatWeapon != CurrentWeapon || !Weapons.IsValidIndex(SeatWeapon))
	{
		FinishSeat();
		return;
	}
	const float Now = float(GetWorld()->GetTimeSeconds() - SeatStartSeconds);
	const float Before = SeatPrevTime;
	SeatPrevTime = Now;
	const auto Play = [this](USoundBase* Sound, float Volume)
	{
		if (Sound)
		{
			PlayGunSound(Sound, Volume, FMath::FRandRange(0.97f, 1.03f));
		}
	};
	const auto Kick = [this](const FOSPoseKick& Each)
	{
		KickPose(Each.Move, Each.Turn);
	};
	for (const FSeatStep& Step : SeatSteps)
	{
		const FOSAttachment* A = FindAttachment(Step.Id);
		if (!A)
		{
			continue;
		}
		const float T0 = Before - Step.Start;
		const float T1 = Now - Step.Start;
		const EOSMountType Type = Weapons[SeatWeapon].Mounts[Step.Mount].Type;
		const auto At = [T0, T1](float Time) { return SeatCrossed(T0, T1, Time); };
		if (Type == EOSMountType::Muzzle)
		{
			const float Turns = FMath::Max(A->ThreadTurns, 0.5f);
			if (Step.bOn)
			{
				if (At(0.0f)) { Play(A->PouchSound, 0.5f); }
				// Three rasping turns (at each whole turn of the thread) and the squeeze.
				for (int32 Turn = 0; Turn < FMath::CeilToInt32(Turns); ++Turn)
				{
					const float Share = FMath::Min(Turn / Turns, 1.0f);
					if (At(A->ApproachSeconds + A->ThreadSeconds * (1.0f - FMath::Sqrt(1.0f - Share)))) { Play(A->RailClick, 0.35f); }
				}
				if (At(A->ApproachSeconds + A->ThreadSeconds)) { Play(A->StopKnock, 0.6f); Kick(A->TightenKick); }
			}
			else
			{
				for (int32 Turn = 0; Turn < FMath::CeilToInt32(Turns); ++Turn)
				{
					if (At(A->UnthreadSeconds * Turn / Turns)) { Play(A->RailClick, 0.3f); }
				}
				if (At(A->UnthreadSeconds)) { Play(A->PouchSound, 0.45f); }
			}
			continue;
		}
		if (Step.bOn)
		{
			if (At(0.0f)) { Play(A->PouchSound, 0.5f); }
			if (At(A->SeatKick.Time)) { Kick(A->SeatKick); }
			if (At(A->ApproachSeconds + A->RiseSeconds * 0.75f)) { Play(A->RailClick, 0.7f); }
			for (const float Detent : A->Detents)
			{
				if (At(Detent)) { Play(A->DetentTick, 0.35f); }
			}
			if (At(A->StopSeconds)) { Play(A->StopKnock, 0.8f); Kick(A->StopKick); }
			if (At(A->LockSeconds)) { Play(A->LockClick, 0.6f); Kick(A->LockKick); }
		}
		else
		{
			if (At(0.0f)) { Play(A->LockClick, 0.6f); }
			for (const float Detent : A->Detents)
			{
				if (At(Detent - (A->StopSeconds - 0.30f))) { Play(A->DetentTick, 0.35f); }
			}
			if (At(0.30f)) { Play(A->RailClick, 0.7f); }
			if (At(0.36f)) { Play(A->PouchSound, 0.45f); }
		}
	}
	const FSeatStep& Last = SeatSteps.Last();
	if (Now >= Last.Start + Last.Length)
	{
		UE_LOG(LogMindKore, Log, TEXT("Attach %s: seated after %.2f s"), *GetNameSafe(GetOwner()), Now);
		SeatSteps.Reset();
		SeatWeapon = INDEX_NONE;
	}
}

void UOSWeaponsComponent::UpdateModules(const FOSWeapon* Weapon)
{
	if (ModulesInView.IsEmpty())
	{
		return;
	}
	bool bTorch = false;
	const float Now = SeatSteps.IsEmpty() ? 0.0f : float(GetWorld()->GetTimeSeconds() - SeatStartSeconds);
	for (int32 Mount = 0; Mount < ModulesInView.Num(); ++Mount)
	{
		UStaticMeshComponent* Module = ModulesInView[Mount];
		if (!Weapon || !Weapon->Mounts.IsValidIndex(Mount) || Weapon->Mounts[Mount].Type == EOSMountType::Magazine)
		{
			Module->SetVisibility(false);
			continue;
		}
		const FOSWeaponMount& Place = Weapon->Mounts[Mount];
		const FTransform Frame = GetMountFrame(Place);
		// Which module is on this place now: one rolling on or off, or the one seated; none between the two steps.
		const FOSAttachment* Shown = FindAttachment(Place.Mounted);
		FOSSeatPose Seat;
		Seat.bShown = Shown != nullptr;
		Seat.bLens = true;
		if (SeatWeapon == ShownWeapon && !SeatSteps.IsEmpty())
		{
			for (const FSeatStep& Step : SeatSteps)
			{
				if (Step.Mount != Mount)
				{
					continue;
				}
				if (Now < Step.Start)
				{
					Shown = nullptr; // the new one still in the pouch
					Seat.bShown = false;
					break;
				}
				if (Now < Step.Start + Step.Length)
				{
					Shown = FindAttachment(Step.Id);
					if (Shown)
					{
						// The pouch, out of frame below-right of the seat in the camera's frame, carried into the mount's.
						const FTransform View = ViewModel->GetRelativeTransform();
						const FVector SeatInView = View.TransformPosition(Frame.GetLocation());
						const FVector PouchInMount = Frame.InverseTransformPosition(View.InverseTransformPosition(SeatInView + Shown->PouchOffsetCm));
						Seat = OSSeatPoseAt(*Shown, Place, Step.bOn, Now - Step.Start, PouchInMount);
					}
					break;
				}
			}
		}
		if (!Shown || !Seat.bShown || !Shown->Mesh)
		{
			Module->SetVisibility(false);
			continue;
		}
		if (Module->GetStaticMesh() != Shown->Mesh)
		{
			Module->SetStaticMesh(Shown->Mesh);
			Module->SetMaterial(0, Shown->Material);
		}
		const FTransform Local(Seat.Turn, Seat.Place);
		Module->SetRelativeTransform(Shown->SeatOffset * Local * Frame);
		Module->SetVisibility(true);
		if (Shown->TorchCandelas > 0.0f && Seat.bLens && IsSwitchOn(Shown->Id) && SwitchStage == ESwitch::Drawn)
		{
			bTorch = true;
			const FTransform Lens = FTransform(Shown->LensOffsetCm) * Local * Frame;
			TorchInView->SetRelativeLocationAndRotation(Lens.GetLocation(), Lens.GetRotation());
			if (TorchInView->Intensity != Shown->TorchCandelas)
			{
				TorchInView->SetIntensity(Shown->TorchCandelas);
				TorchInView->SetInnerConeAngle(Shown->TorchConeDeg * 0.6f);
				TorchInView->SetOuterConeAngle(Shown->TorchConeDeg);
				TorchInView->SetAttenuationRadius(Shown->TorchRangeCm);
			}
		}
	}
	// The laser's world light ticks only while a laser on the gun in view is switched on (its own check is per frame).
	if (LaserSight)
	{
		const FOSAttachment* Rail = Weapon ? GetShownOn(EOSMountType::Rail) : nullptr;
		LaserSight->Wake(Rail && Rail->bLaser && IsSwitchOn(Rail->Id));
	}
	if (TorchInView && TorchInView->IsVisible() != bTorch)
	{
		TorchInView->SetVisibility(bTorch);
	}
	// The clear magazine: its body's material while it is on, its insides with the magazine in view; rebuilt only
	// when the rounds change (UpdateClearMagazine), never per tick.
	if (RoundsInView && MagInView)
	{
		const FOSAttachment* Mag = Weapon ? GetShownOn(EOSMountType::Magazine) : nullptr;
		const bool bClearMag = Mag && Mag->bClearMagazine && Weapon->MagMesh;
		if (!bClearMag && !ClearMagId.IsNone())
		{
			ClearMagId = NAME_None;
			ClearMagRounds = -1;
			MagInView->SetMaterial(0, nullptr); // the stock mesh's own again
		}
		if (bClearMag && (ClearMagRounds != Weapon->Ammo || ClearMagId != Mag->Id))
		{
			UpdateClearMagazine();
		}
		const bool bInside = bClearMag && MagInView->IsVisible();
		if (RoundsInView->IsVisible() != bInside)
		{
			RoundsInView->SetVisibility(bInside);
			FollowerInView->SetVisibility(bInside);
			SpringInView->SetVisibility(bInside);
		}
	}
}

void UOSWeaponsComponent::TransferAttachments(const FOSWeapon& Old, FOSWeapon& Fresh)
{
	// The slot's attachments go onto the new gun where they fit and the place is free (owner 22.10, §4); the rest to the pouch.
	for (const FOSWeaponMount& Place : Old.Mounts)
	{
		const FOSAttachment* On = FindAttachment(Place.Mounted);
		if (!On)
		{
			continue;
		}
		FOSWeaponMount* Into = On->FitsGun(Fresh.Id) ? Fresh.Mounts.FindByPredicate([On](const FOSWeaponMount& Each)
			{ return Each.Type == On->MountType && Each.Mounted.IsNone(); }) : nullptr;
		if (Into && On->MountType != EOSMountType::Magazine)
		{
			Into->Mounted = On->Id;
			UE_LOG(LogMindKore, Log, TEXT("Attach %s: %s moved onto %s"), *GetNameSafe(GetOwner()), *On->Id.ToString(), *Fresh.Name.ToString());
			continue;
		}
		Carried.AddUnique(On->Id);
		ToastGuns(this, FText::Format(LOCTEXT("ToPouch", "{0} — в сумке"), On->Name));
	}
}

void UOSWeaponsComponent::UpdateClearMagazine()
{
	if (!RoundsInView || !Weapons.IsValidIndex(ShownWeapon))
	{
		return;
	}
	FOSWeapon& Weapon = Weapons[ShownWeapon];
	const FOSAttachment* Mag = GetShownOn(EOSMountType::Magazine);
	if (!Mag || !Mag->bClearMagazine)
	{
		return;
	}
	ClearMagRounds = Weapon.Ammo;
	ClearMagId = Mag->Id;
	// The magazine's body is stretched along the well (MagStretch); its insides are not: they sit in the stock's frame.
	const FTransform Unstretch = MagStretchAbout(Weapon.MagPivot, 1.0f / FMath::Max(Weapon.MagStretch, 0.01f));
	for (USceneComponent* Inside : { static_cast<USceneComponent*>(RoundsInView.Get()), static_cast<USceneComponent*>(FollowerInView.Get()),
		static_cast<USceneComponent*>(SpringInView.Get()) })
	{
		Inside->SetRelativeTransform(Unstretch);
	}
	if (UMaterialInterface* Clear = Mag->ClearMaterial.LoadSynchronous())
	{
		MagInView->SetMaterial(0, Clear);
	}
	else if (UMaterialInterface* Fallback = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Translucent.Widget3DPassThrough_Translucent")))
	{
		// M_OS_ClearMag is the PC's (Docs/Attachments.md, «Ждёт ПК»): an engine translucent one until then.
		MagInView->SetMaterial(0, Fallback);
	}
	// A staggered double stack down the well from the lips: 9.3 mm a row, the rows 2 mm either side (owner 22.10 14:19).
	const FVector Down = Weapon.StockMagTravelCm.IsNearlyZero() ? FVector(0.0f, 0.0f, -1.0f) : Weapon.StockMagTravelCm.GetSafeNormal();
	const FVector Across(0.0f, 1.0f, 0.0f);
	// The rounds lie across the well, along the bore, bullets toward the muzzle (the USP's -X).
	const FVector Along = FVector::CrossProduct(Across, Down).GetSafeNormal();
	const FQuat Lying = FQuat::FindBetweenNormals(FVector::UpVector, -Along);
	constexpr float Pitch = 0.465f, Lips = 0.6f;
	const int32 Capacity = FMath::Max(Weapon.MagazineSize, 1);
	TArray<FTransform> Rounds;
	Rounds.Reserve(Weapon.Ammo);
	for (int32 Round = 0; Round < FMath::Min(Weapon.Ammo, Capacity); ++Round)
	{
		const FVector At = Weapon.MagPivot + Down * (Lips + Round * Pitch) + Across * (Round % 2 ? 0.2f : -0.2f);
		Rounds.Add(FTransform(Lying, At, FVector(0.009f, 0.009f, 0.019f)));
	}
	RoundsInView->ClearInstances();
	RoundsInView->AddInstances(Rounds, /*bShouldReturnIndices=*/false);
	// The follower under the last round; the spring from it to the floor, as long as the free space.
	const float FollowerDepth = Lips + Weapon.Ammo * Pitch + 0.3f;
	const float Floor = Lips + Capacity * Pitch + 1.5f;
	FollowerInView->SetRelativeTransform(FTransform(FQuat::FindBetweenNormals(FVector::UpVector, -Down), Weapon.MagPivot + Down * FollowerDepth,
		FVector(0.022f, 0.009f, 0.003f)) * Unstretch);
	const float SpringLength = FMath::Max(Floor - FollowerDepth, 0.2f);
	SpringInView->SetRelativeTransform(FTransform(FQuat::FindBetweenNormals(FVector::UpVector, -Down), Weapon.MagPivot + Down * (FollowerDepth + SpringLength * 0.5f),
		FVector(0.016f, 0.008f, SpringLength / 100.0f)) * Unstretch);
}

FString UOSWeaponsComponent::DescribeGear() const
{
	FString Text;
	for (int32 Gun = 0; Gun < Weapons.Num(); ++Gun)
	{
		const FOSWeapon& Weapon = Weapons[Gun];
		Text += FString::Printf(TEXT("[%d] %s (%s) %s %d/%d:"), Gun + 1, *Weapon.Name.ToString(), *Weapon.Id.ToString(), Weapon.bUnlocked ? TEXT("open") : TEXT("locked"),
			Weapon.Ammo, Weapon.Reserve);
		if (Weapon.Mounts.IsEmpty())
		{
			Text += TEXT(" no places");
		}
		for (const FOSWeaponMount& Place : Weapon.Mounts)
		{
			Text += FString::Printf(TEXT(" %s=%s"), *OSMountName(Place.Type).ToString(), Place.Mounted.IsNone()
				? (Place.Type == EOSMountType::Magazine ? TEXT("stock") : TEXT("-")) : *Place.Mounted.ToString());
		}
		Text += TEXT("\n");
	}
	Text += TEXT("pouch:");
	for (const FName Id : Carried)
	{
		Text += TEXT(" ") + Id.ToString();
	}
	if (Carried.IsEmpty())
	{
		Text += TEXT(" -");
	}
	return Text;
}

void OSReadGear(const UOSWeaponsComponent& Guns, TArray<FString>& OutGuns, TArray<FString>& OutAttachments)
{
	OutGuns.Reset();
	OutAttachments.Reset();
	for (const FOSWeapon& Weapon : Guns.Weapons)
	{
		if (Weapon.bUnlocked)
		{
			OutGuns.Add(Weapon.Id.ToString());
		}
		for (const FOSWeaponMount& Place : Weapon.Mounts)
		{
			if (!Place.Mounted.IsNone())
			{
				OutAttachments.Add(Place.Mounted.ToString() + TEXT("@") + Weapon.Id.ToString());
			}
		}
	}
	for (const FName Id : Guns.Carried)
	{
		OutAttachments.Add(Id.ToString());
	}
}

void OSApplyGear(UOSWeaponsComponent& Guns, const TArray<FString>& InGuns, const TArray<FString>& InAttachments)
{
	// The guns as named: the RMB-93 by its Id swaps the pump out (os.Guns.Give does the same); the rest locked.
	for (const FString& Id : InGuns)
	{
		if (Guns.FindWeapon(Id) == INDEX_NONE)
		{
			OSSwapGun(Guns, 1, Id);
		}
	}
	for (int32 Gun = 0; Gun < Guns.Weapons.Num(); ++Gun)
	{
		const bool bWanted = InGuns.ContainsByPredicate([&](const FString& Id) { return Guns.Weapons[Gun].Id.ToString().Equals(Id, ESearchCase::IgnoreCase); });
		if (bWanted && !Guns.Weapons[Gun].bUnlocked)
		{
			Guns.UnlockWeapon(Gun);
		}
		else if (!bWanted && Guns.Weapons[Gun].bUnlocked)
		{
			Guns.LockWeapon(Gun);
		}
	}
	for (const FOSAttachment& Each : TArray<FOSAttachment>(Guns.Attachments))
	{
		Guns.TakeAttachment(Each.Id);
	}
	for (const FString& Entry : InAttachments)
	{
		FString Id, Gun;
		if (!Entry.Split(TEXT("@"), &Id, &Gun))
		{
			Id = Entry;
		}
		if (!Guns.GiveAttachment(FName(*Id), /*bBagOnly=*/true))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Gear: no attachment %s"), *Id);
			continue;
		}
		if (!Gun.IsEmpty() && !Guns.MountAttachment(FName(*Id), Guns.FindWeapon(Gun)))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Gear: %s does not go on %s, left in the pouch"), *Id, *Gun);
		}
	}
}

bool OSSwapGun(UOSWeaponsComponent& Guns, int32 Index, const FString& To)
{
	const UOSWeaponsComponent* Defaults = GetDefault<UOSWeaponsComponent>();
	const UOSScopeComponent* Sights = GetDefault<UOSScopeComponent>();
	if (!Guns.Weapons.IsValidIndex(Index) || !Defaults->Weapons.IsValidIndex(Index))
	{
		return false;
	}
	FOSWeapon Fresh;
	FOSGunAim Aim = Sights->Aims.IsValidIndex(Index) ? Sights->Aims[Index] : FOSGunAim();
	if (To.Equals(TEXT("Rmb93"), ESearchCase::IgnoreCase) && Index == 1)
	{
		Fresh = OSMakeRmb93(Defaults->Weapons[Index]);
		Aim = OSMakeRmb93Aim(Aim);
	}
	else if (To.Equals(TEXT("Slot"), ESearchCase::IgnoreCase) || To.Equals(Defaults->Weapons[Index].Id.ToString(), ESearchCase::IgnoreCase))
	{
		Fresh = Defaults->Weapons[Index];
	}
	else
	{
		UE_LOG(LogMindKore, Warning, TEXT("Swap: no gun %s for slot %d (Rmb93 into 2, Slot)"), *To, Index + 1);
		return false;
	}
	if (Guns.Weapons[Index].Id == Fresh.Id)
	{
		return true;
	}
	const bool bWasOpen = Guns.Weapons[Index].bUnlocked;
	if (!Guns.ApplyPickupWeapon(Index, Fresh))
	{
		return false;
	}
	if (Guns.Scope && Guns.Scope->Aims.IsValidIndex(Index))
	{
		Guns.Scope->Aims[Index] = Aim;
	}
	if (bWasOpen)
	{
		Guns.UnlockWeapon(Index);
	}
	UE_LOG(LogMindKore, Log, TEXT("Swap: slot %d is %s now"), Index + 1, *Guns.Weapons[Index].Name.ToString());
	return true;
}

#if !UE_BUILD_SHIPPING
namespace
{
	/** The player's guns: the zero-G pawn's or a walker's. */
	UOSWeaponsComponent* PlayerGuns(UWorld* World)
	{
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		UOSWeaponsComponent* Guns = Pawn ? Pawn->FindComponentByClass<UOSWeaponsComponent>() : nullptr;
		if (!Guns)
		{
			UE_LOG(LogMindKore, Warning, TEXT("os.Guns / os.Attach: the player has no guns"));
		}
		return Guns;
	}

	/** The guns a key names: "all", a slot 1..3, an Id or a name; for Give the RMB-93's Id swaps it into slot 2 first. */
	TArray<int32> NamedGuns(UOSWeaponsComponent& Guns, const FString& Key, bool bSwapIn)
	{
		TArray<int32> Out;
		if (Key.Equals(TEXT("all"), ESearchCase::IgnoreCase))
		{
			for (int32 Gun = 0; Gun < Guns.Weapons.Num(); ++Gun)
			{
				Out.Add(Gun);
			}
			return Out;
		}
		int32 Gun = Guns.FindWeapon(Key);
		if (Gun == INDEX_NONE && bSwapIn && OSSwapGun(Guns, 1, Key))
		{
			Gun = 1;
		}
		if (Gun == INDEX_NONE)
		{
			UE_LOG(LogMindKore, Warning, TEXT("os.Guns: no gun %s (1..3, Pistol, Shotgun, Rmb93, Sniper, all)"), *Key);
		}
		else
		{
			Out.Add(Gun);
		}
		return Out;
	}

	TArray<FName> NamedAttachments(const UOSWeaponsComponent& Guns, const FString& Key)
	{
		TArray<FName> Out;
		for (const FOSAttachment& Each : Guns.Attachments)
		{
			if (Key.Equals(TEXT("all"), ESearchCase::IgnoreCase) || Each.Id.ToString().Equals(Key, ESearchCase::IgnoreCase))
			{
				Out.Add(Each.Id);
			}
		}
		if (Out.IsEmpty())
		{
			UE_LOG(LogMindKore, Warning, TEXT("os.Attach: no attachment %s (Laser, Torch, Suppressor, BigMag, all)"), *Key);
		}
		return Out;
	}

	FAutoConsoleCommandWithWorldAndArgs GOSGunsGive(TEXT("os.Guns.Give"),
		TEXT("os.Guns.Give <slot|Id|name|all>: open a gun (1..3, Pistol, Shotgun, Rmb93 - swaps it into 2 -, Sniper)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UOSWeaponsComponent* Guns = PlayerGuns(World))
			{
				for (const int32 Gun : NamedGuns(*Guns, Args.Num() > 0 ? Args[0] : TEXT("all"), true))
				{
					Guns->UnlockWeapon(Gun);
				}
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GOSGunsTake(TEXT("os.Guns.Take"),
		TEXT("os.Guns.Take <slot|Id|name|all>: close a gun - put away if in hand; its rounds and attachments stay with it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UOSWeaponsComponent* Guns = PlayerGuns(World))
			{
				for (const int32 Gun : NamedGuns(*Guns, Args.Num() > 0 ? Args[0] : TEXT("all"), false))
				{
					Guns->LockWeapon(Gun);
				}
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GOSGunsSwap(TEXT("os.Guns.Swap"),
		TEXT("os.Guns.Swap <slot> <Rmb93|Slot>: another gun into the slot, as the RMB-93 pickup does (2 Rmb93; Slot: the slot's own back)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UOSWeaponsComponent* Guns = PlayerGuns(World);
			if (Guns && Args.Num() >= 2)
			{
				OSSwapGun(*Guns, FCString::Atoi(*Args[0]) - 1, Args[1]);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GOSAttachGive(TEXT("os.Attach.Give"),
		TEXT("os.Attach.Give <Id|all> [bag]: into the pouch; onto the gun in hand when a free place fits (bag: the pouch only)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UOSWeaponsComponent* Guns = PlayerGuns(World))
			{
				const bool bBag = Args.Num() > 1 && Args[1].Equals(TEXT("bag"), ESearchCase::IgnoreCase);
				const TArray<FName> Ids = NamedAttachments(*Guns, Args.Num() > 0 ? Args[0] : TEXT("all"));
				for (const FName Id : Ids)
				{
					// "all" never rolls four on at once: the pouch, then the menu.
					Guns->GiveAttachment(Id, bBag || Ids.Num() > 1);
				}
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GOSAttachTake(TEXT("os.Attach.Take"),
		TEXT("os.Attach.Take <Id|all>: off the gun at once and out of the pouch."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UOSWeaponsComponent* Guns = PlayerGuns(World))
			{
				for (const FName Id : NamedAttachments(*Guns, Args.Num() > 0 ? Args[0] : TEXT("all")))
				{
					Guns->TakeAttachment(Id);
				}
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GOSAttachMount(TEXT("os.Attach.Mount"),
		TEXT("os.Attach.Mount <Id> [slot|gun Id]: onto the gun at once, no motion (default the gun in hand); given first if not carried."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UOSWeaponsComponent* Guns = PlayerGuns(World);
			if (!Guns || Args.IsEmpty())
			{
				return;
			}
			const int32 Gun = Args.Num() > 1 ? Guns->FindWeapon(Args[1]) : Guns->CurrentWeapon;
			for (const FName Id : NamedAttachments(*Guns, Args[0]))
			{
				Guns->GiveAttachment(Id, true);
				if (!Guns->MountAttachment(Id, Gun))
				{
					UE_LOG(LogMindKore, Warning, TEXT("os.Attach.Mount: %s does not go on gun %d - in the pouch"), *Id.ToString(), Gun + 1);
				}
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GOSAttachUnmount(TEXT("os.Attach.Unmount"),
		TEXT("os.Attach.Unmount <Id|all>: off its gun at once, into the pouch."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UOSWeaponsComponent* Guns = PlayerGuns(World))
			{
				for (const FName Id : NamedAttachments(*Guns, Args.Num() > 0 ? Args[0] : TEXT("all")))
				{
					Guns->UnmountAttachment(Id);
				}
			}
		}));

	FAutoConsoleCommandWithWorld GOSAttachList(TEXT("os.Attach.List"),
		TEXT("os.Attach.List: the guns, their places, what is on them, the pouch."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UOSWeaponsComponent* Guns = PlayerGuns(World))
			{
				TArray<FString> Lines;
				Guns->DescribeGear().ParseIntoArrayLines(Lines);
				for (const FString& Line : Lines)
				{
					UE_LOG(LogMindKore, Display, TEXT("Gear: %s"), *Line);
				}
			}
		}));
}
#endif

#undef LOCTEXT_NAMESPACE
