#include "JamMuzzleFx.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/HeterogeneousVolumeComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "JamFlyer.h"
#include "JamGravity.h"
#include "JamLivingWall.h"
#include "JamWeapons.h"
#include "JamZeroGPawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MindKore.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Algo/MaxElement.h"

namespace
{
	TAutoConsoleVariable<int32> CVarVolumetricSmoke(
		TEXT("jam.Fx.VolumetricSmoke"), 1,
		TEXT("1: muzzle smoke as volumetric voxel clouds (heterogeneous volumes); 0: card puffs. Both live in the world."));

	/** The exhaust's throw (relative to the cloud's centre of mass) dies down to its drift with this time constant, s. */
	constexpr float GasSlowSeconds = 0.4f;

	/**
	 * Puff Index of a brake's Ports, in the bore's frame: out of alternate sides of the
	 * gun, leaning back, each pair reaching further out than the last - two jets across
	 * the bore. ReachCm comes in as the furthest reach and goes out as this puff's.
	 */
	FVector BrakePort(int32 Index, int32 Ports, float& ReachCm)
	{
		ReachCm *= float(Index / 2 + 1) / float((Ports + 1) / 2);
		return FVector(-0.35f, Index % 2 == 0 ? 1.0f : -1.0f, FMath::FRandRange(-0.15f, 0.15f)).GetSafeNormal();
	}

	/** A puff of the rest of the gas, bore frame: every way, or with a brake straight on out of the bore (what its ports let past). */
	FVector BoreGas(bool bBrake)
	{
		return bBrake ? (FVector::ForwardVector + FMath::VRand() * 0.3f).GetSafeNormal() : FMath::VRand();
	}
}

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
	// Most of the gas out of the brake's side ports: two jets across the gun.
	Sniper.SmokeDriftCm = 3.0f;
	Sniper.SmokeBrake = 0.8f;
	Sniper.SmokeStretch = 2.0f;
	Sniper.SmokeTear = 0.3f;
	// Volumetric clouds (Docs/RecoilWeapons-SmokeSim.md, 3.7): a small grid each - the pistol's thin puff, the
	// shotgun's big slug that splashes and mushrooms (and shoves the older clouds), the SV-98's brake jets.
	const auto Cloud = [](FJamMuzzleFx& Gun, int32 Cells, float CellCm, float SlugLength, float SlugRadius, float SlugSpeed, float SlugRadial,
		float Extinction, float Buoyancy, float Drag, float Vorticity, float HeatSeconds, float Decay, int32 Jacobi, float ActiveAir, float ActiveVacuum,
		float RoundRadius, float RoundPush, float RoundAlong, float Seconds)
	{
		Gun.Cells = Cells;
		Gun.CellCm = CellCm;
		Gun.SlugLengthCm = SlugLength;
		Gun.SlugRadiusCm = SlugRadius;
		Gun.SlugSpeedCm = SlugSpeed;
		Gun.SlugRadialCm = SlugRadial;
		Gun.DenseExtinction = Extinction;
		Gun.Sim.Buoyancy = Buoyancy;
		Gun.Sim.DragSeconds = Drag;
		Gun.Sim.Vorticity = Vorticity;
		Gun.Sim.HeatSeconds = HeatSeconds;
		Gun.Sim.DecayPerSecond = Decay;
		Gun.Sim.Jacobi = Jacobi;
		Gun.ActiveAirSeconds = ActiveAir;
		Gun.ActiveVacuumSeconds = ActiveVacuum;
		Gun.RoundRadiusCm = RoundRadius;
		Gun.RoundPushCm = RoundPush;
		Gun.RoundAlongCm = RoundAlong;
		Gun.CloudSeconds = Seconds;
	};
	Cloud(Pistol, 20, 3.0f, 10.0f, 7.0f, 450.0f, 40.0f, 0.12f, 220.0f, 0.5f, 0.3f, 0.35f, 0.05f, 4, 2.0f, 0.35f, 6.0f, 100.0f, 160.0f, 4.0f);
	Cloud(Shotgun, 24, 128.0f / 24.0f, 22.0f, 16.0f, 600.0f, 90.0f, 0.14f, 260.0f, 0.6f, 0.4f, 0.5f, 0.03f, 4, 3.0f, 0.35f, 10.0f, 140.0f, 220.0f, 6.0f);
	Cloud(Sniper, 22, 98.0f / 22.0f, 12.0f, 9.0f, 500.0f, 50.0f, 0.10f, 240.0f, 0.5f, 0.35f, 0.4f, 0.04f, 4, 2.5f, 0.35f, 8.0f, 120.0f, 260.0f, 5.0f);
	// The blast's swell and the jet's length (not in the design: a slug of a few cells slid as a blob and lost its
	// momentum within 0.2 s on a 4 cm grid; a ball blown up in the first tenth and a jet that keeps pushing travel).
	Pistol.EmitSeconds = 0.06f;
	Shotgun.EmitSeconds = 0.1f;
	Sniper.EmitSeconds = 0.08f;
	Shotgun.Sim.SwellPerSecond = 35.0f;
	Shotgun.ShoveCm = 90.0f;
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
	// The flash rides the gun in view as the weapons component poses it this frame (TickComponent reads
	// FlashGunMesh's transform): that component ticks first, as for the scope.
	if (UJamWeaponsComponent* Weapons = GetOwner()->FindComponentByClass<UJamWeaponsComponent>())
	{
		AddTickPrerequisiteComponent(Weapons);
	}
	View = Camera;
	// The flash's sprites ride the camera, drawn as first person with the gun (UJamScopeComponent
	// types what hangs on the camera); the smoke's sit at the world's origin, instances in the world.
	const auto Make = [this](const TCHAR* Name, UMaterialInterface* Material, int32 DataFloats, USceneComponent* Parent)
	{
		UInstancedStaticMeshComponent* Sprites = NewObject<UInstancedStaticMeshComponent>(GetOwner(), Name);
		Sprites->SetMobility(EComponentMobility::Movable);
		Sprites->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Sprites->SetCastShadow(false);
		Sprites->SetOnlyOwnerSee(true);
		Sprites->SetStaticMesh(SpriteMesh);
		Sprites->SetMaterial(0, Material);
		Sprites->SetNumCustomDataFloats(DataFloats);
		Sprites->SetupAttachment(Parent);
		Sprites->RegisterComponent();
		return Sprites;
	};
	Flash = Make(TEXT("MuzzleFlash"), FlashMaterial.LoadSynchronous(), 4, Camera);
	Smoke = Make(TEXT("MuzzleSmoke"), SmokeMaterial.LoadSynchronous(), 8, nullptr);
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

void UJamMuzzleFxComponent::Shot(int32 Weapon, const FTransform& Muzzle, const USceneComponent* GunMesh)
{
	if (!Smoke || !Guns.IsValidIndex(Weapon))
	{
		return;
	}
	const FJamMuzzleFx& Gun = Guns[Weapon];
	// The flash keeps to the muzzle in the gun's space (the camera's without a gun).
	FlashGun = Weapon;
	FlashGunMesh = GunMesh;
	FlashMuzzle = Muzzle.GetRelativeTransform(GunMesh ? GunMesh->GetComponentTransform() : View->GetComponentTransform());
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
	// The smoke is the world's from here: the gun's velocity at the shot is the owner's
	// before the kick (UJamWeaponsComponent::Fire calls this before the recoil).
	const FVector GunVelocity = GetOwner()->GetVelocity();
	const FVector Bore = Muzzle.GetRotation().GetAxisX();
	if (CVarVolumetricSmoke.GetValueOnGameThread() != 0 && CloudMaterial.LoadSynchronous())
	{
		WaitSims();
		// The round leaves from the eye along the view: through every older cloud in its way it throws the gas on
		// its line aside and drives a jet down it (the pressure solve rolls its mouth into a vortex ring); a
		// shotgun blast out of the muzzle shoves the older clouds ahead of it on.
		Blast(View->GetComponentLocation(), View->GetForwardVector(), Gun.RoundRadiusCm, Gun.RoundAlongCm, Gun.RoundPushCm, Gun.RoundClear, nullptr);
		if (Gun.ShoveCm > 0.0f)
		{
			for (FCloud& Cloud : Clouds)
			{
				const float Distance = FVector::Dist(Cloud.Centre, Muzzle.GetLocation());
				if (Cloud.Age >= 0.0f && Distance < 400.0f && ((Cloud.Centre - Muzzle.GetLocation()) | Bore) > 0.0f)
				{
					Cloud.Carried += Bore * Gun.ShoveCm * (1.0f - Distance / 400.0f);
				}
			}
		}
		SpawnCloud(Weapon, Muzzle, GunVelocity);
		return;
	}
	const int32 Ports = FMath::RoundToInt(Gun.SmokePuffs * Gun.SmokeBrake);
	for (int32 Index = 0; Index < Gun.SmokePuffs; ++Index)
	{
		FPuff& Puff = Puffs.AddDefaulted_GetRef();
		Puff.Origin = Muzzle.GetLocation();
		Puff.Bore = Muzzle.GetRotation();
		Puff.GunVelocity = GunVelocity;
		// ponytail: air or vacuum is decided at the shot, a puff crossing a zone's edge keeps it.
		Puff.AirDragSeconds = JamAirZoneAt(GetWorld(), Muzzle.GetLocation()) ? AirDragSeconds : 0.0f;
		if (Index < Ports)
		{
			// Out of the brake's side ports: a jet each side, each puff drawn out along it.
			Puff.SpreadCm = Gun.SmokeSpreadCm * FMath::FRandRange(0.85f, 1.0f);
			Puff.Direction = BrakePort(Index, Ports, Puff.SpreadCm);
			Puff.Along = Puff.Direction;
		}
		else
		{
			Puff.SpreadCm = Gun.SmokeSpreadCm * FMath::FRandRange(0.4f, 1.0f);
			Puff.Direction = BoreGas(Ports > 0);
		}
		Puff.DriftCm = Gun.SmokeDriftCm * FMath::FRandRange(0.5f, 1.0f);
		Puff.Stretch = FMath::FRandRange(1.0f, Gun.SmokeStretch);
		Puff.Tear = Gun.SmokeTear;
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

void UJamMuzzleFxComponent::PortPuffs(const FJamPortSmoke& Vent, const USceneComponent& Action, int32 Count, float ThrowCm, float AgeSpread)
{
	if (!Smoke || Count <= 0 || Vent.Ports.IsEmpty())
	{
		return;
	}
	// Air and gravity once for the lot (a frame's seep or a shot's puff).
	const FTransform Shroud = Action.GetComponentTransform();
	const FVector GunVelocity = GetOwner()->GetVelocity();
	const bool bAir = JamAirZoneAt(GetWorld(), Shroud.GetLocation()) != nullptr;
	const FVector Lift = bAir ? -JamGravityAt(GetWorld(), Shroud.GetLocation()).Acceleration * Vent.RiseShare : FVector::ZeroVector;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector& Port = Vent.Ports[FMath::RandHelper(Vent.Ports.Num())];
		FPuff& Puff = Puffs.AddDefaulted_GetRef();
		Puff.Origin = Shroud.TransformPosition(Port);
		Puff.Bore = Shroud.GetRotation();
		Puff.GunVelocity = GunVelocity;
		Puff.AirDragSeconds = bAir ? Vent.AirDragSeconds : 0.0f;
		// Out of the hole, away from the bore it vents, a little every way.
		Puff.Direction = ((Port - FVector(Port.X, 0.0f, Vent.BoreZCm)).GetSafeNormal() + FMath::VRand() * 0.35f).GetSafeNormal();
		Puff.Along = Puff.Direction;
		Puff.SpreadCm = ThrowCm * FMath::FRandRange(0.6f, 1.0f);
		Puff.DriftCm = Vent.DriftCm * FMath::FRandRange(0.5f, 1.0f);
		Puff.Stretch = FMath::FRandRange(1.0f, 1.4f);
		Puff.Tear = 0.3f;
		Puff.StartCm = Vent.StartCm;
		Puff.EndCm = Vent.EndCm * FMath::FRandRange(0.7f, 1.0f);
		Puff.Opacity = Vent.Opacity;
		Puff.Life = Vent.Seconds * FMath::FRandRange(0.75f, 1.25f);
		Puff.Age = FMath::FRand() * AgeSpread;
		Puff.Seed = FMath::FRand();
		Puff.Spin = FMath::FRandRange(-0.4f, 0.4f);
		Puff.Lift = Lift;
		Puff.LiftDragSeconds = bAir ? Vent.AirDragSeconds : 0.0f;
		Puff.SwayCm = bAir ? 1.4f : 0.0f;
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

void UJamMuzzleFxComponent::WaitSims() const
{
	for (const FCloud& Cloud : Clouds)
	{
		if (Cloud.Pending.IsValid())
		{
			Cloud.Pending.Wait();
			Cloud.Pending = UE::Tasks::FTask();
		}
	}
}

void UJamMuzzleFxComponent::ClearClouds()
{
	WaitSims();
	for (FCloud& Cloud : Clouds)
	{
		Cloud.Age = -1.0f;
		Cloud.Volume->SetVisibility(false);
	}
}

FString UJamMuzzleFxComponent::DescribeClouds() const
{
	WaitSims();
	FString Line;
	const FVector Eye = View->GetComponentLocation();
	for (const FCloud& Cloud : Clouds)
	{
		if (Cloud.Age >= 0.0f)
		{
			float Mass, Speed, MaxHeat;
			const FVector3f Centroid = Cloud.Sim->Centroid(Mass);
			Cloud.Sim->Stats(Speed, MaxHeat);
			// The gas's centre in the world (the box's centre is a whole number of cells off it).
			const FVector Gas = Cloud.Centre + Cloud.Basis.RotateVector(FVector(Centroid) * Cloud.Sim->CellCm);
			Line += FString::Printf(TEXT(" [gun %d age %.2f %s%s, gas at %s, %.0f cm from its muzzle at the shot, %.0f cm from the eye, mass %.0f, mean |U| %.0f cm/s, heat %.2f, box %.0f cm x%.2f, carried %s cm/s]"),
				Cloud.Gun, Cloud.Age, Cloud.Sim->bAir ? TEXT("air") : TEXT("vacuum"), Cloud.bFrozen ? TEXT(" frozen") : TEXT(""), *Gas.ToCompactString(),
				FVector::Dist(Gas, Cloud.Muzzle), FVector::Dist(Gas, Eye), Mass, Speed, MaxHeat, Cloud.Sim->N * Cloud.Sim->CellCm, Cloud.Scale,
				*Cloud.Carried.ToCompactString());
		}
	}
	return Line;
}

void UJamMuzzleFxComponent::DumpClouds(const FString& Name) const
{
	WaitSims();
	for (int32 Slot = 0; Slot < Clouds.Num(); ++Slot)
	{
		const FCloud& Cloud = Clouds[Slot];
		if (Cloud.Age >= 0.0f && !Cloud.Atlas.IsEmpty())
		{
			const TArrayView<const uint8> Bytes(reinterpret_cast<const uint8*>(Cloud.Atlas.GetData()), Cloud.Atlas.Num() * sizeof(FColor));
			FFileHelper::SaveArrayToFile(Bytes, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SmokeDump"), FString::Printf(TEXT("%s_%d_n%d.raw"), *Name, Slot, Cloud.Sim->N)));
		}
	}
}

int32 UJamMuzzleFxComponent::GetCloudCount() const
{
	int32 Count = 0;
	for (const FCloud& Cloud : Clouds)
	{
		Count += Cloud.Age >= 0.0f ? 1 : 0;
	}
	return Count;
}

namespace
{
	/**
	 * The grids step at a fixed rate - the same gas at 30, 60 or 144 fps (AGENTS.md, В«Р§Р°СЃС‚РѕС‚Р° РєР°РґСЂРѕРІВ») - of 60 Hz while
	 * the slug is fast (FastSeconds after the shot), 30 Hz after: the slow churn does not need more, and a step costs.
	 */
	constexpr float SimStep = 1.0f / 60.0f;
	constexpr float SlowStep = 1.0f / 30.0f;
	constexpr float FastSeconds = 0.5f;

#if !UE_BUILD_SHIPPING
	/** jam.Fx.SmokeCost: what the grids cost on the game thread since the last call. */
	struct FSmokeCost
	{
		double StepUs = 0.0, MaxStepUs = 0.0, FrameUs = 0.0, MaxFrameUs = 0.0;
		int32 Steps = 0, Frames = 0;
	} GSmokeCost;
	FCriticalSection GSmokeCostLock;

	FAutoConsoleCommand GSmokeCostCommand(
		TEXT("jam.Fx.SmokeCost"),
		TEXT("Log what the smoke grids cost since the last call - a step (on a worker thread): mean / max us; the game thread in a frame with a grid stepping (waiting for the steps, the box, the upload): mean / max us - then start over."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			FSmokeCost& C = GSmokeCost;
			UE_LOG(LogMindKore, Log, TEXT("Smoke cost: %d step(s) off the game thread, %.0f us mean, %.0f us max; game thread in %d frame(s) with grids, %.0f us mean, %.0f us max"),
				C.Steps, C.Steps ? C.StepUs / C.Steps : 0.0, C.MaxStepUs, C.Frames, C.Frames ? C.FrameUs / C.Frames : 0.0, C.MaxFrameUs);
			C = FSmokeCost();
		}));

	/**
	 * jam.Fx.SmokeTune "Field=Value ...": overrides for tuning without a rebuild - FJamMuzzleFx fields on every gun
	 * (Sim.Buoyancy=300, DenseExtinction=0.2) and the cloud material's parameters (mat.Ambient=0.02,
	 * mat.Colour=0.4/0.4/0.38). Empty: none (the fields keep what was set, the material its own values).
	 */
	TAutoConsoleVariable<FString> CVarSmokeTune(TEXT("jam.Fx.SmokeTune"), TEXT(""),
		TEXT("Tuning overrides: 'Field=Value ...' on every gun's FJamMuzzleFx (Sim.Buoyancy=300) and mat.<Param>=Value (or R/G/B) on the cloud material."));
#endif
}

void UJamMuzzleFxComponent::SpawnCloud(int32 Weapon, const FTransform& Muzzle, const FVector& GunVelocity)
{
	const FJamMuzzleFx& Gun = Guns[Weapon];
	UWorld* World = GetWorld();
	// A free slot, a new one while under MaxClouds, else the oldest cloud.
	FCloud* Cloud = Clouds.FindByPredicate([](const FCloud& Each) { return Each.Age < 0.0f; });
	if (!Cloud && Clouds.Num() < MaxClouds)
	{
		Cloud = &Clouds.AddDefaulted_GetRef();
		UHeterogeneousVolumeComponent* Volume = NewObject<UHeterogeneousVolumeComponent>(GetOwner());
		Volume->SetUsingAbsoluteLocation(true);
		Volume->SetUsingAbsoluteRotation(true);
		Volume->SetUsingAbsoluteScale(true);
		Volume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		// The box about its centre (by default it grows from a corner: a cloud cut to one octant).
		Volume->bPivotAtCentroid = true;
		// Voxel-exact: the lighting cache (self-shadow) on the grid's own cells, steps of a cell.
		Volume->LightingDownsampleFactor = 1.0f;
		Volume->StepFactor = 1.0f;
		Volume->ShadowStepFactor = 1.0f;
		Volume->RegisterComponent();
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(CloudMaterial.Get(), this);
		Volume->SetMaterial(0, Material);
		Cloud->Volume = Volume;
		Cloud->Material = Material;
		Cloud->Sim = MakeUnique<FJamSmokeSim>();
		CloudObjects.Add(Volume);
		CloudObjects.Add(Material);
	}
	if (!Cloud)
	{
		Cloud = Algo::MaxElementBy(Clouds, [](const FCloud& Each) { return Each.Age; });
	}
	const FQuat BoreQuat = Muzzle.GetRotation();
	const FVector Bore = BoreQuat.GetAxisX();
	const float BoxCm = Gun.Cells * Gun.CellCm;
	Cloud->Gun = Weapon;
	Cloud->Muzzle = Muzzle.GetLocation();
	Cloud->Bore = Bore;
	// A fixed skew off the bore: no voxel axis along the view down the shot (view-long columns drew a cross, 19.10).
	Cloud->Basis = BoreQuat * FQuat(FVector::UpVector, FMath::DegreesToRadians(23.0f)) * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(17.0f));
	// The gas goes forward: the muzzle sits in the back of the box (it follows the gas from then on).
	Cloud->Centre = Cloud->Muzzle + Bore * BoxCm * (Gun.SmokeBrake > 0.0f ? 0.05f : 0.2f);
	// Nothing ties it to the shooter: the gun's velocity at the shot is the centre of mass's (kept in a vacuum, the air takes it).
	Cloud->Carried = GunVelocity;
	Cloud->Rise = FVector::ZeroVector;
	Cloud->Age = 0.0f;
	Cloud->Seed = FMath::FRand();
	Cloud->Awake = 0.0f;
	Cloud->StepCarry = 0.0f;
	Cloud->Scale = 1.0f;
	Cloud->ExpandRate = 0.0f;
	Cloud->bFrozen = false;
	FJamSmokeSim& Sim = *Cloud->Sim;
	const bool bAir = JamAirZoneAt(World, Cloud->Muzzle) != nullptr;
	const FVector Gravity = JamGravityAt(World, Cloud->Muzzle).Acceleration;
	Sim.Reset(Gun.Cells, Gun.CellCm, bAir, FVector3f(Cloud->Basis.UnrotateVector(Gravity.GetSafeNormal())));
	// The floor and a wall: what the bore and the gravity meet within the box become planes the gas cannot enter
	// (Visibility: loose cases and magazines ignore it).
	FCollisionQueryParams Query(SCENE_QUERY_STAT(JamSmokeSolids), false, GetOwner());
	for (const FVector& Direction : { Bore, Gravity.GetSafeNormal() })
	{
		FHitResult Hit;
		if (!Direction.IsNearlyZero() && World->LineTraceSingleByChannel(Hit, Cloud->Muzzle, Cloud->Muzzle + Direction * BoxCm, ECC_Visibility, Query))
		{
			const FVector3f Normal(Cloud->Basis.UnrotateVector(Hit.ImpactNormal));
			const FVector3f Point(Cloud->Basis.UnrotateVector(Hit.ImpactPoint - Cloud->Centre));
			if (!Sim.Planes.ContainsByPredicate([&Normal](const FPlane4f& Each) { return (Each.GetSafeNormal() | Normal) > 0.95f; }))
			{
				Sim.Planes.Emplace(Point, Normal);
			}
		}
	}
	Sim.RebuildSolids();
	// The slug: out of the bore (a brake lets 1 - SmokeBrake on forward and throws the rest out of both side ports).
	const FVector3f MuzzleLocal(Cloud->Basis.UnrotateVector(Cloud->Muzzle - Cloud->Centre));
	const FVector3f BoreLocal(Cloud->Basis.UnrotateVector(Bore));
	const float Throw = bAir ? 1.0f : VacuumThrowShare;
	const float Radial = bAir ? 1.0f : VacuumRadialScale;
	Sim.Inject(MuzzleLocal + BoreLocal * Gun.SlugLengthCm, BoreLocal, Gun.SlugLengthCm, Gun.SlugRadiusCm, 1.0f, 1.0f,
		BoreLocal * Gun.SlugSpeedCm * (1.0f - Gun.SmokeBrake) * Throw, Gun.SlugRadialCm * Radial);
	if (Gun.SmokeBrake > 0.0f)
	{
		for (const float Side : { 1.0f, -1.0f })
		{
			const FVector3f Port(Cloud->Basis.UnrotateVector(BoreQuat.RotateVector(FVector(-0.35f, Side, 0.0f).GetSafeNormal())));
			Sim.Inject(MuzzleLocal + Port * Gun.SlugLengthCm, Port, Gun.SlugLengthCm, Gun.SlugRadiusCm, 1.0f, 1.0f, Port * Gun.SlugSpeedCm * Gun.SmokeBrake * 0.5f * Throw, Gun.SlugRadialCm * 0.5f * Radial);
		}
	}
	// The grid's texture, one per slot (point-sampled: each texel is a voxel).
	const FIntPoint Size = FJamSmokeSim::AtlasSize(Gun.Cells);
	if (!Cloud->Grid || Cloud->Grid->GetSizeX() != Size.X || Cloud->Grid->GetSizeY() != Size.Y)
	{
		UTexture2D* Grid = UTexture2D::CreateTransient(Size.X, Size.Y, PF_B8G8R8A8);
		Grid->SRGB = false;
		Grid->Filter = TF_Nearest;
		Grid->NeverStream = true;
		Grid->UpdateResource();
		Cloud->Grid = Grid;
		CloudObjects.Add(Grid);
		Cloud->Material->SetTextureParameterValue(TEXT("Grid"), Grid);
	}
	Cloud->Volume->SetVolumeResolution(FIntVector(Gun.Cells));
	Cloud->Volume->SetVisibility(true);
}

void UJamMuzzleFxComponent::Blast(const FVector& Origin, const FVector& Direction, float RadiusCm, float Along, float Push, float Clear, const FCloud* Except)
{
	for (FCloud& Cloud : Clouds)
	{
		if (Cloud.Age < 0.0f || &Cloud == Except)
		{
			continue;
		}
		// Box frame; the line counts if it passes within the box's half diagonal of its centre.
		FJamSmokeSim& Sim = *Cloud.Sim;
		const FVector Local = Cloud.Basis.UnrotateVector(Origin - Cloud.Centre);
		const FVector Dir = Cloud.Basis.UnrotateVector(Direction).GetSafeNormal();
		const float Half = 0.5f * Sim.N * Guns[Cloud.Gun].CellCm * Cloud.Scale;
		if ((Local - Dir * (Local | Dir)).Size() > Half * 1.75f)
		{
			continue;
		}
		if (Sim.Impulse(FVector3f(Local), FVector3f(Dir), RadiusCm, Along, Push, Clear))
		{
			Cloud.Awake = WakeSeconds;
		}
	}
}

void UJamMuzzleFxComponent::TickClouds(float DeltaTime)
{
	UWorld* World = GetWorld();
	const FVector Eye = View->GetComponentLocation();
	const FVector EyeStep = Eye - LastEye;
	LastEye = Eye;
#if !UE_BUILD_SHIPPING
	// Tuning overrides (jam.Fx.SmokeTune): the fields on every gun when the string changes, the material's every frame.
	static FString TuneApplied;
	TArray<TPair<FName, FString>> MaterialTune;
	{
		const FString Tune = CVarSmokeTune.GetValueOnGameThread();
		TArray<FString> Items;
		Tune.ParseIntoArrayWS(Items);
		for (const FString& Item : Items)
		{
			FString Key, Value;
			if (!Item.Split(TEXT("="), &Key, &Value))
			{
				continue;
			}
			if (Key.StartsWith(TEXT("mat.")))
			{
				MaterialTune.Emplace(FName(*Key.RightChop(4)), Value);
			}
			else if (Tune != TuneApplied)
			{
				for (FJamMuzzleFx& Each : Guns)
				{
					// "Sim.Buoyancy": a field of the nested struct.
					UScriptStruct* Struct = FJamMuzzleFx::StaticStruct();
					void* Container = &Each;
					FString Field = Key, Inner;
					if (Key.Split(TEXT("."), &Field, &Inner))
					{
						const FStructProperty* Nested = CastField<FStructProperty>(Struct->FindPropertyByName(FName(*Field)));
						Container = Nested ? Nested->ContainerPtrToValuePtr<void>(Container) : nullptr;
						Struct = Nested ? Nested->Struct.Get() : nullptr;
						Field = Inner;
					}
					const FProperty* Property = Struct ? Struct->FindPropertyByName(FName(*Field)) : nullptr;
					if (!Property || !Property->ImportText_InContainer(*Value, Container, nullptr, PPF_None))
					{
						UE_LOG(LogMindKore, Warning, TEXT("jam.Fx.SmokeTune: no field %s"), *Key);
						break;
					}
				}
			}
		}
		if (Tune != TuneApplied)
		{
			UE_LOG(LogMindKore, Log, TEXT("jam.Fx.SmokeTune: %s"), *Tune);
			TuneApplied = Tune;
		}
	}
#endif
	// The last frame's steps: their box shift and atlas (TickComponent waited for them).
	for (FCloud& Cloud : Clouds)
	{
		if (Cloud.Age < 0.0f || !Cloud.bPendingAtlas)
		{
			continue;
		}
		Cloud.bPendingAtlas = false;
		// The box follows the gas by whole cells: the voxels stay where they are in the world.
		Cloud.Centre += Cloud.Basis.RotateVector(FVector(Cloud.PendingShift) * Cloud.Sim->CellCm);
		const FIntPoint Size = FJamSmokeSim::AtlasSize(Cloud.Sim->N);
		// The render thread copies it later: its own buffer and region, freed when done.
		uint8* Data = static_cast<uint8*>(FMemory::Malloc(Cloud.Atlas.Num() * sizeof(FColor)));
		FMemory::Memcpy(Data, Cloud.Atlas.GetData(), Cloud.Atlas.Num() * sizeof(FColor));
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Size.X, Size.Y);
		Cloud.Grid->UpdateTextureRegions(0, 1, Region, Size.X * sizeof(FColor), sizeof(FColor), Data,
			[](uint8* Src, const FUpdateTextureRegion2D* Regions) { FMemory::Free(Src); delete Regions; });
	}
	struct FLaunch
	{
		FCloud* Cloud;
		int32 Steps;
		float FirstAt;
		float Dt;
	};
	TArray<FLaunch, TInlineAllocator<4>> Launches;
	// The youngest clouds that want to move step; past MaxActiveSims the older ones hold still this frame.
	TArray<FCloud*, TInlineAllocator<8>> Stepping;
	for (FCloud& Cloud : Clouds)
	{
		const FJamMuzzleFx& G = Guns[Cloud.Gun];
		if (Cloud.Age >= 0.0f && (Cloud.Age < (Cloud.Sim->bAir ? G.ActiveAirSeconds : G.ActiveVacuumSeconds) || Cloud.Awake > 0.0f))
		{
			Stepping.Add(&Cloud);
		}
	}
	Stepping.Sort([](const FCloud& A, const FCloud& B) { return A.Age < B.Age; });
	Stepping.SetNum(FMath::Min(Stepping.Num(), MaxActiveSims));

	for (FCloud& Cloud : Clouds)
	{
		if (Cloud.Age < 0.0f)
		{
			continue;
		}
		const FJamMuzzleFx& G = Guns[Cloud.Gun];
		FJamSmokeSim& Sim = *Cloud.Sim;
		// A grown vacuum cloud's cells are grown too (a push or a wake works at them).
		Sim.CellCm = G.CellCm * Cloud.Scale;
		Cloud.Age += DeltaTime;
		if (Cloud.Age >= G.CloudSeconds)
		{
			Cloud.Age = -1.0f;
			Cloud.Volume->SetVisibility(false);
			continue;
		}
		// Out of an air zone the gas turns ballistic, into one the air takes it: read every frame.
		Sim.bAir = JamAirZoneAt(World, Cloud.Centre) != nullptr;
		Sim.Down = FVector3f(Cloud.Basis.UnrotateVector(JamGravityAt(World, Cloud.Centre).Acceleration.GetSafeNormal()));
		if (Sim.bAir)
		{
			Cloud.Carried *= FMath::Exp(-DeltaTime / AirDragSeconds);
		}
		// The box rides the carried velocity (and a frozen cloud's rise); the floor stays where it is in the world.
		const FVector Move = (Cloud.Carried + Cloud.Rise) * DeltaTime;
		if (!Move.IsNearlyZero(0.01f))
		{
			Cloud.Centre += Move;
			const FVector3f MoveLocal(Cloud.Basis.UnrotateVector(Move));
			for (FPlane4f& Plane : Sim.Planes)
			{
				Plane.W -= Plane.GetSafeNormal() | MoveLocal;
			}
			if (!Sim.Planes.IsEmpty())
			{
				Sim.RebuildSolids();
			}
		}
		if (Stepping.Contains(&Cloud))
		{
			Cloud.bFrozen = false;
			Cloud.Rise = FVector::ZeroVector;
			Cloud.StepCarry += DeltaTime;
			// Whole fixed steps; a long frame does not pile them up (at most 4: a hitch slows the gas instead).
			const float Dt = Cloud.Age - Cloud.StepCarry < FastSeconds ? SimStep : SlowStep;
			const int32 Steps = FMath::Min(FMath::FloorToInt(Cloud.StepCarry / Dt), 4);
			if (Steps > 0)
			{
				Launches.Add({ &Cloud, Steps, Cloud.Age - Cloud.StepCarry + Dt, Dt });
			}
			Cloud.StepCarry = Steps == 4 ? 0.0f : Cloud.StepCarry - Steps * Dt;
			Cloud.Awake = FMath::Max(0.0f, Cloud.Awake - DeltaTime);
		}
		else if (!Cloud.bFrozen)
		{
			// Frozen: air keeps rising at the gas's mean velocity then (dying); a vacuum's free streaming frozen is a
			// uniform growth of the whole field - the box scales and the density thins as 1/s^3.
			Cloud.bFrozen = true;
			double Mass = 0.0;
			FVector3f Mean = FVector3f::ZeroVector;
			for (int32 I = 0; I < Sim.Rho.Num(); ++I)
			{
				Mass += Sim.Rho[I];
				Mean += FVector3f(Sim.Ux[I], Sim.Uy[I], Sim.Uz[I]) * Sim.Rho[I];
			}
			Mean /= FMath::Max(float(Mass), 1e-3f);
			Cloud.Rise = Sim.bAir ? Cloud.Basis.RotateVector(FVector(Mean)) : FVector::ZeroVector;
			// The grid's own radial speed is smeared within a few steps (semi-Lagrangian self-advection), so the slug's
			// expansion speed sets the rate when it is the larger.
			const float Radial = FMath::Max(Sim.MeanRadialSpeed(), G.SlugRadialCm * VacuumRadialScale * 0.5f);
			Cloud.ExpandRate = Sim.bAir ? 0.0f : FMath::Clamp(Radial / (0.5f * Sim.N * G.CellCm), 0.05f, 1.0f);
		}
		else
		{
			Cloud.Rise *= FMath::Exp(-DeltaTime / 2.0f);
			Cloud.Scale += Cloud.ExpandRate * DeltaTime;
		}
		const float Half = 0.5f * Sim.N * G.CellCm * Cloud.Scale;
		// The pawn passing through parts it (and the voxels by the eye fade).
		const FVector EyeLocal = Cloud.Basis.UnrotateVector(Eye - Cloud.Centre);
		if (EyeLocal.GetAbsMax() < Half && EyeStep.Size() > 1.0f && EyeStep.Size() < 200.0f && DeltaTime > 0.0f)
		{
			Blast(Eye - EyeStep, EyeStep.GetSafeNormal(), 18.0f, EyeStep.Size() / DeltaTime * 0.5f, 40.0f, 0.0f, nullptr);
		}
		UMaterialInstanceDynamic* Material = Cloud.Material;
		Material->SetVectorParameterValue(TEXT("CloudCentre"), FLinearColor(FVector3f(Cloud.Centre)));
		Material->SetVectorParameterValue(TEXT("CloudX"), FLinearColor(FVector3f(Cloud.Basis.GetAxisX())));
		Material->SetVectorParameterValue(TEXT("CloudY"), FLinearColor(FVector3f(Cloud.Basis.GetAxisY())));
		Material->SetVectorParameterValue(TEXT("CloudZ"), FLinearColor(FVector3f(Cloud.Basis.GetAxisZ())));
		Material->SetScalarParameterValue(TEXT("HalfCm"), Half);
		Material->SetScalarParameterValue(TEXT("Cells"), float(Sim.N));
		Material->SetScalarParameterValue(TEXT("Age"), Cloud.Age);
		Material->SetScalarParameterValue(TEXT("Life"), G.CloudSeconds);
		Material->SetScalarParameterValue(TEXT("Thin"), 1.0f / FMath::Cube(Cloud.Scale));
		Material->SetScalarParameterValue(TEXT("Erode"), G.Erode);
		Material->SetScalarParameterValue(TEXT("AmbientShare"), Sim.bAir ? 1.0f : VacuumAmbientShare);
		Material->SetScalarParameterValue(TEXT("Seed"), Cloud.Seed);
		// The volume integrates per local unit (a cell here, HeterogeneousVolumesLiveShadingPipeline.usf: StepSize is
		// world / LocalToWorldScale): extinction and emission per cm times the cell's cm.
		Material->SetScalarParameterValue(TEXT("Extinction"), G.DenseExtinction);
		Material->SetScalarParameterValue(TEXT("UnitCm"), G.CellCm * Cloud.Scale);
		Material->SetVectorParameterValue(TEXT("Eye"), FLinearColor(EyeLocal.X, EyeLocal.Y, EyeLocal.Z, EyeClearCm));
#if !UE_BUILD_SHIPPING
		for (const TPair<FName, FString>& Each : MaterialTune)
		{
			TArray<FString> Parts;
			Each.Value.ParseIntoArray(Parts, TEXT("/"));
			Parts.Num() >= 3
				? Material->SetVectorParameterValue(Each.Key, FLinearColor(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]), 1.0f))
				: Material->SetScalarParameterValue(Each.Key, FCString::Atof(*Each.Value));
		}
#endif
		// The volume box: Cells units a side, each a cell (grown with a frozen vacuum cloud).
		Cloud.Volume->SetWorldTransform(FTransform(Cloud.Basis, Cloud.Centre, FVector(G.CellCm * Cloud.Scale)));
	}
	// A flyer diving through stirs the gas after it: the swirl of its wings.
	if (GetCloudCount() > 0)
	{
		for (TActorIterator<AJamFlyer> Flyer(World); Flyer; ++Flyer)
		{
			const FVector Velocity = Flyer->GetVelocity();
			if (Velocity.SizeSquared() > 100.0f)
			{
				Blast(Flyer->GetActorLocation() - Velocity * DeltaTime, Velocity.GetSafeNormal(), 25.0f, Velocity.Size() * 0.4f, 40.0f, 0.0f, nullptr);
			}
		}
	}
	// The steps, off the game thread while the rest of the frame runs (each grid its own task; ParallelFor inside):
	// the muzzle's stem, the steps, the box's follow (taken next frame), the atlas.
	for (const FLaunch& L : Launches)
	{
		FCloud* Cloud = L.Cloud;
		const FJamMuzzleFx G = Guns[Cloud->Gun];
		const FVector3f Up = Cloud->Sim->Down.IsNearlyZero() ? FVector3f(Cloud->Basis.UnrotateVector(FVector::UpVector)) : -Cloud->Sim->Down;
		const float Occlusion = AmbientOcclusion;
		Cloud->Pending = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Cloud, G, L, Up, Occlusion]()
		{
			FJamSmokeSim& Sim = *Cloud->Sim;
			for (int32 Step = 0; Step < L.Steps; ++Step)
			{
				const float At = L.FirstAt + Step * L.Dt;
				// The muzzle keeps puffing for EmitSeconds (from where it was at the shot, carried with the gas): the gas
				// still leaving the bore behind the blast's ball, a narrower stem. In a vacuum none: the slug flies apart
				// on its own, and a stem behind it drew a streak the box could not follow.
				if (At < G.EmitSeconds && Sim.bAir)
				{
					const FVector3f From(Cloud->Basis.UnrotateVector(Cloud->Muzzle + Cloud->Carried * At - Cloud->Centre));
					const FVector3f BoreLocal(Cloud->Basis.UnrotateVector(Cloud->Bore));
					Sim.Inject(From + BoreLocal * G.SlugLengthCm * 0.4f, BoreLocal, G.SlugLengthCm * 0.4f, G.SlugRadiusCm * 0.5f, 1.0f, 1.0f,
						BoreLocal * G.SlugSpeedCm * (1.0f - G.SmokeBrake), G.SlugRadialCm * 0.5f);
				}
#if !UE_BUILD_SHIPPING
				const double T0 = FPlatformTime::Seconds();
#endif
				Sim.Step(L.Dt, At, G.Sim);
#if !UE_BUILD_SHIPPING
				const double Us = (FPlatformTime::Seconds() - T0) * 1e6;
				FScopeLock Lock(&GSmokeCostLock);
				GSmokeCost.StepUs += Us;
				GSmokeCost.MaxStepUs = FMath::Max(GSmokeCost.MaxStepUs, Us);
				++GSmokeCost.Steps;
#endif
			}
			// The box follows the gas by whole cells (the game thread moves the box by PendingShift next frame).
			float Mass;
			const FVector3f Off = Sim.Centroid(Mass);
			Cloud->PendingShift = FIntVector::ZeroValue;
			if (Mass > 0.01f && Off.GetAbsMax() > 1.5f)
			{
				Cloud->PendingShift = FIntVector(FMath::RoundToInt(Off.X), FMath::RoundToInt(Off.Y), FMath::RoundToInt(Off.Z));
				Sim.Shift(Cloud->PendingShift);
			}
			Sim.Pack(Cloud->Atlas, Up, Occlusion);
			Cloud->bPendingAtlas = true;
		});
	}
}

void UJamMuzzleFxComponent::HotCase(AActor* Case)
{
	if (Case)
	{
		HotCases.Add({ Case, 1.0f, 0.0f });
	}
}

UJamMuzzleFxComponent::FPuff& UJamMuzzleFxComponent::AddCardPuff(const FVector& Where, const FVector& Direction, float StartCm, float EndCm, float Opacity, float Seconds)
{
	const bool bAir = JamAirZoneAt(GetWorld(), Where) != nullptr;
	FPuff& Puff = Puffs.AddDefaulted_GetRef();
	Puff.Origin = Where;
	Puff.Direction = Direction.GetSafeNormal();
	Puff.Along = Puff.Direction;
	Puff.SpreadCm = Direction.Size();
	Puff.Stretch = FMath::FRandRange(1.0f, 1.4f);
	Puff.Tear = 0.3f;
	Puff.StartCm = StartCm;
	Puff.EndCm = EndCm * FMath::FRandRange(0.7f, 1.0f);
	Puff.Opacity = Opacity;
	Puff.Life = Seconds * FMath::FRandRange(0.75f, 1.25f);
	Puff.Seed = FMath::FRand();
	Puff.Spin = FMath::FRandRange(-0.4f, 0.4f);
	if (bAir)
	{
		// Hot smoke rises in air (as the shroud's): a share of the local gravity against it, the air's hold on it.
		Puff.Lift = -JamGravityAt(GetWorld(), Where).Acceleration * 0.12f;
		Puff.LiftDragSeconds = 0.7f;
		Puff.AirDragSeconds = 0.7f;
		Puff.SwayCm = 1.4f;
	}
	if (Puffs.Num() > MaxPuffs)
	{
		Puffs.RemoveAt(0, Puffs.Num() - MaxPuffs);
	}
	return Puffs.Last();
}

void UJamMuzzleFxComponent::TickHotCases(float DeltaTime)
{
	for (int32 Index = HotCases.Num() - 1; Index >= 0; --Index)
	{
		FHotCase& Hot = HotCases[Index];
		Hot.Heat *= FMath::Exp(-DeltaTime / CaseHeatSeconds);
		const AActor* Case = Hot.Case.Get();
		if (!Case || Hot.Heat < 0.15f)
		{
			HotCases.RemoveAt(Index);
			continue;
		}
		const FVector Where = Case->GetActorLocation();
		const UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Case->GetRootComponent());
		const FVector Velocity = Body ? Body->GetPhysicsLinearVelocity() : FVector::ZeroVector;
		bool bInCloud = false;
		for (FCloud& Cloud : Clouds)
		{
			// Air only: in a vacuum nothing drags a wisp after the case (and a woken vacuum cloud would stop growing).
			if (Cloud.Age < 0.0f || !Cloud.Sim->bAir)
			{
				continue;
			}
			FJamSmokeSim& Sim = *Cloud.Sim;
			const FVector Local = Cloud.Basis.UnrotateVector(Where - Cloud.Centre);
			if (Local.GetAbsMax() >= 0.5f * Sim.N * Sim.CellCm)
			{
				continue;
			}
			// Inside a cloud: the case drags a thin hot wisp and cuts a wake through the gas (and wakes it).
			bInCloud = true;
			const FVector3f V(Cloud.Basis.UnrotateVector(Velocity));
			const float Speed = V.Size();
			Sim.Inject(FVector3f(Local), Speed > 1.0f ? V / Speed : FVector3f::ForwardVector, 1.5f * Sim.CellCm, 1.5f * Sim.CellCm,
				FMath::Min(0.25f * Hot.Heat * DeltaTime * 30.0f, 1.0f), 0.5f * Hot.Heat, V * 0.4f, 0.0f);
			if (Speed > 20.0f)
			{
				Sim.Impulse(FVector3f(Local) - V * DeltaTime, V / Speed, 6.0f, Speed * 0.3f, Speed * 0.2f, 0.0f);
			}
			Cloud.Awake = WakeSeconds;
		}
		if (!bInCloud)
		{
			// Out of the clouds (on the floor, in clean air): a thread of card puffs while it is hot.
			Hot.Carry += 8.0f * Hot.Heat * DeltaTime;
			for (; Hot.Carry >= 1.0f; Hot.Carry -= 1.0f)
			{
				FPuff& Puff = AddCardPuff(Where, Velocity * 0.02f + FMath::VRand() * 0.5f, 0.8f, 5.0f, 0.18f * FMath::Min(Hot.Heat * 1.5f, 1.0f), 1.6f);
				Puff.Age = FMath::FRand() * DeltaTime;
			}
		}
	}
}

namespace
{
	/** A smoke texel's size per cm from the eye: ~6 px of 1920 at the bodycam's FOV 134 (2 tan 67 deg = 4.71 view widths per distance). */
	constexpr float TexelPerCm = 6.0f / 1920.0f * 4.71f;
}

void UJamMuzzleFxComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Smoke)
	{
		return;
	}
	// The grids' last steps (off the game thread) first: everything here touches them.
#if !UE_BUILD_SHIPPING
	const double FrameStart = FPlatformTime::Seconds();
	bool bGrids = false;
	for (const FCloud& Cloud : Clouds)
	{
		bGrids |= Cloud.Pending.IsValid();
	}
#endif
	WaitSims();
	TickHotCases(DeltaTime);
	TickClouds(DeltaTime);
#if !UE_BUILD_SHIPPING
	if (bGrids)
	{
		const double Us = (FPlatformTime::Seconds() - FrameStart) * 1e6;
		GSmokeCost.FrameUs += Us;
		GSmokeCost.MaxFrameUs = FMath::Max(GSmokeCost.MaxFrameUs, Us);
		++GSmokeCost.Frames;
	}
#endif

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
			// The gun jumps back and up in the frame after the shot: the flash stays on its muzzle,
			// turned with its bore. The sprites are drawn in the camera's space.
			const USceneComponent* GunMesh = FlashGunMesh.Get();
			const FTransform Camera = View->GetComponentTransform();
			const FTransform MuzzleNow = FlashMuzzle * (GunMesh ? GunMesh->GetComponentTransform() : Camera);
			const FVector Muzzle = Camera.InverseTransformPosition(MuzzleNow.GetLocation());
			const FQuat Bore = Camera.GetRotation().Inverse() * MuzzleNow.GetRotation();
			for (const FFlashBit& Bit : FlashBits)
			{
				const FVector Direction = Bore.RotateVector(Bit.Direction);
				float Intensity, Length, Width;
				FVector Location;
				if (Bit.Speed > 0.0f)
				{
					// A grain in a straight line, drawn as its streak over about one frame.
					if (Age >= Bit.Life)
					{
						continue;
					}
					Location = Muzzle + Direction * Bit.Speed * Age;
					Width = Bit.SizeCm;
					Length = FMath::Max(Width, Bit.Speed * 0.012f);
					Intensity = Bit.Brightness * 0.6f * (1.0f - FMath::Square(Age / Bit.Life));
				}
				else
				{
					Location = Muzzle + Direction * Bit.ReachCm * Out;
					Width = Bit.SizeCm * (0.35f + 0.65f * Out);
					Length = Width * Bit.Stretch;
					Intensity = Bit.Brightness * (Burst + Glow);
				}
				const int32 Instance = Flash->AddInstance(Sprite(Location, Direction, Length, Width));
				Flash->SetCustomData(Instance, { Intensity, Bit.Seed, Bit.Speed > 0.0f ? 0.7f : Heat * Bit.Heat, Bit.Angle }, true);
			}
			Light->SetRelativeLocation(Muzzle);
			Light->SetIntensity(Gun.LightIntensity * (Burst + Glow));
			Light->SetVisibility(true);
		}
	}

	// The card smoke, in the world, as the clouds: thrown out of the muzzle and swollen
	// within a few tenths of a second, then frozen in shape and drifting straight on
	// (nothing to lift it), all the while carried by the gun's velocity at the shot (kept in
	// a vacuum, dying in air). Worked out in the world, drawn in the camera's space.
	if (Puffs.IsEmpty() && Smoke->GetInstanceCount() == 0)
	{
		return;
	}
	Smoke->ClearInstances();
	const FTransform Camera = View->GetComponentTransform();
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
		// The gun's velocity is kept in a vacuum; in air it dies with AirDragSeconds (as a cloud's).
		const FVector Carried = Puff.GunVelocity * (Puff.AirDragSeconds > 0.0f ? Puff.AirDragSeconds * (1.0f - FMath::Exp(-Puff.Age / Puff.AirDragSeconds)) : Puff.Age);
		// Hot smoke in air: its lift against the air's drag, worked out exactly (v = a tau (1 - e^-t/tau)).
		const FVector Risen = Puff.LiftDragSeconds > 0.0f
			? Puff.Lift * Puff.LiftDragSeconds * (Puff.Age - Puff.LiftDragSeconds * (1.0f - FMath::Exp(-Puff.Age / Puff.LiftDragSeconds)))
			: FVector::ZeroVector;
		FVector World = Puff.Origin + Carried + Risen + Puff.Bore.RotateVector(Puff.Direction * (Puff.SpreadCm * Throw + Puff.DriftCm * Puff.Age)
			- FVector(Puff.BackCm * Throw, 0.0f, 0.0f));
		// A wisp drawn out along the shot at the time (or its brake jet)...
		FVector Along = Puff.Bore.RotateVector(Puff.Along);
		if (Puff.LiftDragSeconds > 0.0f)
		{
			// ...or, rising in air, stood up along its rise and swaying across it on a sinuous path.
			const FVector RisenVelocity = Puff.Lift * Puff.LiftDragSeconds * (1.0f - FMath::Exp(-Puff.Age / Puff.LiftDragSeconds));
			Along = (Puff.Bore.RotateVector(Puff.Direction) * Puff.SpreadCm * Throw + RisenVelocity * 0.6f).GetSafeNormal(UE_SMALL_NUMBER, Along);
			const FVector Up = Puff.Lift.GetSafeNormal();
			const FVector Across = (Puff.Bore.GetAxisY() - Up * (Puff.Bore.GetAxisY() | Up)).GetSafeNormal();
			World += Across * Puff.SwayCm * FMath::Sin(Puff.Age * 3.2f + Puff.Seed * UE_TWO_PI) * (1.0f - FMath::Exp(-Puff.Age / 0.3f));
		}
		const FVector Location = Camera.InverseTransformPosition(World);
		const float Width = FMath::Lerp(Puff.StartCm, Puff.EndCm, Grow);
		const int32 Instance = Smoke->AddInstance(Sprite(Location, Camera.InverseTransformVectorNoScale(Along), Width * Puff.Stretch, Width) * Camera, /*bWorldSpace=*/true);
		const float Opacity = Puff.Opacity * FMath::Min(Puff.Age / 0.05f, 1.0f) * FMath::Pow(1.0f - X, 1.5f);
		// Heat shimmer behind a fresh puff, gone by 100 ms.
		const float Shimmer = FMath::Max(0.0f, 1.0f - Puff.Age / 0.1f);
		// The material's texel grid, in texels along and across: a texel about 6 px on
		// screen at 1080p wherever the puff hangs (a fixed size in cm turned into big
		// squares near the eye), its size snapped to powers of two so the grid does
		// not swim as the puff drifts.
		const float TexelCm = FMath::Pow(2.0f, FMath::RoundToFloat(FMath::Log2(FMath::Max(Location.Size() * TexelPerCm, 0.05f))));
		Smoke->SetCustomData(Instance, { Opacity, Puff.Seed, Puff.Seed * 6.2831853f + Puff.Spin * Puff.Age, Puff.Tear, Shimmer,
			Width * Puff.Stretch / TexelCm, Width / TexelCm, 1.0f - X }, true);
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

	/**
	 * jam.Weapons.Smoke [budget | foot cling burst sv98]: the smoke's moments (all, or
	 * the ones named) as they happen in play, on
	 * Lvl_Jam_Range (HighResShot JamSmoke_*; needs a real RHI; run it with
	 * -UseFixedTimeStep -FPS=60 so the physics and the stills keep game time). Only the
	 * stand is staged - the pawn put somewhere at rest, a gun drawn and refilled, the
	 * last moment's smoke cleared, the hand put on the hold - the shots, the recoil, the
	 * arm and the smoke then run as in play:
	 * 1. on foot in the hall (gravity): a shotgun shot, a second one through its cloud;
	 * 2. holding on (cling) to the anchor block past the hall (zero-G): an SV-98 shot, a
	 *    second after the bolt; seen from above for the brake's side jets;
	 * 3. free in zero-G: a pistol burst of four;
	 * 4. free in zero-G: an SV-98 shot; for 2 s the pawn drifts off (a strip from the side),
	 *    then the stand sends it back at 800 cm/s, into the cloud and through it.
	 * First-person stills, and a witness camera with the eye marked by a green sphere and
	 * the muzzle at the shot by a red cross (a yellow line between them). After every
	 * first shot the log has every cloud's world position against its shot's muzzle and
	 * the eye, every 0.25 s for 2 s.
	 * "budget": on the jam.Weapons.Fx stand only, three shotgun shots, then ProfileGPU
	 * (the log has the GPU tree).
	 */
	void RunSmokeMoments(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AJamZeroGPawn* Pawn = Controller ? Cast<AJamZeroGPawn>(Controller->GetPawn()) : nullptr;
		UJamMuzzleFxComponent* Fx = Pawn ? Pawn->FindComponentByClass<UJamMuzzleFxComponent>() : nullptr;
		if (!Fx)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Smoke moments: needs the AJamZeroGPawn with its guns"));
			return;
		}
		// Relative to now: a step scheduled inside a step counts from that step.
		const auto After = [Pawn](float Seconds, TFunction<void()> Step)
		{
			FTimerHandle Handle;
			Pawn->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Pawn, MoveTemp(Step)), FMath::Max(Seconds, 0.0001f), false);
		};
		const auto Place = [Pawn, Controller](const FVector& Location, const FRotator& Look)
		{
			Pawn->Body->SetWorldLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
			Pawn->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Controller->SetControlRotation(Look);
		};
		// A gun drawn, every magazine full, no smoke left.
		const auto Arm = [Pawn, Fx](int32 Gun)
		{
			Pawn->Guns->UnlockWeapon(Gun);
			for (FJamWeapon& Weapon : Pawn->Guns->Weapons)
			{
				Weapon.Ammo = Weapon.MagazineSize;
			}
			Fx->ClearClouds();
		};
		const auto Eye = [Pawn]() { return Pawn->Camera->GetComponentLocation(); };
		const auto Still = [Pawn, Controller, Fx, Eye](const FString& Name)
		{
			UE_LOG(LogMindKore, Log, TEXT("Smoke moments: %s - eye %s, pawn %.0f cm/s;%s"), *Name, *Eye().ToCompactString(), Pawn->GetVelocity().Size(), *Fx->DescribeClouds());
			Fx->DumpClouds(Name);
			Controller->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=JamSmoke_%s"), *Name));
		};
		// The muzzle at the last shot (the witness marks it).
		TSharedRef<FVector> ShotMuzzle = MakeShared<FVector>();
		const auto Fire = [Pawn, ShotMuzzle, Still](const FString& Name)
		{
			*ShotMuzzle = Pawn->Guns->GetMuzzleTransform().GetLocation();
			const FVector Before = Pawn->GetVelocity();
			const bool bFired = Pawn->Fire();
			UE_LOG(LogMindKore, Log, TEXT("Smoke moments: %s %s (%s), muzzle %s (%s), pawn %s cm/s before, %s, %s"), *Name, bFired ? TEXT("fired") : TEXT("NOT FIRED"),
				*Pawn->Guns->Describe(), *ShotMuzzle->ToCompactString(),
				Pawn->Guns->ViewModel->DoesSocketExist(TEXT("Muzzle")) ? TEXT("the gun's Muzzle socket") : TEXT("NO Muzzle socket"), *Before.ToCompactString(), Pawn->IsInGravity() ? TEXT("gravity") : TEXT("zero-G"),
				Pawn->IsClinging() ? TEXT("holding on") : TEXT("free"));
			Still(Name + TEXT("_frame00"));
		};
		// Every cloud against its shot's muzzle and the eye, every 0.25 s for 2 s.
		const auto Track = [After, Fx, Eye](const FString& Name)
		{
			for (int32 Step = 1; Step <= 8; ++Step)
			{
				After(Step * 0.25f, [Fx, Eye, Name, Step]()
				{
					UE_LOG(LogMindKore, Log, TEXT("Smoke track %s +%.2f s: eye %s;%s"), *Name, Step * 0.25f, *Eye().ToCompactString(), *Fx->DescribeClouds());
				});
			}
		};
		// The 60 fps frames after a shot (the tunnel and the vortex ring).
		const auto Frames = [After, Still](const FString& Name)
		{
			for (const int32 Frame : { 1, 3, 8, 15, 30 })
			{
				After(Frame / 60.0f - 0.001f, [Still, Name, Frame]() { Still(FString::Printf(TEXT("%s_frame%02d"), *Name, Frame)); });
			}
		};
		// The witness: a still from From at At, the eye and the shot's muzzle marked; back to the eye two frames on.
		ACameraActor* Witness = World->SpawnActor<ACameraActor>();
		Witness->GetCameraComponent()->SetFieldOfView(90.0f);
		const auto Side = [World, Pawn, Controller, Witness, ShotMuzzle, Still, Eye, After](const FString& Name, const FVector& From, const FVector& At)
		{
			Witness->SetActorLocationAndRotation(From, (At - From).Rotation());
			Controller->SetViewTarget(Witness);
			DrawDebugSphere(World, Eye(), 12.0f, 12, FColor::Green, false, 0.035f);
			DrawDebugCrosshairs(World, *ShotMuzzle, FRotator::ZeroRotator, 25.0f, FColor::Red, false, 0.035f);
			DrawDebugLine(World, *ShotMuzzle, Eye(), FColor(255, 200, 0), false, 0.035f);
			Still(Name + TEXT("_witness"));
			After(0.03f, [Controller, Pawn]() { Controller->SetViewTarget(Pawn); });
		};

		if (Args.Contains(TEXT("room")) || Args.Contains(TEXT("roomspace")))
		{
			// The owner's screenshot (22.10): the RMB-93 in hand, a shot at the floor ~2.5 m ahead in Lvl_Jam_TestRoom
			// (lit, air, gravity); "roomspace": the same shot on Lvl_Jam_ZeroG into the black (vacuum), the pawn held
			// at the stand after the recoil (the stand's teleport, not play) so every still has the same framing.
			const bool bSpace = Args.Contains(TEXT("roomspace"));
			const FString Tag = bSpace ? TEXT("roomspace") : TEXT("room");
			FVector Stand = Pawn->Body->GetComponentLocation();
			FRotator Look(-25.0f, Controller->GetControlRotation().Yaw, 0.0f);
			if (bSpace)
			{
				if (TActorIterator<AJamLivingWall> Wall(World); Wall)
				{
					Stand = Wall->GetActorLocation() + Wall->GetFrontNormal() * 600.0f;
					Look = FRotator(-10.0f, (-Wall->GetFrontNormal().RotateAngleAxis(45.0f, FVector::UpVector)).Rotation().Yaw, 0.0f);
				}
			}
			const auto ArmRmb = [Pawn, Fx]()
			{
				UJamWeaponsComponent* Guns = Pawn->Guns;
				Guns->ApplyPickupWeapon(1, JamMakeRmb93(GetDefault<UJamWeaponsComponent>()->Weapons[1]));
				Guns->Scope->Aims[1] = JamMakeRmb93Aim(GetDefault<UJamScopeComponent>()->Aims[1]);
				Guns->UnlockWeapon(1);
				Guns->FinishSwitch();
				for (FJamWeapon& Weapon : Guns->Weapons)
				{
					Weapon.Ammo = Weapon.MagazineSize;
				}
				Fx->ClearClouds();
			};
			// Stable light for comparable stills: the hall's flicker held at its base.
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (FProperty* Flicker = It->GetClass()->FindPropertyByName(TEXT("FlickerIntensity")))
				{
					*Flicker->ContainerPtrToValuePtr<float>(*It) = 0.0f;
					if (FProperty* Stutter = It->GetClass()->FindPropertyByName(TEXT("StutterChancePerSecond")))
					{
						*Stutter->ContainerPtrToValuePtr<float>(*It) = 0.0f;
					}
				}
			}
			const auto Frame = [](const FRotator& View, const FVector& Origin, float AlongCm, float RightCm, float UpCm)
			{
				return Origin + FRotationMatrix(View).TransformVector(FVector(AlongCm, RightCm, UpCm));
			};
			const TSharedRef<FVector> At = MakeShared<FVector>(Stand);
			const auto Hold = [Place, At, Look, bSpace]() { if (bSpace) { Place(*At, Look); } };
			After(2.0f, [ArmRmb, Place, Pawn, At, Look, bSpace]() { if (!bSpace) { *At = Pawn->Body->GetComponentLocation(); } ArmRmb(); Place(*At, Look); });
			After(4.0f, [Pawn, Place, At, Look]() { Place(*At, Look); Pawn->Fire(); }); // the shaders' warm-up
			After(8.0f, [ArmRmb, Place, At, Look]() { ArmRmb(); Place(*At, Look); });
			// A: one shot, stills 0.05..4 s, witnesses from the side, the shell on the floor.
			After(9.0f, [Fire, Track, Still, Side, After, Eye, Frame, Look, Tag, Hold, Pawn]()
			{
				Fire(Tag + TEXT("_shot"));
				Track(Tag);
				const FVector E = Eye();
				const FRotator Flat(0.0f, Look.Yaw, 0.0f);
				for (const float T : { 0.05f, 0.2f, 0.5f, 1.0f, 2.0f, 4.0f })
				{
					After(T, [Still, Tag, T, Hold]() { Hold(); Still(FString::Printf(TEXT("%s_%04dms"), *Tag, FMath::RoundToInt(T * 1000.0f))); });
				}
				for (const float T : { 0.25f, 0.55f, 1.05f, 2.05f })
				{
					After(T, [Side, E, Frame, Flat, Tag, T]()
					{
						Side(FString::Printf(TEXT("%s_%04dms"), *Tag, FMath::RoundToInt(T * 1000.0f)), Frame(Flat, E, 170.0f, 300.0f, -20.0f), Frame(Flat, E, 170.0f, 0.0f, -100.0f));
					});
				}
				// The spent hull, close: where it lies (or flies) then.
				for (const float T : { 0.6f, 1.4f })
				{
					After(T + 0.05f, [Side, Pawn, Tag, T]()
					{
						const AActor* Case = Pawn->Guns->GetLastDebris();
						const FVector C = Case ? Case->GetActorLocation() : Pawn->GetActorLocation();
						UE_LOG(LogMindKore, Log, TEXT("Smoke moments: %s shell at %s"), *Tag, *C.ToCompactString());
						Side(FString::Printf(TEXT("%s_shell_%04dms"), *Tag, FMath::RoundToInt(T * 1000.0f)), C + FVector(-45.0f, -45.0f, 30.0f), C);
					});
				}
			});
			// B: a shot, a second through its cloud at 1.5 s (its frames), then settled.
			After(16.0f, [ArmRmb, Place, At, Look]() { ArmRmb(); Place(*At, Look); });
			After(17.0f, [Fire, Frames, Still, After, Tag, Hold]()
			{
				Fire(Tag + TEXT("_b_shot1"));
				After(1.5f, [Fire, Frames, Still, After, Tag, Hold]()
				{
					Hold();
					Fire(Tag + TEXT("_b_shot2"));
					Frames(Tag + TEXT("_b_shot2"));
					After(1.5f, [Still, Tag, Hold]() { Hold(); Still(Tag + TEXT("_b_shot2_settled_1500ms")); });
				});
			});
			After(21.5f, [Controller]() { Controller->ConsoleCommand(TEXT("jam.Fx.SmokeCost")); });
			After(22.0f, []() { UE_LOG(LogMindKore, Log, TEXT("Smoke moments: done")); });
			return;
		}

		if (Args.Contains(TEXT("budget")))
		{
			// The jam.Weapons.Fx stand (6 m before the living wall, looking at it) and the
			// steps of the budget check as before the smoke went to the world (three
			// shotgun shots, the first may still meet the draw), the stand restored before each.
			FVector Target = Pawn->GetActorLocation() + FVector(600.0f, 0.0f, 0.0f);
			FVector Stand = Pawn->GetActorLocation();
			if (TActorIterator<AJamLivingWall> Wall(World); Wall)
			{
				Target = Wall->GetActorLocation();
				Stand = Target + Wall->GetFrontNormal() * 600.0f;
			}
			const FRotator Look = (Target - Stand).Rotation();
			Place(Stand, Look);
			After(8.0f, [Pawn, Place, Stand, Look]() { Pawn->Guns->UnlockWeapon(1); Place(Stand, Look); });
			for (int32 Index = 0; Index < 3; ++Index)
			{
				After(8.5f + Index * 0.9f, [Pawn, Place, Stand, Look]() { Place(Stand, Look); Pawn->Fire(); });
			}
			After(11.0f, [Fx, Controller, Place, Stand, Look]()
			{
				Place(Stand, Look);
				UE_LOG(LogMindKore, Log, TEXT("Smoke moments: ProfileGPU with %d cloud(s) alive;%s"), Fx->GetCloudCount(), *Fx->DescribeClouds());
				Controller->ConsoleCommand(TEXT("ProfileGPU"));
			});
			After(11.5f, [Still]() { Still(TEXT("budget_three_shotgun_clouds")); });
			After(12.0f, [Controller]() { Controller->ConsoleCommand(TEXT("jam.Fx.SmokeCost")); });
			After(12.5f, []() { UE_LOG(LogMindKore, Log, TEXT("Smoke moments: done")); });
			return;
		}

		// Where the pawn stands at the start (the hall's floor) and how it looks: flat ahead.
		const FRotator Ahead(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
		TSharedRef<FVector> Foot = MakeShared<FVector>();
		// The witness's frame for a moment: Forward along the shot, Right of it, about the eye at the shot.
		const auto Frame = [](const FRotator& Look, const FVector& Origin, float AlongCm, float RightCm, float UpCm)
		{
			return Origin + FRotationMatrix(Look).TransformVector(FVector(AlongCm, RightCm, UpCm));
		};

		// Only the moments named (foot, cling, burst, sv98), or all of them.
		const auto Wants = [&Args](const TCHAR* Name) { return !Args.ContainsByPredicate([](const FString& Arg) { return Arg == TEXT("foot") || Arg == TEXT("cling") || Arg == TEXT("burst") || Arg == TEXT("sv98"); }) || Args.Contains(Name); };
		// 1. On foot in the hall: a shotgun shot, a second through its cloud.
		if (Wants(TEXT("foot")))
		{
			After(2.0f, [Arm]() { Arm(1); });
			After(3.9f, [Pawn, Foot]() { *Foot = Pawn->Body->GetComponentLocation(); });
			After(4.0f, [Pawn]() { Pawn->Fire(); }); // the shaders' warm-up
			After(6.0f, [Arm, Place, Foot, Ahead]() { Arm(1); Place(*Foot, Ahead); });
			After(7.0f, [Fire, Track, Frames, Still, Side, After, Eye, Frame, Ahead]()
			{
				Fire(TEXT("1_foot_shot1"));
				Track(TEXT("1_foot"));
				const FVector E = Eye();
				After(0.3f, [Still]() { Still(TEXT("1_foot_shot1_0300ms")); });
				After(0.6f, [Side, E, Frame, Ahead]() { Side(TEXT("1_foot_shot1_0600ms"), Frame(Ahead, E, 150.0f, -380.0f, -30.0f), Frame(Ahead, E, 150.0f, 0.0f, -5.0f)); });
				After(1.0f, [Still]() { Still(TEXT("1_foot_shot1_1000ms")); });
				After(1.5f, [Fire, Frames, Still, Side, After, E, Frame, Ahead]()
				{
					Fire(TEXT("1_foot_shot2"));
					Frames(TEXT("1_foot_shot2"));
					After(1.5f, [Still]() { Still(TEXT("1_foot_shot2_settled_1500ms")); });
					After(1.6f, [Side, E, Frame, Ahead]() { Side(TEXT("1_foot_shot2_settled_1600ms"), Frame(Ahead, E, 150.0f, -380.0f, -30.0f), Frame(Ahead, E, 150.0f, 0.0f, -5.0f)); });
				});
			});
		}

		// 2. Holding on to the anchor block past the hall (zero-G): an SV-98 shot, a second after the bolt.
		// Lvl_Jam_Range's Anchor_A (Scripts/build_range.py), the static block nearest its place.
		UStaticMeshComponent* Block = nullptr;
		const FVector AnchorA(3400.0f, -300.0f, 600.0f);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(It->GetRootComponent());
			if (Mesh && !Mesh->IsSimulatingPhysics() && Mesh->GetCollisionEnabled() != ECollisionEnabled::NoCollision
				&& Mesh->Bounds.SphereRadius > 100.0f && Mesh->Bounds.SphereRadius < 300.0f && FVector::Dist(Mesh->Bounds.Origin, AnchorA) < 300.0f
				&& (!Block || FVector::Dist(Mesh->Bounds.Origin, AnchorA) < FVector::Dist(Block->Bounds.Origin, AnchorA)))
			{
				Block = Mesh;
			}
		}
		if (!Block)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Smoke moments: no anchor block near %s (Lvl_Jam_Range's Anchor_A); the zero-G moments are skipped"), *AnchorA.ToCompactString());
			After(12.0f, []() { UE_LOG(LogMindKore, Log, TEXT("Smoke moments: done")); });
			return;
		}
		// The hand on the block's +Y face, the body an arm out from it, looking along +X.
		const FBox Box = Block->Bounds.GetBox();
		const FVector Hand(Box.GetCenter().X, Box.Max.Y, Box.GetCenter().Z + 30.0f);
		const FRotator East(0.0f, 0.0f, 0.0f);
		if (Wants(TEXT("cling")))
		{
			After(12.0f, [Arm]() { Arm(2); });
			After(13.0f, [Pawn, Place, Block, Hand, East]()
			{
				Place(Hand + FVector(0.0f, 65.0f, -40.0f), East);
				UE_LOG(LogMindKore, Log, TEXT("Smoke moments: 2_cling %s %s"), Pawn->Cling(Block, Hand) ? TEXT("holding on to") : TEXT("NOT holding on to"), *GetNameSafe(Block->GetOwner()));
			});
			After(15.0f, [Fire, Track, Frames, Still, Side, After, Eye, Frame, East, Pawn, Controller, ShotMuzzle]()
			{
				Fire(TEXT("2_cling_shot1"));
				Track(TEXT("2_cling"));
				const FVector E = Eye();
				// The arm swings the body after the kick: the second shot is aimed at the cloud
				// (a little ahead of the first shot's muzzle), as a player would aim it.
				const FVector Cloud = *ShotMuzzle + FVector(25.0f, 0.0f, 0.0f);
				After(1.4f, [Controller, Eye, Cloud]() { Controller->SetControlRotation((Cloud - Eye()).Rotation()); });
				After(0.15f, [Still]() { Still(TEXT("2_cling_shot1_0150ms")); });
				After(0.3f, [Still]() { Still(TEXT("2_cling_shot1_0300ms")); });
				// From above: the brake's side jets across the shot.
				After(0.45f, [Side, E, Frame, East]() { Side(TEXT("2_cling_shot1_brake_0450ms"), Frame(East, E, 70.0f, -5.0f, 260.0f), Frame(East, E, 70.0f, 0.0f, 0.0f)); });
				After(1.0f, [Still]() { Still(TEXT("2_cling_shot1_1000ms")); });
				After(1.2f, [Side, E, Frame, East]() { Side(TEXT("2_cling_shot1_1200ms"), Frame(East, E, 100.0f, 420.0f, 60.0f), Frame(East, E, 60.0f, 0.0f, -10.0f)); });
				After(1.8f, [Fire, Frames, Still, Side, After, E, Frame, East, Pawn]()
				{
					UE_LOG(LogMindKore, Log, TEXT("Smoke moments: 2_cling_shot2 aimed along %s"), *Pawn->Camera->GetForwardVector().ToCompactString());
					Fire(TEXT("2_cling_shot2"));
					Frames(TEXT("2_cling_shot2"));
					After(1.5f, [Still]() { Still(TEXT("2_cling_shot2_settled_1500ms")); });
					After(1.6f, [Side, E, Frame, East]() { Side(TEXT("2_cling_shot2_settled_1600ms"), Frame(East, E, 100.0f, 420.0f, 60.0f), Frame(East, E, 60.0f, 0.0f, -10.0f)); });
					After(2.0f, [Pawn]() { Pawn->LetGoCling(); });
				});
			});
		}

		// 3. Free in zero-G past the hall, facing -Y (into the open): a pistol burst of four.
		const FRotator South(0.0f, -90.0f, 0.0f);
		const FVector Open(3700.0f, 300.0f, 700.0f);
		if (Wants(TEXT("burst")))
		{
			After(22.0f, [Arm]() { Arm(0); });
			After(23.0f, [Place, Open, South]() { Place(Open, South); });
			After(25.0f, [Fire, Track, Still, Side, After, Eye, Frame, South, Place, Open]()
			{
				Place(Open, South);
				const FVector E = Eye();
				Fire(TEXT("3_burst_shot1"));
				Track(TEXT("3_burst"));
				for (int32 Shot = 2; Shot <= 4; ++Shot)
				{
					After((Shot - 1) * 0.18f, [Fire, Shot]() { Fire(FString::Printf(TEXT("3_burst_shot%d"), Shot)); });
				}
				After(0.65f, [Still]() { Still(TEXT("3_burst_0650ms")); });
				After(1.5f, [Still]() { Still(TEXT("3_burst_1500ms")); });
				After(1.6f, [Side, E, Frame, South]() { Side(TEXT("3_burst_1600ms"), Frame(South, E, -100.0f, -650.0f, 80.0f), Frame(South, E, -100.0f, 0.0f, 0.0f)); });
				After(2.5f, [Still]() { Still(TEXT("3_burst_2500ms")); });
				After(2.6f, [Side, E, Frame, South]() { Side(TEXT("3_burst_2600ms"), Frame(South, E, -100.0f, -650.0f, 80.0f), Frame(South, E, -100.0f, 0.0f, 0.0f)); });
			});
		}

		// 4. Free in zero-G: an SV-98 shot, the pawn drifts off; then back into the cloud.
		const FVector Open2(3700.0f, -100.0f, 700.0f);
		if (Wants(TEXT("sv98")))
		{
			After(31.0f, [Arm]() { Arm(2); });
			After(32.0f, [Place, Open2, South]() { Place(Open2, South); });
			After(34.0f, [Fire, Track, Still, Side, After, Eye, Frame, South, Place, Open2, Pawn, ShotMuzzle]()
			{
				Place(Open2, South);
				const FVector E = Eye();
				Fire(TEXT("4_sv98_shot"));
				Track(TEXT("4_sv98"));
				// The strip: from the side, far enough to hold the cloud and the pawn 14 m off.
				for (int32 Step = 1; Step <= 8; ++Step)
				{
					After(Step * 0.25f, [Side, E, Frame, South, Step]()
					{
						Side(FString::Printf(TEXT("4_sv98_strip_%04dms"), Step * 250), Frame(South, E, -650.0f, -1000.0f, 150.0f), Frame(South, E, -650.0f, 0.0f, 0.0f));
					});
				}
				for (const float At : { 0.12f, 0.62f, 1.12f, 2.12f })
				{
					After(At, [Still, At]() { Still(FString::Printf(TEXT("4_sv98_%04dms"), FMath::RoundToInt(At * 1000.0f))); });
				}
				// Back toward the shot's muzzle at 800 cm/s (the stand's push), stills as the eye
				// comes within 1.5 m of it, passes and leaves.
				After(2.2f, [Pawn, After, Still, ShotMuzzle, Eye]()
				{
					const FVector Back = *ShotMuzzle - Eye();
					Pawn->Body->SetPhysicsLinearVelocity(Back.GetSafeNormal() * 800.0f);
					UE_LOG(LogMindKore, Log, TEXT("Smoke moments: 4_sv98 sent back toward the shot's muzzle, %.0f cm off, at 800 cm/s"), Back.Size());
					for (const float Before : { 150.0f, 80.0f, 40.0f, 0.0f, -30.0f, -60.0f, -120.0f })
					{
						After((Back.Size() - Before) / 800.0f, [Still, Before]()
						{
							Still(FString::Printf(TEXT("4_sv98_back_%s%03dcm"), Before < 0.0f ? TEXT("past") : TEXT("to"), FMath::RoundToInt(FMath::Abs(Before))));
						});
					}
				});
			});
		}
		After(42.0f, []() { UE_LOG(LogMindKore, Log, TEXT("Smoke moments: done")); });
	}

	FAutoConsoleCommandWithWorldAndArgs GSmokeMoments(
		TEXT("jam.Weapons.Smoke"),
		TEXT("Stills of the smoke's moments in play on Lvl_Jam_Range (run with -UseFixedTimeStep -FPS=60): on foot, holding on, a pistol burst, an SV-98 shot drifting off and back (or only those named: foot, cling, burst, sv98). 'budget': three shotgun clouds on the jam.Weapons.Fx stand, then ProfileGPU."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunSmokeMoments));

	FAutoConsoleCommandWithWorld GClearSmoke(
		TEXT("jam.Fx.ClearSmoke"),
		TEXT("Remove every muzzle smoke cloud of the player (stills)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			APlayerController* Controller = World->GetFirstPlayerController();
			APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
			if (UJamMuzzleFxComponent* Fx = Pawn ? Pawn->FindComponentByClass<UJamMuzzleFxComponent>() : nullptr)
			{
				Fx->ClearClouds();
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GMuzzleFxShots(
		TEXT("jam.Weapons.Fx"),
		TEXT("First-person stills of each gun's muzzle flash frame by frame (60 fps frames 0-12 at time dilation 0.01), its smoke over 3 s and two more bursts (HighResShot; needs a real RHI). 'space': look away from the wall."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunMuzzleFxShots));
}
