#include "OSMuzzleFx.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/HeterogeneousVolumeComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "OSFlyer.h"
#include "OSGravity.h"
#include "OSLivingWall.h"
#include "OSWeapons.h"
#include "OSZeroGPawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MindKore.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Algo/MaxElement.h"

namespace
{
	TAutoConsoleVariable<int32> CVarVolumetricSmoke(
		TEXT("os.Fx.VolumetricSmoke"), 1,
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

UOSMuzzleFxComponent::UOSMuzzleFxComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	SpriteMesh = Plane.Object;

	// Pistol: a small tight pop, a couple of grains, a wisp.
	FOSMuzzleFx Pistol;
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
	FOSMuzzleFx Shotgun;
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
	FOSMuzzleFx Sniper;
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
	// Volumetric clouds: a thin pistol puff, a big torn shotgun cloud that shoves the
	// older ones, the SV-98's brake jets.
	Pistol.CloudCm = 55.0f;
	Pistol.CloudVoxels = 12;
	Pistol.CloudThrowCm = 260.0f;
	Pistol.CloudExtinction = 0.03f;
	Pistol.TunnelCm = 6.0f;
	Shotgun.CloudCm = 130.0f;
	Shotgun.CloudVoxels = 26;
	Shotgun.CloudThrowCm = 280.0f;
	Shotgun.CloudExtinction = 0.018f;
	Shotgun.TunnelCm = 12.0f;
	Shotgun.ShoveCm = 90.0f;
	Sniper.CloudCm = 90.0f;
	Sniper.CloudVoxels = 18;
	Sniper.CloudThrowCm = 300.0f;
	Sniper.CloudExtinction = 0.012f;
	Sniper.TunnelCm = 8.0f;
	Guns = { Pistol, Shotgun, Sniper };
}

void UOSMuzzleFxComponent::BeginPlay()
{
	Super::BeginPlay();
	UCameraComponent* Camera = GetOwner()->FindComponentByClass<UCameraComponent>();
	if (!Camera)
	{
		return;
	}
	// The flash rides the gun in view as the weapons component poses it this frame (TickComponent reads
	// FlashGunMesh's transform): that component ticks first, as for the scope.
	if (UOSWeaponsComponent* Weapons = GetOwner()->FindComponentByClass<UOSWeaponsComponent>())
	{
		AddTickPrerequisiteComponent(Weapons);
	}
	View = Camera;
	// The flash's sprites ride the camera, drawn as first person with the gun (UOSScopeComponent
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
	Smoke = Make(TEXT("MuzzleSmoke"), SmokeMaterial.LoadSynchronous(), 7, nullptr);
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

void UOSMuzzleFxComponent::Shot(int32 Weapon, const FTransform& Muzzle, const USceneComponent* GunMesh, float FlashScale, float SmokeScale)
{
	if (!Smoke || !Guns.IsValidIndex(Weapon))
	{
		return;
	}
	const FOSMuzzleFx& Gun = Guns[Weapon];
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
	const auto Lobe = [this, FlashScale](const FVector& Direction, float ReachCm, float SizeCm, float Stretch, float Brightness, float Heat)
	{
		FFlashBit& Bit = FlashBits.AddDefaulted_GetRef();
		Bit.Direction = Direction;
		Bit.ReachCm = ReachCm * FlashScale;
		Bit.SizeCm = SizeCm * FMath::Sqrt(FlashScale);
		Bit.Stretch = Stretch;
		Bit.Brightness = Brightness * FlashScale;
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
	for (int32 Index = 0; Index < FMath::RoundToInt32(Gun.Sparks * FlashScale); ++Index)
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
	// before the kick (UOSWeaponsComponent::Fire calls this before the recoil).
	const FVector GunVelocity = GetOwner()->GetVelocity();
	const FVector Bore = Muzzle.GetRotation().GetAxisX();
	if (CVarVolumetricSmoke.GetValueOnGameThread() != 0 && CloudMaterial.LoadSynchronous())
	{
		// The round leaves from the eye along the view: through every older cloud in its
		// way it bores a tunnel and sheds a vortex ring; a shotgun blast out of the muzzle
		// shoves the older clouds ahead of it on.
		AddWake(View->GetComponentLocation(), View->GetForwardVector(), Gun.TunnelCm, 90.0f, nullptr);
		if (Gun.ShoveCm > 0.0f)
		{
			for (FCloud& Cloud : Clouds)
			{
				const float Distance = FVector::Dist(Cloud.Centre, Muzzle.GetLocation());
				if (Cloud.Age >= 0.0f && Distance < 400.0f && ((Cloud.Centre - Muzzle.GetLocation()) | Bore) > 0.0f)
				{
					Cloud.Velocity += Bore * Gun.ShoveCm * (1.0f - Distance / 400.0f);
				}
			}
		}
		SpawnCloud(Weapon, Muzzle, GunVelocity);
		return;
	}
	const int32 PuffCount = FMath::RoundToInt32(Gun.SmokePuffs * SmokeScale);
	const int32 Ports = FMath::RoundToInt(PuffCount * Gun.SmokeBrake);
	for (int32 Index = 0; Index < PuffCount; ++Index)
	{
		FPuff& Puff = Puffs.AddDefaulted_GetRef();
		Puff.Origin = Muzzle.GetLocation();
		Puff.Bore = Muzzle.GetRotation();
		Puff.GunVelocity = GunVelocity;
		// ponytail: air or vacuum is decided at the shot, a puff crossing a zone's edge keeps it.
		Puff.AirDragSeconds = OSAirZoneAt(GetWorld(), Muzzle.GetLocation()) ? AirDragSeconds : 0.0f;
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
		Puff.Opacity = Gun.SmokeOpacity * FMath::Min(SmokeScale, 1.0f);
		Puff.Life = Gun.SmokeSeconds * FMath::FRandRange(0.75f, 1.25f);
		Puff.Seed = FMath::FRand();
		Puff.Spin = FMath::FRandRange(-0.4f, 0.4f);
	}
	if (Puffs.Num() > MaxPuffs)
	{
		Puffs.RemoveAt(0, Puffs.Num() - MaxPuffs);
	}
}

void UOSMuzzleFxComponent::PortPuffs(const FOSPortSmoke& Vent, const USceneComponent& Action, int32 Count, float ThrowCm, float AgeSpread)
{
	if (!Smoke || Count <= 0 || Vent.Ports.IsEmpty())
	{
		return;
	}
	// Air and gravity once for the lot (a frame's seep or a shot's puff).
	const FTransform Shroud = Action.GetComponentTransform();
	const FVector GunVelocity = GetOwner()->GetVelocity();
	const bool bAir = OSAirZoneAt(GetWorld(), Shroud.GetLocation()) != nullptr;
	const FVector Lift = bAir ? -OSGravityAt(GetWorld(), Shroud.GetLocation()).Acceleration * Vent.RiseShare : FVector::ZeroVector;
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
	}
	if (Puffs.Num() > MaxPuffs)
	{
		Puffs.RemoveAt(0, Puffs.Num() - MaxPuffs);
	}
}

FTransform UOSMuzzleFxComponent::Sprite(const FVector& Location, const FVector& XAxis, float LengthCm, float WidthCm)
{
	// The engine plane faces +Z and is 100 units across; the camera is at the origin.
	// Seen end-on, a stretched sprite has no length to show: any in-plane axis will do.
	const FVector Along = FMath::Abs(Location.GetSafeNormal() | XAxis) > 0.98f ? FVector::UpVector : XAxis;
	return FTransform(FRotationMatrix::MakeFromZX(-Location, Along).ToQuat(), Location, FVector(LengthCm / 100.0f, WidthCm / 100.0f, 1.0f));
}

void UOSMuzzleFxComponent::ClearClouds()
{
	for (FCloud& Cloud : Clouds)
	{
		Cloud.Age = -1.0f;
		Cloud.Volume->SetVisibility(false);
	}
}

FString UOSMuzzleFxComponent::DescribeClouds() const
{
	FString Line;
	const FVector Eye = View->GetComponentLocation();
	for (const FCloud& Cloud : Clouds)
	{
		if (Cloud.Age >= 0.0f)
		{
			// Its speed toward the eye: positive would be the cloud coming after the shooter.
			const FVector ToEye = (Eye - Cloud.Centre).GetSafeNormal();
			const FVector Velocity = Cloud.Carried + Cloud.Velocity;
			Line += FString::Printf(TEXT(" [gun %d age %.2f at %s, its muzzle at the shot %s (%.0f cm off), %.0f cm from the eye, %s cm/s (from the gun %s, throw and drift %.0f), %.1f cm/s toward the eye, %d wake(s)]"),
				Cloud.Gun, Cloud.Age, *Cloud.Centre.ToCompactString(), *Cloud.Muzzle.ToCompactString(), FVector::Dist(Cloud.Centre, Cloud.Muzzle),
				FVector::Dist(Cloud.Centre, Eye), *Velocity.ToCompactString(), *Cloud.Carried.ToCompactString(), Cloud.Velocity.Size(), Velocity | ToEye, Cloud.Wakes.Num());
		}
	}
	return Line;
}

int32 UOSMuzzleFxComponent::GetCloudCount() const
{
	int32 Count = 0;
	for (const FCloud& Cloud : Clouds)
	{
		Count += Cloud.Age >= 0.0f ? 1 : 0;
	}
	return Count;
}

void UOSMuzzleFxComponent::SpawnCloud(int32 Weapon, const FTransform& Muzzle, const FVector& GunVelocity)
{
	const FOSMuzzleFx& Gun = Guns[Weapon];
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

		Volume->RegisterComponent();
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(CloudMaterial.Get(), this);
		Volume->SetMaterial(0, Material);
		Cloud->Volume = Volume;
		Cloud->Material = Material;
		CloudObjects.Add(Volume);
		CloudObjects.Add(Material);
	}
	if (!Cloud)
	{
		Cloud = Algo::MaxElementBy(Clouds, [](const FCloud& Each) { return Each.Age; });
	}
	const FQuat Bore = Muzzle.GetRotation();
	Cloud->Gun = Weapon;
	Cloud->Centre = Muzzle.GetLocation();
	Cloud->Muzzle = Cloud->Centre;
	// The material draws the puffs out along the cloud's X: the shot, or across it for a brake's side jets.
	Cloud->Basis = Gun.SmokeBrake > 0.0f ? Bore * FQuat(FVector::UpVector, UE_HALF_PI) : Bore;
	// Born where the gas leaves the gun, with the gun's velocity at the shot and its throw
	// out of the muzzle (a brake turns most of it out of the side ports, both ways: little
	// is left forward). The throw dies down to a slow drift as the gas spreads; the gun's
	// velocity is the centre of mass's and stays in a vacuum - only air takes it
	// (TickClouds). Nothing ties it to the shooter: kicked back by the recoil, the shooter
	// drifts away from it.
	Cloud->Carried = GunVelocity;
	Cloud->Velocity = Bore.GetAxisX() * Gun.CloudThrowCm * (1.0f - Gun.SmokeBrake);
	Cloud->DriftVelocity = Bore.RotateVector(FVector(1.0f, FMath::FRandRange(-0.3f, 0.3f), FMath::FRandRange(-0.3f, 0.3f))) * Gun.SmokeDriftCm;
	Cloud->Age = 0.0f;
	Cloud->Seed = FMath::FRand();
	Cloud->Wakes.Reset();
	Cloud->PuffThrow.Reset();
	const int32 Count = FMath::Min(Gun.SmokePuffs, 8);
	const int32 Ports = FMath::RoundToInt(Count * Gun.SmokeBrake);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		// As the cards: out of a brake's side ports (as far as the box allows) and on out of its bore, or every way.
		float ReachCm = Index < Ports ? Gun.CloudCm * 0.3f : FMath::Min(Gun.SmokeSpreadCm, Gun.CloudCm * 0.2f) * FMath::FRandRange(0.4f, 1.0f);
		const FVector Direction = Index < Ports ? BrakePort(Index, Ports, ReachCm) : BoreGas(Ports > 0);
		const FVector Reach = Cloud->Basis.UnrotateVector(Bore.RotateVector(Direction * ReachCm));
		// Puffs a little fuller than the cards.
		Cloud->PuffThrow.Emplace(float(Reach.X), float(Reach.Y), float(Reach.Z), FMath::Min(Gun.SmokeEndCm * 0.75f, Gun.CloudCm * 0.2f) * FMath::FRandRange(0.7f, 1.0f));
	}
	Cloud->Volume->SetVolumeResolution(FIntVector(Gun.CloudVoxels));
	Cloud->Volume->SetVisibility(true);
}

void UOSMuzzleFxComponent::AddWake(const FVector& Origin, const FVector& Direction, float RadiusCm, float RingSpeed, const FCloud* Except)
{
	for (FCloud& Cloud : Clouds)
	{
		if (Cloud.Age < 0.0f || &Cloud == Except)
		{
			continue;
		}
		// In cloud space; the line counts if it comes within half the box of the centre.
		const FVector Local = Cloud.Basis.UnrotateVector(Origin - Cloud.Centre);
		const FVector Along = Cloud.Basis.UnrotateVector(Direction).GetSafeNormal();
		const FVector Nearest = Local - Along * (Local | Along);
		if (Nearest.Size() > Guns[Cloud.Gun].CloudCm * 0.5f)
		{
			continue;
		}
		if (Cloud.Wakes.Num() >= 3)
		{
			Cloud.Wakes.RemoveAt(0);
		}
		FWake& Wake = Cloud.Wakes.AddDefaulted_GetRef();
		// Anchored where the line passes the centre, so a ring starts in the heart of the cloud.
		Wake.Origin = Nearest;
		Wake.Direction = Along;
		Wake.RadiusCm = RadiusCm;
		Wake.RingSpeed = RingSpeed;
	}
}

void UOSMuzzleFxComponent::TickClouds(float DeltaTime)
{
	const FVector Eye = View->GetComponentLocation();
	const FVector EyeStep = Eye - LastEye;
	LastEye = Eye;
	for (FCloud& Cloud : Clouds)
	{
		if (Cloud.Age < 0.0f)
		{
			continue;
		}
		Cloud.Age += DeltaTime;
		if (Cloud.Age >= CloudSeconds)
		{
			Cloud.Age = -1.0f;
			Cloud.Volume->SetVisibility(false);
			continue;
		}
		const FOSMuzzleFx& Gun = Guns[Cloud.Gun];
		Cloud.Velocity = Cloud.DriftVelocity + (Cloud.Velocity - Cloud.DriftVelocity) * FMath::Exp(-DeltaTime / GasSlowSeconds);
		if (OSAirZoneAt(GetWorld(), Cloud.Centre))
		{
			Cloud.Carried *= FMath::Exp(-DeltaTime / AirDragSeconds);
		}
		Cloud.Centre += (Cloud.Carried + Cloud.Velocity) * DeltaTime;
		const float Half = Gun.CloudCm * 0.5f;
		// The pawn flying through parts it: a wake along its path (and the voxels by the eye fade).
		const FVector EyeLocal = Cloud.Basis.UnrotateVector(Eye - Cloud.Centre);
		if (EyeLocal.GetAbsMax() < Half && EyeStep.Size() > 1.0f && EyeStep.Size() < 200.0f
			&& (Cloud.Wakes.IsEmpty() || Cloud.Wakes.Last().Age > 0.15f))
		{
			AddWake(Eye, EyeStep.GetSafeNormal(), 18.0f, 0.0f, nullptr);
		}
		for (FWake& Wake : Cloud.Wakes)
		{
			Wake.Age += DeltaTime;
		}
		Cloud.Wakes.RemoveAll([](const FWake& Wake) { return Wake.Age > 2.5f; });

		const float Throw = 1.0f - FMath::Exp(-Cloud.Age / 0.12f);
		const float Grow = 1.0f - FMath::Exp(-Cloud.Age / 0.15f);
		const float X = Cloud.Age / CloudSeconds;
		// Fades in (squared, over 0.4 s) as it leaves the muzzle: a fresh cloud half a metre
		// from the eye must not fill the frame; it comes up as it spreads.
		const float Fade = FMath::Square(FMath::Min(Cloud.Age / 0.4f, 1.0f)) * (X < 0.6f ? 1.0f : FMath::Square((1.0f - X) / 0.4f));
		UMaterialInstanceDynamic* Material = Cloud.Material;
		Material->SetVectorParameterValue(TEXT("CloudCentre"), FLinearColor(FVector3f(Cloud.Centre)));
		Material->SetVectorParameterValue(TEXT("CloudX"), FLinearColor(FVector3f(Cloud.Basis.GetAxisX())));
		Material->SetVectorParameterValue(TEXT("CloudY"), FLinearColor(FVector3f(Cloud.Basis.GetAxisY())));
		Material->SetVectorParameterValue(TEXT("CloudZ"), FLinearColor(FVector3f(Cloud.Basis.GetAxisZ())));
		Material->SetScalarParameterValue(TEXT("VoxelCm"), Gun.CloudCm / Gun.CloudVoxels);
		Material->SetScalarParameterValue(TEXT("HalfCm"), Half);
		Material->SetScalarParameterValue(TEXT("Fade"), Fade);
		Material->SetScalarParameterValue(TEXT("Seed"), Cloud.Seed);
		Material->SetScalarParameterValue(TEXT("Tear"), Gun.SmokeTear);
		Material->SetScalarParameterValue(TEXT("Stretch"), Gun.SmokeStretch);
		Material->SetScalarParameterValue(TEXT("Extinction"), Gun.CloudExtinction);
		Material->SetVectorParameterValue(TEXT("Eye"), FLinearColor(EyeLocal.X, EyeLocal.Y, EyeLocal.Z, EyeClearCm));
		for (int32 Index = 0; Index < 8; ++Index)
		{
			FLinearColor Puff(0.0f, 0.0f, 0.0f, 0.0f);
			if (Cloud.PuffThrow.IsValidIndex(Index))
			{
				const FVector4f& P = Cloud.PuffThrow[Index];
				Puff = FLinearColor(P.X * Throw, P.Y * Throw, P.Z * Throw, FMath::Lerp(Gun.SmokeStartCm * 0.5f, P.W, Grow));
			}
			Material->SetVectorParameterValue(*FString::Printf(TEXT("Puff%d"), Index), Puff);
		}
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const FWake* Wake = Cloud.Wakes.IsValidIndex(Index) ? &Cloud.Wakes[Index] : nullptr;
			Material->SetVectorParameterValue(*FString::Printf(TEXT("WakeA%d"), Index),
				Wake ? FLinearColor(Wake->Origin.X, Wake->Origin.Y, Wake->Origin.Z, Wake->RadiusCm) : FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
			Material->SetVectorParameterValue(*FString::Printf(TEXT("WakeB%d"), Index),
				Wake ? FLinearColor(Wake->Direction.X, Wake->Direction.Y, Wake->Direction.Z, Wake->Age) : FLinearColor(1.0f, 0.0f, 0.0f, 0.0f));
			Material->SetScalarParameterValue(*FString::Printf(TEXT("WakeRing%d"), Index), Wake ? Wake->RingSpeed : 0.0f);
		}
		// The volume box: CloudVoxels units a side, scaled to CloudCm.
		Cloud.Volume->SetWorldTransform(FTransform(Cloud.Basis, Cloud.Centre, FVector(Gun.CloudCm / Gun.CloudVoxels)));
	}
	// A flyer diving through drags a wake with a ring after it: the swirl of its wings.
	if (GetCloudCount() > 0)
	{
		for (TActorIterator<AOSFlyer> Flyer(GetWorld()); Flyer; ++Flyer)
		{
			const FVector Velocity = Flyer->GetVelocity();
			if (Velocity.SizeSquared() > 100.0f && FMath::FRand() < DeltaTime * 6.0f)
			{
				AddWake(Flyer->GetActorLocation(), Velocity.GetSafeNormal(), 25.0f, 40.0f, nullptr);
			}
		}
	}
}

namespace
{
	/** A smoke texel's size per cm from the eye: ~6 px of 1920 at the bodycam's FOV 134 (2 tan 67 deg = 4.71 view widths per distance). */
	constexpr float TexelPerCm = 6.0f / 1920.0f * 4.71f;
}

void UOSMuzzleFxComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Smoke)
	{
		return;
	}
	TickClouds(DeltaTime);

	// The flash: white-hot for its peak, a fast fall, then a dim cooling glow.
	if (Flash->GetInstanceCount() > 0)
	{
		Flash->ClearInstances();
		Light->SetVisibility(false);
	}
	if (Guns.IsValidIndex(FlashGun))
	{
		const FOSMuzzleFx& Gun = Guns[FlashGun];
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
		const FVector World = Puff.Origin + Carried + Risen + Puff.Bore.RotateVector(Puff.Direction * (Puff.SpreadCm * Throw + Puff.DriftCm * Puff.Age)
			- FVector(Puff.BackCm * Throw, 0.0f, 0.0f));
		const FVector Location = Camera.InverseTransformPosition(World);
		const float Width = FMath::Lerp(Puff.StartCm, Puff.EndCm, Grow);
		// A wisp drawn out along the shot at the time (or its brake jet).
		const int32 Instance = Smoke->AddInstance(Sprite(Location, Camera.InverseTransformVectorNoScale(Puff.Bore.RotateVector(Puff.Along)), Width * Puff.Stretch, Width) * Camera, /*bWorldSpace=*/true);
		const float Opacity = Puff.Opacity * FMath::Min(Puff.Age / 0.05f, 1.0f) * FMath::Pow(1.0f - X, 1.5f);
		// Heat shimmer behind a fresh puff, gone by 100 ms.
		const float Shimmer = FMath::Max(0.0f, 1.0f - Puff.Age / 0.1f);
		// The material's texel grid, in texels along and across: a texel about 6 px on
		// screen at 1080p wherever the puff hangs (a fixed size in cm turned into big
		// squares near the eye), its size snapped to powers of two so the grid does
		// not swim as the puff drifts.
		const float TexelCm = FMath::Pow(2.0f, FMath::RoundToFloat(FMath::Log2(FMath::Max(Location.Size() * TexelPerCm, 0.05f))));
		Smoke->SetCustomData(Instance, { Opacity, Puff.Seed, Puff.Seed * 6.2831853f + Puff.Spin * Puff.Age, Puff.Tear, Shimmer,
			Width * Puff.Stretch / TexelCm, Width / TexelCm }, true);
	}
}

namespace
{
	/**
	 * os.Weapons.Fx [space]: first-person stills of each gun's flash and smoke
	 * (HighResShot; needs a real RHI). The zero-G pawn is held 6 m in front of a
	 * living wall as in os.Weapons.Shots, looking at it - or, with "space", 45
	 * degrees to its right into the black (the smoke against empty space). Per gun: a warm-up shot (shaders), the gun at rest, then
	 * a shot at time dilation 0.01 caught at 60 fps frames 0, 1, 2, 3, 5, 8, 12
	 * (+0.008..+0.208 s; a hitch on the shot frame cannot skip them), the smoke at
	 * +0.5, 1, 2 and 3 s, then the first frame of two more shots (each burst differs).
	 */
	void RunMuzzleFxShots(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AOSZeroGPawn* Pawn = Controller ? Cast<AOSZeroGPawn>(Controller->GetPawn()) : nullptr;
		if (!Pawn)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Muzzle fx shots: needs the AOSZeroGPawn"));
			return;
		}
		FVector Target = Pawn->GetActorLocation() + FVector(600.0f, 0.0f, 0.0f);
		FVector Stand = Pawn->GetActorLocation();
		if (TActorIterator<AOSLivingWall> Wall(World); Wall)
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
	 * os.Weapons.Smoke [budget | foot cling burst sv98]: the smoke's moments (all, or
	 * the ones named) as they happen in play, on
	 * Lvl_OS_Range (HighResShot OSSmoke_*; needs a real RHI; run it with
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
	 * "budget": on the os.Weapons.Fx stand only, three shotgun shots, then ProfileGPU
	 * (the log has the GPU tree).
	 */
	void RunSmokeMoments(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AOSZeroGPawn* Pawn = Controller ? Cast<AOSZeroGPawn>(Controller->GetPawn()) : nullptr;
		UOSMuzzleFxComponent* Fx = Pawn ? Pawn->FindComponentByClass<UOSMuzzleFxComponent>() : nullptr;
		if (!Fx)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Smoke moments: needs the AOSZeroGPawn with its guns"));
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
			for (FOSWeapon& Weapon : Pawn->Guns->Weapons)
			{
				Weapon.Ammo = Weapon.MagazineSize;
			}
			Fx->ClearClouds();
		};
		const auto Eye = [Pawn]() { return Pawn->Camera->GetComponentLocation(); };
		const auto Still = [Pawn, Controller, Fx, Eye](const FString& Name)
		{
			UE_LOG(LogMindKore, Log, TEXT("Smoke moments: %s - eye %s, pawn %.0f cm/s;%s"), *Name, *Eye().ToCompactString(), Pawn->GetVelocity().Size(), *Fx->DescribeClouds());
			Controller->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=OSSmoke_%s"), *Name));
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

		if (Args.Contains(TEXT("budget")))
		{
			// The os.Weapons.Fx stand (6 m before the living wall, looking at it) and the
			// steps of the budget check as before the smoke went to the world (three
			// shotgun shots, the first may still meet the draw), the stand restored before each.
			FVector Target = Pawn->GetActorLocation() + FVector(600.0f, 0.0f, 0.0f);
			FVector Stand = Pawn->GetActorLocation();
			if (TActorIterator<AOSLivingWall> Wall(World); Wall)
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
		// Lvl_OS_Range's Anchor_A (Scripts/build_range.py), the static block nearest its place.
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
			UE_LOG(LogMindKore, Warning, TEXT("Smoke moments: no anchor block near %s (Lvl_OS_Range's Anchor_A); the zero-G moments are skipped"), *AnchorA.ToCompactString());
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
		TEXT("os.Weapons.Smoke"),
		TEXT("Stills of the smoke's moments in play on Lvl_OS_Range (run with -UseFixedTimeStep -FPS=60): on foot, holding on, a pistol burst, an SV-98 shot drifting off and back (or only those named: foot, cling, burst, sv98). 'budget': three shotgun clouds on the os.Weapons.Fx stand, then ProfileGPU."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunSmokeMoments));

	FAutoConsoleCommandWithWorld GClearSmoke(
		TEXT("os.Fx.ClearSmoke"),
		TEXT("Remove every muzzle smoke cloud of the player (stills)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			APlayerController* Controller = World->GetFirstPlayerController();
			APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
			if (UOSMuzzleFxComponent* Fx = Pawn ? Pawn->FindComponentByClass<UOSMuzzleFxComponent>() : nullptr)
			{
				Fx->ClearClouds();
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GMuzzleFxShots(
		TEXT("os.Weapons.Fx"),
		TEXT("First-person stills of each gun's muzzle flash frame by frame (60 fps frames 0-12 at time dilation 0.01), its smoke over 3 s and two more bursts (HighResShot; needs a real RHI). 'space': look away from the wall."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunMuzzleFxShots));
}
