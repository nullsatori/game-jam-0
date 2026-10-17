#include "JamWeapons.h"

#include "EnhancedInputComponent.h"
#include "Engine/World.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "MKImpactDelivery.h"
#include "MindKore.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

UJamWeaponsComponent::UJamWeaponsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Guns as movement. Pistol: quick nudges (15 cm/s in zero-G); shotgun: a shove
	// and a spray that scatters junk; sniper: one slow shot that throws you back 4 m/s.
	FJamWeapon Pistol;
	Pistol.Name = NSLOCTEXT("JamZeroG", "Pistol", "Пистолет");
	Pistol.Damage = 25.0f;
	Pistol.FireInterval = 0.15f;
	Pistol.MagazineSize = 12;
	Pistol.Reserve = 60;
	Pistol.ReloadSeconds = 1.2f;
	Pistol.SpreadDeg = 0.5f;
	Pistol.RecoilImpulse = 4000.0f;
	Pistol.HitImpulse = 3000.0f;
	Pistol.SoundPitch = 1.3f;
	Pistol.bAutomatic = true;
	FJamWeapon Shotgun;
	Shotgun.Name = NSLOCTEXT("JamZeroG", "Shotgun", "Дробовик");
	Shotgun.Damage = 12.0f;
	Shotgun.FireInterval = 0.8f;
	Shotgun.MagazineSize = 6;
	Shotgun.Reserve = 24;
	Shotgun.ReloadSeconds = 2.0f;
	Shotgun.Pellets = 8;
	Shotgun.SpreadDeg = 6.0f;
	Shotgun.RecoilImpulse = 25000.0f;
	Shotgun.HitImpulse = 4000.0f;
	Shotgun.RangeCm = 3000.0f;
	Shotgun.SoundPitch = 0.9f;
	FJamWeapon Sniper;
	Sniper.Name = NSLOCTEXT("JamZeroG", "Sniper", "Снапа");
	Sniper.Damage = 150.0f;
	Sniper.FireInterval = 1.2f;
	Sniper.MagazineSize = 4;
	Sniper.Reserve = 16;
	Sniper.ReloadSeconds = 2.5f;
	Sniper.RecoilImpulse = 70000.0f;
	Sniper.HitImpulse = 60000.0f;
	Sniper.RangeCm = 15000.0f;
	Sniper.SoundPitch = 0.7f;
	Weapons = { Pistol, Shotgun, Sniper };

	// MindKore's own content (CREDITS.md lists the sound donors).
	static ConstructorHelpers::FObjectFinder<UMKImpactDataAsset> DefaultImpactData(
		TEXT("/Game/MindKore/Impacts/DA_Impact_Default.DA_Impact_Default"));
	ImpactData = DefaultImpactData.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> BlastTransient(
		TEXT("/Game/MindKore/Audio/SW_MK_Blast_Transient.SW_MK_Blast_Transient"));
	static ConstructorHelpers::FObjectFinder<USoundBase> BlastTail(
		TEXT("/Game/MindKore/Audio/SW_MK_Blast_Tail.SW_MK_Blast_Tail"));
	for (USoundBase* Sound : { BlastTransient.Object, BlastTail.Object })
	{
		if (Sound)
		{
			ShotSounds.Add(Sound);
		}
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Decal(
		TEXT("/Game/MindKore/Impacts/Decals/M_Decal_Default.M_Decal_Default"));
	DefaultImpactDecal = Decal.Object;
}

void UJamWeaponsComponent::BeginPlay()
{
	Super::BeginPlay();
	for (FJamWeapon& Weapon : Weapons)
	{
		Weapon.Ammo = Weapon.MagazineSize;
		Weapon.bUnlocked = bStartWithAllWeapons;
	}
}

void UJamWeaponsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (ReloadEndSeconds > 0.0 && GetWorld()->GetTimeSeconds() >= ReloadEndSeconds && Weapons.IsValidIndex(CurrentWeapon))
	{
		FJamWeapon& Weapon = Weapons[CurrentWeapon];
		const int32 Moved = FMath::Min(Weapon.MagazineSize - Weapon.Ammo, Weapon.Reserve);
		Weapon.Ammo += Moved;
		Weapon.Reserve -= Moved;
		ReloadEndSeconds = 0.0;
		UE_LOG(LogMindKore, Log, TEXT("Weapon %s: %s reloaded, %d/%d"), *GetNameSafe(GetOwner()), *Weapon.Name.ToString(), Weapon.Ammo, Weapon.Reserve);
	}
}

void UJamWeaponsComponent::BindInput(UEnhancedInputComponent& Input, UInputMappingContext& Mapping)
{
	const auto MakeAction = [this, &Mapping](const TCHAR* Name, EInputActionValueType Type, const FKey& Key)
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = Type;
		Mapping.MapKey(Action, Key);
		InputActions.Add(Action);
		return Action;
	};
	Input.BindAction(MakeAction(TEXT("IA_Jam_Reload"), EInputActionValueType::Boolean, EKeys::R), ETriggerEvent::Started, this, &UJamWeaponsComponent::Reload);
	Input.BindActionValueLambda(MakeAction(TEXT("IA_Jam_Wheel"), EInputActionValueType::Axis1D, EKeys::MouseWheelAxis), ETriggerEvent::Started,
		[this](const FInputActionValue& Value) { SelectStep(Value.Get<float>() > 0.0f ? 1 : -1); });
	const FKey Keys[3] = { EKeys::One, EKeys::Two, EKeys::Three };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UInputAction* Select = MakeAction(*FString::Printf(TEXT("IA_Jam_Weapon%d"), Index + 1), EInputActionValueType::Boolean, Keys[Index]);
		Input.BindActionValueLambda(Select, ETriggerEvent::Started, [this, Index](const FInputActionValue&) { SelectWeapon(Index); });
	}
}

void UJamWeaponsComponent::SelectStep(int32 Step)
{
	// The next unlocked gun in the wheel's direction.
	const int32 Count = Weapons.Num();
	for (int32 Tries = 1; Tries <= Count; ++Tries)
	{
		const int32 Index = ((CurrentWeapon + Step * Tries) % Count + Count) % Count;
		if (Weapons[Index].bUnlocked)
		{
			SelectWeapon(Index);
			return;
		}
	}
}

void UJamWeaponsComponent::UnlockWeapon(int32 Index)
{
	if (!Weapons.IsValidIndex(Index))
	{
		return;
	}
	Weapons[Index].bUnlocked = true;
	CurrentWeapon = INDEX_NONE; // so SelectWeapon takes it even when it was the current slot
	SelectWeapon(Index);
	UE_LOG(LogMindKore, Log, TEXT("Weapon %s: %s unlocked"), *GetNameSafe(GetOwner()), *Weapons[Index].Name.ToString());
}

void UJamWeaponsComponent::SelectWeapon(int32 Index)
{
	if (!Weapons.IsValidIndex(Index) || Index == CurrentWeapon || !Weapons[Index].bUnlocked)
	{
		return;
	}
	CurrentWeapon = Index;
	ReloadEndSeconds = 0.0; // A switch abandons a reload.
	UE_LOG(LogMindKore, Log, TEXT("Weapon %s: %s (%d/%d)"), *GetNameSafe(GetOwner()), *Weapons[Index].Name.ToString(),
		Weapons[Index].Ammo, Weapons[Index].Reserve);
}

float UJamWeaponsComponent::GetReloadRemaining() const
{
	return FMath::Max(0.0f, float(ReloadEndSeconds - GetWorld()->GetTimeSeconds()));
}

FString UJamWeaponsComponent::Describe() const
{
	const FJamWeapon* Weapon = GetWeapon();
	if (!Weapon)
	{
		return TEXT("без оружия");
	}
	FString Line = FString::Printf(TEXT("[%d] %s %d/%d"), CurrentWeapon + 1, *Weapon->Name.ToString(), Weapon->Ammo, Weapon->Reserve);
	if (const float Reload = GetReloadRemaining(); Reload > 0.0f)
	{
		Line += FString::Printf(TEXT(" (перезарядка %.1f)"), Reload);
	}
	return Line;
}

void UJamWeaponsComponent::Reload()
{
	FJamWeapon* Weapon = IsWeaponUnlocked(CurrentWeapon) ? &Weapons[CurrentWeapon] : nullptr;
	if (!Weapon || Weapon->Ammo >= Weapon->MagazineSize || Weapon->Reserve <= 0 || GetReloadRemaining() > 0.0f)
	{
		return;
	}
	ReloadEndSeconds = GetWorld()->GetTimeSeconds() + Weapon->ReloadSeconds;
	UE_LOG(LogMindKore, Log, TEXT("Weapon %s: reloading %s, %.1f s"), *GetNameSafe(GetOwner()), *Weapon->Name.ToString(), Weapon->ReloadSeconds);
}

bool UJamWeaponsComponent::Fire(const FVector& Start, const FVector& Direction, FVector& OutRecoil)
{
	OutRecoil = FVector::ZeroVector;
	FJamWeapon* Weapon = IsWeaponUnlocked(CurrentWeapon) ? &Weapons[CurrentWeapon] : nullptr;
	const double Now = GetWorld()->GetTimeSeconds();
	if (!Weapon || Now < NextShotSeconds || GetReloadRemaining() > 0.0f)
	{
		return false;
	}
	if (Weapon->Ammo <= 0)
	{
		UE_LOG(LogMindKore, Log, TEXT("Weapon %s: %s is empty"), *GetNameSafe(GetOwner()), *Weapon->Name.ToString());
		Reload();
		return false;
	}
	--Weapon->Ammo;
	NextShotSeconds = Now + Weapon->FireInterval;
	// One kick per shot, whatever the pellets do. Guns cost no oxygen.
	OutRecoil = -Direction * Weapon->RecoilImpulse;
	for (USoundBase* Sound : ShotSounds)
	{
		UGameplayStatics::PlaySound2D(this, Sound, 1.0f, Weapon->SoundPitch);
	}
	for (int32 Pellet = 0; Pellet < Weapon->Pellets; ++Pellet)
	{
		FireRay(*Weapon, Start, Weapon->SpreadDeg > 0.0f
			? FMath::VRandCone(Direction, FMath::DegreesToRadians(Weapon->SpreadDeg))
			: Direction);
	}
	UE_LOG(LogMindKore, Log, TEXT("Weapon %s: %s fired, %d pellet(s), recoil %.0f kg cm/s, %d/%d left"), *GetNameSafe(GetOwner()),
		*Weapon->Name.ToString(), Weapon->Pellets, Weapon->RecoilImpulse, Weapon->Ammo, Weapon->Reserve);
	return true;
}

void UJamWeaponsComponent::FireRay(const FJamWeapon& Weapon, const FVector& Start, const FVector& Direction)
{
	UWorld* World = GetWorld();
	AActor* Shooter = GetOwner();
	// As UMKFirearmComponent traces: the Shot channel (ECC_GameTraceChannel2),
	// complex collision (the glass pane's shard triangles), physical material;
	// multi-trace and MKSelectResolvedHit, so hit-zone boxes (overlaps in front
	// of the blocking hit) win on zone priority within the same actor.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JamShot), /*bTraceComplex=*/true, Shooter);
	Params.bReturnPhysicalMaterial = true;
	TArray<FHitResult> Hits;
	World->LineTraceMultiByChannel(Hits, Start, Start + Direction * Weapon.RangeCm, ECC_GameTraceChannel2, Params);
	TArray<FMKHitCandidate, TInlineAllocator<8>> Candidates;
	int32 NearestZone = INDEX_NONE;
	for (const FHitResult& Each : Hits)
	{
		FMKHitCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.Distance = Each.Distance;
		Candidate.bBlocking = Each.bBlockingHit;
		Candidate.ActorKey = Each.GetActor();
		Candidate.HitZone = MKResolveHitZoneFromComponent(Each.GetComponent());
		if (!Candidate.bBlocking && Candidate.HitZone != EMKHitZone::Default
			&& (NearestZone == INDEX_NONE || Candidate.Distance < Candidates[NearestZone].Distance))
		{
			NearestZone = Candidates.Num() - 1;
		}
	}
	// Not in MindKore: a body made only of hit-zone boxes (AMKImpactDummy) has
	// nothing that blocks Shot, so its nearest zone stands in for the blocking hit.
	if (NearestZone != INDEX_NONE)
	{
		Candidates[NearestZone].bBlocking = true;
	}
	const int32 Selected = MKSelectResolvedHit(Candidates, /*SameActorToleranceCm=*/2.5f);
	if (Selected == INDEX_NONE)
	{
		return;
	}
	const FHitResult& Hit = Hits[Selected];
	FMKImpactResolveParams ResolveParams;
	ResolveParams.ImpactData = ImpactData;
	FMKImpactFeedback Feedback;
	FMKResolvedImpact Resolved = MKResolveImpactFromHit(Hit, Direction, ResolveParams, Feedback);
	Resolved.ShotId = FGuid::NewGuid();
	Resolved.Kind = EMKImpactKind::Round;
	Resolved.SourceActor = Shooter;
	Resolved.ImpactTimeSeconds = World->GetTimeSeconds();
	Resolved.RangeCm = FVector::Dist(Shooter->GetActorLocation(), Hit.ImpactPoint);
	Resolved.DamageAmount = Weapon.Damage * Resolved.DamageMultiplier;
	// N s for receivers that read it (another pawn); kg cm/s / 100.
	Resolved.ImpulseNewtonSeconds = Resolved.ShotDirection * Weapon.HitImpulse / 100.0f;
	Resolved.bHasPhysicsImpulse = true;
	const bool bDelivered = MKDeliverResolvedImpact(Shooter, Shooter, Resolved);
	// Every loose body takes the gun's push at the hit point (MindKore's props
	// ignore rounds; here the push is the point).
	UPrimitiveComponent* HitComponent = Hit.GetComponent();
	if (HitComponent && HitComponent->IsSimulatingPhysics() && HitComponent->GetOwner() != Shooter)
	{
		HitComponent->AddImpulseAtLocation(Direction * Weapon.HitImpulse, Hit.ImpactPoint);
	}
	UE_LOG(LogMindKore, Verbose, TEXT("Jam shot: %s / %s, zone %d, damage %.1f, %s"), *GetNameSafe(Hit.GetActor()),
		*GetNameSafe(HitComponent), int32(Resolved.HitZone), Resolved.DamageAmount, bDelivered ? TEXT("delivered") : TEXT("no receiver"));
	if (Feedback.ImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Feedback.ImpactSound, Hit.ImpactPoint);
	}
	if (UMaterialInterface* DecalMaterial = Feedback.DecalMaterial ? Feedback.DecalMaterial.Get() : DefaultImpactDecal.Get())
	{
		UGameplayStatics::SpawnDecalAttached(DecalMaterial, FVector(4.0f, 6.0f, 6.0f), HitComponent, NAME_None,
			Hit.ImpactPoint, (-Hit.ImpactNormal).Rotation(), EAttachLocation::KeepWorldPosition, 30.0f);
	}
}
