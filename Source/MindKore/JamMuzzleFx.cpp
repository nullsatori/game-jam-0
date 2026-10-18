#include "JamMuzzleFx.h"

#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "JamLivingWall.h"
#include "JamWeapons.h"
#include "JamZeroGPawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "MindKore.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

UJamMuzzleFxComponent::UJamMuzzleFxComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	SpriteMesh = Plane.Object;

	// Pistol: a small quick pop and a wisp.
	FJamMuzzleFx Pistol;
	Pistol.FlashSeconds = 0.05f;
	Pistol.FlashStartCm = 10.0f;
	Pistol.FlashEndCm = 28.0f;
	Pistol.LightCandela = 15.0f;
	Pistol.SmokePuffs = 4;
	Pistol.SmokeSeconds = 3.0f;
	Pistol.SmokeStartCm = 6.0f;
	Pistol.SmokeEndCm = 30.0f;
	Pistol.SmokeSpreadCm = 12.0f;
	Pistol.SmokeBackCm = 10.0f;
	Pistol.SmokeOpacity = 0.2f;
	// Shotgun: a wide ball (a big charge, an open bore) and the most smoke.
	FJamMuzzleFx Shotgun;
	Shotgun.FlashSeconds = 0.07f;
	Shotgun.FlashStartCm = 18.0f;
	Shotgun.FlashEndCm = 60.0f;
	Shotgun.LightCandela = 35.0f;
	Shotgun.SmokePuffs = 9;
	Shotgun.SmokeSeconds = 4.5f;
	Shotgun.SmokeStartCm = 10.0f;
	Shotgun.SmokeEndCm = 55.0f;
	Shotgun.SmokeSpreadCm = 25.0f;
	Shotgun.SmokeBackCm = 25.0f;
	Shotgun.SmokeOpacity = 0.15f;
	// SV-98: a tighter ball and the muzzle brake's two side jets.
	FJamMuzzleFx Sniper;
	Sniper.FlashSeconds = 0.06f;
	Sniper.FlashStartCm = 14.0f;
	Sniper.FlashEndCm = 36.0f;
	Sniper.SideJetCm = 60.0f;
	Sniper.LightCandela = 40.0f;
	Sniper.SmokePuffs = 7;
	Sniper.SmokeSeconds = 4.5f;
	Sniper.SmokeStartCm = 8.0f;
	Sniper.SmokeEndCm = 45.0f;
	Sniper.SmokeSpreadCm = 18.0f;
	Sniper.SmokeBackCm = 18.0f;
	Sniper.SmokeOpacity = 0.25f;
	Guns = { Pistol, Shotgun, Sniper };
}

void UJamMuzzleFxComponent::BeginPlay()
{
	Super::BeginPlay();
	UCameraComponent* Camera = GetOwner()->FindComponentByClass<UCameraComponent>();
	if (!Camera)
	{
		return;
	}
	const auto Make = [this, Camera](const TCHAR* Name, UMaterialInterface* Material, int32 DataFloats)
	{
		UInstancedStaticMeshComponent* Sprites = NewObject<UInstancedStaticMeshComponent>(GetOwner(), Name);
		Sprites->SetMobility(EComponentMobility::Movable);
		Sprites->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Sprites->SetCastShadow(false);
		Sprites->SetOnlyOwnerSee(true);
		Sprites->SetStaticMesh(SpriteMesh);
		Sprites->SetMaterial(0, Material);
		Sprites->SetNumCustomDataFloats(DataFloats);
		Sprites->SetupAttachment(Camera);
		Sprites->RegisterComponent();
		return Sprites;
	};
	Flash = Make(TEXT("MuzzleFlash"), FlashMaterial.LoadSynchronous(), 2);
	Smoke = Make(TEXT("MuzzleSmoke"), SmokeMaterial.LoadSynchronous(), 3);
	Light = NewObject<UPointLightComponent>(GetOwner(), TEXT("MuzzleLight"));
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetAttenuationRadius(500.0f);
	Light->SetCastShadows(false);
	Light->SetLightColor(FLinearColor(1.0f, 0.7f, 0.45f));
	Light->SetVisibility(false);
	Light->SetupAttachment(Camera);
	Light->RegisterComponent();
}

void UJamMuzzleFxComponent::Shot(int32 Weapon, const FVector& Muzzle, const USceneComponent* GunMesh)
{
	if (!Smoke || !Guns.IsValidIndex(Weapon))
	{
		return;
	}
	const FJamMuzzleFx& Gun = Guns[Weapon];
	// The sprites' component sits on the camera with no offset: its space is the camera's.
	const FVector Origin = Smoke->GetComponentTransform().InverseTransformPosition(Muzzle);
	FlashGun = Weapon;
	FlashGunMesh = GunMesh;
	FlashOrigin = GunMesh ? GunMesh->GetComponentTransform().InverseTransformPosition(Muzzle) : Origin;
	FlashAge = 0.0f;
	FlashSeed = FMath::FRand();
	for (int32 Index = 0; Index < Gun.SmokePuffs; ++Index)
	{
		FPuff& Puff = Puffs.AddDefaulted_GetRef();
		Puff.Origin = Origin;
		// A sphere of directions; behind a muzzle brake half the puffs lean out of its ports.
		const float Side = Gun.SideJetCm > 0.0f && Index % 2 ? (Index % 4 == 1 ? 1.0f : -1.0f) : 0.0f;
		Puff.Direction = (FMath::VRand() + FVector(0.0f, Side, 0.0f)).GetSafeNormal();
		Puff.SpreadCm = Gun.SmokeSpreadCm * FMath::FRandRange(0.4f, 1.0f);
		Puff.BackCm = Gun.SmokeBackCm * FMath::FRandRange(0.5f, 1.0f);
		Puff.StartCm = Gun.SmokeStartCm;
		Puff.EndCm = Gun.SmokeEndCm * FMath::FRandRange(0.7f, 1.0f);
		Puff.Opacity = Gun.SmokeOpacity;
		Puff.Life = Gun.SmokeSeconds * FMath::FRandRange(0.75f, 1.25f);
		Puff.Seed = FMath::FRand();
		Puff.Spin = FMath::FRandRange(-0.4f, 0.4f);
	}
	if (Puffs.Num() > MaxPuffs)
	{
		Puffs.RemoveAt(0, Puffs.Num() - MaxPuffs);
	}
}

FTransform UJamMuzzleFxComponent::Sprite(const FVector& Location, const FVector& XAxis, float LengthCm, float WidthCm)
{
	// The engine plane faces +Z and is 100 units across; the camera is at the origin.
	return FTransform(FRotationMatrix::MakeFromZX(-Location, XAxis).ToQuat(), Location, FVector(LengthCm / 100.0f, WidthCm / 100.0f, 1.0f));
}

void UJamMuzzleFxComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Smoke)
	{
		return;
	}

	// The flash: swells fast, dies out as it grows (the gas thins in vacuum).
	if (Flash->GetInstanceCount() > 0)
	{
		Flash->ClearInstances();
		Light->SetVisibility(false);
	}
	if (Guns.IsValidIndex(FlashGun))
	{
		const FJamMuzzleFx& Gun = Guns[FlashGun];
		const float X = FlashAge / Gun.FlashSeconds;
		FlashAge += DeltaTime;
		if (X >= 1.0f)
		{
			FlashGun = INDEX_NONE;
		}
		else
		{
			const float Grow = 1.0f - FMath::Square(1.0f - X);
			const float Fade = FMath::Square(1.0f - X);
			const float Size = FMath::Lerp(Gun.FlashStartCm, Gun.FlashEndCm, Grow);
			// The ball rides a little ahead of the muzzle: the gas leaves forward.
			// The gun jumps back and up in the frame after the shot: the flash stays on its muzzle.
			const USceneComponent* GunMesh = FlashGunMesh.Get();
			const FVector Muzzle = GunMesh ? Smoke->GetComponentTransform().InverseTransformPosition(GunMesh->GetComponentTransform().TransformPosition(FlashOrigin)) : FlashOrigin;
			const FVector Centre = Muzzle + FVector(Size * 0.2f, 0.0f, 0.0f);
			Flash->AddInstance(Sprite(Centre, FVector::UpVector, Size, Size));
			Flash->SetCustomDataValue(0, 0, Fade);
			Flash->SetCustomDataValue(0, 1, FlashSeed, true);
			if (Gun.SideJetCm > 0.0f)
			{
				const float Length = Gun.SideJetCm * Grow;
				for (const float Side : { 1.0f, -1.0f })
				{
					// Out of the brake's ports, just behind the muzzle, along the gun's sides.
					const FVector Out(0.0f, Side, 0.0f);
					const int32 Jet = Flash->AddInstance(Sprite(Muzzle + Out * Length * 0.5f - FVector(1.5f, 0.0f, 0.0f), Out, Length, Length * 0.35f));
					Flash->SetCustomDataValue(Jet, 0, Fade * 0.8f);
					Flash->SetCustomDataValue(Jet, 1, FlashSeed + Side * 0.3f, true);
				}
			}
			Light->SetRelativeLocation(Centre);
			Light->SetIntensity(Gun.LightCandela * Fade);
			Light->SetVisibility(true);
		}
	}

	// The smoke: puffs swell and spread fast at first, then creep; the cloud rolls back over the gun.
	if (Puffs.IsEmpty() && Smoke->GetInstanceCount() == 0)
	{
		return;
	}
	Smoke->ClearInstances();
	for (int32 Index = Puffs.Num() - 1; Index >= 0; --Index)
	{
		Puffs[Index].Age += DeltaTime;
		if (Puffs[Index].Age >= Puffs[Index].Life)
		{
			Puffs.RemoveAt(Index);
		}
	}
	for (const FPuff& Puff : Puffs)
	{
		const float X = Puff.Age / Puff.Life;
		const float Spread = 1.0f - FMath::Cube(1.0f - X);
		const float Grow = 1.0f - FMath::Square(1.0f - X);
		const FVector Location = Puff.Origin + Puff.Direction * Puff.SpreadCm * Spread - FVector(Puff.BackCm * Spread, 0.0f, 0.0f);
		const float Size = FMath::Lerp(Puff.StartCm, Puff.EndCm, Grow);
		const int32 Instance = Smoke->AddInstance(Sprite(Location, FVector::UpVector, Size, Size));
		const float Opacity = Puff.Opacity * FMath::Min(Puff.Age / 0.1f, 1.0f) * FMath::Pow(1.0f - X, 1.5f);
		Smoke->SetCustomData(Instance, { Opacity, Puff.Seed, Puff.Seed * 6.2831853f + Puff.Spin * Puff.Age }, true);
	}
}

namespace
{
	/**
	 * jam.Weapons.Fx: first-person stills of each gun's flash and smoke (HighResShot;
	 * needs a real RHI). The zero-G pawn is held 6 m in front of a living wall as in
	 * jam.Weapons.Shots. Per gun: at rest; the flash, caught 0.012 s into the shot at
	 * time dilation 0.001 (12 s of real time: a hitch on the shot frame cannot skip it); then the smoke at +0.3, 0.7, 1.2, 2 and 3 s.
	 */
	void RunMuzzleFxShots(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AJamZeroGPawn* Pawn = Controller ? Cast<AJamZeroGPawn>(Controller->GetPawn()) : nullptr;
		if (!Pawn)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Muzzle fx shots: needs the AJamZeroGPawn"));
			return;
		}
		FVector Target = Pawn->GetActorLocation() + FVector(600.0f, 0.0f, 0.0f);
		FVector Stand = Pawn->GetActorLocation();
		if (TActorIterator<AJamLivingWall> Wall(World); Wall)
		{
			Target = Wall->GetActorLocation();
			Stand = Target + Wall->GetFrontNormal() * 600.0f;
		}
		const auto Hold = [Pawn, Controller, Stand, Target]()
		{
			Pawn->Body->SetWorldLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
			Pawn->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Controller->SetControlRotation((Target - Pawn->Camera->GetComponentLocation()).Rotation());
		};
		const auto Shot = [Pawn, Controller, Hold](const FString& Name)
		{
			Hold();
			UE_LOG(LogMindKore, Log, TEXT("Muzzle fx shots: %s - %s"), *Name, *Pawn->Guns->Describe());
			Controller->ConsoleCommand(TEXT("HighResShot 1"));
		};
		const auto After = [Pawn](float Seconds, TFunction<void()> Step)
		{
			FTimerHandle Handle;
			Pawn->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Pawn, MoveTemp(Step)), Seconds, false);
		};
		Hold();
		const TCHAR* Names[3] = { TEXT("pistol"), TEXT("shotgun"), TEXT("SV-98") };
		for (int32 Gun = 0; Gun < 3; ++Gun)
		{
			// Long first wait: shaders may still compile. World seconds (the flash runs dilated).
			const float At = 8.0f + Gun * 7.0f;
			const FString Name = Names[Gun];
			After(At, [Pawn, Hold, Gun]() { Pawn->Guns->UnlockWeapon(Gun); Hold(); });
			After(At + 1.0f, [Shot, Name]() { Shot(Name + TEXT(" at rest")); });
			After(At + 1.5f, [Pawn, World, Hold]()
			{
				Hold();
				UGameplayStatics::SetGlobalTimeDilation(World, 0.001f);
				Pawn->Fire();
			});
			After(At + 1.512f, [Shot, Name]() { Shot(Name + TEXT(" flash +0.012 s")); });
			After(At + 1.514f, [World]() { UGameplayStatics::SetGlobalTimeDilation(World, 1.0f); });
			for (const float Smoke : { 0.3f, 0.7f, 1.2f, 2.0f, 3.0f })
			{
				After(At + 1.5f + Smoke, [Shot, Name, Smoke]() { Shot(FString::Printf(TEXT("%s smoke +%.1f s"), *Name, Smoke)); });
			}
		}
	}

	FAutoConsoleCommandWithWorld GMuzzleFxShots(
		TEXT("jam.Weapons.Fx"),
		TEXT("First-person stills of each gun's muzzle flash (at time dilation 0.001) and smoke over 3 s (HighResShot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunMuzzleFxShots));
}
