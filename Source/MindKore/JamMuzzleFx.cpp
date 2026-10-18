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

	// Pistol: a small tight pop, a couple of grains, a wisp.
	FJamMuzzleFx Pistol;
	Pistol.FlashLobes = 6;
	Pistol.FlashSizeCm = 8.0f;
	Pistol.FlashReachCm = 4.0f;
	Pistol.FlashForward = 1.5f;
	Pistol.FlashPeakSeconds = 0.012f;
	Pistol.FlashGlowSeconds = 0.1f;
	Pistol.Sparks = 3;
	Pistol.SparkSpeed = 500.0f;
	Pistol.LightIntensity = 30.0f;
	Pistol.SmokePuffs = 4;
	Pistol.SmokeSeconds = 3.0f;
	Pistol.SmokeStartCm = 5.0f;
	Pistol.SmokeEndCm = 18.0f;
	Pistol.SmokeSpreadCm = 8.0f;
	Pistol.SmokeBackCm = 6.0f;
	Pistol.SmokeOpacity = 0.22f;
	// A thin puff.
	Pistol.SmokeDriftCm = 2.0f;
	Pistol.SmokeStretch = 1.8f;
	Pistol.SmokeTear = 0.15f;
	// Shotgun: a big wide fireball (a big charge, an open bore), many grains, the most smoke.
	FJamMuzzleFx Shotgun;
	Shotgun.FlashLobes = 14;
	Shotgun.FlashSizeCm = 16.0f;
	Shotgun.FlashReachCm = 10.0f;
	Shotgun.FlashForward = 0.6f;
	Shotgun.FlashPeakSeconds = 0.03f;
	Shotgun.FlashGlowSeconds = 0.18f;
	Shotgun.Sparks = 8;
	Shotgun.SparkSpeed = 600.0f;
	Shotgun.LightIntensity = 60.0f;
	Shotgun.SmokePuffs = 10;
	Shotgun.SmokeSeconds = 4.5f;
	Shotgun.SmokeStartCm = 10.0f;
	Shotgun.SmokeEndCm = 34.0f;
	Shotgun.SmokeSpreadCm = 22.0f;
	Shotgun.SmokeBackCm = 18.0f;
	Shotgun.SmokeOpacity = 0.4f;
	// A torn cloud.
	Shotgun.SmokeDriftCm = 3.0f;
	Shotgun.SmokeStretch = 1.3f;
	Shotgun.SmokeTear = 0.6f;
	// SV-98: a tight core forward and the muzzle brake's two side jets.
	FJamMuzzleFx Sniper;
	Sniper.FlashLobes = 5;
	Sniper.FlashSizeCm = 9.0f;
	Sniper.FlashReachCm = 5.0f;
	Sniper.FlashForward = 2.0f;
	Sniper.FlashPeakSeconds = 0.02f;
	Sniper.FlashGlowSeconds = 0.15f;
	Sniper.SideJetCm = 30.0f;
	Sniper.Sparks = 4;
	Sniper.SparkSpeed = 800.0f;
	Sniper.LightIntensity = 70.0f;
	Sniper.SmokePuffs = 10;
	Sniper.SmokeSeconds = 4.5f;
	Sniper.SmokeStartCm = 6.0f;
	Sniper.SmokeEndCm = 18.0f;
	Sniper.SmokeSpreadCm = 28.0f;
	Sniper.SmokeBackCm = 6.0f;
	Sniper.SmokeOpacity = 0.35f;
	// A long ring/cone out of the brake.
	Sniper.SmokeDriftCm = 3.0f;
	Sniper.SmokeRing = 0.8f;
	Sniper.SmokeStretch = 2.0f;
	Sniper.SmokeTear = 0.3f;
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
	Flash = Make(TEXT("MuzzleFlash"), FlashMaterial.LoadSynchronous(), 4);
	Smoke = Make(TEXT("MuzzleSmoke"), SmokeMaterial.LoadSynchronous(), 7);
	Light = NewObject<UPointLightComponent>(GetOwner(), TEXT("MuzzleLight"));
	Light->SetMobility(EComponentMobility::Movable);
	// No inverse square: the smoke is born right on the light, and 1/d^2 at a few
	// cm lit it a thousandfold (a white ball). A smooth falloff over the radius
	// lights the gun, the smoke and the near walls about alike.
	Light->bUseInverseSquaredFalloff = false;
	Light->LightFalloffExponent = 2.0f;
	Light->SetIntensityUnits(ELightUnits::Unitless);
	Light->SetAttenuationRadius(250.0f);
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
	// A new burst every shot: lobes of random size, turn and direction (leaning
	// along the bore by FlashForward), the first one the small white-hot core on
	// the muzzle; the rest are yellow-orange gas from the first frame, dimmer, so
	// the burst reads as hot gas and not a white disc.
	FlashBits.Reset();
	const auto Lobe = [this](const FVector& Direction, float ReachCm, float SizeCm, float Stretch, float Brightness, float Heat)
	{
		FFlashBit& Bit = FlashBits.AddDefaulted_GetRef();
		Bit.Direction = Direction;
		Bit.ReachCm = ReachCm;
		Bit.SizeCm = SizeCm;
		Bit.Stretch = Stretch;
		Bit.Brightness = Brightness;
		Bit.Heat = Heat;
		Bit.Seed = FMath::FRand();
		Bit.Angle = FMath::FRand() * UE_TWO_PI;
	};
	for (int32 Index = 0; Index < Gun.FlashLobes; ++Index)
	{
		const FVector Direction = (FMath::VRand() + FVector(Gun.FlashForward, 0.0f, 0.0f)).GetSafeNormal();
		Index == 0
			? Lobe(Direction, Gun.FlashReachCm * 0.15f, Gun.FlashSizeCm * 0.5f, 1.0f, 1.5f, 1.0f)
			: Lobe(Direction, Gun.FlashReachCm * FMath::FRandRange(0.3f, 1.0f), Gun.FlashSizeCm * FMath::FRandRange(0.6f, 1.2f),
				FMath::FRandRange(1.0f, 1.6f), FMath::FRandRange(0.18f, 0.3f), 0.35f);
	}
	if (Gun.SideJetCm > 0.0f)
	{
		// The brake's ports throw the gas sideways: three lobes a side, stretched outwards.
		for (const float Side : { 1.0f, -1.0f })
		{
			for (int32 Index = 0; Index < 3; ++Index)
			{
				const FVector Out = (FVector(0.0f, Side, 0.0f) + FMath::VRand() * 0.25f).GetSafeNormal();
				const float Reach = Gun.SideJetCm * (0.25f + 0.35f * Index) * FMath::FRandRange(0.8f, 1.1f);
				Lobe(Out, Reach, Gun.SideJetCm * FMath::FRandRange(0.35f, 0.5f), FMath::FRandRange(1.6f, 2.4f), 0.55f - 0.1f * Index, 0.5f);
			}
		}
	}
	for (int32 Index = 0; Index < Gun.Sparks; ++Index)
	{
		FFlashBit& Spark = FlashBits.AddDefaulted_GetRef();
		Spark.Direction = (FMath::VRand() + FVector(Gun.FlashForward + 0.5f, 0.0f, 0.0f)).GetSafeNormal();
		Spark.Speed = Gun.SparkSpeed * FMath::FRandRange(0.5f, 1.2f);
		Spark.SizeCm = FMath::FRandRange(0.6f, 1.0f);
		Spark.Brightness = FMath::FRandRange(0.5f, 1.0f);
		Spark.Life = FMath::FRandRange(0.08f, 0.22f);
		Spark.Seed = FMath::FRand();
	}
	for (int32 Index = 0; Index < Gun.SmokePuffs; ++Index)
	{
		FPuff& Puff = Puffs.AddDefaulted_GetRef();
		Puff.Origin = FlashOrigin;
		// A ring around the bore leaning forward (a cone, out of a brake's ports), or a sphere of directions.
		const int32 Ring = FMath::RoundToInt(Gun.SmokePuffs * Gun.SmokeRing);
		if (Index < Ring)
		{
			const float Around = UE_TWO_PI * (Index + FMath::FRandRange(-0.25f, 0.25f)) / Ring;
			Puff.Direction = FVector(0.35f, FMath::Cos(Around), FMath::Sin(Around)).GetSafeNormal();
		}
		else
		{
			Puff.Direction = FMath::VRand();
		}
		Puff.DriftCm = Gun.SmokeDriftCm * FMath::FRandRange(0.5f, 1.0f);
		Puff.Stretch = FMath::FRandRange(1.0f, Gun.SmokeStretch);
		Puff.Tear = Gun.SmokeTear;
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
	// Seen end-on, a stretched sprite has no length to show: any in-plane axis will do.
	const FVector Along = FMath::Abs(Location.GetSafeNormal() | XAxis) > 0.98f ? FVector::UpVector : XAxis;
	return FTransform(FRotationMatrix::MakeFromZX(-Location, Along).ToQuat(), Location, FVector(LengthCm / 100.0f, WidthCm / 100.0f, 1.0f));
}

void UJamMuzzleFxComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Smoke)
	{
		return;
	}

	// The flash: white-hot for its peak, a fast fall, then a dim cooling glow.
	if (Flash->GetInstanceCount() > 0)
	{
		Flash->ClearInstances();
		Light->SetVisibility(false);
	}
	if (Guns.IsValidIndex(FlashGun))
	{
		const FJamMuzzleFx& Gun = Guns[FlashGun];
		const float Age = FlashAge;
		FlashAge += DeltaTime;
		if (Age >= Gun.FlashGlowSeconds)
		{
			FlashGun = INDEX_NONE;
		}
		else
		{
			const float Burst = Age < Gun.FlashPeakSeconds ? 1.0f : FMath::Exp(-(Age - Gun.FlashPeakSeconds) / 0.008f);
			const float Glow = 0.015f * (1.0f - Age / Gun.FlashGlowSeconds);
			const float Heat = FMath::Exp(-Age / 0.04f);
			// Lobes are thrown out and swell within the first frame.
			const float Out = 1.0f - FMath::Exp(-Age / 0.006f);
			// The gun jumps back and up in the frame after the shot: the flash stays on its muzzle.
			const USceneComponent* GunMesh = FlashGunMesh.Get();
			const FVector Muzzle = GunMesh ? Smoke->GetComponentTransform().InverseTransformPosition(GunMesh->GetComponentTransform().TransformPosition(FlashOrigin)) : FlashOrigin;
			for (const FFlashBit& Bit : FlashBits)
			{
				float Intensity, Length, Width;
				FVector Location;
				if (Bit.Speed > 0.0f)
				{
					// A grain in a straight line, drawn as its streak over about one frame.
					if (Age >= Bit.Life)
					{
						continue;
					}
					Location = Muzzle + Bit.Direction * Bit.Speed * Age;
					Width = Bit.SizeCm;
					Length = FMath::Max(Width, Bit.Speed * 0.012f);
					Intensity = Bit.Brightness * 0.6f * (1.0f - FMath::Square(Age / Bit.Life));
				}
				else
				{
					Location = Muzzle + Bit.Direction * Bit.ReachCm * Out;
					Width = Bit.SizeCm * (0.35f + 0.65f * Out);
					Length = Width * Bit.Stretch;
					Intensity = Bit.Brightness * (Burst + Glow);
				}
				const int32 Instance = Flash->AddInstance(Sprite(Location, Bit.Direction, Length, Width));
				Flash->SetCustomData(Instance, { Intensity, Bit.Seed, Bit.Speed > 0.0f ? 0.7f : Heat * Bit.Heat, Bit.Angle }, true);
			}
			Light->SetRelativeLocation(Muzzle);
			Light->SetIntensity(Gun.LightIntensity * (Burst + Glow));
			Light->SetVisibility(true);
		}
	}

	// The smoke, in vacuum: thrown out and swollen within a few tenths of a second,
	// then frozen in shape and drifting straight on (no air to slow it, nothing to lift it).
	if (Puffs.IsEmpty() && Smoke->GetInstanceCount() == 0)
	{
		return;
	}
	Smoke->ClearInstances();
	// Born on the muzzle and carried with the gun (its kick, its sway): the cloud clings to it.
	const USceneComponent* GunMesh = FlashGunMesh.Get();
	const FTransform GunInCamera = GunMesh ? GunMesh->GetComponentTransform().GetRelativeTransform(Smoke->GetComponentTransform()) : FTransform::Identity;
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
		const float Throw = 1.0f - FMath::Exp(-Puff.Age / 0.12f);
		const float Grow = 1.0f - FMath::Exp(-Puff.Age / 0.15f);
		const FVector Location = GunInCamera.TransformPosition(Puff.Origin) + Puff.Direction * (Puff.SpreadCm * Throw + Puff.DriftCm * Puff.Age)
			- FVector(Puff.BackCm * Throw, 0.0f, 0.0f);
		const float Width = FMath::Lerp(Puff.StartCm, Puff.EndCm, Grow);
		// A wisp drawn out along the shot (the bore, camera forward).
		const int32 Instance = Smoke->AddInstance(Sprite(Location, FVector::ForwardVector, Width * Puff.Stretch, Width));
		const float Opacity = Puff.Opacity * FMath::Min(Puff.Age / 0.05f, 1.0f) * FMath::Pow(1.0f - X, 1.5f);
		// Heat shimmer behind a fresh puff, gone by 100 ms.
		const float Shimmer = FMath::Max(0.0f, 1.0f - Puff.Age / 0.1f);
		// Its size too: the material's texel grid stays the same in cm on every puff.
		Smoke->SetCustomData(Instance, { Opacity, Puff.Seed, Puff.Seed * 6.2831853f + Puff.Spin * Puff.Age, Puff.Tear, Shimmer,
			Width * Puff.Stretch, Width }, true);
	}
}

namespace
{
	/**
	 * jam.Weapons.Fx [space]: first-person stills of each gun's flash and smoke
	 * (HighResShot; needs a real RHI). The zero-G pawn is held 6 m in front of a
	 * living wall as in jam.Weapons.Shots, looking at it - or, with "space", 45
	 * degrees to its right into the black (the smoke against empty space). Per gun: a warm-up shot (shaders), the gun at rest, then
	 * a shot at time dilation 0.01 caught at 60 fps frames 0, 1, 2, 3, 5, 8, 12
	 * (+0.008..+0.208 s; a hitch on the shot frame cannot skip them), the smoke at
	 * +0.5, 1, 2 and 3 s, then the first frame of two more shots (each burst differs).
	 */
	void RunMuzzleFxShots(const TArray<FString>& Args, UWorld* World)
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
			if (Args.Contains(TEXT("space")))
			{
				// Turned 45 degrees to the right of the wall: the black of space fills the middle of the frame.
				Target = Stand - Wall->GetFrontNormal().RotateAngleAxis(45.0f, FVector::UpVector) * 600.0f;
			}
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
		// World seconds: the dilated stretches cost 100x in real time.
		const auto Slow = [Pawn, World, Hold, After](float At)
		{
			After(At, [Pawn, World, Hold]()
			{
				Hold();
				UGameplayStatics::SetGlobalTimeDilation(World, 0.01f);
				Pawn->Fire();
			});
		};
		const auto Normal = [World, After](float At) { After(At, [World]() { UGameplayStatics::SetGlobalTimeDilation(World, 1.0f); }); };
		Hold();
		const TCHAR* Names[3] = { TEXT("pistol"), TEXT("shotgun"), TEXT("SV-98") };
		for (int32 Gun = 0; Gun < 3; ++Gun)
		{
			// Long first wait: shaders may still compile.
			const float At = 8.0f + Gun * 22.0f;
			const FString Name = Names[Gun];
			After(At, [Pawn, Hold, Gun]() { Pawn->Guns->UnlockWeapon(Gun); Hold(); });
			After(At + 1.0f, [Pawn, Hold]() { Hold(); Pawn->Fire(); });
			After(At + 7.0f, [Shot, Name]() { Shot(Name + TEXT(" at rest")); });
			Slow(At + 7.5f);
			for (const int32 Frame : { 0, 1, 2, 3, 5, 8, 12 })
			{
				After(At + 7.5f + 0.008f + Frame / 60.0f, [Shot, Name, Frame]() { Shot(FString::Printf(TEXT("%s frame %d"), *Name, Frame)); });
			}
			Normal(At + 7.71f);
			for (const float Smoke : { 0.5f, 1.0f, 2.0f, 3.0f })
			{
				After(At + 7.5f + Smoke, [Shot, Name, Smoke]() { Shot(FString::Printf(TEXT("%s smoke +%.1f s"), *Name, Smoke)); });
			}
			// Two more bursts, after the smoke of the first is gone.
			for (int32 Again = 0; Again < 2; ++Again)
			{
				const float When = At + 16.0f + Again * 2.5f;
				Slow(When);
				After(When + 0.008f, [Shot, Name, Again]() { Shot(FString::Printf(TEXT("%s shot %d frame 0"), *Name, Again + 2)); });
				Normal(When + 0.01f);
			}
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GMuzzleFxShots(
		TEXT("jam.Weapons.Fx"),
		TEXT("First-person stills of each gun's muzzle flash frame by frame (60 fps frames 0-12 at time dilation 0.01), its smoke over 3 s and two more bursts (HighResShot; needs a real RHI). 'space': look away from the wall."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunMuzzleFxShots));
}
