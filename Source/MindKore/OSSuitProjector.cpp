#include "OSSuitProjector.h"

#include "Algo/BinarySearch.h"
#include "CanvasItem.h"
#include "Components/LightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/Canvas.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MindKore.h"

namespace
{
	// MindKore's character generator, MK Pribor N (its MKDeviceFrame.h/.cpp; the table is its
	// generated MKDeviceFontData.inl, copied with the MIT licence in DeviceFont/).
	const FOSProjectorGlyph OSProjectorGlyphs[] = {
#include "DeviceFont/MKDeviceFontData.inl"
	};
}

const FOSProjectorGlyph* OSProjectorGlyph(TCHAR Code)
{
	const int32 Index = Algo::BinarySearchBy(OSProjectorGlyphs, uint16(Code), &FOSProjectorGlyph::Code);
	return Index == INDEX_NONE ? (Code == TEXT(' ') ? nullptr : OSProjectorGlyph(TEXT(' '))) : &OSProjectorGlyphs[Index];
}

int32 OSProjectorTextCells(FStringView Text)
{
	int32 Cells = 0;
	for (const TCHAR Char : Text)
	{
		const FOSProjectorGlyph* Glyph = OSProjectorGlyph(Char);
		Cells += Glyph ? Glyph->Advance : 0;
	}
	return FMath::Max(Cells - 1, 0);
}

float OSProjectorHash(int64 A, int64 B)
{
	uint32 H = uint32(A) * 0x9E3779B1u ^ uint32(B) * 0x85EBCA6Bu;
	H ^= H >> 15;
	H *= 0x2C1B3C6Du;
	H ^= H >> 12;
	return float(H & 0xFFFF) / 65535.0f;
}

void FOSProjectorPaint::Box(int32 X, int32 Y, int32 W, int32 H, float Halo, float Core) const
{
	const int32 X0 = FMath::Clamp(X, 0, Width);
	const int32 Y0 = FMath::Clamp(Y, 0, Height);
	const int32 X1 = FMath::Clamp(X + W, 0, Width);
	const int32 Y1 = FMath::Clamp(Y + H, 0, Height);
	if (X1 > X0 && Y1 > Y0)
	{
		Out.Add({ int16(X0), int16(Y0), int16(X1 - X0), int16(Y1 - Y0), Halo, Core });
	}
}

int32 FOSProjectorPaint::Text(FStringView Line, int32 X, int32 Y, int32 Cell, float Halo, float Core, bool bCut) const
{
	OSProjectorTextRuns(Line, [&](int32 RunX, int32 RunY, int32 Length)
	{
		Box(X + RunX * Cell, Y + RunY * Cell, Length * Cell, Cell, bCut ? 0.0f : Halo, bCut ? 0.0f : Core);
	});
	return TextWidth(Line, Cell);
}

int32 FOSProjectorPaint::TextRight(FStringView Line, int32 Right, int32 Y, int32 Cell, float Halo, float Core) const
{
	return Text(Line, Right - TextWidth(Line, Cell), Y, Cell, Halo, Core);
}

void FOSProjectorPaint::Dotted(int32 X0, int32 X1, int32 Y, float Halo, int32 Thick, int32 Dash, int32 Gap) const
{
	for (int32 X = X0; X < X1; X += Dash + Gap)
	{
		Box(X, Y, FMath::Min(Dash, X1 - X), Thick, Halo);
	}
}

void FOSProjectorPaint::Corners(int32 X, int32 Y, int32 W, int32 H, int32 Arm, int32 Thick, float Halo, float Core) const
{
	const int32 R = X + W - Thick;
	const int32 B = Y + H - Thick;
	for (const FIntPoint& At : { FIntPoint(X, Y), FIntPoint(R, Y), FIntPoint(X, B), FIntPoint(R, B) })
	{
		const int32 AlongX = At.X == X ? X : X + W - Arm;
		const int32 AlongY = At.Y == Y ? Y : Y + H - Arm;
		Box(AlongX, At.Y, Arm, Thick, Halo, Core);
		Box(At.X, AlongY, Thick, Arm, Halo, Core);
	}
}

void FOSProjectorPaint::Outline(int32 X, int32 Y, int32 W, int32 H, int32 Thick, float Halo) const
{
	Box(X, Y, W, Thick, Halo);
	Box(X, Y + H - Thick, W, Thick, Halo);
	Box(X, Y + Thick, Thick, H - 2 * Thick, Halo);
	Box(X + W - Thick, Y + Thick, Thick, H - 2 * Thick, Halo);
}

void OSProjectorDistort(TArray<FOSWeldBox>& Boxes, int32 Width, int32 Height, float WaveRows, int32 SyncBandY, int64 Tick, bool bGrain)
{
	// The weld frame's numbers (UOSWeldProjectorComponent::Compose): the mirror, not the drawing, is what a jolt disturbs.
	constexpr int32 Band = 8;
	constexpr int32 SyncBandHeight = 154;
	if (WaveRows > 0.0f || SyncBandY >= 0)
	{
		TArray<FOSWeldBox> Cut;
		Cut.Reserve(Boxes.Num() * 2);
		for (const FOSWeldBox& Box : Boxes)
		{
			if (Box.Angle != 0.0f)
			{
				FOSWeldBox Piece = Box;
				Piece.X = int16(Piece.X + FMath::RoundToInt32(WaveRows * FMath::Sin((Box.Y + Box.H / 2) / Band * 0.55f + float(Tick) * 1.3f)));
				Cut.Add(Piece);
				continue;
			}
			for (int32 TopRow = Box.Y; TopRow < Box.Y + Box.H;)
			{
				const int32 Index = TopRow / Band;
				const int32 Bottom = FMath::Min((Index + 1) * Band, Box.Y + Box.H);
				FOSWeldBox Piece = Box;
				Piece.Y = int16(TopRow);
				Piece.H = int16(Bottom - TopRow);
				TopRow = Bottom;
				const int32 Shift = FMath::RoundToInt32(WaveRows * FMath::Sin(Index * 0.55f + float(Tick) * 1.3f));
				if (SyncBandY >= 0)
				{
					const int32 Into = ((Piece.Y + Piece.H / 2 - SyncBandY) % Height + Height) % Height;
					const float Lost = Into < SyncBandHeight ? 0.12f : 1.0f;
					Piece.Halo *= Lost;
					Piece.Core *= Lost;
				}
				const int32 L = FMath::Clamp(Piece.X + Shift, 0, Width);
				const int32 R = FMath::Clamp(Piece.X + Piece.W + Shift, 0, Width);
				Piece.X = int16(L);
				Piece.W = int16(R - L);
				if (Piece.W > 0)
				{
					Cut.Add(Piece);
				}
			}
		}
		Boxes = MoveTemp(Cut);
	}
	if (bGrain)
	{
		for (int32 Index = 0; Index < Boxes.Num(); ++Index)
		{
			const float Keep = 1.0f - 0.2f * OSProjectorHash(Index, Tick);
			Boxes[Index].Halo *= Keep;
			Boxes[Index].Core *= Keep;
		}
	}
}

UOSSuitProjectorComponent::UOSSuitProjectorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Dark, the device does not tick at all; held, every frame (the frame's parameters follow the camera).
	PrimaryComponentTick.bStartWithTickEnabled = true;

	PictureFunction = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/OSArt/Materials/Hull/M_OS_Projection.M_OS_Projection")));

	// The raster's corner off the axis, plus a degree: the cone holds the whole picture and no more.
	const float Cone = FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(FMath::Square(FMath::Tan(FMath::DegreesToRadians(HalfAngleXDeg)))
		+ FMath::Square(FMath::Tan(FMath::DegreesToRadians(HalfAngleYDeg)))))) + 1.0f;
	auto MakeLaser = [this, Cone](const TCHAR* Name)
	{
		USpotLightComponent* Laser = CreateDefaultSubobject<USpotLightComponent>(Name);
		Laser->SetupAttachment(this);
		// Rides the camera: the picture is in the projector's own frame, not on a breach.
		Laser->bUseInverseSquaredFalloff = true;
		Laser->IntensityUnits = ELightUnits::Candelas;
		Laser->Intensity = 0.0f;
		Laser->bUseTemperature = false;
		Laser->InnerConeAngle = Cone;
		Laser->OuterConeAngle = Cone;
		Laser->AttenuationRadius = 1000.0f;
		Laser->SourceRadius = 0.0f;
		Laser->SoftSourceRadius = 0.0f;
		// The weld lasers' reasons (OSWeldProjector.cpp): no shadow, no MegaLights, no translucency, no bounce.
		Laser->CastShadows = false;
		Laser->bAllowMegaLights = false;
		Laser->bAffectTranslucentLighting = false;
		Laser->IndirectLightingIntensity = 0.0f;
		Laser->VolumetricScatteringIntensity = 0.0f;
		Laser->LightFunctionFadeDistance = 100000.0f;
		Laser->DisabledBrightness = 0.0f;
		Laser->SetVisibleFlag(false);
		return Laser;
	};
	HaloLaser = MakeLaser(TEXT("HaloLaser"));
	CoreLaser = MakeLaser(TEXT("CoreLaser"));
}

void UOSSuitProjectorComponent::BeginPlay()
{
	Super::BeginPlay();
	Raster = UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(this, UCanvasRenderTarget2D::StaticClass(), RasterWidth, RasterHeight);
	if (Raster)
	{
		Raster->ClearColor = FLinearColor::Black;
		Raster->RenderTargetFormat = RTF_RGBA8;
		Raster->bAutoGenerateMips = true;
		Raster->InitAutoFormat(RasterWidth, RasterHeight);
		Raster->OnCanvasRenderTargetUpdate.AddDynamic(this, &UOSSuitProjectorComponent::DrawRaster);
		Raster->UpdateResource();
	}
	HaloLaser->SetLightColor(HaloColour);
	CoreLaser->SetLightColor(CoreColour);
	if (UMaterialInterface* Function = PictureFunction.LoadSynchronous())
	{
		auto MakeFunction = [this, Function](USpotLightComponent* Laser, const FLinearColor& Channel)
		{
			UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Function, this);
			Instance->SetTextureParameterValue(TEXT("Raster"), Raster);
			Instance->SetVectorParameterValue(TEXT("Channel"), Channel);
			Instance->SetVectorParameterValue(TEXT("TanHalf"), FLinearColor(FMath::Tan(FMath::DegreesToRadians(HalfAngleXDeg)),
				FMath::Tan(FMath::DegreesToRadians(HalfAngleYDeg)), 0.0f, 0.0f));
			Laser->SetLightFunctionMaterial(Instance);
			return Instance;
		};
		HaloFunction = MakeFunction(HaloLaser, FLinearColor(0.0f, 1.0f, 0.0f, 0.0f));
		CoreFunction = MakeFunction(CoreLaser, FLinearColor(1.0f, 0.0f, 0.0f, 0.0f));
		// The weld frame's warm-up (OSWeldProjector.cpp, BeginPlay): a second of light too weak to see.
		PrimeUntil = GetWorld()->GetTimeSeconds() + 1.0;
		for (USpotLightComponent* Laser : { HaloLaser.Get(), CoreLaser.Get() })
		{
			Laser->SetIntensity(0.01f);
			Laser->SetVisibility(true, false);
		}
	}
	else if (!bWarnedFunction)
	{
		bWarnedFunction = true;
		UE_LOG(LogMindKore, Warning, TEXT("Suit projector %s: %s did not load, the projector stays dark (the attachment menu goes to the HUD)"),
			*GetNameSafe(GetOwner()), *PictureFunction.ToString());
		SetComponentTickEnabled(false);
	}
}

bool UOSSuitProjectorComponent::Acquire(EOSProjectorClient Client)
{
	if (Client == EOSProjectorClient::None || Client < Holder)
	{
		return Client == Holder;
	}
	if (Client != Holder)
	{
		Holder = Client;
		Picture.Reset();
		bPictureDirty = true;
		LastTick = MIN_int64; // the range finder reads on the very next tick
		StarPoint = FVector(1.0e9f);
		LastVelocity = GetOwner() ? GetOwner()->GetVelocity() : FVector::ZeroVector;
		Jolt = 0.0f;
	}
	SetComponentTickEnabled(true);
	return true;
}

void UOSSuitProjectorComponent::Release(EOSProjectorClient Client)
{
	if (Client != Holder || Client == EOSProjectorClient::None)
	{
		return;
	}
	Holder = EOSProjectorClient::None;
	Picture.Reset();
	bWall = false;
	GoDark();
	if (PrimeUntil < 0.0)
	{
		SetComponentTickEnabled(false);
	}
}

void UOSSuitProjectorComponent::SetPicture(TArray<FOSWeldBox>&& Boxes)
{
	Picture = MoveTemp(Boxes);
	bPictureDirty = true;
}

bool UOSSuitProjectorComponent::IsLit() const
{
	return PrimeUntil < 0.0 && HaloLaser && HaloLaser->IsVisible();
}

bool UOSSuitProjectorComponent::RasterOf(const FVector& World, FVector2D& OutPixel) const
{
	const FTransform Frame = GetComponentTransform();
	const FVector Local = Frame.InverseTransformPositionNoScale(World);
	if (Local.X <= 1.0)
	{
		return false;
	}
	const double U = Local.Y / (Local.X * FMath::Tan(FMath::DegreesToRadians(HalfAngleXDeg)));
	const double V = Local.Z / (Local.X * FMath::Tan(FMath::DegreesToRadians(HalfAngleYDeg)));
	OutPixel = FVector2D((0.5 + 0.5 * U) * RasterWidth, (0.5 - 0.5 * V) * RasterHeight);
	return true;
}

float UOSSuitProjectorComponent::PixelsAt(float Cm, float DistanceCm) const
{
	const float Across = 2.0f * FMath::Max(DistanceCm, 1.0f) * FMath::Tan(FMath::DegreesToRadians(HalfAngleXDeg));
	return Cm / Across * RasterWidth;
}

void UOSSuitProjectorComponent::ReadSurroundings()
{
	// The range finder: one trace along the axis on a device tick.
	const FVector Eye = GetComponentLocation();
	const FVector Axis = GetForwardVector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OSSuitProjectorRange), /*bTraceComplex=*/false, GetOwner());
	FHitResult Hit;
	bWall = GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + Axis * WallRangeCm, ECC_Visibility, Params);
	if (bWall)
	{
		WallCm = float(Hit.Distance);
		WallPoint = Hit.ImpactPoint;
		WallNormal = Hit.ImpactNormal;
		Slant = FMath::Max(FMath::Abs(float(FVector::DotProduct(Axis, WallNormal))), 0.3f);
	}
	else
	{
		WallCm = WallRangeCm;
		Slant = 1.0f;
	}
	// The star's light where the picture lands, read again only when the wall moves (a trace per light).
	const FVector Face = bWall ? WallPoint + WallNormal * 20.0f : Eye;
	if (FVector::DistSquared(Face, StarPoint) > FMath::Square(50.0f))
	{
		StarPoint = Face;
		StarLux = 0.0f;
		for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
		{
			const ULightComponent* Light = It->GetLightComponent();
			if (!Light || !Light->IsVisible() || !Light->bAffectsWorld)
			{
				continue;
			}
			const FVector ToStar = -Light->GetDirection();
			const float Cos = bWall ? float(FVector::DotProduct(WallNormal, ToStar)) : 1.0f;
			if (Cos <= 0.0f)
			{
				continue;
			}
			FCollisionQueryParams StarParams(SCENE_QUERY_STAT(OSSuitProjectorStar), /*bTraceComplex=*/false, GetOwner());
			if (Light->CastShadows && GetWorld()->LineTraceTestByChannel(Face, Face + ToStar * 20000.0f, ECC_Visibility, StarParams))
			{
				continue;
			}
			StarLux += Light->Intensity * Cos;
		}
		const bool bWasBright = bBright;
		bBright = StarLux >= LitLux;
		// Into the star's light the camera loses the scanner's phase for a second and a half.
		if (bBright && !bWasBright)
		{
			SyncTicksLeft = SyncLossTicks;
		}
	}
	const FVector Velocity = GetOwner() ? GetOwner()->GetVelocity() : FVector::ZeroVector;
	Jolt = FMath::Max(Jolt * 0.5f, (Velocity - LastVelocity).Size() > 150.0f ? 10.0f : 0.0f);
	LastVelocity = Velocity;
	SyncTicksLeft = FMath::Max(SyncTicksLeft - 1, 0);
}

void UOSSuitProjectorComponent::Aim()
{
	// Power by the square of the distance and the slant, never under MinRangeGain of full power (MindKore's
	// range gain with its burn-out floor: the menu is read at a hand's length too). No wall: set for the gun.
	const float Distance = bWall ? WallCm : 30.0f;
	const float Floor = MinRangeGain * FMath::Square(FullPowerCm / 100.0f);
	const float Drive = FMath::Lerp(1.0f, SunDrive, FMath::Clamp((StarLux - LitLux) / FMath::Max(SunLux - LitLux, 0.01f), 0.0f, 1.0f));
	const float Power = Drive * FMath::Max(FMath::Square(Distance / 100.0f) / Slant, Floor);
	Candelas = FMath::Min(HaloLux * Power, 200000.0f);
	const float Reach = Distance + 150.0f;
	for (USpotLightComponent* Laser : { HaloLaser.Get(), CoreLaser.Get() })
	{
		if (FMath::Abs(Laser->AttenuationRadius - Reach) > 25.0f)
		{
			Laser->SetAttenuationRadius(Reach);
		}
	}
	HaloLaser->SetIntensity(Candelas);
	CoreLaser->SetIntensity(FMath::Min(CoreLux * Power, 200000.0f));
	HaloLaser->SetVisibility(true, false);
	// The core laser is switched off, not just dark, while nothing in the picture is pale (MindKore's red laser).
	CoreLaser->SetVisibility(Drawn.ContainsByPredicate([](const FOSWeldBox& Box) { return Box.Core > 0.0f; }), false);
}

void UOSSuitProjectorComponent::GoDark()
{
	if (!HaloLaser->IsVisible() && !CoreLaser->IsVisible() && Drawn.IsEmpty())
	{
		return;
	}
	Drawn.Reset();
	HaloLaser->SetVisibility(false, false);
	CoreLaser->SetVisibility(false, false);
	if (Raster)
	{
		Raster->UpdateResource();
	}
}

void UOSSuitProjectorComponent::DrawRaster(UCanvas* Canvas, int32 Width, int32 Height)
{
	if (!Canvas)
	{
		return;
	}
	for (const FOSWeldBox& Box : Drawn)
	{
		FCanvasTileItem Tile(FVector2D(Box.X, Box.Y), FVector2D(Box.W, Box.H), FLinearColor(Box.Core, Box.Halo, 0.0f, 1.0f));
		Tile.BlendMode = SE_BLEND_Opaque;
		if (Box.Angle != 0.0f)
		{
			Tile.Rotation = FRotator(0.0f, Box.Angle, 0.0f);
			Tile.PivotPoint = FVector2D(0.5f, 0.5f);
		}
		Canvas->DrawItem(Tile);
	}
}

void UOSSuitProjectorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double WorldNow = World->GetTimeSeconds();
	if (PrimeUntil >= 0.0)
	{
		if (WorldNow < PrimeUntil)
		{
			return;
		}
		PrimeUntil = -1.0;
		HaloLaser->SetVisibility(false, false);
		CoreLaser->SetVisibility(false, false);
		if (Holder == EOSProjectorClient::None)
		{
			SetComponentTickEnabled(false);
			return;
		}
	}
	if (Holder == EOSProjectorClient::None)
	{
		SetComponentTickEnabled(false);
		return;
	}
	const int64 Tick = TickAt(WorldNow);
	if (Tick != LastTick)
	{
		LastTick = Tick;
		ReadSurroundings();
		bPictureDirty = true; // the distortion is per device tick
	}
	// No wall in reach: the holder reads its rows on the HUD; the lasers stay on for the gun's bracket only if it drew one.
	if (!HaloFunction || Picture.IsEmpty())
	{
		GoDark();
		return;
	}
	if (bPictureDirty)
	{
		bPictureDirty = false;
		Drawn = Picture;
		constexpr int32 SyncBandStep = 85;
		const int32 SyncBandY = SyncTicksLeft > 0 ? ((SyncLossTicks - SyncTicksLeft) * SyncBandStep) % RasterHeight : -1;
		const float Wave = FMath::Clamp(float(LastVelocity.Size()) / 360.0f, 0.0f, 1.0f) * 3.0f + Jolt;
		OSProjectorDistort(Drawn, RasterWidth, RasterHeight, Wave >= 0.5f ? Wave : 0.0f, SyncBandY, Tick, bBright);
		if (Raster)
		{
			Raster->UpdateResource();
		}
		Aim();
	}
	// The light function maps the world through the projector's frame, which moves with the camera every frame.
	const FTransform Frame = GetComponentTransform();
	for (UMaterialInstanceDynamic* Function : { HaloFunction.Get(), CoreFunction.Get() })
	{
		Function->SetVectorParameterValue(TEXT("Origin"), FLinearColor(Frame.GetLocation()));
		Function->SetVectorParameterValue(TEXT("AxisX"), FLinearColor(Frame.GetUnitAxis(EAxis::X)));
		Function->SetVectorParameterValue(TEXT("AxisY"), FLinearColor(Frame.GetUnitAxis(EAxis::Y)));
		Function->SetVectorParameterValue(TEXT("AxisZ"), FLinearColor(Frame.GetUnitAxis(EAxis::Z)));
	}
}
