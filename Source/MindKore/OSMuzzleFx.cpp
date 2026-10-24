#include "OSMuzzleFx.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/HeterogeneousVolumeComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "UObject/UObjectIterator.h"
#include "Components/SphereComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "OSFlyer.h"
#include "OSGas.h"
#include "OSGravity.h"
#include "OSLivingWall.h"
#include "OSWeapons.h"
#include "OSZeroGPawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MindKore.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Engine/GameViewportClient.h"
#include "UObject/ConstructorHelpers.h"
#include "Algo/MaxElement.h"

namespace
{
	TAutoConsoleVariable<int32> CVarSmokeStamp(
		TEXT("os.Fx.SmokeStamp"), 1,
		TEXT("1: a shot's first frame of gas is laid into its grid on the game thread (seen on the shot's frame); 0: only the grid's step lays it (a frame later)."));

	TAutoConsoleVariable<int32> CVarVolumetricSmoke(
		TEXT("os.Fx.VolumetricSmoke"), 1,
		TEXT("1: muzzle smoke as volumetric voxel clouds (heterogeneous volumes); 0: card puffs. Both live in the world."));

	TAutoConsoleVariable<int32> CVarHoleWrap(
		TEXT("os.Fx.HoleWrap"), 1,
		TEXT("1 (v16): a shroud hole's pop rides with the gun 0.2 s, then every hole lets a slow wisp out and back along the shroud that climbs round it; the seep leaves along its hole and rises. 0: the pop left in the world at once, threads straight up out of 4 holes, the seep straight up."));

	TAutoConsoleVariable<int32> CVarHoleLive(
		TEXT("os.Fx.HoleLive"), 1,
		TEXT("1 (v16c): a shroud hole's pop and wrap leave with the hole's live velocity, 0.6 of it, given to the air over 0.18 s - a short soft tail behind each hole on the move, nothing riding on and stopping dead; 0: the pop and wrap ride with the gun (cling) at the shot's velocity."));

	/** v16c (os.Fx.HoleLive): a hole's gas takes 0.6 of the hole's velocity and gives it to the air over 0.18 s, no cling. */
	void HoleCarrier(FOSGasRecipe& R)
	{
		if (CVarHoleLive.GetValueOnGameThread() != 0)
		{
			R.ClingSeconds = 0.0f;
			R.CarrierShare = 0.6f;
			R.CarrierSeconds = 0.18f;
			R.bLiveCarrier = true;
		}
	}

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
	// Volumetric clouds (Docs/RecoilWeapons-SmokeSim.md): how each gun's gas behaves in its grid. What flows into it
	// and from where is the weapon's (FOSWeapon::GasVents).
	const auto Cloud = [](FOSMuzzleFx& Gun, int32 Cells, float CellCm,
		float Extinction, float Buoyancy, float Drag, float Vorticity, float HeatSeconds, float Decay, int32 Jacobi, float ActiveAir, float ActiveVacuum,
		float RoundRadius, float RoundPush, float RoundAlong, float Seconds)
	{
		Gun.Cells = Cells;
		Gun.CellCm = CellCm;
		Gun.DenseExtinction = Extinction;
		Gun.Sim.Buoyancy = Buoyancy;
		Gun.Sim.DragSeconds = Drag;
		Gun.Sim.Vorticity = Vorticity;
		Gun.Sim.HeatSeconds = HeatSeconds;
		Gun.Sim.DecayPerSecond = Decay;
		Gun.Sim.Jacobi = Jacobi;
		// No swelling: the gas is a jet, not a ball blown up (v4, owner: «выглядит как шарик»).
		Gun.Sim.SwellPerSecond = 0.0f;
		Gun.ActiveAirSeconds = ActiveAir;
		Gun.ActiveVacuumSeconds = ActiveVacuum;
		Gun.RoundRadiusCm = RoundRadius;
		Gun.RoundPushCm = RoundPush;
		Gun.RoundAlongCm = RoundAlong;
		Gun.CloudSeconds = Seconds;
	};
	// In air the grid steps the cloud's whole life (v6: it froze at 2-3 s and the clouds stood like stills).
	Cloud(Pistol, 20, 3.0f, 0.03f, 220.0f, 0.6f, 0.3f, 0.35f, 0.05f, 4, 4.0f, 0.35f, 6.0f, 100.0f, 160.0f, 4.0f);
	Cloud(Shotgun, 32, 5.0f, 0.03f, 160.0f, 0.8f, 0.8f, 0.5f, 0.03f, 4, 6.0f, 0.35f, 10.0f, 140.0f, 220.0f, 6.0f);
	Cloud(Sniper, 22, 98.0f / 22.0f, 0.03f, 240.0f, 0.6f, 0.35f, 0.4f, 0.04f, 4, 5.0f, 0.35f, 8.0f, 120.0f, 260.0f, 5.0f);
	Shotgun.ShoveCm = 90.0f;
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
	// v11: the gas is the vents' parcels (Vents, UOSGasSubsystem); no grid cloud of the shot's own.
	if (UOSGasSubsystem::Get(this))
	{
		return;
	}
	if (CVarVolumetricSmoke.GetValueOnGameThread() != 0 && CloudMaterial.LoadSynchronous())
	{
		// Every round punches its own tunnel as it flies (CarveRay from UOSWeaponsComponent); a shotgun blast out of
		// the muzzle shoves the older clouds ahead of it on.
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

namespace
{
	/** The gun's smoke look and the vents' parcel recipes (v11, contract 2.2). */
	FOSGasLook GunSmoke()
	{
		return FOSGasLook();
	}

	/** The pistol's wisp after a shot: 10 small parcels over 1 s from the muzzle, almost still, rising. */
	FOSGasRecipe PistolWisp()
	{
		FOSGasRecipe R;
		R.BurstCount = 10;
		R.BurstSeconds = 1.0f;
		R.SpeedCm = 12.0f;
		R.SpeedMin = 0.3f;
		R.ConeDeg = 25.0f;
		R.RadiusMinCm = 0.8f;
		R.RadiusMaxCm = 1.4f;
		R.MaxRadiusCm = 6.0f;
		R.Mass = 0.5f;
		R.Heat = 0.4f;
		R.LifeSeconds = 2.5f;
		return R;
	}

	/**
	 * A shroud hole's thread after a shot (contract 3h E): 6 parcels over 1.5 s out of the hole, rising at 20-25 cm/s
	 * against gravity in a narrow cone, 1.5-2 cm growing to 4.5, free of the gun (the hole moves, the thread stays).
	 */
	FOSGasRecipe HoleThread()
	{
		FOSGasRecipe R;
		R.BurstCount = 6;
		R.BurstSeconds = 1.5f;
		R.SpeedCm = 25.0f;
		R.SpeedMin = 0.8f;
		R.ConeDeg = 6.0f;
		R.RadiusMinCm = 1.5f;
		R.RadiusMaxCm = 2.0f;
		R.MaxRadiusCm = 4.5f;
		R.Mass = 1.0f;
		R.Heat = 0.5f;
		R.LifeSeconds = 2.5f;
		return R;
	}

	/**
	 * A shroud hole's wrap after its pop (v16, owner: «плавно обволакивать оружие»): 3 small parcels over 0.45 s, out of
	 * the hole and back along the shroud at 7-14 cm/s (a ring or two back in half a second), riding with the gun for
	 * 0.3 s, then the air's; warm, so they climb round the barrel (a terminal rise of ~7 cm/s: FallCmPerS2 x 0.7 s) -
	 * the holes under the bore send theirs up its sides. Let out of the hole as it moves: the wisps lie along its path.
	 */
	FOSGasRecipe HoleWrap()
	{
		FOSGasRecipe R;
		R.BurstCount = 3;
		R.BurstSeconds = 0.45f;
		R.SpeedCm = 14.0f;
		R.SpeedMin = 0.5f;
		R.ConeDeg = 15.0f;
		R.RadiusMinCm = 1.5f;
		R.RadiusMaxCm = 2.5f;
		R.MaxRadiusCm = 6.0f;
		R.Mass = 1.0f;
		R.Heat = 0.5f;
		R.LifeSeconds = 2.2f;
		R.ClingSeconds = 0.3f;
		R.FallCmPerS2 = -10.0f;
		HoleCarrier(R);
		return R;
	}

	/** Where a hole's wrap and seep go, in the vent's frame: out of the hole, Back of the way back along the bore. */
	FVector HoleOut(const FOSGasVent& Vent, float Back)
	{
		return (Vent.Direction.GetSafeNormal() - FVector::ForwardVector * Back).GetSafeNormal();
	}

	FOSGasRecipe VentRecipe(const FOSGasVent& Vent)
	{
		FOSGasRecipe R;
		R.SpeedCm = Vent.SpeedCm;
		R.ConeDeg = Vent.ConeDeg;
		if (Vent.bJet)
		{
			// The muzzle: 60-90 parcels over 0.1 s (by the vent's Rate), a vortex ring round a forward jet.
			R.BurstCount = FMath::Clamp(FMath::RoundToInt(Vent.Rate * 230.0f), 40, 160);
			R.BurstSeconds = 0.1f;
			R.RingCm = Vent.Direction.X > 0.9f ? 10.0f : 0.0f;
			R.RingTurnsPerSecond = 2.0f;
			// More, smaller parcels (owner 23.10: «набор шаров»): 160 at most, 2-4 cm in air, 4-6 in a vacuum.
			R.RadiusMinCm = 2.0f;
			R.RadiusMaxCm = 4.0f;
			// A narrow muzzle (WidthCm under 3, the pistol): a thin stream, not a puff (owner 23.10: «у пистолета дым должен
			// выходить более тонкой струйкой») - 25 parcels over 0.06 s, 1-2 cm growing to 6, no ring, 3 s; its
			// lingering wisp from the muzzle is PistolWisp.
			if (Vent.WidthCm < 3.0f)
			{
				R.BurstCount = 25;
				R.BurstSeconds = 0.06f;
				R.VacuumBurstCount = 30;
				R.RingCm = 0.0f;
				R.RadiusMinCm = 1.0f;
				R.RadiusMaxCm = 2.0f;
				R.VacuumRadiusMinCm = 2.0f;
				R.VacuumRadiusMaxCm = 3.0f;
				R.MaxRadiusCm = 6.0f;
				R.SpeedMin = 0.5f;
				R.LifeSeconds = 3.0f;
			}
			R.VacuumBurstCount = FMath::Clamp(FMath::RoundToInt(Vent.Rate * 230.0f), 40, 160);
			R.VacuumBurstSeconds = 0.15f;
			R.VacuumSpeedMin = 0.3f;
			R.VacuumRadiusMinCm = 4.0f;
			R.VacuumRadiusMaxCm = 6.0f;
			R.Mass = 1.0f;
			R.Heat = 1.0f;
			// The blast is seen at once (the shot's frame); everything else fades in over 0.12 s.
			R.bInstant = true;
			R.LifeSeconds = 6.0f;
		}
		else
		{
			// A shroud hole's pop at the shot, as real gas out of a shroud (owner 23.10: «списано с настоящего дыма»):
			// 5 parcels of 2-3 cm in 0.03 s at 270-450 cm/s along the hole's normal, stopped by the air within
			// ~12 cm (their own drag, tau 0.028 s) - a small puff at each hole (3 of 1.5-2.5 cm in 8 cm did not show:
			// owner «незаметно»). The thin rising thread after it is the seep (Seep).
			// v16 (os.Fx.HoleWrap): 4, and the pop rides with the gun for 0.2 s before the air takes it (left in the
			// world at once, a walker's shroud outran its own puffs); the wrap (HoleWrap) follows it out of every hole.
			const bool bWrap = CVarHoleWrap.GetValueOnGameThread() != 0;
			R.BurstCount = bWrap ? 4 : 5;
			R.ClingSeconds = bWrap ? 0.2f : 0.0f;
			if (bWrap)
			{
				HoleCarrier(R);
			}
			R.BurstSeconds = 0.03f;
			R.SpeedCm = 450.0f;
			R.SpeedMin = 0.6f;
			R.ConeDeg = 20.0f;
			R.DragSeconds = 0.028f;
			// 3h: up to 3 cm, 1.5 s - read as a puff at each hole before the muzzle's cloud covers them.
			R.RadiusMinCm = 2.0f;
			R.RadiusMaxCm = 3.0f;
			R.MaxRadiusCm = 3.0f;
			R.Mass = 1.0f;
			R.Heat = 0.8f;
			R.LifeSeconds = 1.5f;
		}
		return R;
	}
}

bool OSGasCardsEnabled();

void UOSMuzzleFxComponent::Vents(const FOSWeapon& Def, const FTransform& Muzzle, const USceneComponent* Action, float JetScale)
{
	if (UOSGasSubsystem* Gas = UOSGasSubsystem::Get(this))
	{
		// v11: every vent is a gas source; the muzzle's card stream only with os.Gas.Cards 1.
		// The gun's velocity the gas takes: whole in a vacuum, a quarter in air - the still air takes the rest at once
		// (walking back while shooting, the whole jet flew back at the shooter: owner 23.10).
		const FVector Carrier = GetOwner()->GetVelocity() * (OSAirZoneAt(GetWorld(), Muzzle.GetLocation()) ? 0.25f : 1.0f);
		// Heat without smoke out of the muzzle (v12 7b.1): the air's shimmer over 80 ms - pistol 6, RMB-93 12, SV-98 16.
		Gas->AddHeat(Muzzle.GetLocation(), Muzzle.GetRotation().GetForwardVector(), Def.Id == TEXT("Pistol") ? 6 : Def.Id == TEXT("Sniper") ? 16 : 12);
		// The shot shoves the room's old haze aside (v14; its own cloud untouched).
		Gas->PushHaze(Muzzle.GetLocation(), Muzzle.GetRotation().GetForwardVector(), 120.0f * JetScale, 80.0f * JetScale);
		for (const FOSGasVent& Vent : Def.GasVents)
		{
			if (!Vent.bJet && !Vent.bCling)
			{
				continue;
			}
			const bool bOnAction = Vent.Space == EOSVentSpace::Action && Action;
			const FTransform Frame = bOnAction ? Action->GetComponentTransform() : Muzzle;
			// A muzzle device (a suppressor, FOSAttachment::MuzzleJetScale) throttles what leaves the muzzle: fewer
			// parcels (6 at least) and slower; the gun's own ports and shroud holes blow as ever.
			FOSGasRecipe Recipe = VentRecipe(Vent);
			if (Vent.Space == EOSVentSpace::Muzzle && JetScale < 1.0f)
			{
				Recipe.BurstCount = FMath::Max(6, FMath::RoundToInt(Recipe.BurstCount * JetScale));
				Recipe.VacuumBurstCount = Recipe.VacuumBurstCount > 0 ? FMath::Max(6, FMath::RoundToInt(Recipe.VacuumBurstCount * JetScale)) : 0;
				Recipe.SpeedCm *= JetScale;
			}
			// A jet into a vacuum: a fan of streamers, as the holes' threads (owner 23.10: one jet read as a capsule) -
			// 7 in a cone of 25 degrees round the bore, each a 3 degree thread with its own share.
			const FVector Where = Frame.TransformPosition(Vent.Location);
			const FVector Axis = Frame.TransformVectorNoScale(Vent.Direction.GetSafeNormal());
			if (Vent.bJet && !OSAirZoneAt(GetWorld(), Where) && Recipe.VacuumBurstCount > 0 && Vent.WidthCm < 3.0f)
			{
				// A narrow muzzle (the pistol) into a vacuum: a small quick breath in a 35 degree fan, faint and gone in
				// under a second - no streamers (4 of them read as bright comets: owner 23.10 «у пистолета какие-то кометы»).
				FOSGasRecipe Breath = Recipe;
				Breath.VacuumBurstCount = Breath.BurstCount = 28;
				Breath.ConeDeg = 35.0f;
				Breath.Mass = 0.5f;
				Breath.LifeSeconds = 0.9f;
				Breath.bShotCloud = true;
				Gas->AddSource(Breath, GunSmoke(), Where, Axis, Carrier);
				continue;
			}
			if (Vent.bJet && !OSAirZoneAt(GetWorld(), Where) && Recipe.VacuumBurstCount > 0)
			{
				const int32 Streamers = 7;
				for (int32 Index = 0; Index < Streamers; ++Index)
				{
					FOSGasRecipe Thread = Recipe;
					// The first (along the bore) starts the shot's cloud, the rest join it; even shares (a first of 20 at least
					// made the pistol's one bright comet).
					Thread.VacuumBurstCount = FMath::Max(5, FMath::RoundToInt(Recipe.VacuumBurstCount * FMath::FRandRange(0.6f, 1.4f) / Streamers));
					Thread.bShotCloud = Index == 0;
					Thread.BurstCount = Thread.VacuumBurstCount;
					Thread.ConeDeg = 3.0f;
					Thread.bJoinThisFrame = Index > 0;
					const FVector Dir = Index == 0 ? Axis : FMath::VRandCone(Axis, FMath::DegreesToRadians(12.5f));
					Gas->AddSource(Thread, GunSmoke(), Where, Dir, Carrier);
				}
				continue;
			}
			// A clinging hole's wisps ride with the gun whole (a hold, not an inherited velocity); a jet takes Carrier.
			Gas->AddSource(Recipe, GunSmoke(), Where, Axis, Vent.bCling && !Vent.bJet ? GetOwner()->GetVelocity() : Carrier);
			// A narrow muzzle's lingering wisp: up out of the muzzle for a second, following the gun; a suppressor's
			// (JetScale < 1) lazier, 1.5 s, out of its end (v12 7b.5).
			const bool bSuppressed = JetScale < 1.0f && Vent.Space == EOSVentSpace::Muzzle;
			if (Vent.bJet && (Vent.WidthCm < 3.0f || bSuppressed))
			{
				FOSGasRecipe Wisp = PistolWisp();
				if (bSuppressed)
				{
					Wisp.BurstSeconds = 1.5f;
					Wisp.BurstCount = 12;
					Wisp.SpeedCm = 8.0f;
				}
				const FVector Up = -OSGravityAt(GetWorld(), Frame.GetLocation()).Acceleration.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
				if (Action)
				{
					const FTransform ActionFrame = Action->GetComponentTransform();
					Gas->AddSource(Wisp, GunSmoke(), ActionFrame.InverseTransformPosition(Frame.TransformPosition(Vent.Location)), ActionFrame.InverseTransformVectorNoScale(Up), FVector::ZeroVector, Action);
				}
				else
				{
					Gas->AddSource(Wisp, GunSmoke(), Frame.TransformPosition(Vent.Location), Up, FVector::ZeroVector);
				}
			}
		}
		// After the pops, the shroud's holes on the moving gun (Action): v16 (os.Fx.HoleWrap) every hole's wrap - out
		// of it and back along the shroud, climbing round the barrel (HoleWrap); before it 4 holes at random let a
		// thread straight up each, 24 parcels over 1.5 s (HoleThread; in zero-G its fallback pointed down).
		if (Action)
		{
			TArray<const FOSGasVent*, TInlineAllocator<32>> Holes;
			for (const FOSGasVent& Vent : Def.GasVents)
			{
				if (Vent.bCling && !Vent.bJet && Vent.Space == EOSVentSpace::Action)
				{
					Holes.Add(&Vent);
				}
			}
			if (CVarHoleWrap.GetValueOnGameThread() != 0)
			{
				for (const FOSGasVent* Hole : Holes)
				{
					Gas->AddSource(HoleWrap(), GunSmoke(), Hole->Location, HoleOut(*Hole, 1.0f), GetOwner()->GetVelocity(), Action);
				}
			}
			else
			{
				const FTransform ActionFrame = Action->GetComponentTransform();
				const FVector Up = -OSGravityAt(GetWorld(), ActionFrame.GetLocation()).Acceleration.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
				for (int32 Index = 0; Index < 4 && !Holes.IsEmpty(); ++Index)
				{
					const FOSGasVent* Hole = Holes[FMath::RandHelper(Holes.Num())];
					Holes.RemoveSingleSwap(Hole);
					Gas->AddSource(HoleThread(), GunSmoke(), Hole->Location, ActionFrame.InverseTransformVectorNoScale(Up), FVector::ZeroVector, Action);
				}
			}
		}
		// All of the shot's gas is out: its stamp starts on a worker now, the gas tick joins it (os.Gas.StampAsync).
		Gas->KickStamps();
		if (!OSGasCardsEnabled())
		{
			return;
		}
	}
	if (!Smoke)
	{
		return;
	}
	// The cloud this shot just made (Shot spawned it): its jets.
	FCloud* Cloud = nullptr;
	for (FCloud& Each : Clouds)
	{
		if (Each.Age == 0.0f && (!Cloud || &Each == LastSpawned))
		{
			Cloud = &Each;
		}
	}
	bool bStamped = false;
	for (FOSGasVent Vent : Def.GasVents)
	{
		// A muzzle device (a suppressor) throttles what leaves the muzzle; the gun's own ports blow as ever.
		if (Vent.Space == EOSVentSpace::Muzzle)
		{
			Vent.Rate *= JetScale;
			Vent.SpeedCm *= JetScale;
		}
		if (Vent.bJet)
		{
			if (Cloud)
			{
				// Where it leaves the gun at the shot, in the world: the gas is the world's from here.
				const FTransform Frame = Vent.Space == EOSVentSpace::Action && Action ? Action->GetComponentTransform() : Muzzle;
				FCloud::FJet& Jet = Cloud->Jets.AddDefaulted_GetRef();
				Jet.Origin = Frame.TransformPosition(Vent.Location);
				Jet.Direction = Frame.TransformVectorNoScale(Vent.Direction.GetSafeNormal());
				Jet.GunVelocity = GetOwner()->GetVelocity();
				Jet.Vent = Vent;
				if (CVarSmokeStamp.GetValueOnGameThread() != 0)
				{
					// The first frame's gas laid on the game thread (a few splats): the shot is seen on its own frame,
					// not a grid step later; the grid's steps carry on from this front (v6, owner: «задержка»).
					LayJet(*Cloud->Sim, Jet, Cloud->Basis, Cloud->Centre, 1.0f / 60.0f);
					bStamped = true;
				}
			}
			continue;
		}
		if (Vent.Seconds > 0.0f && Vent.Rate > 0.0f)
		{
			FFlow& Flow = Flows.AddDefaulted_GetRef();
			Flow.Vent = Vent;
			Flow.Look = Def.PortSmoke;
			Flow.Muzzle = Muzzle;
			Flow.Action = Action;
		}
	}
	if (bStamped)
	{
		Cloud->Sim->Pack(Cloud->Atlas, Cloud->KeyDir, KeyShadow, AmbientOcclusion, Sharpen);
		Cloud->bPendingAtlas = true;
		Cloud->bHasGas = true;
	}
}

void UOSMuzzleFxComponent::LayJet(FOSSmokeSim& Sim, FCloud::FJet& Jet, const FQuat& Basis, const FVector& Centre, float At)
{
	// The gas the vent let out since the last front, laid along the jet from its old front to the new (the front runs
	// out at the vent's speed dying with DecaySeconds), widening with the distance and moving on at the jet's speed
	// there - a long thin jet that frays, never a ball (v4). Laid, not left to the advection alone: a jet a cell or two
	// wide was smeared to nothing on the 5 cm grid. The flow dies to a thin dribble (v6: «слишком долго льется»).
	const FOSGasVent& V = Jet.Vent;
	if (At >= V.Seconds)
	{
		return;
	}
	const float Flow = FMath::Exp(-At / V.DecaySeconds);
	const float Speed = V.SpeedCm * Flow;
	const float From = Jet.FrontCm;
	Jet.FrontCm = FMath::Max(Jet.FrontCm, V.SpeedCm * V.DecaySeconds * (1.0f - Flow));
	const FVector3f Mouth(Basis.UnrotateVector(Jet.Origin + Jet.GunVelocity * At - Centre));
	const FVector3f Dir(Basis.UnrotateVector(FMath::VRandCone(Jet.Direction, FMath::DegreesToRadians(FMath::Max(V.ConeDeg, 0.5f)))));
	// The burst: the gas that came out since the last step lies between the old front and the new - as much gas as
	// length, so as dense all along (it was thinned with the flow and the far end came out empty).
	const float Grow = Jet.FrontCm - From;
	if (Grow > Sim.CellCm * 0.3f)
	{
		const float Mid = 0.5f * (From + Jet.FrontCm);
		const float Radius = FMath::Max(V.WidthCm * 0.5f + 0.22f * Mid, Sim.CellCm * 0.8f);
		Sim.Inject(Mouth + Dir * Mid, Dir, FMath::Max(0.5f * Grow, Sim.CellCm * 0.7f), Radius, V.Rate, 1.0f, Dir * Speed, 0.0f);
	}
	// The dribble after it, at the mouth and dying with the flow.
	if (Flow > 0.05f)
	{
		Sim.Inject(Mouth + Dir * Sim.CellCm, Dir, Sim.CellCm * 0.7f, FMath::Max(V.WidthCm * 0.5f, Sim.CellCm * 0.8f), V.Rate * Flow, 1.0f, Dir * Speed, 0.0f);
	}
}

void UOSMuzzleFxComponent::Seep(const FOSWeapon& Def, const FTransform& Muzzle, const USceneComponent* Action, int32 Count, float AgeSpread)
{
	TArray<const FOSGasVent*, TInlineAllocator<32>> Seeping;
	for (const FOSGasVent& Vent : Def.GasVents)
	{
		if (Vent.bSeep)
		{
			Seeping.Add(&Vent);
		}
	}
	if (UOSGasSubsystem* Gas = UOSGasSubsystem::Get(this); Gas && !Seeping.IsEmpty())
	{
		// The hot barrel's heat as its seep (v12 7b.1, 7d): two puffs for each wisp, living up to 3 s.
		Gas->AddHeat(Muzzle.GetLocation(), FVector::UpVector, 2 * Count, 0.6f, 0.0f, 3.0f);
		// v11: the seep is small parcels out of a random seeping hole, a tenth of its speed.
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FOSGasVent& Vent = *Seeping[FMath::RandHelper(Seeping.Num())];
			const bool bOnAction = Vent.Space == EOSVentSpace::Action && Action;
			const FTransform Frame = bOnAction ? Action->GetComponentTransform() : Muzzle;
			// The hot barrel's thread: a small parcel rising out of the hole (20 cm/s against gravity, a narrow cone: a
			// straight thread first, the flow curls it after), not carried by the gun - moved, the thread tears off and
			// hangs. The heat (FOSPortSmoke) sets how many: one shot a faint thread, a burst a thicker, longer one.
			// v16 (os.Fx.HoleWrap): out of its hole (a little back along the bore) at 6-12 cm/s and warm, so it rises
			// round the barrel - a hole under the bore seeped straight up through it; in zero-G it leaves along the hole
			// (the gravity fallback sent it into the barrel).
			FOSGasRecipe R;
			const bool bWrap = CVarHoleWrap.GetValueOnGameThread() != 0;
			R.SpeedCm = bWrap ? 12.0f : 20.0f;
			R.SpeedMin = bWrap ? 0.5f : 0.6f;
			R.ConeDeg = bWrap ? 10.0f : 6.0f;
			R.RadiusMinCm = 1.5f;
			R.RadiusMaxCm = 2.0f;
			R.MaxRadiusCm = 5.0f;
			R.Mass = 0.7f;
			R.Heat = 0.5f;
			R.LifeSeconds = 2.5f;
			R.FallCmPerS2 = bWrap ? -14.0f : 0.0f;
			const FVector Where = Frame.TransformPosition(Vent.Location);
			const FVector Out = bWrap ? Frame.TransformVectorNoScale(HoleOut(Vent, 0.3f))
				: -OSGravityAt(GetWorld(), Where).Acceleration.GetSafeNormal(UE_SMALL_NUMBER, Frame.TransformVectorNoScale(Vent.Direction.GetSafeNormal()));
			Gas->EmitNow(R, GunSmoke(), Where, Out, FVector::ZeroVector, 1, true);
		}
		if (!OSGasCardsEnabled())
		{
			return;
		}
	}
	for (int32 Index = 0; Smoke && !Seeping.IsEmpty() && Index < Count; ++Index)
	{
		// A seep barely leaves the hole: a tenth of the vent's speed.
		EmitWisp(*Seeping[FMath::RandHelper(Seeping.Num())], Def.PortSmoke, Muzzle, Action, 0.1f, FMath::FRand() * AgeSpread, 0.6f);
	}
}

void UOSMuzzleFxComponent::EmitWisp(const FOSGasVent& Vent, const FOSPortSmoke& Look, const FTransform& Muzzle, const USceneComponent* Action, float Speed, float AgeOffset, float OpacityScale)
{
	UWorld* World = GetWorld();
	const bool bOnAction = Vent.Space == EOSVentSpace::Action && Action;
	const FTransform Frame = bOnAction ? Action->GetComponentTransform() : Muzzle;
	const FVector Where = Frame.TransformPosition(Vent.Location);
	const bool bAir = OSAirZoneAt(World, Where) != nullptr;
	// Out of the vent within its cone, at its speed now.
	const FVector Out = FMath::VRandCone(Vent.Direction.GetSafeNormal(), FMath::DegreesToRadians(FMath::Max(Vent.ConeDeg, 0.5f)));
	const float SpeedCm = Vent.SpeedCm * Speed;
	FPuff& Puff = Puffs.AddDefaulted_GetRef();
	Puff.Origin = Where;
	Puff.Bore = Frame.GetRotation();
	Puff.GunVelocity = GetOwner()->GetVelocity();
	Puff.AirDragSeconds = bAir ? AirDragSeconds : 0.0f;
	Puff.StartCm = Look.StartCm;
	Puff.EndCm = Look.EndCm * FMath::FRandRange(0.7f, 1.0f);
	Puff.Opacity = Look.Opacity * OpacityScale;
	Puff.Life = Look.Seconds * FMath::FRandRange(0.75f, 1.25f);
	Puff.Age = AgeOffset;
	Puff.Seed = FMath::FRand();
	Puff.Spin = FMath::FRandRange(-0.4f, 0.4f);
	Puff.Tear = 0.3f;
	// Streaks: long along their motion, fading in (no pop).
	Puff.Stretch = FMath::FRandRange(2.5f, 3.5f);
	Puff.FadeInSeconds = 0.15f;
	if (bAir)
	{
		Puff.Lift = -OSGravityAt(World, Where).Acceleration * Look.RiseShare;
		Puff.LiftDragSeconds = Look.AirDragSeconds;
		Puff.SwayCm = 1.4f;
	}
	if (Vent.bCling && bOnAction)
	{
		// Round the bore in the part's space: out of the hole, then back along the barrel and round it; a vacuum lets
		// go at once (nothing holds the gas to the gun there: on outward, straight).
		Puff.Anchor = Action;
		Puff.Port = Vent.Location;
		Puff.BoreZ = Look.BoreZCm;
		Puff.Theta = FMath::Atan2(Vent.Location.Z - Look.BoreZCm, Vent.Location.Y);
		Puff.Radius = FVector2D(Vent.Location.Y, Vent.Location.Z - Look.BoreZCm).Size();
		Puff.SwirlRadPerSecond = Look.SwirlCmPerSecond / FMath::Max(Puff.Radius + 1.0f, 1.0f) * (FMath::RandBool() ? 1.0f : -1.0f) * FMath::FRandRange(0.7f, 1.0f);
		Puff.CreepCmPerSecond = Look.CreepCmPerSecond * FMath::FRandRange(0.6f, 1.0f);
		Puff.OutCm = FMath::Max(SpeedCm * 0.06f, 0.3f);
		Puff.ClingSeconds = bAir ? Look.ClingSeconds * FMath::FRandRange(0.8f, 1.2f) : 0.1f;
		Puff.Velocity = bAir ? FVector::ZeroVector : Frame.TransformVectorNoScale(Out) * SpeedCm * 0.6f + Puff.GunVelocity;
		Puff.WorldPos = Frame.TransformPosition(ClingPath(Puff, Puff.Age));
		Puff.PrevAttached = Puff.WorldPos;
		Puff.PrevGun = Frame;
		Puff.AlongWorld = Frame.GetRotation().GetAxisX();
	}
	else
	{
		// Straight out of the vent, in the world (the muzzle's thin stream ahead of the jet).
		Puff.Direction = Out;
		Puff.Along = Out;
		// The muzzle's stream is the first thing seen: no fade-in delay.
		Puff.FadeInSeconds = 0.02f;
		Puff.SpreadCm = SpeedCm * 0.12f;
		Puff.DriftCm = SpeedCm * 0.08f;
	}
	if (Puffs.Num() > MaxPuffs)
	{
		Puffs.RemoveAt(0, Puffs.Num() - MaxPuffs);
	}
}

FVector UOSMuzzleFxComponent::ClingPath(const FPuff& Puff, float Age)
{
	// Out of the hole a little (OutCm, easing in), round the bore (swirl easing in over 0.3 s), creeping back along it.
	const float Angle = Puff.Theta + Puff.SwirlRadPerSecond * (Age - 0.3f * (1.0f - FMath::Exp(-Age / 0.3f)));
	const float Radius = Puff.Radius + 0.6f + Puff.OutCm * (1.0f - FMath::Exp(-Age / 0.4f));
	return FVector(Puff.Port.X - Puff.CreepCmPerSecond * Age, Radius * FMath::Cos(Angle), Puff.BoreZ + Radius * FMath::Sin(Angle));
}

FTransform UOSMuzzleFxComponent::Sprite(const FVector& Location, const FVector& XAxis, float LengthCm, float WidthCm)
{
	// The engine plane faces +Z and is 100 units across; the camera is at the origin.
	// Seen end-on, a stretched sprite has no length to show: any in-plane axis will do.
	const FVector Along = FMath::Abs(Location.GetSafeNormal() | XAxis) > 0.98f ? FVector::UpVector : XAxis;
	return FTransform(FRotationMatrix::MakeFromZX(-Location, Along).ToQuat(), Location, FVector(LengthCm / 100.0f, WidthCm / 100.0f, 1.0f));
}

void UOSMuzzleFxComponent::WaitSims() const
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

void UOSMuzzleFxComponent::ClearClouds()
{
	if (UOSGasSubsystem* Gas = UOSGasSubsystem::Get(this))
	{
		Gas->ClearClouds();
	}
	WaitSims();
	for (FCloud& Cloud : Clouds)
	{
		Cloud.Age = -1.0f;
		Cloud.Volume->SetVisibility(false);
	}
}

FString UOSMuzzleFxComponent::DescribeClouds() const
{
	WaitSims();
	const UOSGasSubsystem* GasSystem = UOSGasSubsystem::Get(this);
	FString Line = GasSystem ? GasSystem->DescribeClouds() : FString();
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
			// The view's transmittance through the gas's centre (continuous density x the dense band's extinction).
			float Tau = 0.0f;
			{
				const FOSSmokeSim& S = *Cloud.Sim;
				const FVector3f From(Cloud.Basis.UnrotateVector(Eye - Cloud.Centre));
				const FVector3f To(FVector3f(Centroid) * S.CellCm);
				const FVector3f Dir = (To - From).GetSafeNormal();
				const float Half = 0.5f * S.N * S.CellCm;
				for (float T = 0.0f; T < (To - From).Size() + Half * 2.0f; T += S.CellCm * 0.5f)
				{
					const FVector3f P = From + Dir * T;
					if (P.GetAbsMax() < Half)
					{
						const FOSMuzzleFx& G = Guns[Cloud.Gun];
						const float Erode = G.Erode * FMath::Clamp((Cloud.Age - 0.5f) / FMath::Max(G.CloudSeconds - 0.5f, 0.1f), 0.0f, 1.0f) * 0.675f;
						Tau += FMath::Max(S.Sample(S.Rho, (P + Half) / S.CellCm - 0.5f) / FMath::Cube(Cloud.Scale) - Erode, 0.0f) * G.DenseExtinction * S.CellCm * 0.5f;
					}
				}
			}
			Line += FString::Printf(TEXT(" [gun %d age %.2f T %.2f %s%s, gas at %s, %.0f cm from its muzzle at the shot, %.0f cm from the eye, mass %.0f, mean |U| %.0f cm/s, heat %.2f, box %.0f cm x%.2f, carried %s cm/s, key %s]"),
				Cloud.Gun, Cloud.Age, FMath::Exp(-Tau), Cloud.Sim->bAir ? TEXT("air") : TEXT("vacuum"), Cloud.bFrozen ? TEXT(" frozen") : TEXT(""), *Gas.ToCompactString(),
				FVector::Dist(Gas, Cloud.Muzzle), FVector::Dist(Gas, Eye), Mass, Speed, MaxHeat, Cloud.Sim->N * Cloud.Sim->CellCm, Cloud.Scale,
				*Cloud.Carried.ToCompactString(), *FString::Printf(TEXT("%s (%.2f, %.2f, %.2f)"), *Cloud.KeyName, Cloud.KeyColour.R, Cloud.KeyColour.G, Cloud.KeyColour.B));
		}
	}
	return Line;
}

void UOSMuzzleFxComponent::DumpClouds(const FString& Name) const
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

int32 UOSMuzzleFxComponent::GetTunnelCount() const
{
	const UOSGasSubsystem* Gas = UOSGasSubsystem::Get(this);
	return Tunnels.Num() + (Gas ? Gas->GetTunnelCount() : 0);
}

int32 UOSMuzzleFxComponent::GetCloudCount() const
{
	const UOSGasSubsystem* Gas = UOSGasSubsystem::Get(this);
	int32 Count = Gas ? Gas->GetCloudCount() : 0;
	for (const FCloud& Cloud : Clouds)
	{
		Count += Cloud.Age >= 0.0f ? 1 : 0;
	}
	return Count;
}

namespace
{
	/**
	 * The grids step at a fixed rate - the same gas at 30, 60 or 144 fps (AGENTS.md, «Частота кадров») - of 60 Hz while
	 * the slug is fast (FastSeconds after the shot), 30 Hz after: the slow churn does not need more, and a step costs.
	 */
	constexpr float SimStep = 1.0f / 60.0f;
	constexpr float SlowStep = 1.0f / 30.0f;
	constexpr float FastSeconds = 0.5f;
	/** Late in life (from LateSeconds) the slow drift steps at 15 Hz: it moves under a pixel a step. */
	constexpr float LateStep = 1.0f / 15.0f;
	constexpr float LateSeconds = 2.0f;
	/** A projectile's tunnel spares the clouds this young (its own shot's gas, born with it). */
	constexpr float OwnShotSeconds = 0.15f;

#if !UE_BUILD_SHIPPING
	/** os.Fx.SmokeCost: what the grids cost on the game thread since the last call. */
	struct FSmokeCost
	{
		double StepUs = 0.0, MaxStepUs = 0.0, FrameUs = 0.0, MaxFrameUs = 0.0;
		int32 Steps = 0, Frames = 0;
	} GSmokeCost;
	FCriticalSection GSmokeCostLock;

	FAutoConsoleCommand GSmokeCostCommand(
		TEXT("os.Fx.SmokeCost"),
		TEXT("Log what the smoke grids cost since the last call - a step (on a worker thread): mean / max us; the game thread in a frame with a grid stepping (waiting for the steps, the box, the upload): mean / max us - then start over."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			FSmokeCost& C = GSmokeCost;
			UE_LOG(LogMindKore, Log, TEXT("Smoke cost: %d step(s) off the game thread, %.0f us mean, %.0f us max; game thread in %d frame(s) with grids, %.0f us mean, %.0f us max"),
				C.Steps, C.Steps ? C.StepUs / C.Steps : 0.0, C.MaxStepUs, C.Frames, C.Frames ? C.FrameUs / C.Frames : 0.0, C.MaxFrameUs);
			C = FSmokeCost();
			// v11: the gas subsystem's own (every world with one).
			for (TObjectIterator<UOSGasSubsystem> It; It; ++It)
			{
				if (It->GetWorld())
				{
					UE_LOG(LogMindKore, Log, TEXT("%s"), *It->TakeCostLine());
				}
			}
		}));

	/**
	 * os.Fx.SmokeTune "Field=Value ...": overrides for tuning without a rebuild - FOSMuzzleFx fields on every gun
	 * (Sim.Buoyancy=300, DenseExtinction=0.2) and the cloud material's parameters (mat.Ambient=0.02,
	 * mat.Colour=0.4/0.4/0.38). Empty: none (the fields keep what was set, the material its own values).
	 */
	TAutoConsoleVariable<FString> CVarSmokeTune(TEXT("os.Fx.SmokeTune"), TEXT(""),
		TEXT("Tuning overrides: 'Field=Value ...' on every gun's FOSMuzzleFx (Sim.Buoyancy=300) and mat.<Param>=Value (or R/G/B) on the cloud material."));
#endif
}

void UOSMuzzleFxComponent::SpawnCloud(int32 Weapon, const FTransform& Muzzle, const FVector& GunVelocity)
{
	const FOSMuzzleFx& Gun = Guns[Weapon];
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
		// The lighting cache (self-shadow) on the grid's own cells; the march step is r.HeterogeneousVolumes.StepSize (DefaultEngine.ini, 4 cm).
		Volume->LightingDownsampleFactor = 1.0f;
		Volume->StepFactor = 1.0f;
		Volume->ShadowStepFactor = 1.0f;
		Volume->RegisterComponent();
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(CloudMaterial.Get(), this);
		Volume->SetMaterial(0, Material);
		Cloud->Volume = Volume;
		Cloud->Material = Material;
		Cloud->Sim = MakeUnique<FOSSmokeSim>();
		CloudObjects.Add(Volume);
		CloudObjects.Add(Material);
	}
	if (!Cloud)
	{
		Cloud = Algo::MaxElementBy(Clouds, [](const FCloud& Each) { return Each.Age; });
	}
	// That was the last free slot: the oldest starts fading out (EvictSeconds), so the next shot finds one.
	if (!Clouds.ContainsByPredicate([Cloud](const FCloud& Each) { return Each.Age < 0.0f && &Each != Cloud; })
		&& Clouds.Num() >= MaxClouds)
	{
		FCloud* Oldest = nullptr;
		for (FCloud& Each : Clouds)
		{
			if (&Each != Cloud && Each.Age >= 0.0f && Each.Evict <= 0.0f && (!Oldest || Each.Age > Oldest->Age))
			{
				Oldest = &Each;
			}
		}
		if (Oldest)
		{
			Oldest->Evict = EvictSeconds;
		}
	}
	// The slot's last step may still run (the game thread does not wait for steps otherwise).
	if (Cloud->Pending.IsValid())
	{
		Cloud->Pending.Wait();
		Cloud->Pending = UE::Tasks::FTask();
	}
	Cloud->PlaneMove = FVector3f::ZeroVector;
	Cloud->Evict = 0.0f;
	Cloud->Spin = FVector3f::ZeroVector;
	Cloud->bHasGas = false;
	Cloud->bShown = false;
	Cloud->ShotFrame = GFrameCounter;
	const FQuat BoreQuat = Muzzle.GetRotation();
	const FVector Bore = BoreQuat.GetAxisX();
	const float BoxCm = Gun.Cells * Gun.CellCm;
	Cloud->Gun = Weapon;
	Cloud->Muzzle = Muzzle.GetLocation();
	Cloud->Bore = Bore;
	// A fixed skew off the bore: no voxel axis along the view down the shot (view-long columns drew a cross, 19.10).
	Cloud->Basis = BoreQuat * FQuat(FVector::UpVector, FMath::DegreesToRadians(23.0f)) * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(17.0f));
	// The box at the muzzle, long way along the bore (the gas is a jet out of the vents, FOSWeapon::GasVents; it fills
	// the box from the back). Born where the gas leaves the gun, never at the hit (v4).
	FCollisionQueryParams Query(SCENE_QUERY_STAT(OSSmokeSolids), false, GetOwner());
	Cloud->Centre = Cloud->Muzzle + Bore * BoxCm * (Gun.SmokeBrake > 0.0f ? 0.05f : 0.3f);
	Cloud->Jets.Reset();
	LastSpawned = Cloud;
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
	FOSSmokeSim& Sim = *Cloud->Sim;
	const bool bAir = OSAirZoneAt(World, Cloud->Muzzle) != nullptr;
	const FVector Gravity = OSGravityAt(World, Cloud->Muzzle).Acceleration;
	Sim.Reset(Gun.Cells, Gun.CellCm, bAir, FVector3f(Cloud->Basis.UnrotateVector(Gravity.GetSafeNormal())));
	// The floor and a wall: what the bore and the gravity meet within the box become planes the gas cannot enter
	// (Visibility: loose cases and magazines ignore it).
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
	// The grid's texture, one per slot (bilinear: the material samples the gas smooth and quantizes it to its own pixel).
	const FIntPoint Size = FOSSmokeSim::AtlasSize(Gun.Cells);
	if (!Cloud->Grid || Cloud->Grid->GetSizeX() != Size.X || Cloud->Grid->GetSizeY() != Size.Y)
	{
		UTexture2D* Grid = UTexture2D::CreateTransient(Size.X, Size.Y, PF_B8G8R8A8);
		Grid->SRGB = false;
		Grid->Filter = TF_Bilinear;
		Grid->NeverStream = true;
		Grid->UpdateResource();
		Cloud->Grid = Grid;
		CloudObjects.Add(Grid);
		Cloud->Material->SetTextureParameterValue(TEXT("Grid"), Grid);
	}
	// The atlas of the fresh slug right away: until the first upload the texture held the slot's last cloud (or, new,
	// uninitialised memory) - a frame of stray glowing voxels.
	PickKey(Cloud->Centre, Cloud->Basis, Cloud->KeyDir, Cloud->KeyColour);
	Sim.Pack(Cloud->Atlas, Cloud->KeyDir, KeyShadow, AmbientOcclusion, Sharpen);
	Cloud->PendingShift = FIntVector::ZeroValue;
	Cloud->bPendingAtlas = true;
	Cloud->Volume->SetVolumeResolution(FIntVector(Gun.Cells));
	Cloud->Volume->SetVisibility(true);
}

void UOSMuzzleFxComponent::PickKey(const FVector& Where, const FQuat& Basis, FVector3f& OutDir, FLinearColor& OutColour)
{
	UWorld* World = GetWorld();
	if (KeyLightsAge > 1.0f)
	{
		KeyLightsAge = 0.0f;
		KeyLights.Reset();
		for (TObjectIterator<ULightComponent> It; It; ++It)
		{
			if (It->GetWorld() == World)
			{
				KeyLights.Add(*It);
			}
		}
	}
	float Best = 0.0f;
	FVector Dir = FVector::UpVector;
	FLinearColor Colour = FLinearColor(0.6f, 0.65f, 0.75f);
	FString Name = TEXT("none");
	for (const TWeakObjectPtr<ULightComponent>& Each : KeyLights)
	{
		const ULightComponent* L = Each.Get();
		if (!L || !L->IsVisible() || !L->bAffectsWorld || L->Intensity <= 0.0f)
		{
			continue;
		}
		const float Brightness = L->ComputeLightBrightness();
		float Score;
		FVector Toward;
		if (L->IsA<UDirectionalLightComponent>())
		{
			Toward = -L->GetDirection();
			Score = Brightness;
		}
		else
		{
			const ULocalLightComponent* Local = Cast<ULocalLightComponent>(L);
			const FVector Offset = L->GetComponentLocation() - Where;
			const float Cm = Offset.Size();
			if (!Local || Cm > Local->AttenuationRadius)
			{
				continue;
			}
			Toward = Offset / FMath::Max(Cm, 1.0f);
			const UPointLightComponent* Point = Cast<UPointLightComponent>(L);
			Score = Point && !Point->bUseInverseSquaredFalloff
				? Brightness * FMath::Pow(1.0f - Cm / Local->AttenuationRadius, Point->LightFalloffExponent) * 10.0f
				: Brightness / FMath::Max(FMath::Square(Cm / 100.0f), 0.1f);
		}
		if (Score > Best)
		{
			Best = Score;
			Dir = Toward;
			Colour = L->GetLightColor();
			Name = GetNameSafe(L->GetOwner());
		}
	}
	// As bright as the light makes a surface there (its illuminance, lux, over pi): the cloud's lit side sits with the
	// floor's under the same lamp; the flash, close, lights the fresh gas from inside.
	OutDir = FVector3f(Basis.UnrotateVector(Dir));
	// Capped: a lamp right over the cloud or the flash inside it would burn it white (owner: never blown-out white).
	OutColour = Colour * FMath::Min(Best / UE_PI * KeyStrength, KeyMax);
	LastKeyName = Name;
}

void UOSMuzzleFxComponent::Blast(const FVector& Origin, const FVector& Direction, float RadiusCm, float Along, float Push, float Clear, const FCloud* Except, float LengthCm)
{
	for (FCloud& Cloud : Clouds)
	{
		if (Cloud.Age < 0.0f || &Cloud == Except)
		{
			continue;
		}
		// A cloud mid-step takes it once the step is done (TickClouds): a push is never lost.
		if (Cloud.IsBusy())
		{
			Cloud.Deferred.Add({ Origin, Direction, RadiusCm, Along, Push, Clear, LengthCm });
			continue;
		}
		BlastCloud(Cloud, Origin, Direction, RadiusCm, Along, Push, Clear, LengthCm);
	}
}

bool UOSMuzzleFxComponent::BlastCloud(FCloud& Cloud, const FVector& Origin, const FVector& Direction, float RadiusCm, float Along, float Push, float Clear, float LengthCm)
{
	// Box frame; the line counts if it passes within the box's half diagonal of its centre.
	FOSSmokeSim& Sim = *Cloud.Sim;
	const FVector Local = Cloud.Basis.UnrotateVector(Origin - Cloud.Centre);
	const FVector Dir = Cloud.Basis.UnrotateVector(Direction).GetSafeNormal();
	const float Half = 0.5f * Sim.N * Guns[Cloud.Gun].CellCm * Cloud.Scale;
	if ((Local - Dir * (Local | Dir)).Size() > Half * 1.75f)
	{
		return false;
	}
	if (Sim.Impulse(FVector3f(Local), FVector3f(Dir), RadiusCm, Along, Push, Clear, LengthCm))
	{
		Cloud.Awake = WakeSeconds;
		return true;
	}
	return false;
}

bool UOSMuzzleFxComponent::GetYoungestCloud(float MinAge, FVector& OutCentre, float& OutHalf) const
{
	if (const UOSGasSubsystem* Gas = UOSGasSubsystem::Get(this); Gas && Gas->GetYoungestCloud(MinAge, OutCentre, OutHalf))
	{
		return true;
	}
	const FCloud* Best = nullptr;
	for (const FCloud& Cloud : Clouds)
	{
		if (Cloud.Age >= MinAge && (!Best || Cloud.Age < Best->Age))
		{
			Best = &Cloud;
		}
	}
	if (Best)
	{
		OutCentre = Best->Centre;
		OutHalf = 0.5f * Best->Sim->N * Guns[Best->Gun].CellCm * Best->Scale;
	}
	return Best != nullptr;
}

void UOSMuzzleFxComponent::UpdateTunnels(FCloud& Cloud, float Half)
{
	TArray<FLinearColor, TInlineAllocator<MaxTunnels * 2>> Rows;
	Rows.SetNumZeroed(MaxTunnels * 2);
	int32 Count = 0;
	for (const FTunnel& Tunnel : Tunnels)
	{
		// Not through the gas its own shot let out (born with it, within 0.15 s): the pellets fly down the very axis
		// of their muzzle's jet and ate it whole.
		if (Cloud.Age < Tunnel.Age + OwnShotSeconds)
		{
			continue;
		}
		const FVector Closest = FMath::ClosestPointOnSegment(Cloud.Centre, Tunnel.Start, Tunnel.End);
		// Through the box at all (its half diagonal, 1.73 x the half side).
		if (FVector::Dist(Closest, Cloud.Centre) > Half * 1.74f + Tunnel.RadiusCm)
		{
			continue;
		}
		// Clean for the hold, then the radius shrinks to nothing and the edge goes ragged over the heal.
		const float Slow = Tunnel.bVacuum ? 2.0f : 1.0f;
		const float Heal = FMath::SmoothStep(TunnelHoldSeconds * Slow, (TunnelHoldSeconds + TunnelHealSeconds) * Slow, Tunnel.Age);
		const float Ragged = FMath::SmoothStep(TunnelHoldSeconds * Slow, (TunnelHoldSeconds + 0.5f * TunnelHealSeconds) * Slow, Tunnel.Age) * 0.75f;
		if (Tunnel.RadiusCm * (1.0f - Heal) < 0.3f)
		{
			continue;
		}
		const FVector A = Cloud.Basis.UnrotateVector(Tunnel.Start - Cloud.Centre);
		const FVector B = Cloud.Basis.UnrotateVector(Tunnel.End - Cloud.Centre);
		// Newest first in the rows' order does not matter; the oldest were dropped in CarveRay.
		Rows[Count] = FLinearColor(A.X, A.Y, A.Z, Tunnel.RadiusCm * (1.0f - Heal));
		Rows[MaxTunnels + Count] = FLinearColor(B.X, B.Y, B.Z, Ragged);
		++Count;
	}
	for (const FOSSmokeSim::FMover& M : Cloud.Movers)
	{
		if (Count >= MaxTunnels)
		{
			break;
		}
		if (!M.Extent.IsZero())
		{
			continue;
		}
		Rows[Count] = FLinearColor(M.Centre.X, M.Centre.Y, M.Centre.Z, -M.RadiusCm);
		Rows[MaxTunnels + Count] = FLinearColor(M.Centre.X, M.Centre.Y, M.Centre.Z, 0.0f);
		++Count;
	}
	if (Count == 0 && Cloud.TunnelCount == 0)
	{
		return;
	}
	if (!Cloud.TunnelTexture)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(MaxTunnels, 2, PF_A32B32G32R32F);
		Texture->SRGB = false;
		Texture->Filter = TF_Nearest;
		Texture->NeverStream = true;
		Texture->UpdateResource();
		Cloud.TunnelTexture = Texture;
		CloudObjects.Add(Texture);
		Cloud.Material->SetTextureParameterValue(TEXT("Tunnels"), Texture);
	}
	Cloud.TunnelCount = Count;
	Cloud.Material->SetScalarParameterValue(TEXT("TunnelCount"), float(Count));
	if (Count > 0)
	{
		uint8* Data = static_cast<uint8*>(FMemory::Malloc(Rows.Num() * sizeof(FLinearColor)));
		FMemory::Memcpy(Data, Rows.GetData(), Rows.Num() * sizeof(FLinearColor));
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, MaxTunnels, 2);
		Cloud.TunnelTexture->UpdateTextureRegions(0, 1, Region, MaxTunnels * sizeof(FLinearColor), sizeof(FLinearColor), Data,
			[](uint8* Src, const FUpdateTextureRegion2D* Regions) { FMemory::Free(Src); delete Regions; });
	}
}

void UOSMuzzleFxComponent::FindMovers(FCloud& Cloud, float Half, float DeltaTime, TSet<TObjectKey<UPrimitiveComponent>>& Seen)
{
	Cloud.Movers.Reset();
	FCollisionObjectQueryParams Types;
	Types.AddObjectTypesToQuery(ECC_PhysicsBody);
	Types.AddObjectTypesToQuery(ECC_WorldDynamic);
	Types.AddObjectTypesToQuery(ECC_Pawn);
	TArray<FOverlapResult> Hits;
	GetWorld()->OverlapMultiByObjectType(Hits, Cloud.Centre, Cloud.Basis, Types, FCollisionShape::MakeBox(FVector(Half)),
		FCollisionQueryParams(SCENE_QUERY_STAT(OSSmokeMovers), false));
	for (const FOverlapResult& Hit : Hits)
	{
		UPrimitiveComponent* Body = Hit.GetComponent();
		// The bounds' sphere (not the mesh): a barrel, a crate, a capsule. Big things count only while they move (a
		// door's leaf, a gate's panel) - walls and frames stay out: the world's solids are the planes traced at the shot.
		const float Radius = Body ? Body->Bounds.SphereRadius : 0.0f;
		if (!Body || Radius < 1.0f || Radius > 400.0f || Cloud.Movers.Num() >= 8)
		{
			continue;
		}
		// Of the shooter only its body: the gun in his hands would carve its own muzzle's gas.
		if (Body->GetOwner() == GetOwner() && Body != GetOwner()->GetRootComponent())
		{
			continue;
		}
		const TObjectKey<UPrimitiveComponent> Key(Body);
		const FVector At = Body->Bounds.Origin;
		const FVector* Last = LastMoverAt.Find(Key);
		const FVector Velocity = Body->IsSimulatingPhysics() ? Body->GetPhysicsLinearVelocity()
			: Last && DeltaTime > 0.0f ? (At - *Last) / DeltaTime : FVector::ZeroVector;
		LastMoverAt.Add(Key, At);
		Seen.Add(Key);
		const bool bBig = Radius > 90.0f;
		if (bBig && Velocity.SizeSquared() < 25.0f && !Body->IsSimulatingPhysics())
		{
			continue;
		}
		if (bBig)
		{
			// Its own box, turned with it (the grid only: an opaque door hides the smoke behind it anyway).
			const FBoxSphereBounds Local = Body->CalcBounds(FTransform::Identity);
			const FTransform Frame = Body->GetComponentTransform();
			FOSSmokeSim::FMover& Box = Cloud.Movers.AddDefaulted_GetRef();
			Box.Centre = FVector3f(Cloud.Basis.UnrotateVector(Frame.TransformPosition(Local.Origin) - Cloud.Centre));
			Box.RadiusCm = 0.0f;
			Box.Velocity = FVector3f(Cloud.Basis.UnrotateVector(Velocity.GetClampedToMaxSize(1500.0f)));
			Box.Extent = FVector3f(Local.BoxExtent * Frame.GetScale3D().GetAbs());
			Box.Rotation = FQuat4f(Cloud.Basis.Inverse() * Frame.GetRotation());
			Cloud.Awake = WakeSeconds;
			continue;
		}
		// A sphere round a long body is too fat; the box's middle extent is closer to its girth.
		const FVector E = Body->Bounds.BoxExtent;
		const float Girth = FMath::Min(Radius, FMath::Max3(FMath::Min(E.X, E.Y), FMath::Min(E.Y, E.Z), FMath::Min(E.X, E.Z)));
		Cloud.Movers.Add({ FVector3f(Cloud.Basis.UnrotateVector(At - Cloud.Centre)), Girth,
			FVector3f(Cloud.Basis.UnrotateVector(Velocity.GetClampedToMaxSize(1500.0f))) });
		// A moving body wakes a resting grid.
		if (Velocity.SizeSquared() > 400.0f)
		{
			Cloud.Awake = WakeSeconds;
		}
	}
}

void UOSMuzzleFxComponent::Burst(const FVector& Location, const FVector& Normal, float RadiusCm, float SpeedCm)
{
	if (UOSGasSubsystem* Gas = UOSGasSubsystem::Get(this))
	{
		Gas->Burst(Location, Normal, RadiusCm, SpeedCm);
	}
	if (GetCloudCount() == 0 || RadiusCm <= 0.0f)
	{
		return;
	}
	// A vacuum: no air holds the gas back, the same blow throws it half again as fast.
	const float Speed = SpeedCm * (OSAirZoneAt(GetWorld(), Location) ? 1.0f : 1.5f);
	const FVector Out = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	// A short thick push along the normal from just behind the point: out of the line every way, along it outward;
	// 0.4 of the gas at the centre blown away.
	Blast(Location - Out * RadiusCm * 0.5f, Out, RadiusCm, Speed * (Normal.IsNearlyZero() ? 0.0f : 0.6f), Speed, 0.4f, nullptr, RadiusCm);
}

void UOSMuzzleFxComponent::AddAirFlow(const FVector& Location, const FVector& Direction, float SpeedCm, float ReachCm, float Seconds)
{
	if (UOSGasSubsystem* Gas = UOSGasSubsystem::Get(this))
	{
		Gas->AddAirFlow(Location, Direction, SpeedCm, ReachCm, Seconds);
	}
	AirFlows.Add({ Location, Direction.GetSafeNormal(), SpeedCm, ReachCm, Seconds });
}

void UOSMuzzleFxComponent::CarveRay(const FVector& Start, const FVector& End, float RadiusCm)
{
	if (UOSGasSubsystem* Gas = UOSGasSubsystem::Get(this))
	{
		Gas->CarveRay(Start, End, RadiusCm);
	}
	const FVector Along = End - Start;
	const float Length = Along.Size();
	if (Length < 1.0f || RadiusCm <= 0.0f)
	{
		return;
	}
	// Only where it crosses a cloud's box (a round flies metres a frame; most of them through no smoke).
	bool bThrough = false;
	FVector Where = Start;
	for (const FCloud& Cloud : Clouds)
	{
		if (Cloud.Age >= OwnShotSeconds)
		{
			const float Half = 0.5f * Cloud.Sim->N * Guns[Cloud.Gun].CellCm * Cloud.Scale;
			const FVector Closest = FMath::ClosestPointOnSegment(Cloud.Centre, Start, End);
			if (FVector::Dist(Closest, Cloud.Centre) < Half * 1.75f)
			{
				bThrough = true;
				Where = Closest;
			}
		}
	}
	if (!bThrough)
	{
		return;
	}
	// The shot's own cloud is spared (its gas flies with the pellets).
	const FCloud* Fresh = LastSpawned && LastSpawned->Age >= 0.0f && LastSpawned->Age < OwnShotSeconds ? LastSpawned : nullptr;
	// A round's flight comes a frame at a time: its next stretch lengthens its tunnel (one tunnel per round).
	for (FTunnel& Each : Tunnels)
	{
		if (Each.RadiusCm == RadiusCm && FVector::DistSquared(Each.End, Start) < 1.0f
			&& ((Each.End - Each.Start).GetSafeNormal() | (Along / Length)) > 0.999f)
		{
			Each.End = End;
			Blast(Start, Along / Length, RadiusCm * 1.2f, 120.0f, 90.0f, 0.85f, Fresh, Length);
			return;
		}
	}
	FTunnel& Tunnel = Tunnels.AddDefaulted_GetRef();
	Tunnel.Start = Start;
	Tunnel.End = End;
	Tunnel.RadiusCm = RadiusCm;
	Tunnel.bVacuum = OSAirZoneAt(GetWorld(), Where) == nullptr;
	if (Tunnels.Num() > MaxTunnels)
	{
		Tunnels.RemoveAt(0, Tunnels.Num() - MaxTunnels);
	}
	// The grid: the gas in the tunnel thrown aside and a push out of it and along it - when the drawn hole heals,
	// the grid already has it and its inflow closes it with swirls. No vortex ring per pellet: a mild push.
	Blast(Start, Along / Length, RadiusCm * 1.2f, 120.0f, 90.0f, 0.85f, Fresh, Length);
}

void UOSMuzzleFxComponent::TickClouds(float DeltaTime)
{
	UWorld* World = GetWorld();
	const FVector Eye = View->GetComponentLocation();
	// The pawn's body for the material's soft hole (its push is the movers' path, FindMovers).
	const FVector Body = GetOwner()->GetActorLocation();
	float BodyRadius = 0.0f, BodyHalfHeight = 0.0f;
	GetOwner()->GetSimpleCollisionCylinder(BodyRadius, BodyHalfHeight);
	BodyRadius = FMath::Clamp(BodyRadius, 15.0f, 45.0f);
	TSet<TObjectKey<UPrimitiveComponent>> SeenMovers;
	// The tunnels heal; a healed one goes.
	for (int32 Index = Tunnels.Num() - 1; Index >= 0; --Index)
	{
		FTunnel& Tunnel = Tunnels[Index];
		Tunnel.Age += DeltaTime;
		if (Tunnel.Age >= (TunnelHoldSeconds + TunnelHealSeconds) * (Tunnel.bVacuum ? 2.0f : 1.0f))
		{
			Tunnels.RemoveAt(Index);
		}
	}
#if !UE_BUILD_SHIPPING
	// Tuning overrides (os.Fx.SmokeTune): the fields on every gun when the string changes, the material's every frame.
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
				for (FOSMuzzleFx& Each : Guns)
				{
					// "Sim.Buoyancy": a field of the nested struct.
					UScriptStruct* Struct = FOSMuzzleFx::StaticStruct();
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
						UE_LOG(LogMindKore, Warning, TEXT("os.Fx.SmokeTune: no field %s"), *Key);
						break;
					}
				}
			}
		}
		if (Tune != TuneApplied)
		{
			UE_LOG(LogMindKore, Log, TEXT("os.Fx.SmokeTune: %s"), *Tune);
			TuneApplied = Tune;
		}
	}
#endif
	// The last frame's steps: their box shift and atlas (TickComponent waited for them).
	for (FCloud& Cloud : Clouds)
	{
		if (Cloud.Age < 0.0f || Cloud.IsBusy() || !Cloud.bPendingAtlas)
		{
			continue;
		}
		Cloud.bPendingAtlas = false;
#if !UE_BUILD_SHIPPING
		if (Cloud.bHasGas && !Cloud.bShown)
		{
			Cloud.bShown = true;
			UE_LOG(LogMindKore, Log, TEXT("Smoke latency: gas drawn %d frame(s) after the shot (stamp %d)"), int32(GFrameCounter - Cloud.ShotFrame), CVarSmokeStamp.GetValueOnGameThread());
		}
#endif
		// The box follows the gas by whole cells: the voxels stay where they are in the world.
		Cloud.Centre += Cloud.Basis.RotateVector(FVector(Cloud.PendingShift) * Cloud.Sim->CellCm);
		const FIntPoint Size = FOSSmokeSim::AtlasSize(Cloud.Sim->N);
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
		const FOSMuzzleFx& G = Guns[Cloud.Gun];
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
		const FOSMuzzleFx& G = Guns[Cloud.Gun];
		FOSSmokeSim& Sim = *Cloud.Sim;
		const bool bBusy = Cloud.IsBusy();
		Cloud.Age += DeltaTime;
		const bool bEvicted = Cloud.Evict > 0.0f && (Cloud.Evict -= DeltaTime) <= 0.0f;
		if (Cloud.Age >= G.CloudSeconds || bEvicted)
		{
			Cloud.Age = -1.0f;
			Cloud.Volume->SetVisibility(false);
			continue;
		}
		if (!bBusy)
		{
			// A grown vacuum cloud's cells are grown too (a push or a wake works at them).
			Sim.CellCm = G.CellCm * Cloud.Scale;
			// Out of an air zone the gas turns ballistic, into one the air takes it: read every frame.
			Sim.bAir = OSAirZoneAt(World, Cloud.Centre) != nullptr;
			Sim.Down = FVector3f(Cloud.Basis.UnrotateVector(OSGravityAt(World, Cloud.Centre).Acceleration.GetSafeNormal()));
		}
		if (Sim.bAir)
		{
			Cloud.Carried *= FMath::Exp(-DeltaTime / AirDragSeconds);
		}
		// The box rides the carried velocity (and a frozen cloud's rise); the floor stays where it is in the world.
		const FVector Move = (Cloud.Carried + Cloud.Rise) * DeltaTime;
		if (!Move.IsNearlyZero(0.01f))
		{
			Cloud.Centre += Move;
			Cloud.PlaneMove += FVector3f(Cloud.Basis.UnrotateVector(Move));
		}
		if (!bBusy && !Cloud.Deferred.IsEmpty())
		{
			for (const FCloud::FDeferredBlast& D : Cloud.Deferred)
			{
				BlastCloud(Cloud, D.Origin, D.Direction, D.RadiusCm, D.Along, D.Push, D.Clear, D.LengthCm);
			}
			Cloud.Deferred.Reset();
		}
		if (!bBusy && !Cloud.PlaneMove.IsNearlyZero(0.01f))
		{
			for (FPlane4f& Plane : Sim.Planes)
			{
				Plane.W -= Plane.GetSafeNormal() | Cloud.PlaneMove;
			}
			Cloud.PlaneMove = FVector3f::ZeroVector;
			if (!Sim.Planes.IsEmpty())
			{
				Sim.RebuildSolids();
			}
		}
		if (bBusy)
		{
			// Mid-step: its time piles up for the next launch (at most 4 steps: a long hitch slows the gas instead).
			Cloud.StepCarry += DeltaTime;
		}
		else if (Stepping.Contains(&Cloud))
		{
			Cloud.bFrozen = false;
			Cloud.Rise = FVector::ZeroVector;
			Cloud.StepCarry += DeltaTime;
			// Whole fixed steps; a long frame does not pile them up (at most 4: a hitch slows the gas instead).
			const float Since = Cloud.Age - Cloud.StepCarry;
			const float Dt = Since < FastSeconds ? SimStep : Since < LateSeconds ? SlowStep : LateStep;
			const int32 Steps = FMath::Min(FMath::FloorToInt(Cloud.StepCarry / Dt + 1e-3f), 4);
			if (Steps > 0)
			{
				Launches.Add({ &Cloud, Steps, Cloud.Age - Cloud.StepCarry + Dt, Dt });
			}
			Cloud.StepCarry = Steps == 4 ? 0.0f : FMath::Max(0.0f, Cloud.StepCarry - Steps * Dt);
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
			// Its spin: angular momentum over the moment of inertia about the gas's centre - kept in a vacuum, the
			// frozen cloud keeps turning (v6: a vacuum cloud must still look alive, eerily).
			if (!Sim.bAir)
			{
				float SpinMass;
				const FVector3f C = Sim.Centroid(SpinMass) * Sim.CellCm;
				FVector3f L = FVector3f::ZeroVector;
				float Inertia = 0.0f;
				for (int32 Z = 0; Z < Sim.N; ++Z)
				{
					for (int32 Y = 0; Y < Sim.N; ++Y)
					{
						for (int32 X = 0; X < Sim.N; ++X)
						{
							const int32 I = Sim.Index(X, Y, Z);
							if (Sim.Rho[I] > 0.004f)
							{
								const FVector3f P = Sim.CellCentre(X, Y, Z) - C;
								L += (P ^ FVector3f(Sim.Ux[I], Sim.Uy[I], Sim.Uz[I])) * Sim.Rho[I];
								Inertia += P.SizeSquared() * Sim.Rho[I];
							}
						}
					}
				}
				Cloud.Spin = L / FMath::Max(Inertia, 1.0f);
				// At least a slow turn, at most a visible one (rad/s).
				const float W = FMath::Clamp(Cloud.Spin.Size(), 0.06f, 0.35f);
				Cloud.Spin = Cloud.Spin.GetSafeNormal(UE_SMALL_NUMBER, FVector3f(FMath::VRand())) * W;
			}
			const float Radial = FMath::Max(Sim.MeanRadialSpeed(), 30.0f);
			Cloud.ExpandRate = Sim.bAir ? 0.0f : FMath::Clamp(Radial / (0.5f * Sim.N * G.CellCm), 0.05f, 0.35f);
		}
		else
		{
			Cloud.Rise *= FMath::Exp(-DeltaTime / 2.0f);
			Cloud.Scale += Cloud.ExpandRate * DeltaTime;
			if (!Cloud.Spin.IsNearlyZero())
			{
				Cloud.Basis = Cloud.Basis * FQuat(FVector(Cloud.Spin.GetSafeNormal()), Cloud.Spin.Size() * DeltaTime);
			}
		}
		const float Half = 0.5f * Sim.N * G.CellCm * Cloud.Scale;
		// Everything in the box - props, the pawn, flyers, cases, anything moved by physics, a hand or a door - is a
		// moving body in the grid (its next steps) and carved in the material now (v9; the pawn's push and the flyers'
		// wakes were special cases of it). The voxels by the eye still fade in the material.
		const FVector EyeLocal = Cloud.Basis.UnrotateVector(Eye - Cloud.Centre);
		const FVector BodyLocal = Cloud.Basis.UnrotateVector(Body - Cloud.Centre);
		FindMovers(Cloud, Half, DeltaTime, SeenMovers);
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
		// In a vacuum the thinning by the growth does the dying; erosion on top of it left nothing by 2 s.
		Material->SetScalarParameterValue(TEXT("Erode"), Sim.bAir ? G.Erode : G.Erode * 0.35f);
		// The end is a process, not an event (v6, owner: «дым резко пропадает»): over the last 40% of its life the
		// density sinks to nothing - thin parts fall below the lowest band first, holes grow, strands thin - and an
		// evicted cloud does the same over EvictSeconds.
		const float LifeFade = 1.0f - FMath::SmoothStep(0.6f * G.CloudSeconds, G.CloudSeconds, Cloud.Age);
		const float EvictFade = Cloud.Evict > 0.0f ? Cloud.Evict / EvictSeconds : 1.0f;
		Material->SetScalarParameterValue(TEXT("Gain"), LifeFade * EvictFade);
		// The key light: the brightest at the cloud now (the flash while it burns); the grid's shadow is packed toward it.
		// Eased (tau 0.25 s, the same at any frame rate): a change of key light never pops.
		{
			FVector3f Dir;
			FLinearColor Colour;
			PickKey(Cloud.Centre, Cloud.Basis, Dir, Colour);
			const float Ease = 1.0f - FMath::Exp(-DeltaTime / 0.25f);
			Cloud.KeyDir = (Cloud.KeyDir + (Dir - Cloud.KeyDir) * Ease).GetSafeNormal(UE_SMALL_NUMBER, Dir);
			Cloud.KeyColour = Cloud.KeyColour + (Colour - Cloud.KeyColour) * Ease;
			Cloud.KeyName = LastKeyName;
		}
		Material->SetVectorParameterValue(TEXT("KeyColour"), Cloud.KeyColour);
		// The drawn pixel: ~5 px at 1080p at any distance (the bodycam's FOV 134: 4.71 view widths per distance).
		Material->SetScalarParameterValue(TEXT("PixelCm"), VoxelPixelCm * Cloud.Scale);
		Material->SetScalarParameterValue(TEXT("AmbientShare"), Sim.bAir ? 1.0f : VacuumAmbientShare);
		Material->SetScalarParameterValue(TEXT("Seed"), Cloud.Seed);
		// The volume integrates per local unit (a cell here, HeterogeneousVolumesLiveShadingPipeline.usf: StepSize is
		// world / LocalToWorldScale): extinction and emission per cm times the cell's cm.
		Material->SetScalarParameterValue(TEXT("Extinction"), G.DenseExtinction);
		Material->SetScalarParameterValue(TEXT("UnitCm"), G.CellCm * Cloud.Scale);
		Material->SetVectorParameterValue(TEXT("Eye"), FLinearColor(EyeLocal.X, EyeLocal.Y, EyeLocal.Z, EyeClearCm));
		Material->SetVectorParameterValue(TEXT("Body"), FLinearColor(BodyLocal.X, BodyLocal.Y, BodyLocal.Z, BodyRadius));
		UpdateTunnels(Cloud, Half);
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
	// Air leaving through a hole: each cloud in reach drifts toward it (the whole box: Carried), faster the closer,
	// and the gas at the hole is blown out along the flow.
	for (int32 Index = AirFlows.Num() - 1; Index >= 0; --Index)
	{
		FAirFlow& Flow = AirFlows[Index];
		Flow.Seconds -= DeltaTime;
		if (Flow.Seconds <= 0.0f)
		{
			AirFlows.RemoveAt(Index);
			continue;
		}
		for (FCloud& Cloud : Clouds)
		{
			const FVector ToHole = Flow.Location - Cloud.Centre;
			const float Distance = ToHole.Size();
			if (Cloud.Age < 0.0f || Distance > Flow.ReachCm)
			{
				continue;
			}
			const float Pull = Flow.SpeedCm * FMath::Square(1.0f - Distance / Flow.ReachCm);
			Cloud.Carried += (ToHole / FMath::Max(Distance, 1.0f) * Pull - Cloud.Carried) * (1.0f - FMath::Exp(-DeltaTime / 0.3f));
			Cloud.Awake = WakeSeconds;
		}
		Blast(Flow.Location - Flow.Direction * 30.0f, Flow.Direction, 40.0f, Flow.SpeedCm, 0.0f, 0.0f, nullptr, 200.0f);
	}
	// Bodies not seen in any cloud this frame start afresh next time (no stale velocity).
	for (auto It = LastMoverAt.CreateIterator(); It; ++It)
	{
		if (!SeenMovers.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
	// The steps, off the game thread while the rest of the frame runs (each grid its own task; ParallelFor inside):
	// the muzzle's stem, the steps, the box's follow (taken next frame), the atlas.
	for (const FLaunch& L : Launches)
	{
		FCloud* Cloud = L.Cloud;
		const FOSMuzzleFx G = Guns[Cloud->Gun];
		const FVector3f KeyDir = Cloud->KeyDir;
		const float Occlusion = AmbientOcclusion, Shadow = KeyShadow, Sharp = Sharpen;
		// The box as of the launch: the game thread moves it on while the step runs.
		const FVector Centre = Cloud->Centre;
		const FQuat Basis = Cloud->Basis;
		const TArray<FOSSmokeSim::FMover> Movers = Cloud->Movers;
		Cloud->Pending = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Cloud, G, L, KeyDir, Occlusion, Shadow, Sharp, Centre, Basis, Movers]()
		{
			FOSSmokeSim& Sim = *Cloud->Sim;
			for (int32 Step = 0; Step < L.Steps; ++Step)
			{
				const float At = L.FirstAt + Step * L.Dt;
				// The vents' jets: each step the gas the vent let out since the last one, laid along the jet from its old
				// front to the new (the front runs out at the vent's speed dying with DecaySeconds), widening with the
				// distance and moving on at the jet's speed there - a long thin jet that frays, never a ball (v4). Laid,
				// not left to the advection alone: a jet a cell or two wide was smeared to nothing on the 5 cm grid.
				for (FCloud::FJet& Jet : Cloud->Jets)
				{
					LayJet(Sim, Jet, Basis, Centre, At);
				}
#if !UE_BUILD_SHIPPING
				const double T0 = FPlatformTime::Seconds();
#endif
				Sim.Movers = Movers;
				for (FOSSmokeSim::FMover& M : Sim.Movers)
				{
					M.Centre += M.Velocity * (Step * L.Dt);
				}
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
			Sim.Pack(Cloud->Atlas, KeyDir, Shadow, Occlusion, Sharp);
			Cloud->bHasGas = Mass > 0.01f;
			Cloud->bPendingAtlas = true;
		});
	}
}

void UOSMuzzleFxComponent::HotCase(AActor* Case)
{
	if (Case)
	{
		HotCases.Add({ Case, 1.0f, 0.0f });
	}
}

UOSMuzzleFxComponent::FPuff& UOSMuzzleFxComponent::AddCardPuff(const FVector& Where, const FVector& Direction, float StartCm, float EndCm, float Opacity, float Seconds)
{
	const bool bAir = OSAirZoneAt(GetWorld(), Where) != nullptr;
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
		Puff.Lift = -OSGravityAt(GetWorld(), Where).Acceleration * 0.12f;
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

void UOSMuzzleFxComponent::TickHotCases(float DeltaTime)
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
		if (UOSGasSubsystem* Gas = UOSGasSubsystem::Get(this); Gas && !OSGasCardsEnabled())
		{
			// v11: a hot case lets out one small parcel a second (contract 2.2), no cards.
			Hot.Carry += DeltaTime;
			if (Hot.Carry >= 1.0f)
			{
				Hot.Carry -= 1.0f;
				FOSGasRecipe R;
				R.SpeedCm = 10.0f;
				R.ConeDeg = 30.0f;
				R.RadiusMinCm = R.RadiusMaxCm = 1.5f;
				R.Mass = 0.2f;
				R.Heat = 0.5f;
				R.LifeSeconds = 2.5f;
				Gas->EmitNow(R, GunSmoke(), Where, -OSGravityAt(GetWorld(), Where).Acceleration.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector), Velocity * 0.3f, 1);
			}
			continue;
		}
		bool bInCloud = false;
		for (FCloud& Cloud : Clouds)
		{
			// Air only: in a vacuum nothing drags a wisp after the case (and a woken vacuum cloud would stop growing).
			if (Cloud.Age < 0.0f || !Cloud.Sim->bAir)
			{
				continue;
			}
			FOSSmokeSim& Sim = *Cloud.Sim;
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
			Hot.Carry += 14.0f * Hot.Heat * DeltaTime;
			for (; Hot.Carry >= 1.0f; Hot.Carry -= 1.0f)
			{
				FPuff& Puff = AddCardPuff(Where, Velocity * 0.02f + FMath::VRand() * 0.5f, 1.5f, 10.0f, 0.45f * FMath::Min(Hot.Heat * 1.5f, 1.0f), 1.8f);
				Puff.Age = FMath::FRand() * DeltaTime;
			}
		}
	}
}

namespace
{
	/** A smoke texel's size per cm from the eye: ~5 px of 1920 at the bodycam's FOV 134 (2 tan 67 deg = 4.71 view widths per distance). */
	constexpr float TexelPerCm = 5.0f / 1920.0f * 4.71f;
	/** A clinging wisp takes this share of the gun's own move (v5: walking, the wisps trail the barrel). */
	constexpr float ClingInherit = 0.5f;
}

void UOSMuzzleFxComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
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
	for (const FCloud& Cloud : Clouds)
	{
		if (Cloud.Pending.IsValid() && Cloud.Pending.IsCompleted())
		{
			Cloud.Pending = UE::Tasks::FTask();
		}
	}
	TickHotCases(DeltaTime);
	KeyLightsAge += DeltaTime;
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
	// The vents' streams: wisps at their Rate (dying with DecaySeconds) while they flow, the fraction carried over -
	// chained one after another along the stream (streaks, not separate puffs); the same count at any frame rate.
	for (int32 Index = Flows.Num() - 1; Index >= 0; --Index)
	{
		FFlow& Flow = Flows[Index];
		const float Share = FMath::Exp(-Flow.Age / Flow.Vent.DecaySeconds);
		Flow.Carry += Flow.Vent.Rate * Share * DeltaTime;
		for (int32 Born = FMath::FloorToInt(Flow.Carry); Born > 0; --Born)
		{
			Flow.Carry -= 1.0f;
			EmitWisp(Flow.Vent, Flow.Look, Flow.Muzzle, Flow.Action.Get(), Share, FMath::FRand() * DeltaTime);
		}
		Flow.Age += DeltaTime;
		if (Flow.Age >= Flow.Vent.Seconds)
		{
			Flows.RemoveAt(Index);
		}
	}
	for (int32 Index = Puffs.Num() - 1; Index >= 0; --Index)
	{
		FPuff& Puff = Puffs[Index];
		const float Before = Puff.Age;
		Puff.Age += DeltaTime;
		if (Puff.Age >= Puff.Life)
		{
			Puffs.RemoveAt(Index);
			continue;
		}
		if (Puff.ClingSeconds > 0.0f)
		{
			// A clinging wisp follows its path on the gun with a weight dying from 1 (the gun's motion and its own creep
			// and swirl), then the world takes it: in air its rise against the drag, in a vacuum the gun's velocity kept.
			const float Cling = FMath::Exp(-Puff.Age / Puff.ClingSeconds);
			if (const USceneComponent* Anchor = Puff.Anchor.Get())
			{
				const FTransform Gun = Anchor->GetComponentTransform();
				const FVector Path = ClingPath(Puff, Puff.Age);
				const FVector Attached = Gun.TransformPosition(Path);
				// The path's own creep and swirl (on the gun as it was), and the gun's move since: half of it inherited
				// (v5: walking, the wisps rode along with the barrel; now they trail behind it). Velocities, never a jump.
				const FVector Own = Puff.PrevGun.TransformPosition(Path) - Puff.PrevAttached;
				const FVector Carry = Attached - Puff.PrevGun.TransformPosition(Path);
				const FVector Step = (Own + Carry * ClingInherit) * Cling;
				Puff.WorldPos += Step;
				Puff.PrevAttached = Attached;
				Puff.PrevGun = Gun;
				// Drawn out along its own path (a streak round and back along the barrel), the barrel's axis when still.
				Puff.AlongWorld = Step.SizeSquared() > 1e-4f ? Step.GetSafeNormal() : Gun.GetRotation().GetAxisX();
			}
			Puff.WorldPos += Puff.Velocity * (1.0f - Cling) * DeltaTime;
			const auto Rise = [&Puff](float T)
			{
				return Puff.LiftDragSeconds > 0.0f
					? Puff.Lift * Puff.LiftDragSeconds * (T - Puff.LiftDragSeconds * (1.0f - FMath::Exp(-T / Puff.LiftDragSeconds)))
					: FVector::ZeroVector;
			};
			// The rise only as the gun lets go (v3: the wisps went straight up instead of hugging the shroud).
			Puff.WorldPos += (Rise(Puff.Age) - Rise(Before)) * (1.0f - Cling);

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
		if (Puff.ClingSeconds > 0.0f)
		{
			// ...a clinging one where its path put it, long along the barrel.
			World = Puff.WorldPos;
			Along = Puff.AlongWorld;
		}
		else if (Puff.LiftDragSeconds > 0.0f)
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
		const float Opacity = Puff.Opacity * FMath::SmoothStep(0.0f, Puff.FadeInSeconds, Puff.Age) * FMath::Pow(1.0f - X, 1.5f);
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
			UE_LOG(LogMindKore, Log, TEXT("Smoke moments: %s - eye %s, pawn %.0f cm/s, %d card puff(s), %d hot case(s), %d tunnel(s);%s"), *Name, *Eye().ToCompactString(), Pawn->GetVelocity().Size(), Fx->GetPuffCount(), Fx->GetHotCaseCount(), Fx->GetTunnelCount(), *Fx->DescribeClouds());
			Fx->DumpClouds(Name);
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

		if (Args.Contains(TEXT("room")) || Args.Contains(TEXT("roomspace")))
		{
			// The owner's screenshot (22.10): the RMB-93 in hand, a shot at the floor ~2.5 m ahead in Lvl_OS_TestRoom
			// (lit, air, gravity); "roomspace": the same shot on Lvl_OS_ZeroG into the black (vacuum), the pawn held
			// at the stand after the recoil (the stand's teleport, not play) so every still has the same framing.
			const bool bSpace = Args.Contains(TEXT("roomspace"));
			const FString Tag = bSpace ? TEXT("roomspace") : TEXT("room");
			FVector Stand = Pawn->Body->GetComponentLocation();
			FRotator Look(-25.0f, Controller->GetControlRotation().Yaw, 0.0f);
			if (bSpace)
			{
				if (TActorIterator<AOSLivingWall> Wall(World); Wall)
				{
					Stand = Wall->GetActorLocation() + Wall->GetFrontNormal() * 600.0f;
					Look = FRotator(-10.0f, (-Wall->GetFrontNormal().RotateAngleAxis(45.0f, FVector::UpVector)).Rotation().Yaw, 0.0f);
				}
			}
			const auto ArmRmb = [Pawn, Fx]()
			{
				UOSWeaponsComponent* Guns = Pawn->Guns;
				Guns->ApplyPickupWeapon(1, OSMakeRmb93(GetDefault<UOSWeaponsComponent>()->Weapons[1]));
				Guns->Scope->Aims[1] = OSMakeRmb93Aim(GetDefault<UOSScopeComponent>()->Aims[1]);
				Guns->UnlockWeapon(1);
				Guns->FinishSwitch();
				for (FOSWeapon& Weapon : Guns->Weapons)
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
			// The warm-up shot's cost (first-time work, shaders) logged apart: the shots after it are the measure.
			After(8.5f, [Controller]() { Controller->ConsoleCommand(TEXT("os.Fx.SmokeCost")); });
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
			After(17.0f, [Fire, Frames, Still, After, Tag, Hold, Pawn, Controller, Eye, Look]()
			{
				Fire(Tag + TEXT("_b_shot1"));
				After(1.5f, [Fire, Frames, Still, After, Tag, Hold, Pawn, Controller, Eye, Look]()
				{
					Hold();
					Fire(Tag + TEXT("_b_shot2"));
					Frames(Tag + TEXT("_b_shot2"));
					// The hull smoking on the floor, first person (the card puffs are drawn for their owner only); the
					// view back on the cloud before the settled still.
					for (const float T : { 0.8f, 1.2f })
					{
						After(T, [Pawn, Controller, Eye, Still, Tag, T]()
						{
							if (const AActor* Case = Pawn->Guns->GetLastDebris())
							{
								Controller->SetControlRotation((Case->GetActorLocation() - Eye()).Rotation());
							}
							Still(FString::Printf(TEXT("%s_b_shell_%04dms"), *Tag, FMath::RoundToInt(T * 1000.0f)));
						});
					}
					After(1.3f, [Controller, Look]() { Controller->SetControlRotation(Look); });
					After(1.5f, [Still, Tag, Hold]() { Hold(); Still(Tag + TEXT("_b_shot2_settled_1500ms")); });
				});
			});
			After(21.5f, [Controller]() { Controller->ConsoleCommand(TEXT("os.Fx.SmokeCost")); });
			After(22.0f, []() { UE_LOG(LogMindKore, Log, TEXT("Smoke moments: done")); });
			return;
		}

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
				UE_LOG(LogMindKore, Log, TEXT("Smoke moments: ProfileGPU with %d cloud(s) alive, %d tunnel(s);%s"), Fx->GetCloudCount(), Fx->GetTunnelCount(), *Fx->DescribeClouds());
				Controller->ConsoleCommand(TEXT("ProfileGPU"));
			});
			After(11.5f, [Still]() { Still(TEXT("budget_three_shotgun_clouds")); });
			After(12.0f, [Controller]() { Controller->ConsoleCommand(TEXT("os.Fx.SmokeCost")); });
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
		TEXT("Stills of the smoke's moments in play on Lvl_OS_Range (run with -UseFixedTimeStep -FPS=60): on foot, holding on, a pistol burst, an SV-98 shot drifting off and back (or only those named: foot, cling, burst, sv98). 'budget': three shotgun clouds on the os.Weapons.Fx stand, then ProfileGPU. 'room': the RMB-93 at the floor of Lvl_OS_TestRoom, 'roomspace': the same shot on Lvl_OS_ZeroG into the black."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunSmokeMoments));

#if !UE_BUILD_SHIPPING
	/**
	 * os.Gas.WalkProbe: a shot while walking ahead at 3 m/s, a stop, 1.2 s still, 1 m back, turns 90 degrees, walks 3 m on
	 * an arc round the cloud and 2 m back (the body's velocity set every frame). Logs the youngest cloud's parcel centroid,
	 * box centre and pawn distance every 0.25 s; HighResShot OSWalk_* at the shot, 1 s, the step back and the turn
	 * (os.Gas.Debug 1 draws the gas's insides).
	 */
	TAutoConsoleVariable<FString> CVarGasProbeSpawn(TEXT("os.Gas.ProbeSpawn"), TEXT(""), TEXT("A preset: os.Gas.WalkProbe near spawns it (os.Gas.Spawn) instead of each shot, and runs 24 s."));

	TAutoConsoleVariable<float> CVarGasProbeSeconds(TEXT("os.Gas.ProbeSeconds"), 0.0f, TEXT("> 0: os.Gas.WalkProbe runs this long and takes a HighResShot (OSWalk_4_end) half a second and a ProfileGPU 2 s before its end (the haze's frames and cost)."));
	TAutoConsoleVariable<FString> CVarGasProbeSpawnLate(TEXT("os.Gas.ProbeSpawnLate"), TEXT(""), TEXT("A preset (or a whole os. command): os.Gas.WalkProbe spawns (runs) it once at 5 s (after its shots)."));

	TAutoConsoleVariable<int32> CVarGasProbeSuppressor(TEXT("os.Gas.ProbeSuppressor"), 0, TEXT("1: os.Gas.WalkProbe mounts the suppressor first."));

	TAutoConsoleVariable<int32> CVarGasCapture(TEXT("os.Gas.Capture"), 0, TEXT("1: os.Gas.WalkProbe saves every frame from 0.2 to 3.2 s (a self-check)."));

	FAutoConsoleCommandWithWorldAndArgs GGasWalkProbe(
		TEXT("os.Gas.WalkProbe"),
		TEXT("[after s] Shoot while walking, stop, step back, turn, walk round the cloud; log the cloud's centroid every 0.25 s (and HighResShot OSWalk_*)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			// [after s]: starts that many seconds later (the pawn spawned, the shaders warm).
			const float Delay = Args.Num() > 0 ? FMath::Max(FCString::Atof(*Args[0]), 0.01f) : 0.01f;
			FTimerHandle Later;
			// [after s] back: walking back at 4 m/s, five shots 0.3 s apart (owner 23.10: the gas flew at him).
			const bool bBack = Args.Num() > 1 && Args[1] == TEXT("back");
			// [after s] back [shots] [interval s]: how many (5), how far apart (0.3 s). near: standing, two shots, then the
			// shots 2.5 s later beside their clouds.
			// space: as near, but facing the farthest open way (8 tried, 30 m) - a vacuum's cone seen against the stars.
			// side: as space, and 0.05 s after the first shot the view turns 55 degrees in 0.2 s (the cone seen from the side, still in view).
			const bool bSide = Args.Num() > 1 && Args[1] == TEXT("side");
			const bool bSpace = Args.Num() > 1 && (Args[1] == TEXT("space") || bSide);
			// living: as near, facing the nearest living wall.
			const bool bLiving = Args.Num() > 1 && Args[1] == TEXT("living");
			const bool bNear = Args.Num() > 1 && (Args[1] == TEXT("near") || bSpace || bLiving);
			const int32 BackCount = Args.Num() > 2 ? FCString::Atoi(*Args[2]) : 5;
			const float BackEvery = Args.Num() > 3 ? FMath::Max(FCString::Atof(*Args[3]), 0.05f) : 0.3f;
			World->GetTimerManager().SetTimer(Later, FTimerDelegate::CreateLambda([World, bBack, bNear, bSpace, bSide, bLiving, BackCount, BackEvery]()
			{
			APlayerController* Controller = World->GetFirstPlayerController();
			AOSZeroGPawn* Pawn = Controller ? Cast<AOSZeroGPawn>(Controller->GetPawn()) : nullptr;
			UOSGasSubsystem* Gas = UOSGasSubsystem::Get(World);
			if (!Pawn || !Gas || !Pawn->Body)
			{
				UE_LOG(LogMindKore, Warning, TEXT("Walk probe: no zero-G pawn or no gas"));
				return;
			}
			// os.Gas.ProbeSuppressor 1: the suppressor on the gun first (its MuzzleJetScale throttles the muzzle's gas).
			if (IConsoleManager::Get().FindConsoleVariable(TEXT("os.Gas.ProbeSuppressor"))->GetInt() > 0 && Pawn->Guns)
			{
				Pawn->Guns->GiveAttachment(TEXT("Suppressor"), true);
				Pawn->Guns->MountAttachment(TEXT("Suppressor"), 0);
			}
			Gas->ClearClouds();
			Gas->DrawnSetChanges = 0;
			Gas->TakeCostLine();
			const double Start = World->GetTimeSeconds();
			const FVector Home = Pawn->Body->GetComponentLocation();
			FRotator Look = Controller->GetControlRotation();
			// Walking back: the room behind, not a wall (else turned round).
			if (bBack)
			{
				FHitResult Hit;
				const FVector Behind = Home - FRotator(0.0f, Look.Yaw, 0.0f).Vector() * 300.0f;
				if (World->LineTraceSingleByChannel(Hit, Home, Behind, ECC_Visibility, FCollisionQueryParams(SCENE_QUERY_STAT(OSWalkProbe), false, Pawn)))
				{
					Look.Yaw += 180.0f;
				}
			}
			if (bLiving)
			{
				const AOSLivingWall* Nearest = nullptr;
				for (TActorIterator<AOSLivingWall> It(World); It; ++It)
				{
					if (!Nearest || FVector::Dist(It->GetActorLocation(), Home) < FVector::Dist(Nearest->GetActorLocation(), Home))
					{
						Nearest = *It;
					}
				}
				if (Nearest)
				{
					Look = (Nearest->GetActorLocation() - Home).Rotation();
					Look.Pitch += 10.0f;
					UE_LOG(LogMindKore, Log, TEXT("Walk probe: facing the living wall %s at %.0f cm"), *Nearest->GetName(), FVector::Dist(Nearest->GetActorLocation(), Home));
				}
			}
			if (bSpace)
			{
				float Best = -1.0f;
				const float Yaw0 = Look.Yaw;
				for (int32 K = 0; K < 8; ++K)
				{
					const FVector Way = FRotator(0.0f, Yaw0 + K * 45.0f, 0.0f).Vector();
					FHitResult Hit;
					const float Free = World->LineTraceSingleByChannel(Hit, Home, Home + Way * 3000.0f, ECC_Visibility, FCollisionQueryParams(SCENE_QUERY_STAT(OSWalkProbe), false, Pawn)) ? Hit.Distance : 3000.0f;
					if (Free > Best)
					{
						Best = Free;
						Look.Yaw = Yaw0 + K * 45.0f;
					}
				}
				UE_LOG(LogMindKore, Log, TEXT("Walk probe: facing yaw %.0f, open %.0f cm"), Look.Yaw, Best);
			}
			Controller->SetControlRotation(Look);
			// os.Gas.DebugAlpha 1: the gas's opacity a frame, per screen block; its largest jump a frame (the gun's corner
			// left out: it recoils over the gas), the frames over 0.25.
			TSharedRef<TArray<float>> LastBlocks = MakeShared<TArray<float>>();
			TSharedRef<FVector4> Alpha = MakeShared<FVector4>(0.0, 0.0, 0.0, 0.0);
			// The probe's clock and its last shot, for the jumps' log (a jump within 2 frames of a shot is the shot's).
			TSharedRef<FVector2D> Clock = MakeShared<FVector2D>(0.0, -1.0);
			TSharedRef<int32> JumpLines = MakeShared<int32>(0);
			// Only at 1: the delegate takes every screenshot (os.Gas.Capture's too) instead of saving it.
			if (GEngine->GameViewport && IConsoleManager::Get().FindConsoleVariable(TEXT("os.Gas.DebugAlpha"))->GetInt() == 1)
			{
				GEngine->GameViewport->OnScreenshotCaptured().AddLambda([LastBlocks, Alpha, Clock, JumpLines](int32 Width, int32 Height, const TArray<FColor>& Pixels)
				{
					const int32 Block = FMath::Max(1, FMath::RoundToInt(Height / 270.0f));
					const int32 BX = Width / Block, BY = Height / Block;
					if (Pixels.Num() < Width * Height || BX <= 0 || BY <= 0)
					{
						return;
					}
					TArray<float> Blocks;
					Blocks.SetNumZeroed(BX * BY);
					for (int32 Y = 0; Y < BY * Block; ++Y)
					{
						for (int32 X = 0; X < BX * Block; ++X)
						{
							Blocks[(Y / Block) * BX + X / Block] += Pixels[Y * Width + X].R / (255.0f * Block * Block);
						}
					}
					if (LastBlocks->Num() == Blocks.Num())
					{
						float Most = 0.0f;
						int32 MostAt = 0;
						for (int32 I = 0; I < Blocks.Num(); ++I)
						{
							// The gun's corner left out: it recoils over the gas.
							if (I % BX > BX / 2 && I / BX > BY * 45 / 100)
							{
								continue;
							}
							const float Jump = FMath::Abs(Blocks[I] - (*LastBlocks)[I]);
							// The muzzle's jet in its first 0.25 s (round the aim, 45 blocks of 270 rows): a front at
							// 4.5 m/s half a metre from the eye crosses ~30 blocks a frame at 60 FPS - speed, not a jerk.
							const float MX = I % BX - BX * 0.5f, MY = I / BX - BY * 0.575f;
							if (Clock->Y >= 0.0 && Clock->X - Clock->Y < 0.25 && MX * MX + MY * MY < FMath::Square(BY / 6.0f))
							{
								Alpha->W = FMath::Max(Alpha->W, double(Jump));
								continue;
							}
							if (Jump > Most)
							{
								Most = Jump;
								MostAt = I;
							}
						}
						// A shot's own frames (0.05 s after it) are not counted: its blast appears at once by design.
						const bool bShot = Clock->Y >= 0.0 && Clock->X - Clock->Y < 0.05;
						if (!bShot)
						{
							Alpha->X = FMath::Max(Alpha->X, double(Most));
							Alpha->Y += Most > 0.25f ? 1.0 : 0.0;
							Alpha->Z += 1.0;
						}
						if (Most > 0.25f && *JumpLines < 40)
						{
							++*JumpLines;
							UE_LOG(LogMindKore, Log, TEXT("Walk probe: jump %.2f at block (%d, %d) of (%d, %d), t %.3f s, %.3f s after a shot%s"),
								Most, MostAt % BX, MostAt / BX, BX, BY, Clock->X, Clock->X - Clock->Y, bShot ? TEXT(" (the shot's)") : TEXT(""));
						}
					}
					*LastBlocks = MoveTemp(Blocks);
				});
			}
			TSharedRef<FVector> First = MakeShared<FVector>(FVector::ZeroVector);
			TSharedRef<int32> Logged = MakeShared<int32>(-1);
			TSharedRef<int32> Shots = MakeShared<int32>(0);
			TSharedRef<bool> Fired = MakeShared<bool>(false);
			// Per frame: the largest jump of the centroid and of the drawn box (same cloud), and the farthest the
			// centroid came toward the pawn's start.
			TSharedRef<FVector4> Jumps = MakeShared<FVector4>(0.0, 0.0, 0.0, 0.0);
			TSharedRef<FVector> LastC = MakeShared<FVector>(FVector::ZeroVector);
			TSharedRef<FVector> LastB = MakeShared<FVector>(FVector::ZeroVector);
			TSharedRef<int32> LastN = MakeShared<int32>(0);
			TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
			World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([World, Handle, Controller, Pawn, Gas, Start, Home, Look, First, Logged, Shots, Fired, bBack, bNear, bSide, BackCount, BackEvery, Jumps, LastC, LastB, LastN, LastBlocks, Alpha, Clock]()
			{
				const float T = float(World->GetTimeSeconds() - Start);
				Clock->X = T;
				const FVector Ahead = FRotator(0.0f, Look.Yaw, 0.0f).Vector();
				const FVector Side = FVector::CrossProduct(FVector::UpVector, Ahead);
				// The walk (the owner's 23.10 video): 0-0.6 s ahead at 3 m/s, a shot 25 degrees down at 0.3 s, still to 1.2 s,
				// then a half circle of 1.5 m round the gas at 4.4 m/s looking at it (through its edge), then still to 8 s.
				FVector Velocity = FVector::ZeroVector;
				FRotator View = Look;
				View.Pitch = -25.0f;
				if (T <= 0.6f) { Velocity = Ahead * 300.0f; }
				if (T >= 0.3f && !*Fired) { *Fired = true; if (!bBack && !bNear) { Pawn->Fire(); Clock->Y = T; } }
				if (T > 1.2f && T <= 1.2f + UE_PI * 150.0f / 440.0f && !First->IsZero())
				{
					const FVector P = Pawn->Body->GetComponentLocation();
					FVector Off = P - *First;
					Off.Z = 0.0;
					const FVector Out = Off.GetSafeNormal();
					const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Out);
					Velocity = Tangent * 440.0f + Out * (150.0f - Off.Size()) * 4.0f;
					View = (-Out).Rotation();
					View.Pitch = -15.0f;
				}
				else if (T > 1.2f)
				{
					View = Controller->GetControlRotation();
				}
				if (bBack)
				{
					View = Look;
					Velocity = T <= 2.0f ? -Ahead * 400.0f : FVector::ZeroVector;
					static int32 BackShots = 0;
					if (T < 0.1f) { BackShots = 0; }
					if (BackShots < BackCount && T >= 0.3f + BackEvery * BackShots) { ++BackShots; Pawn->Fire(); Clock->Y = T; }
				}
				if (bNear)
				{
					View = Look;
					View.Pitch = -10.0f;
					if (bSide)
					{
						View.Yaw += 55.0f * FMath::Clamp((T - 0.35f) / 0.2f, 0.0f, 1.0f);
						View.Pitch = 0.0f;
					}
					Velocity = FVector::ZeroVector;
					static int32 NearShots = 0;
					if (T < 0.1f) { NearShots = 0; }
					const float At = NearShots < 2 ? 0.3f + 0.3f * NearShots : 2.8f + BackEvery * (NearShots - 2);
					if (NearShots < 2 + BackCount && T >= At)
					{
						++NearShots;
						const FString Spawn = IConsoleManager::Get().FindConsoleVariable(TEXT("os.Gas.ProbeSpawn"))->GetString();
						// A preset each shot, or a whole command once (os.Gas.SourceTest weld pulse).
						if (Spawn.IsEmpty()) { Pawn->Fire(); }
						else if (!Spawn.StartsWith(TEXT("os."))) { GEngine->Exec(World, *(TEXT("os.Gas.Spawn ") + Spawn)); }
						else if (NearShots == 1) { GEngine->Exec(World, *Spawn); }
						Clock->Y = T;
					}
				}
				// os.Gas.Capture 1: every frame 0.2-3.2 s saved (ScreenShot*.png) - a self-check at -FPS=30.
				static uint64 CaptureFrame = 0;
				if (CaptureFrame != GFrameCounter && T >= 0.2f && T <= 3.2f && IConsoleManager::Get().FindConsoleVariable(TEXT("os.Gas.Capture"))->GetInt() > 0)
				{
					CaptureFrame = GFrameCounter;
					FScreenshotRequest::RequestScreenshot(false);
				}
				// Once a frame (the timer may fire several times in one): a screenshot of the frame to the delegate below.
				static uint64 AlphaFrame = 0;
				if (AlphaFrame != GFrameCounter && IConsoleManager::Get().FindConsoleVariable(TEXT("os.Gas.DebugAlpha"))->GetInt() == 1)
				{
					AlphaFrame = GFrameCounter;
					FScreenshotRequest::RequestScreenshot(false);
				}
				const FVector Now = Pawn->Body->GetPhysicsLinearVelocity();
				Pawn->Body->SetPhysicsLinearVelocity(FVector(Velocity.X, Velocity.Y, Now.Z));
				Controller->SetControlRotation(View);
				// Measuring the opacity, nothing waits for the steps (the probes below do): the steps late as in play.
				const bool bAlphaMode = IConsoleManager::Get().FindConsoleVariable(TEXT("os.Gas.DebugAlpha"))->GetInt() > 0;
				if (!bAlphaMode)
				{
					FVector C, B;
					int32 N = 0, Behind = 0;
					float Sp = 0.0f, Hf = 0.0f, Gc = 0.0f, Old = 0.0f;
					if (Gas->ProbeYoungest(C, N, Behind, B, Sp, Hf, Gc, Old))
					{
						if (*LastN > 0 && FMath::Abs(N - *LastN) < FMath::Max(3, *LastN / 10))
						{
							Jumps->X = FMath::Max(Jumps->X, FVector::Dist(C, *LastC));
							Jumps->Y = FMath::Max(Jumps->Y, FVector::Dist(B, *LastB));
						}
						if (!First->IsZero())
						{
							// Toward the pawn's start (the shooter walks back from there): + is toward him.
							Jumps->Z = FMath::Max(Jumps->Z, (C - *First) | (Home - *First).GetSafeNormal2D());
						}
						*LastC = C;
						*LastB = B;
						*LastN = N;
					}
				}
				const int32 Tick = FMath::FloorToInt(T / 0.25f);
				if (Tick != *Logged && !bAlphaMode)
				{
					*Logged = Tick;
					FVector Centroid, Box;
					int32 Parcels = 0, Behind = 0;
					float Spread = 0.0f, Half = 0.0f, GridCell = 0.0f, OldSpeed = 0.0f;
					if (Tick == 2 || Tick == 3 || Tick == 4 || Tick == 8)
					{
						UE_LOG(LogMindKore, Log, TEXT("Walk probe: t %.2f s,%s"), T, *Gas->DescribeClouds());
					}
					if (Gas->ProbeYoungest(Centroid, Parcels, Behind, Box, Spread, Half, GridCell, OldSpeed))
					{
						if (First->IsZero())
						{
							*First = Centroid;
						}
						const FVector P = Pawn->Body->GetComponentLocation();
						UE_LOG(LogMindKore, Log, TEXT("Walk probe: t %.2f s, centroid %s (%.0f cm from the first), box %s half %.0f cm, grid cell %.0f cm, spread %.0f cm, old gas %.0f cm/s, %d cloud(s), %d parcels, %d behind a plane, pawn %s at %.0f cm, |v| %.0f"),
							T, *Centroid.ToCompactString(), FVector::Dist(Centroid, *First), *Box.ToCompactString(), Half, GridCell, Spread, OldSpeed, Gas->GetCloudCount(), Parcels, Behind, *P.ToCompactString(),
							FVector::Dist(P, Centroid), Pawn->Body->GetPhysicsLinearVelocity().Size());
					}
					else
					{
						UE_LOG(LogMindKore, Log, TEXT("Walk probe: t %.2f s, no cloud"), T);
					}
				}
				// near / space: a GPU profile at 3.2 s (the clouds alive then; M_OS_GasPost's line in the log).
				static bool bProfiled = false;
				if (T < 0.1f) { bProfiled = false; }
				if (bNear && !bProfiled && T >= 3.2f)
				{
					bProfiled = true;
					UE_LOG(LogMindKore, Log, TEXT("Walk probe: ProfileGPU with %d cloud(s)"), Gas->GetCloudCount());
					Controller->ConsoleCommand(TEXT("ProfileGPU"));
				}
				// What came into the world since the probe began (at 0.6 s): a self-check of what draws near the shot.
				static bool bListed = false;
				if (T < 0.1f) { bListed = false; }
				if (!bListed && T >= 0.6f)
				{
					bListed = true;
					for (TActorIterator<AActor> It(World); It; ++It)
					{
						if (It->CreationTime >= Start - 0.01 && FVector::Dist(It->GetActorLocation(), Home) < 1500.0f)
						{
							TArray<UActorComponent*> Parts;
							It->GetComponents(Parts);
							FString Names;
							for (const UActorComponent* Part : Parts)
							{
								Names += TEXT(" ") + Part->GetClass()->GetName();
							}
							UE_LOG(LogMindKore, Log, TEXT("Walk probe: new actor %s (%s) at %.0f cm:%s"), *It->GetName(), *It->GetClass()->GetName(), FVector::Dist(It->GetActorLocation(), Home), *Names);
						}
					}
				}
				static const float ShotAt[] = { 0.4f, 1.5f, 2.0f, 3.0f };
				static const TCHAR* ShotName[] = { TEXT("0_shot"), TEXT("1_stopped_1s"), TEXT("2_mid_arc"), TEXT("3_after_arc") };
				if (*Shots < 4 && T >= ShotAt[*Shots])
				{
					Controller->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=OSWalk_%s"), ShotName[*Shots]));
					++*Shots;
				}
				// A late source (os.Gas.ProbeSpawnLate) once at 5 s.
				static bool bLate = false;
				if (T < 0.1f) { bLate = false; }
				if (!bLate && T >= 5.0f)
				{
					bLate = true;
					const FString Late = IConsoleManager::Get().FindConsoleVariable(TEXT("os.Gas.ProbeSpawnLate"))->GetString();
					if (!Late.IsEmpty())
					{
						// A preset, or a whole command (os.Gas.TestShaft 600).
						GEngine->Exec(World, *(Late.StartsWith(TEXT("os.")) ? Late : TEXT("os.Gas.Spawn ") + Late));
					}
				}
				const bool bLongRun = !IConsoleManager::Get().FindConsoleVariable(TEXT("os.Gas.ProbeSpawn"))->GetString().IsEmpty();
				const float RunFor = IConsoleManager::Get().FindConsoleVariable(TEXT("os.Gas.ProbeSeconds"))->GetFloat();
				static bool bEndShot = false;
				if (T < 0.1f) { bEndShot = false; }
				if (RunFor > 0.0f && !bEndShot && T >= RunFor - 0.5f)
				{
					bEndShot = true;
					Controller->ConsoleCommand(TEXT("HighResShot 1 filename=OSWalk_4_end"));
				}
				// The cost 2 s before the end (in the screenshot's frame it profiled the shot's own pass).
				static bool bEndProfile = false;
				if (T < 0.1f) { bEndProfile = false; }
				if (RunFor > 0.0f && !bEndProfile && T >= RunFor - 2.0f)
				{
					bEndProfile = true;
					Controller->ConsoleCommand(TEXT("ProfileGPU"));
				}
				if (T > (RunFor > 0.0f ? RunFor : bLongRun ? 24.0f : 8.0f))
				{
					UE_LOG(LogMindKore, Log, TEXT("Walk probe: the drawn set changed %d time(s)"), Gas->DrawnSetChanges);
					UE_LOG(LogMindKore, Log, TEXT("Walk probe: the gas's opacity - the largest jump a frame in a block %.3f, %d of %d frame(s) over 0.25 (the gun's corner left out; the muzzle's jet in its first 0.25 s apart: up to %.2f); %s"),
						Alpha->X, int32(Alpha->Y), int32(Alpha->Z), Alpha->W, *Gas->TakeCostLine());
					UE_LOG(LogMindKore, Log, TEXT("Walk probe: largest jump a frame - centroid %.1f cm, box %.1f cm; the centroid came %.0f cm toward the shooter's start"), Jumps->X, Jumps->Y, Jumps->Z);
					UE_LOG(LogMindKore, Log, TEXT("Walk probe: done"));
					if (GEngine->GameViewport)
					{
						GEngine->GameViewport->OnScreenshotCaptured().Clear();
					}
					// Last: clearing the timer frees this lambda and its captures.
					World->GetTimerManager().ClearTimer(*Handle);
					return;
				}
			}), 0.001f, true);
			}), Delay, false);
		}));

	/** os.Fx.TunnelStill: waits for a cloud 0.6 s old, carves a 3 x 3 grid of tunnels along the view through it, shoots it carved, at +1 s and +2 s (healing). */
	FAutoConsoleCommandWithWorldAndArgs GTunnelStill(
		TEXT("os.Fx.TunnelStill"),
		TEXT("[after s] Wait (from world time 'after') for the player's next smoke cloud (0.6 s old), carve a 3 x 3 grid of tunnels along the view through it (CarveRay, 7 cm), HighResShot OSTunnels_0/1000/2000ms."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const float After = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.0f;
			TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
			World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([World, Handle, After]()
			{
				if (World->GetTimeSeconds() < After)
				{
					return;
				}
				APlayerController* Controller = World->GetFirstPlayerController();
				APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
				UOSMuzzleFxComponent* Fx = Pawn ? Pawn->FindComponentByClass<UOSMuzzleFxComponent>() : nullptr;
				FVector Centre;
				float Half;
				if (!Fx || !Fx->GetYoungestCloud(0.6f, Centre, Half))
				{
					return;
				}
				World->GetTimerManager().ClearTimer(*Handle);
				FVector Eye;
				FRotator View;
				Controller->GetPlayerViewPoint(Eye, View);
				const FVector Right = FRotationMatrix(View).GetUnitAxis(EAxis::Y);
				const FVector Up = FRotationMatrix(View).GetUnitAxis(EAxis::Z);
				// A 3 x 3 grid along the view (as a shooter sees his own rounds' tunnels), 25 cm apart.
				const FVector Forward = (Centre - Eye).GetSafeNormal();
				for (int32 I = -1; I <= 1; ++I)
				{
					for (int32 J = -1; J <= 1; ++J)
					{
						const FVector Mid = Centre + Right * (I * 25.0f) + Up * (J * 25.0f);
						Fx->CarveRay(Mid - Forward * Half * 1.5f, Mid + Forward * Half * 1.5f, 7.0f);
					}
				}
				UE_LOG(LogMindKore, Log, TEXT("Tunnel still: 9 tunnels along the view through the cloud at %s (half %.0f cm), %d alive"), *Centre.ToCompactString(), Half, Fx->GetTunnelCount());
				for (const float At : { 0.05f, 1.0f, 2.0f })
				{
					FTimerHandle Shot;
					World->GetTimerManager().SetTimer(Shot, FTimerDelegate::CreateLambda([Controller, At]()
					{
						Controller->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=OSTunnels_%04dms"), FMath::RoundToInt(At * 1000.0f)));
					}), At, false);
				}
			}), 0.05f, true);
		}));
#endif

	/** os.Fx.MoverStill [after s]: the nearest loose physics prop thrown through the player's next cloud (0.6 s old), stills at +0.1/0.25/0.5 s. */
	FAutoConsoleCommandWithWorldAndArgs GMoverStill(
		TEXT("os.Fx.MoverStill"),
		TEXT("[after s] Wait for the player's next smoke cloud (0.6 s old), throw the nearest simulating prop through it across the view at 3.5 m/s, HighResShot OSMover_0100/0250/0500ms."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const float After = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.0f;
			TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
			World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([World, Handle, After]()
			{
				APlayerController* Controller = World->GetFirstPlayerController();
				APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
				UOSMuzzleFxComponent* Fx = Pawn ? Pawn->FindComponentByClass<UOSMuzzleFxComponent>() : nullptr;
				FVector Centre;
				float Half;
				if (World->GetTimeSeconds() < After || !Fx || !Fx->GetYoungestCloud(0.6f, Centre, Half))
				{
					return;
				}
				World->GetTimerManager().ClearTimer(*Handle);
				UPrimitiveComponent* Prop = nullptr;
				for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
				{
					if (It->GetWorld() == World && It->IsSimulatingPhysics() && It->GetOwner() != Pawn && It->Bounds.SphereRadius > 10.0f && It->Bounds.SphereRadius < 90.0f
						&& (!Prop || FVector::DistSquared(It->Bounds.Origin, Centre) < FVector::DistSquared(Prop->Bounds.Origin, Centre)))
					{
						Prop = *It;
					}
				}
				if (!Prop)
				{
					UE_LOG(LogMindKore, Warning, TEXT("Mover still: no loose prop on the map"));
					return;
				}
				FVector Eye;
				FRotator View;
				Controller->GetPlayerViewPoint(Eye, View);
				const FVector Right = FRotationMatrix(View).GetUnitAxis(EAxis::Y);
				const FVector Gravity = OSGravityAt(World, Centre).Acceleration;
				Prop->SetWorldLocation(Centre - Right * 70.0f - Gravity * 0.02f, false, nullptr, ETeleportType::TeleportPhysics);
				// Across the view through the cloud, a little up against gravity so it crosses it level-ish.
				Prop->SetPhysicsLinearVelocity(Right * 350.0f - Gravity * 0.15f);
				UE_LOG(LogMindKore, Log, TEXT("Mover still: %s (r %.0f cm) thrown through the cloud at %s"), *GetNameSafe(Prop->GetOwner()), Prop->Bounds.SphereRadius, *Centre.ToCompactString());
				for (const float At : { 0.1f, 0.25f, 0.5f })
				{
					FTimerHandle Shot;
					World->GetTimerManager().SetTimer(Shot, FTimerDelegate::CreateLambda([Controller, At]()
					{
						Controller->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=OSMover_%04dms"), FMath::RoundToInt(At * 1000.0f)));
					}), At, false);
				}
			}), 0.05f, true);
		}));

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
