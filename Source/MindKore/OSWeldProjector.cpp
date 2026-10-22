#include "OSWeldProjector.h"

#include "Algo/BinarySearch.h"
#include "Camera/PlayerCameraManager.h"
#include "CanvasItem.h"
#include "Components/LightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/Canvas.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "OSHullTools.h"
#include "OSShipParts.h"
#include "OSZeroGPawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MindKore.h"

namespace
{
	// MindKore's character generator, MK Pribor N (its MKDeviceFrame.h/.cpp; the table is its
	// generated MKDeviceFontData.inl, copied with the MIT licence in DeviceFont/). Ten rows from the
	// top, bit 0 leftmost; Advance in cells.
	struct FOSGlyph
	{
		uint16 Code;
		uint8 Advance;
		uint16 Rows[10];
	};

	const FOSGlyph OSWeldGlyphs[] = {
#include "DeviceFont/MKDeviceFontData.inl"
	};

	const FOSGlyph* OSWeldGlyph(TCHAR Code)
	{
		const int32 Index = Algo::BinarySearchBy(OSWeldGlyphs, uint16(Code), &FOSGlyph::Code);
		return Index == INDEX_NONE ? (Code == TEXT(' ') ? nullptr : OSWeldGlyph(TEXT(' '))) : &OSWeldGlyphs[Index];
	}

	/** Width of a line in cells, without the gap after its last glyph (MKDeviceTextCells). */
	int32 OSWeldTextCells(FStringView Text)
	{
		int32 Cells = 0;
		for (const TCHAR Char : Text)
		{
			const FOSGlyph* Glyph = OSWeldGlyph(Char);
			Cells += Glyph ? Glyph->Advance : 0;
		}
		return FMath::Max(Cells - 1, 0);
	}

	/** The lit cells of a line as horizontal runs: Emit(X, Y, Length) in cells, Y from the top row (MKDeviceTextRuns). */
	template <typename EmitType>
	void OSWeldTextRuns(FStringView Text, EmitType&& Emit)
	{
		int32 PenX = 0;
		for (const TCHAR Char : Text)
		{
			const FOSGlyph* Glyph = OSWeldGlyph(Char);
			for (int32 Row = 0; Glyph && Row < 10; ++Row)
			{
				for (int32 Bit = 0; Bit < 16;)
				{
					if (!(Glyph->Rows[Row] >> Bit & 1))
					{
						++Bit;
						continue;
					}
					int32 End = Bit;
					while (End < 16 && (Glyph->Rows[Row] >> End & 1))
					{
						++End;
					}
					Emit(PenX + Bit, Row, End - Bit);
					Bit = End;
				}
			}
			PenX += Glyph ? Glyph->Advance : 0;
		}
	}

	// A number in 0..1 that is the same for the same pair and unrelated for neighbours (MindKore's Hash01).
	float OSWeldHash(int64 A, int64 B)
	{
		uint32 H = uint32(A) * 0x9E3779B1u ^ uint32(B) * 0x85EBCA6Bu;
		H ^= H >> 15;
		H *= 0x2C1B3C6Du;
		H ^= H >> 12;
		return float(H & 0xFFFF) / 65535.0f;
	}

	// MindKore's heartbeat for a stable reading (MKProjectorPulse): about 65 a minute on the 12 Hz clock.
	float OSWeldPulse(int64 Tick)
	{
		static const float Beat[] = { 1.0f, 0.55f, 1.0f, 0.55f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f };
		return Beat[int32(((Tick % 11) + 11) % 11)];
	}

	constexpr double OSDeviceClockHz = 12.0;

	/** Boxes in the breach frame (cm) onto the raster (pixels). */
	struct FOSWeldPaint
	{
		TArray<FOSWeldBox>& Out;

		static float Px(float Cm)
		{
			return (Cm / UOSWeldProjectorComponent::RasterHalfCm * 0.5f + 0.5f) * UOSWeldProjectorComponent::RasterSize;
		}

		void Rect(const FVector2D& A, const FVector2D& B, float Halo, float Core = 0.0f) const
		{
			constexpr int32 N = UOSWeldProjectorComponent::RasterSize;
			const int32 X0 = FMath::Clamp(FMath::RoundToInt32(Px(float(FMath::Min(A.X, B.X)))), 0, N);
			const int32 X1 = FMath::Clamp(FMath::RoundToInt32(Px(float(FMath::Max(A.X, B.X)))), 0, N);
			const int32 Y0 = FMath::Clamp(FMath::RoundToInt32(Px(float(FMath::Min(A.Y, B.Y)))), 0, N);
			const int32 Y1 = FMath::Clamp(FMath::RoundToInt32(Px(float(FMath::Max(A.Y, B.Y)))), 0, N);
			if (X0 < N && Y0 < N)
			{
				Out.Add({ int16(X0), int16(Y0), int16(FMath::Max(X1 - X0, 1)), int16(FMath::Max(Y1 - Y0, 1)), Halo, Core });
			}
		}

		/** A stroke at any slant from From to To, WidthCm across: one box turned about its centre. */
		void Line(const FVector2D& From, const FVector2D& To, float WidthCm, float Halo, float Core = 0.0f) const
		{
			const FVector2D A(Px(float(From.X)), Px(float(From.Y)));
			const FVector2D B(Px(float(To.X)), Px(float(To.Y)));
			const FVector2D Mid = (A + B) * 0.5;
			const float Length = FMath::Max(float((B - A).Size()), 1.0f);
			const float Across = FMath::Max(WidthCm / UOSWeldProjectorComponent::RasterHalfCm * 0.5f * UOSWeldProjectorComponent::RasterSize, 1.0f);
			FOSWeldBox Box;
			Box.X = int16(FMath::RoundToInt32(Mid.X - Length * 0.5f));
			Box.Y = int16(FMath::RoundToInt32(Mid.Y - Across * 0.5f));
			Box.W = int16(FMath::RoundToInt32(Length));
			Box.H = int16(FMath::Max(FMath::RoundToInt32(Across), 1));
			Box.Halo = Halo;
			Box.Core = Core;
			Box.Angle = FMath::RadiansToDegrees(FMath::Atan2(float(B.Y - A.Y), float(B.X - A.X)));
			Out.Add(Box);
		}

		/** An axis-aligned stroke from From to To, WidthCm across. */
		void Stroke(const FVector2D& From, const FVector2D& To, float WidthCm, float Halo, float Core = 0.0f) const
		{
			const FVector2D Along = (To - From).GetSafeNormal();
			const FVector2D Side = FVector2D(-Along.Y, Along.X) * (WidthCm * 0.5f);
			Rect(From - Side, To + Side, Halo, Core);
		}

		/** A line of MK Pribor N from its top-left corner, Cell cm a cell, reading along Right with rows going Down. */
		void Text(FStringView Line, const FVector2D& TopLeft, const FVector2D& Right, const FVector2D& Down, float Cell, float Level) const
		{
			OSWeldTextRuns(Line, [&](int32 X, int32 Y, int32 Length)
			{
				const FVector2D A = TopLeft + Right * (X * Cell) + Down * (Y * Cell);
				Rect(A, A + Right * (Length * Cell) + Down * Cell, Level);
			});
		}
	};
}

UOSWeldProjectorComponent::UOSWeldProjectorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// The device clock is 12 Hz; dark, the tick only looks for a breach.
	PrimaryComponentTick.TickInterval = 1.0f / 24.0f;

	PictureFunction = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/OSArt/Materials/Hull/M_OS_WeldProjection.M_OS_WeldProjection")));

	auto MakeLaser = [this](const TCHAR* Name)
	{
		USpotLightComponent* Laser = CreateDefaultSubobject<USpotLightComponent>(Name);
		Laser->SetupAttachment(this);
		// Rides the camera, aimed at the breach on device ticks.
		Laser->SetUsingAbsoluteRotation(true);
		Laser->bUseInverseSquaredFalloff = true;
		Laser->IntensityUnits = ELightUnits::Candelas;
		Laser->Intensity = 0.0f;
		Laser->bUseTemperature = false;
		Laser->InnerConeAngle = 30.0f;
		Laser->OuterConeAngle = 30.0f;
		Laser->AttenuationRadius = 2000.0f;
		Laser->SourceRadius = 0.0f;
		Laser->SoftSourceRadius = 0.0f;
		// No shadow: the hands under the camera would hide the bead under the torch, and a light that moves
		// with the camera every frame would redraw its shadow pages every frame. The light function keeps the
		// picture to a slab round the hull's face instead.
		Laser->CastShadows = false;
		Laser->bAllowMegaLights = false;
		// Translucent surfaces take a light without its function: the whole cone as one disc (MindKore, PIE 2026-10-02).
		Laser->bAffectTranslucentLighting = false;
		// Bounce light has no light function either: the bare cone would tint the hull round the frame.
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

float UOSWeldProjectorComponent::ViewPixelAngle(const APawn* Pawn)
{
	const APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	float Fov = 90.0f;
	int32 Width = 0;
	int32 Height = 0;
	if (Controller)
	{
		if (Controller->PlayerCameraManager)
		{
			Fov = Controller->PlayerCameraManager->GetFOVAngle();
		}
		Controller->GetViewportSize(Width, Height);
	}
	return 2.0f * FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(Fov, 10.0f, 170.0f)) * 0.5f) / float(Width > 0 ? Width : 1280);
}

void UOSWeldProjectorComponent::BeginPlay()
{
	Super::BeginPlay();
	Raster = UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(this, UCanvasRenderTarget2D::StaticClass(), RasterSize, RasterSize);
	if (Raster)
	{
		Raster->ClearColor = FLinearColor::Black;
		Raster->RenderTargetFormat = RTF_RGBA8;
		// Mips: from 8 m a stroke one cell wide would shimmer (MindKore's beam read mip 2 for the same reason).
		Raster->bAutoGenerateMips = true;
		Raster->InitAutoFormat(RasterSize, RasterSize);
		Raster->OnCanvasRenderTargetUpdate.AddDynamic(this, &UOSWeldProjectorComponent::DrawRaster);
		// Cleared once now: a target never drawn into holds whatever was in its memory.
		Raster->UpdateResource();
	}
	HaloLaser->SetLightColor(HaloColour);
	CoreLaser->SetLightColor(CoreColour);
	if (UMaterialInterface* Picture = PictureFunction.LoadSynchronous())
	{
		auto MakeFunction = [this, Picture](USpotLightComponent* Laser, const FLinearColor& Channel)
		{
			UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Picture, this);
			Instance->SetTextureParameterValue(TEXT("Raster"), Raster);
			Instance->SetVectorParameterValue(TEXT("Channel"), Channel);
			Laser->SetLightFunctionMaterial(Instance);
			return Instance;
		};
		HaloFunction = MakeFunction(HaloLaser, FLinearColor(0.0f, 1.0f, 0.0f, 0.0f));
		CoreFunction = MakeFunction(CoreLaser, FLinearColor(1.0f, 0.0f, 0.0f, 0.0f));
	}
	else
	{
		UE_LOG(LogMindKore, Warning, TEXT("Weld projector %s: %s did not load, the projector stays dark"), *GetNameSafe(GetOwner()), *PictureFunction.ToString());
	}
	// The first frames a light function is ever used the light shines without it - the bare cone
	// (MindKore, in-engine clips 2026-10-03). A second of light far too weak to see does that use now;
	// the raster is black, so through the function it is no light at all.
	if (HaloFunction)
	{
		PrimeUntil = GetWorld()->GetTimeSeconds() + 1.0;
		for (USpotLightComponent* Laser : { HaloLaser.Get(), CoreLaser.Get() })
		{
			Laser->SetIntensity(0.01f);
			Laser->SetVisibility(true, false);
		}
	}
}

AOSPlateSocket* UOSWeldProjectorComponent::FindLookedAt() const
{
	const FVector Eye = GetComponentLocation();
	const FVector View = GetForwardVector();
	const float CosCone = FMath::Cos(FMath::DegreesToRadians(LookConeDeg));
	AOSPlateSocket* Best = nullptr;
	double BestDistance = ViewRangeCm;
	for (TActorIterator<AOSPlateSocket> Socket(GetWorld()); Socket; ++Socket)
	{
		const FTransform& Frame = Socket->GetActorTransform();
		const FVector Centre = Frame.TransformPositionNoScale(FVector(0.0f, 0.0f, Socket->SurfaceZ));
		const double Distance = FVector::Dist(Eye, Centre);
		if (Distance >= BestDistance)
		{
			continue;
		}
		// The view ray crosses the frame's square, or the centre is near the view axis.
		bool bLooked = FVector::DotProduct((Centre - Eye).GetSafeNormal(), View) >= CosCone;
		const FVector LocalEye = Frame.InverseTransformPositionNoScale(Eye);
		const FVector LocalView = Frame.InverseTransformVectorNoScale(View);
		if (!bLooked && FMath::Abs(LocalView.Z) > 1.0e-3)
		{
			const double T = (Socket->SurfaceZ - LocalEye.Z) / LocalView.Z;
			const FVector OnPlane = LocalEye + LocalView * T;
			bLooked = T > 0.0 && FMath::Abs(OnPlane.X) <= RasterHalfCm && FMath::Abs(OnPlane.Y) <= RasterHalfCm;
		}
		if (bLooked)
		{
			Best = *Socket;
			BestDistance = Distance;
		}
	}
	if (Best)
	{
		// In sight: no fixed structure but the hull round the hole between the eye and the frame. Loose things
		// (junk, a plate, a flyer) do not blind the device.
		const FTransform& Frame = Best->GetActorTransform();
		const FVector Centre = Frame.TransformPositionNoScale(FVector(0.0f, 0.0f, Best->SurfaceZ));
		FCollisionQueryParams Params(SCENE_QUERY_STAT(OSWeldProjectorSight), /*bTraceComplex=*/false, GetOwner());
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByObjectType(Hit, Eye, Centre, FCollisionObjectQueryParams(ECC_WorldStatic), Params)
			&& FVector::Dist(Hit.ImpactPoint, Centre) > RasterHalfCm)
		{
			return nullptr;
		}
	}
	return Best;
}

void UOSWeldProjectorComponent::Acquire(AOSPlateSocket* Socket)
{
	Target = Socket;
	MissTicks = 0;
	// The star's light on the breach's face, from the directional lights the breach is not shadowed from.
	// Read from the scene, not from the frame: brightness read under the projector's own light feeds itself (MindKore, PIE 2026-10-02).
	const FTransform& Frame = Socket->GetActorTransform();
	const FVector Up = Frame.GetUnitAxis(EAxis::Z);
	const FVector Face = Frame.TransformPositionNoScale(FVector(0.0f, 0.0f, Socket->SurfaceZ + 30.0f));
	StarLux = 0.0f;
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		const ULightComponent* Light = It->GetLightComponent();
		if (!Light || !Light->IsVisible() || !Light->bAffectsWorld)
		{
			continue;
		}
		const FVector ToStar = -Light->GetDirection();
		const float Cos = float(FVector::DotProduct(Up, ToStar));
		if (Cos <= 0.0f)
		{
			continue;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(OSWeldProjectorStar), /*bTraceComplex=*/false, GetOwner());
		Params.AddIgnoredActor(Socket->GetPlate());
		if (Light->CastShadows && GetWorld()->LineTraceTestByChannel(Face, Face + ToStar * 20000.0f, ECC_Visibility, Params))
		{
			continue;
		}
		StarLux += Light->Intensity * Cos;
	}
	bBright = StarLux >= LitLux;
	// Taking a lit breach, the camera loses the scanner's phase and finds it again in a second and a half.
	SyncTicksLeft = bBright ? SyncLossTicks : 0;
	LastVelocity = GetOwner() ? GetOwner()->GetVelocity() : FVector::ZeroVector;
	Jolt = 0.0f;
	UE_LOG(LogMindKore, Log, TEXT("Weld projector: on %s, %.0f cm, star light %.2f lux (%s)"), *Socket->GetName(),
		FVector::Dist(GetComponentLocation(), Socket->GetActorLocation()), StarLux, bBright ? TEXT("lit") : TEXT("dark"));
}

float UOSWeldProjectorComponent::FlightCurve(float Tau, float AnticipationShare, float OvershootShare)
{
	// A grown-up move (Docs/Animation.md): a wind-up back, a fast travel that overshoots the lock, a damped
	// settle with a small undershoot; no linear stretch and no plateau.
	constexpr float Wind = 0.18f;
	constexpr float Travel = 0.44f;
	if (Tau <= 0.0f)
	{
		return 0.0f;
	}
	if (Tau < Wind)
	{
		return -AnticipationShare * FMath::Sin(PI * Tau / Wind);
	}
	if (Tau < Wind + Travel)
	{
		const float S = (Tau - Wind) / Travel;
		return (1.0f + OvershootShare) * (1.0f - FMath::Cube(1.0f - S));
	}
	if (Tau < 1.0f)
	{
		const float S = (Tau - Wind - Travel) / (1.0f - Wind - Travel);
		return 1.0f + OvershootShare * FMath::Cos(1.5f * PI * S) * FMath::Exp(-3.0f * S);
	}
	return 1.0f;
}

namespace
{
	// The order the groups set off in: neighbours never together, so the frame closes from several sides.
	constexpr int32 OSWeldGroupRank[UOSWeldProjectorComponent::GroupCount] = { 3, 7, 0, 10, 5, 1, 9, 4, 11, 2, 8, 6 };

	// Seven-segment digits: bit 0 a (top), b (upper right), c (lower right), d (bottom), e (lower left), f (upper left), g (middle).
	constexpr uint8 OSWeldSegmentDigits[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
	// Each segment as a box in a 5 x 9 digit, units from the top left; segments do not meet at the corners.
	constexpr float OSWeldSegmentBoxes[7][4] = {
		{ 1.0f, 0.0f, 4.0f, 1.0f }, { 4.0f, 1.0f, 5.0f, 4.0f }, { 4.0f, 5.0f, 5.0f, 8.0f }, { 1.0f, 8.0f, 4.0f, 9.0f },
		{ 0.0f, 5.0f, 1.0f, 8.0f }, { 0.0f, 1.0f, 1.0f, 4.0f }, { 1.0f, 4.0f, 4.0f, 5.0f } };

	/** A sign of the readout: a letter of MK Pribor N or a segment digit. */
	struct FOSWeldSign
	{
		TCHAR Char = 0;
		bool bDigit = false;
	};
}

float UOSWeldProjectorComponent::GroupDelay(int32 Index) const
{
	return FMath::Max(AppearSeconds - FlightSeconds, 0.0f) * float(OSWeldGroupRank[Index % GroupCount]) / float(GroupCount - 1);
}

UPrimitiveComponent* UOSWeldProjectorComponent::FindPlate(const AOSPlateSocket& Socket) const
{
	const auto Near = [this, &Socket](UPrimitiveComponent* Body)
	{
		return Body && Body->GetOwner() && Body->GetOwner()->ActorHasTag(Socket.RequiredTag)
			&& FVector::Dist(Body->GetComponentLocation(), Socket.GetActorLocation()) <= PlateRangeCm;
	};
	if (const AOSZeroGPawn* Pawn = Cast<AOSZeroGPawn>(GetOwner()))
	{
		if (Near(Pawn->GetGrabbed()))
		{
			return Pawn->GetGrabbed();
		}
		for (const FOSStrapLine& Line : Pawn->HullTools->GetLines())
		{
			if (Near(Line.A.Get()))
			{
				return Line.A.Get();
			}
			if (Near(Line.B.Get()))
			{
				return Line.B.Get();
			}
		}
	}
	AActor* Lying = Socket.GetPlate();
	UPrimitiveComponent* Body = Lying ? Cast<UPrimitiveComponent>(Lying->GetRootComponent()) : nullptr;
	return Near(Body) ? Body : nullptr;
}

FOSWeldFootprint UOSWeldProjectorComponent::Footprint(const AOSPlateSocket& Socket, const UPrimitiveComponent& Plate) const
{
	// The held body's own size: its local bounds, scaled; the two longest axes lie on the hull, the
	// thinnest stands off it.
	FOSWeldFootprint Print;
	const FTransform& Frame = Socket.GetActorTransform();
	const FTransform& Body = Plate.GetComponentTransform();
	const FBoxSphereBounds Local = Plate.CalcLocalBounds();
	const FVector Extent = Local.BoxExtent * Body.GetScale3D().GetAbs();
	const FVector Centre = Body.TransformPosition(Local.Origin);
	int32 Thin = 0;
	for (int32 Axis = 1; Axis < 3; ++Axis)
	{
		Thin = Extent[Axis] < Extent[Thin] ? Axis : Thin;
	}
	const int32 First = (Thin + 1) % 3;
	const int32 Second = (Thin + 2) % 3;
	const EAxis::Type Axes[3] = { EAxis::X, EAxis::Y, EAxis::Z };
	const FVector LocalCentre = Frame.InverseTransformPositionNoScale(Centre);
	const FVector A1 = Frame.InverseTransformVectorNoScale(Body.GetUnitAxis(Axes[First]));
	const FVector A2 = Frame.InverseTransformVectorNoScale(Body.GetUnitAxis(Axes[Second]));
	const FVector Normal = Frame.InverseTransformVectorNoScale(Body.GetUnitAxis(Axes[Thin]));
	const FVector2D C(LocalCentre.X, LocalCentre.Y);
	const FVector2D U = FVector2D(A1.X, A1.Y) * Extent[First];
	const FVector2D V = FVector2D(A2.X, A2.Y) * Extent[Second];
	// Square to the breach and near its centre: locked on, drawn straight.
	const float Turn = FMath::RadiansToDegrees(FMath::Atan2(float(A1.Y), float(A1.X)));
	const float OffSquare = FMath::Abs(Turn - 90.0f * FMath::RoundToFloat(Turn / 90.0f));
	Print.bSnapped = C.Size() <= SnapCm && OffSquare <= SnapDeg && FMath::Abs(Normal.Z) >= 0.96f;
	if (Print.bSnapped)
	{
		const bool bFirstAlongX = FMath::Abs(A1.X) >= FMath::Abs(A1.Y);
		const FVector2D Half(bFirstAlongX ? Extent[First] : Extent[Second], bFirstAlongX ? Extent[Second] : Extent[First]);
		Print.Corner[0] = FVector2D(-Half.X, -Half.Y);
		Print.Corner[1] = FVector2D(Half.X, -Half.Y);
		Print.Corner[2] = FVector2D(Half.X, Half.Y);
		Print.Corner[3] = FVector2D(-Half.X, Half.Y);
	}
	else
	{
		Print.Corner[0] = C - U - V;
		Print.Corner[1] = C + U - V;
		Print.Corner[2] = C + U + V;
		Print.Corner[3] = C - U + V;
		if (FVector2D::CrossProduct(Print.Corner[1] - Print.Corner[0], Print.Corner[2] - Print.Corner[1]) < 0.0)
		{
			Swap(Print.Corner[1], Print.Corner[3]); // anticlockwise, as the hole's corners run
		}
	}
	const auto Inside = [&Print](const FVector2D& Point)
	{
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			if (FVector2D::CrossProduct(Print.Corner[(Corner + 1) % 4] - Print.Corner[Corner], Point - Print.Corner[Corner]) < 0.0)
			{
				return false;
			}
		}
		return true;
	};
	// The breach's own measure, on the outline: a grid of 10 x 10 over the hole, and every edge piece 4 cm inside it
	// (AOSPlateSocket::Measure probes there).
	constexpr int32 Grid = 10;
	int32 Covered = 0;
	for (int32 X = 0; X < Grid; ++X)
	{
		for (int32 Y = 0; Y < Grid; ++Y)
		{
			const FVector2D Cell((X + 0.5f) / Grid * 2.0f - 1.0f, (Y + 0.5f) / Grid * 2.0f - 1.0f);
			Covered += Inside(Cell * (Socket.HoleHalfCm - FVector2D(5.0f, 5.0f))) ? 1 : 0;
		}
	}
	Print.Coverage = float(Covered) / (Grid * Grid);
	bool bEdges = true;
	const TArray<FVector2D>& Centres = Socket.GetSegmentCentres();
	const TArray<FVector2D>& Normals = Socket.GetSegmentNormals();
	for (int32 Index = 0; Index < Centres.Num() && bEdges; ++Index)
	{
		bEdges = Inside(Centres[Index] - Normals[Index] * 4.0f);
	}
	Print.bCovers = bEdges && Print.Coverage >= Socket.SealCoverage;
	Print.bPlaced = Socket.GetPlate() == Plate.GetOwner() && Socket.GetCoverage() > 0.0f;
	return Print;
}

double UOSWeldProjectorComponent::Clock() const
{
	return HeldAt >= 0.0 ? HeldAt : GetWorld()->GetTimeSeconds();
}

bool UOSWeldProjectorComponent::IsAnimating(double Now) const
{
	// Frame by frame while anything moves faster than the device clock: the flight in, the strobes it leaves,
	// the readout typing, the seal's pulse, the break-up.
	if (Phase == EPhase::Appearing || Phase == EPhase::Vanishing || HeldAt >= 0.0)
	{
		return true;
	}
	return Phase == EPhase::Open && (Now - PhaseStart < FMath::Max(StrobeLingerSeconds, 1.0f) || Now - SealAt < SealPulseSeconds);
}

void UOSWeldProjectorComponent::HoldClock(double SecondsAfterAppear)
{
	HeldAt = SecondsAfterAppear >= 0.0 ? AppearOrigin + SecondsAfterAppear : -1.0;
}

void UOSWeldProjectorComponent::Replay()
{
	if (!Target.IsValid())
	{
		Target = FindLookedAt();
	}
	if (AOSPlateSocket* Socket = Target.Get())
	{
		const bool bWasHeld = HeldAt >= 0.0;
		HeldAt = -1.0;
		Acquire(Socket);
		Phase = EPhase::Appearing;
		AppearScale = 1.0f;
		PhaseStart = AppearOrigin = Clock();
		if (bWasHeld)
		{
			HeldAt = AppearOrigin;
		}
	}
}

void UOSWeldProjectorComponent::Compose(const AOSPlateSocket& Socket, double Now, float WaveRows)
{
	Boxes.Reset();
	Lines.Reset();
	const FOSWeldPaint Paint{ Boxes };
	const double WorldNow = GetWorld()->GetTimeSeconds();
	const FTransform& Frame = Socket.GetActorTransform();
	const FVector Eye = GetComponentLocation();
	const FVector LocalEye = Frame.InverseTransformPositionNoScale(Eye);
	const float Distance = float(FVector::Dist(Eye, Frame.TransformPositionNoScale(FVector(0.0f, 0.0f, Socket.SurfaceZ))));
	// A thin line at any range: StrokePx of the view (MindKore's two-pixel rule for motes, here one or two).
	const float PixelCm = Distance * ViewPixelAngle(Cast<APawn>(GetOwner()));
	const float Width = FMath::Max(PixelCm * StrokePx, MinStrokeCm);
	const double T = Now - PhaseStart;

	// The text reads the right way up for the eye: its up is the frame axis nearest the camera's up,
	// mirrored when the eye is inside the hull, under the face.
	const FVector LocalUp = Frame.InverseTransformVectorNoScale(GetUpVector());
	const FVector2D Up = FMath::Abs(LocalUp.X) >= FMath::Abs(LocalUp.Y) ? FVector2D(FMath::Sign(LocalUp.X), 0.0) : FVector2D(0.0, FMath::Sign(LocalUp.Y));
	const FVector2D Down = -Up;
	const FVector2D Right = FVector2D(-Up.Y, Up.X) * (LocalEye.Z >= Socket.SurfaceZ ? 1.0 : -1.0);
	const bool bDownAlongX = Down.X != 0.0;
	const FVector2D Hole = Socket.HoleHalfCm;
	const float HoleAlongDown = float(bDownAlongX ? Hole.X : Hole.Y);
	const float HoleAlongRight = float(bDownAlongX ? Hole.Y : Hole.X);
	const float Edge = 1.0f; // strokes start just outside the hole: on the hull's rim with no plate, on the plate when it lies

	// Where each of the twelve groups is on its flight, how lit, how much it trembles.
	FVector2D GroupOffset[GroupCount];
	float GroupLevel[GroupCount];
	float GroupTremble[GroupCount];
	for (int32 Group = 0; Group < GroupCount; ++Group)
	{
		// The hole's sides (0..7) fly in slanting from outside, each half from its own end; the frame's sides
		// (8..11) whole, slanting the other way.
		const int32 Side = Group < 8 ? Group / 2 : Group - 8;
		const FVector2D Out = Side == 0 ? FVector2D(0.0, -1.0) : Side == 1 ? FVector2D(1.0, 0.0) : Side == 2 ? FVector2D(0.0, 1.0) : FVector2D(-1.0, 0.0);
		const FVector2D Along(-Out.Y, Out.X);
		const FVector2D Direction = Out * 0.77 + Along * (Group < 8 ? ((Group % 2) ? 0.64 : -0.64) : -0.64);
		const float Reach = FlyCm * (0.8f + 0.4f * OSWeldHash(Group, 91));
		const float Rank = float(OSWeldGroupRank[Group]) / float(GroupCount - 1);
		GroupOffset[Group] = FVector2D::ZeroVector;
		GroupLevel[Group] = 1.0f;
		GroupTremble[Group] = 1.0f;
		if (Phase == EPhase::Appearing)
		{
			const float Tau = float(T - GroupDelay(Group) * AppearScale) / (FlightSeconds * AppearScale);
			GroupOffset[Group] = Direction * (Reach * (1.0f - FlightCurve(Tau, Anticipation, Overshoot)));
			GroupLevel[Group] = Tau <= 0.0f ? 0.0f : FMath::Min(Tau / 0.2f, 1.0f);
			GroupTremble[Group] = FMath::Lerp(AppearJitterGain, 1.0f, FMath::Clamp(Tau, 0.0f, 1.0f));
		}
		else if (Phase == EPhase::Vanishing)
		{
			// Quicker and plainer going: outward, accelerating, fading, staggered over the first 40 %.
			const float U = FMath::Clamp((float(T) - VanishSeconds * 0.4f * Rank) / (VanishSeconds * 0.6f), 0.0f, 1.0f);
			GroupOffset[Group] = Direction * (Reach * 0.8f * U * U);
			GroupLevel[Group] = (1.0f - U) * (1.0f - U);
			GroupTremble[Group] = FMath::Lerp(1.0f, AppearJitterGain, U);
		}
	}
	// Strobing points: on the way in and for StrobeLingerSeconds after the lock.
	const float StrobeAmp = Phase == EPhase::Appearing ? 1.0f : Phase == EPhase::Open ? FMath::Clamp(1.0f - float(T) / FMath::Max(StrobeLingerSeconds, 0.01f), 0.0f, 1.0f) : 0.0f;
	// The tremble steps on its own rate; the gaps' dropouts with it.
	const int64 Step = FMath::FloorToInt64(Now * JitterHz);
	// The sealed ring's one pulse: up fast, then down into the steady ring.
	const float SealX = float(Now - SealAt) / SealPulseSeconds;
	const float SealPulse = SealX < 0.0f || SealX >= 1.0f ? 0.0f : SealX < 0.18f ? SealX / 0.18f : FMath::Exp(-(SealX - 0.18f) * 4.5f) * (1.0f - SealX);

	// Empty hands: the hole traced and the square a plate must cover round it. A plate held, on a strap or
	// over the hole: its own outline where it would lie now, and the hole's edges it would leave open broken.
	const UPrimitiveComponent* Plate = FramedPlate.Get();
	const FOSWeldFootprint Print = Plate ? Footprint(Socket, *Plate) : FOSWeldFootprint();
	const auto Inside = [&Print](const FVector2D& Point)
	{
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const FVector2D A = Print.Corner[Corner];
			const FVector2D B = Print.Corner[(Corner + 1) % 4];
			if (FVector2D::CrossProduct(B - A, Point - A) < 0.0)
			{
				return false;
			}
		}
		return true;
	};

	const TArray<float>& Seam = Socket.GetSeam();
	const TArray<float>& Gaps = Socket.GetGaps();
	const TArray<FVector2D>& Centres = Socket.GetSegmentCentres();
	const TArray<FVector2D>& Normals = Socket.GetSegmentNormals();
	const TArray<double>& Welded = Socket.GetLastWelded();
	const bool bSealed = Socket.bFilled;
	const bool bHeld = Socket.IsTacked();
	const FVector2D Corners[4] = { { -Hole.X, -Hole.Y }, { Hole.X, -Hole.Y }, { Hole.X, Hole.Y }, { -Hole.X, Hole.Y } };
	int32 SegmentBase = 0;
	int32 GapCount = 0;
	for (int32 Side = 0; Side < 4; ++Side)
	{
		const FVector2D From = Corners[Side];
		const FVector2D To = Corners[(Side + 1) % 4];
		const float Length = float((To - From).Size());
		const FVector2D Along = (To - From) / Length;
		const FVector2D Out(Along.Y, -Along.X);
		const int32 Count = FMath::Max(1, FMath::RoundToInt32(Length / Socket.SegmentCm));
		int32 Dash = 0;
		for (float S = 0.0f; S < Length; ++Dash)
		{
			const int32 Segment = SegmentBase + FMath::Clamp(FMath::FloorToInt32(S / (Length / Count)), 0, Count - 1);
			const float Piece = Seam.IsValidIndex(Segment) ? Seam[Segment] : 0.0f;
			// Broken: under a lying plate where it does not lie on the edge (measured); under one on its way
			// where its outline would leave the edge open; never with empty hands.
			const bool bGap = Piece <= 0.0f && Plate
				&& (Print.bPlaced ? FMath::Abs(Gaps[Segment]) > Socket.MaxGapCm : !Inside(Centres[Segment] - Normals[Segment] * 4.0f));
			float DashLength = DashCm;
			float Space = DashGapCm;
			float Level = bHeld ? 0.55f : 0.75f;
			float Core = 0.0f;
			float Wide = 1.0f;
			float Tremble = 1.0f;
			if (Piece >= 1.0f)
			{
				// Welded: the dashes close into a steadier, brighter line; a fresh bead is wider with a pale core.
				const float Age = float(WorldNow - Welded[Segment]);
				const float Hot = Age < 1.5f ? 1.0f - Age / 1.5f : 0.0f;
				DashLength = DashCm + DashGapCm;
				Space = 0.0f;
				Level = 0.9f + 0.1f * SealPulse;
				Core = FMath::Max(Hot, SealPulse);
				Wide = 1.4f * (1.0f + Hot + 0.6f * SealPulse);
				Tremble = 0.3f;
			}
			else if (Piece > 0.0f)
			{
				// Under the torch: denser and brighter as the seam grows.
				DashLength = DashCm + DashGapCm * Piece * 0.8f;
				Space = DashGapCm * (1.0f - Piece * 0.8f);
				Level = 0.7f + 0.2f * Piece;
				Wide = 1.15f;
			}
			else if (bGap)
			{
				// A gap: broken further, trembling and flickering harder.
				DashLength = DashCm * GapDashShare;
				Space = DashGapCm + DashCm * (1.0f - GapDashShare);
				Level = 0.8f;
				Tremble = GapJitterGain;
			}
			const float Start = S;
			const float End = FMath::Min(S + DashLength, Length);
			S += DashLength + Space;
			const int32 Group = Side * 2 + ((Start + End) * 0.5f > Length * 0.5f ? 1 : 0);
			const int64 Key = int64(Side) * 4096 + Dash;
			if (bGap)
			{
				++GapCount;
				if (OSWeldHash(Key, Step + 7) < GapDropout)
				{
					continue;
				}
				Level *= 1.0f - GapFlicker * OSWeldHash(Key, Step + 3);
			}
			const float Shake = (OSWeldHash(Key, Step) - 0.5f) * 2.0f * JitterCm * Tremble * GroupTremble[Group];
			// The width wobbles about a steady centre line, less on the steadier strokes.
			const float Stroke = Width * Wide * (1.0f + JitterWidth * FMath::Min(Tremble, 1.0f) * (OSWeldHash(Key, Step + 31) - 0.5f) * 2.0f);
			Level *= (1.0f - JitterFlicker * OSWeldHash(Key, Step + 57)) * GroupLevel[Group];
			if (StrobeAmp > 0.0f && OSWeldHash(Key, 5) < StrobeShare)
			{
				const float Hz = FMath::Lerp(StrobeMinHz, StrobeMaxHz, OSWeldHash(Key, 6));
				const float Wave = 0.5f + 0.5f * FMath::Sin(2.0f * PI * (Hz * float(Now - AppearOrigin) + OSWeldHash(Key, 8)));
				Level *= 1.0f - StrobeAmp * (0.85f - 0.85f * Wave);
				Level = FMath::Min(Level * (1.0f + 0.6f * StrobeAmp * Wave), 1.0f);
			}
			if (Level <= 0.0f)
			{
				continue;
			}
			const FVector2D Shift = Out * (Edge + Width * Wide * 0.5f + Shake) + GroupOffset[Group];
			const FVector2D A = From + Along * Start + Shift;
			const FVector2D B = From + Along * End + Shift;
			if (Core > 0.0f)
			{
				// The light falls off from a pale sliver into the navy - drawn, not bloomed.
				Paint.Stroke(A, B, Stroke * 2.4f, Level * 0.3f);
				Paint.Stroke(A, B, Stroke, Level);
				Paint.Stroke(A, B, Stroke * 0.4f, Level, Core * GroupLevel[Group]);
			}
			else
			{
				Paint.Stroke(A, B, Stroke, Level);
			}
		}
		SegmentBase += Count;
	}

	// The frame: dashes along four edges - with empty hands the square round the hole a plate's rim must rest
	// on; with a plate, that plate's outline as it would lie now. Locked and covering, it steadies, brightens
	// and takes a pale core: the right signal. Its dashes that fall inside the hole (the plate falls short)
	// flash. Once the plate is tacked the frame is a steady solid line.
	FVector2D Quad[4];
	if (Plate)
	{
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			Quad[Corner] = Print.Corner[Corner];
		}
	}
	else
	{
		const FVector2D Half = Hole + FVector2D(PlateMarginCm, PlateMarginCm);
		Quad[0] = FVector2D(-Half.X, -Half.Y);
		Quad[1] = FVector2D(Half.X, -Half.Y);
		Quad[2] = FVector2D(Half.X, Half.Y);
		Quad[3] = FVector2D(-Half.X, Half.Y);
	}
	const bool bRight = Plate && Print.bCovers;
	const float FrameLevel = bRight ? 0.95f : Plate ? 0.7f : 0.5f;
	const float FrameTremble = bRight ? 0.35f : 1.0f;
	const float FrameCore = bRight && !bHeld ? 0.35f : 0.0f;
	int32 ShortDashes = 0;
	for (int32 Edge4 = 0; Edge4 < 4; ++Edge4)
	{
		const FVector2D From = Quad[Edge4];
		const FVector2D To = Quad[(Edge4 + 1) % 4];
		const float Length = float((To - From).Size());
		if (Length < 1.0f)
		{
			continue;
		}
		const FVector2D Along = (To - From) / Length;
		const FVector2D Across(Along.Y, -Along.X);
		// Which group flies it in: the hole side it faces.
		const FVector2D Mid = (From + To) * 0.5;
		const int32 Facing = FMath::Abs(Mid.X) > FMath::Abs(Mid.Y) ? (Mid.X > 0.0 ? 1 : 3) : (Mid.Y > 0.0 ? 2 : 0);
		const int32 Group = 8 + Facing;
		if (GroupLevel[Group] <= 0.0f)
		{
			continue;
		}
		const float Period = DashCm + DashGapCm;
		const float Dash = bHeld ? Period : DashCm;
		int32 Index = 0;
		for (float S = 0.0f; S < Length; S += Period, ++Index)
		{
			const int64 Key = int64(Group) * 4096 + Index;
			const FVector2D A = From + Along * S;
			const FVector2D B = From + Along * FMath::Min(S + Dash, Length);
			const FVector2D Centre = (A + B) * 0.5;
			float Level = FrameLevel * GroupLevel[Group] * (1.0f - JitterFlicker * OSWeldHash(Key, Step + 57));
			if (Plate && FMath::Abs(Centre.X) < Hole.X - 2.0 && FMath::Abs(Centre.Y) < Hole.Y - 2.0)
			{
				// Falls short: inside the hole, flashing at 3 Hz, out of step dash to dash.
				++ShortDashes;
				Level *= FMath::Frac(float(Now) * 3.0f + OSWeldHash(Key, 4) * 0.3f) < 0.5f ? 1.0f : 0.15f;
			}
			if (StrobeAmp > 0.0f && OSWeldHash(Key, 5) < StrobeShare)
			{
				const float Hz = FMath::Lerp(StrobeMinHz, StrobeMaxHz, OSWeldHash(Key, 6));
				const float Wave = 0.5f + 0.5f * FMath::Sin(2.0f * PI * (Hz * float(Now - AppearOrigin) + OSWeldHash(Key, 8)));
				Level *= 1.0f - StrobeAmp * (0.85f - 0.85f * Wave);
			}
			const float Shake = (OSWeldHash(Key, Step) - 0.5f) * 2.0f * JitterCm * FrameTremble * GroupTremble[Group];
			const FVector2D Shift = Across * Shake + GroupOffset[Group];
			if (FrameCore > 0.0f)
			{
				Paint.Line(A + Shift, B + Shift, Width * 2.4f, Level * 0.3f);
				Paint.Line(A + Shift, B + Shift, Width * 1.2f, Level);
				Paint.Line(A + Shift, B + Shift, Width * 0.45f, Level, FrameCore * GroupLevel[Group]);
			}
			else
			{
				Paint.Line(A + Shift, B + Shift, Width, Level);
			}
		}
	}

	// The readout: sparse letters of MK Pribor N, wide apart, and segment numbers - the seam and how much of
	// the hole the plate covers; sealed, one word. Inside the frame along its near edge: on the plate where
	// there is one (with no plate the light falls through the hole). Junk in the hull, the reason the torch
	// refuses, as MindKore's refusals read: dark signs on a lit plate, mid frame. Typed in after the lock.
	const int32 SeamPercent = FMath::RoundToInt32(Socket.GetSeamFraction() * 100.0f);
	// The cover the plate has (measured) once it lies over the hole; on its way, what its outline would cover.
	const int32 CoverPercent = FMath::RoundToInt32((Print.bPlaced ? Socket.GetCoverage() : Print.Coverage) * 100.0f);
	const int32 Junk = Socket.JunkCount();
	TArray<FOSWeldSign> Readout;
	TArray<FOSWeldSign> Refusal;
	const auto Add = [](TArray<FOSWeldSign>& Signs, const FString& Text, bool bDigits)
	{
		for (const TCHAR Char : Text)
		{
			Signs.Add({ Char, bDigits && FChar::IsDigit(Char) });
		}
	};
	if (!Plate)
	{
		Lines.Add(TEXT("(empty hands)"));
	}
	else if (bSealed)
	{
		Add(Readout, TEXT("ГЕРМ"), false);
		Lines.Add(TEXT("ГЕРМ"));
	}
	else
	{
		Add(Readout, TEXT("ШВ "), false);
		Add(Readout, FString::FromInt(SeamPercent), true);
		Add(Readout, TEXT("   ЛСТ "), false);
		Add(Readout, FString::FromInt(CoverPercent), true);
		Lines.Add(FString::Printf(TEXT("ШВ %d  ЛСТ %d"), SeamPercent, CoverPercent));
	}
	if (Junk > 0)
	{
		Add(Refusal, TEXT("МСР "), false);
		Add(Refusal, FString::FromInt(Junk), true);
		Lines.Add(FString::Printf(TEXT("[МСР %d]"), Junk));
	}
	// Units: a digit is 5 x 9 of them, a letter's cell 0.6 of one; about 1.1 px of the view a unit.
	const float LetterShare = 0.6f;
	const auto Measure = [this, LetterShare](const TArray<FOSWeldSign>& Signs)
	{
		float Units = 0.0f;
		for (const FOSWeldSign& Sign : Signs)
		{
			const FOSGlyph* Glyph = Sign.bDigit ? nullptr : OSWeldGlyph(Sign.Char);
			Units += Sign.bDigit ? 7.0f : (Glyph ? Glyph->Advance + TrackCells : 0) * LetterShare;
		}
		return Units;
	};
	float Unit = FMath::Clamp(PixelCm * 1.1f, 1.0f, 5.0f);
	Unit = FMath::Max(FMath::Min(Unit, 2.0f * (HoleAlongRight - 12.0f) / FMath::Max(Measure(Readout), 1.0f)), 0.8f);
	int32 Shown = 0;
	if (Phase == EPhase::Open)
	{
		Shown = FMath::FloorToInt32(float(T) * TypeSignsPerSecond);
	}
	else if (Phase == EPhase::Vanishing && T < 0.08 && OSWeldHash(Step, 13) > 0.4f)
	{
		Shown = MAX_int32; // flickering out first
	}
	int32 Typed = 0;
	const auto Draw = [&](const TArray<FOSWeldSign>& Signs, const FVector2D& TopLeft, float Level)
	{
		float Pen = 0.0f;
		for (const FOSWeldSign& Sign : Signs)
		{
			if (Typed >= Shown)
			{
				return;
			}
			// The newest sign is still settling: it flickers.
			const float SignLevel = Typed + 1 == Shown && Phase == EPhase::Open ? Level * (0.35f + 0.65f * OSWeldHash(Typed, FMath::FloorToInt64(Now * 30.0))) : Level;
			++Typed;
			const FVector2D Origin = TopLeft + Right * (Pen * Unit);
			if (Sign.bDigit)
			{
				const uint8 Bits = OSWeldSegmentDigits[Sign.Char - TEXT('0')];
				for (int32 Segment = 0; Segment < 7; ++Segment)
				{
					if (Bits >> Segment & 1)
					{
						const float* Box = OSWeldSegmentBoxes[Segment];
						Paint.Rect(Origin + Right * (Box[0] * Unit) + Down * (Box[1] * Unit), Origin + Right * (Box[2] * Unit) + Down * (Box[3] * Unit), SignLevel);
					}
				}
				Pen += 7.0f;
			}
			else
			{
				// Capitals stand on the digits' baseline: rows 1..7 of the glyph's ten.
				const float Cell = Unit * LetterShare;
				const FVector2D GlyphTop = Origin + Down * (9.0f * Unit - 8.0f * Cell);
				OSWeldTextRuns(FStringView(&Sign.Char, 1), [&](int32 X, int32 Y, int32 Length)
				{
					const FVector2D A = GlyphTop + Right * (X * Cell) + Down * (Y * Cell);
					Paint.Rect(A, A + Right * (Length * Cell) + Down * Cell, SignLevel);
				});
				const FOSGlyph* Glyph = OSWeldGlyph(Sign.Char);
				Pen += (Glyph ? Glyph->Advance + TrackCells : 0) * LetterShare;
			}
		}
	};
	const float Below = HoleAlongDown - Width * 3.0f - 8.0f - 9.0f * Unit;
	Draw(Readout, Down * Below - Right * (Measure(Readout) * Unit * 0.5f), 0.9f);
	if (!Refusal.IsEmpty() && Typed < Shown)
	{
		const float PlateWidth = (Measure(Refusal) + 4.0f) * Unit;
		const float PlateHeight = 13.0f * Unit;
		const FVector2D TopLeft = -Down * (PlateHeight * 0.5f) - Right * (PlateWidth * 0.5f);
		Paint.Rect(TopLeft, TopLeft + Right * PlateWidth + Down * PlateHeight, 0.85f);
		Draw(Refusal, TopLeft + Right * (2.0f * Unit) + Down * (2.0f * Unit), 0.0f);
	}
	if (Plate)
	{
		Lines.Add(FString::Printf(TEXT("outline covers %.0f%% of the hole%s, corner %s"), Print.Coverage * 100.0f, Print.bCovers ? TEXT(" and every edge") : TEXT(""),
			*Print.Corner[2].ToString()));
	}
	Lines.Add(FString::Printf(TEXT("gaps %d dashes, %s frame%s%s%s%s"), GapCount, Plate ? (Print.bSnapped ? TEXT("plate, snapped") : TEXT("plate")) : TEXT("square"),
		bRight ? TEXT(", covers") : TEXT(""), ShortDashes > 0 ? *FString::Printf(TEXT(", %d dashes short"), ShortDashes) : TEXT(""),
		bHeld ? TEXT(", tacked") : TEXT(""), SealPulse > 0.0f ? TEXT(", sealed pulse") : TEXT("")));

	if (Phase != EPhase::Open)
	{
		return;
	}
	// What the scanner does to its own picture (MindKore's Distort): the raster in bands of eight rows,
	// each band moved or dimmed whole - the mirror, not the drawing, is what a jolt disturbs.
	constexpr int32 Band = 8;
	constexpr int32 SyncBandHeight = 154;
	constexpr int32 SyncBandStep = 85;
	const int32 SyncBandY = SyncTicksLeft > 0 ? ((SyncLossTicks - SyncTicksLeft) * SyncBandStep) % RasterSize : -1;
	if (WaveRows > 0.0f || SyncBandY >= 0)
	{
		TArray<FOSWeldBox> Cut;
		Cut.Reserve(Boxes.Num() * 2);
		for (const FOSWeldBox& Box : Boxes)
		{
			if (Box.Angle != 0.0f)
			{
				// A turned stroke moves whole, with the band its centre is in.
				FOSWeldBox Piece = Box;
				Piece.X = int16(Piece.X + FMath::RoundToInt32(WaveRows * FMath::Sin((Box.Y + Box.H / 2) / Band * 0.55f + float(LastTick) * 1.3f)));
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
				const int32 Shift = FMath::RoundToInt32(WaveRows * FMath::Sin(Index * 0.55f + float(LastTick) * 1.3f));
				if (SyncBandY >= 0)
				{
					const int32 Into = ((Piece.Y + Piece.H / 2 - SyncBandY) % RasterSize + RasterSize) % RasterSize;
					const float Lost = Into < SyncBandHeight ? 0.12f : 1.0f;
					Piece.Halo *= Lost;
					Piece.Core *= Lost;
				}
				const int32 L = FMath::Clamp(Piece.X + Shift, 0, RasterSize);
				const int32 R = FMath::Clamp(Piece.X + Piece.W + Shift, 0, RasterSize);
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
	if (bBright)
	{
		// In the star's light the picture drops out as grain, a box at a time, anew every device tick.
		for (int32 Index = 0; Index < Boxes.Num(); ++Index)
		{
			const float Keep = 1.0f - 0.2f * OSWeldHash(Index, LastTick);
			Boxes[Index].Halo *= Keep;
			Boxes[Index].Core *= Keep;
		}
	}
}

void UOSWeldProjectorComponent::Aim(const AOSPlateSocket& Socket)
{
	const FTransform& Frame = Socket.GetActorTransform();
	const FVector Centre = Frame.TransformPositionNoScale(FVector(0.0f, 0.0f, Socket.SurfaceZ));
	const FVector Eye = GetComponentLocation();
	const FVector ToCentre = Centre - Eye;
	const float Distance = FMath::Max(float(ToCentre.Size()), 50.0f);
	// The cone holds the raster's square from here; no wider (the light function pass covers the cone's bounds).
	const float Cone = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(RasterHalfCm * 1.42f, Distance)) + 3.0f, 5.0f, 80.0f);
	// The range finder sets the power by the square of the distance and the slant, so the frame is as bright
	// at 8 m as at 2 m (MindKore's range gain, without its burn-out floor: nothing here is nearer than a metre);
	// in the star's light the lasers are driven harder (its SunDrive).
	const float Slant = FMath::Max(FMath::Abs(float(FVector::DotProduct(ToCentre / Distance, Frame.GetUnitAxis(EAxis::Z)))), 0.3f);
	const float Drive = FMath::Lerp(1.0f, SunDrive, FMath::Clamp((StarLux - LitLux) / FMath::Max(SunLux - LitLux, 0.01f), 0.0f, 1.0f));
	const float Power = Drive * FMath::Square(Distance / 100.0f) / Slant;
	Candelas = FMath::Min(HaloLux * Power, 200000.0f);
	for (USpotLightComponent* Laser : { HaloLaser.Get(), CoreLaser.Get() })
	{
		Laser->SetWorldRotation(ToCentre.Rotation());
		if (FMath::Abs(Cone - Laser->OuterConeAngle) > 0.5f)
		{
			Laser->SetInnerConeAngle(Cone);
			Laser->SetOuterConeAngle(Cone);
		}
		Laser->SetAttenuationRadius(Distance + RasterHalfCm * 1.5f + 100.0f);
	}
	HaloLaser->SetIntensity(Candelas);
	CoreLaser->SetIntensity(FMath::Min(CoreLux * Power, 200000.0f));
	for (UMaterialInstanceDynamic* Function : { HaloFunction.Get(), CoreFunction.Get() })
	{
		Function->SetVectorParameterValue(TEXT("Origin"), FLinearColor(Frame.GetLocation()));
		Function->SetVectorParameterValue(TEXT("AxisX"), FLinearColor(Frame.GetUnitAxis(EAxis::X)));
		Function->SetVectorParameterValue(TEXT("AxisY"), FLinearColor(Frame.GetUnitAxis(EAxis::Y)));
		Function->SetVectorParameterValue(TEXT("AxisZ"), FLinearColor(Frame.GetUnitAxis(EAxis::Z)));
		Function->SetScalarParameterValue(TEXT("HalfCm"), RasterHalfCm);
		Function->SetScalarParameterValue(TEXT("SurfaceZ"), Socket.SurfaceZ);
	}
	HaloLaser->SetVisibility(true, false);
	// The core laser is switched off, not just dark, while nothing in the picture is pale (MindKore's red laser).
	bCoreOn = Boxes.ContainsByPredicate([](const FOSWeldBox& Box) { return Box.Core > 0.0f; });
	CoreLaser->SetVisibility(bCoreOn, false);
}

void UOSWeldProjectorComponent::GoDark()
{
	if (!HaloLaser->IsVisible() && !CoreLaser->IsVisible() && Boxes.IsEmpty())
	{
		return;
	}
	Boxes.Reset();
	Lines.Reset();
	bCoreOn = false;
	HaloLaser->SetVisibility(false, false);
	CoreLaser->SetVisibility(false, false);
	if (Raster)
	{
		Raster->UpdateResource();
	}
}

void UOSWeldProjectorComponent::DrawRaster(UCanvas* Canvas, int32 Width, int32 Height)
{
	if (!Canvas)
	{
		return;
	}
	for (const FOSWeldBox& Box : Boxes)
	{
		// R drives the core laser, G the halo. Boxes are laid in order and each replaces what is under it: a cut is black.
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

void UOSWeldProjectorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const UWorld* World = GetWorld();
	if (!World || !HaloFunction)
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
	}
	const int64 Tick = FMath::FloorToInt64(WorldNow * OSDeviceClockHz);
	const bool bDeviceTick = Tick != LastTick;
	LastTick = Tick;
	double Now = Clock();

	if (bDeviceTick)
	{
		// What the view is on, held for a dwell before it is taken and kept through a glance away (MindKore's device frame).
		AOSPlateSocket* Looked = FindLookedAt();
		if (Looked == Seen.Get())
		{
			++SeenTicks;
		}
		else
		{
			Seen = Looked;
			SeenTicks = 0;
		}
		if (!Target.IsValid() && Phase != EPhase::Closed)
		{
			Phase = EPhase::Closed;
		}
		if (Phase == EPhase::Closed)
		{
			// Back on a breach it left a moment ago: taken at once, without the dwell.
			if (Looked && (SeenTicks >= DwellTicks || (Looked == Left.Get() && Tick - LeftTick <= ReacquireTicks)))
			{
				Acquire(Looked);
				Phase = EPhase::Appearing;
				AppearScale = 1.0f;
				PhaseStart = AppearOrigin = Now;
			}
		}
		else if (Phase != EPhase::Vanishing && HeldAt < 0.0)
		{
			MissTicks = Looked == Target.Get() ? 0 : MissTicks + 1;
			if (MissTicks > LoseTicks)
			{
				Left = Target;
				LeftTick = Tick;
				Phase = EPhase::Vanishing;
				PhaseStart = Now;
			}
		}
		// A step or a jolt bends the scan lines; a jolt dies away by half a tick (MindKore's ReadSurroundings).
		const FVector Velocity = GetOwner()->GetVelocity();
		Jolt = FMath::Max(Jolt * 0.5f, (Velocity - LastVelocity).Size() > 150.0f ? 10.0f : 0.0f);
		LastVelocity = Velocity;
		SyncTicksLeft = FMath::Max(SyncTicksLeft - 1, 0);
	}
	// A plate picked up, let go or laid: the frame converges again, quicker, onto the new outline.
	if (bDeviceTick && Target.IsValid() && (Phase == EPhase::Appearing || Phase == EPhase::Open))
	{
		UPrimitiveComponent* Plate = FindPlate(*Target);
		if (Plate != FramedPlate.Get())
		{
			FramedPlate = Plate;
			if (Phase == EPhase::Open && HeldAt < 0.0)
			{
				Phase = EPhase::Appearing;
				AppearScale = FMath::Clamp(ReconvergeSeconds / AppearSeconds, 0.1f, 1.0f);
				PhaseStart = AppearOrigin = Now;
			}
		}
	}
	// The phases end on the clock, at their exact ends, so a held clock replays them the same.
	if (Phase == EPhase::Appearing && Now - PhaseStart >= AppearSeconds * AppearScale)
	{
		PhaseStart += AppearSeconds * AppearScale;
		Phase = EPhase::Open;
		if (Target.IsValid() && Target->bFilled)
		{
			SealAt = PhaseStart; // a sealed ring pulses once as it comes up
		}
	}
	else if (Phase == EPhase::Vanishing && Now - PhaseStart >= VanishSeconds)
	{
		Phase = EPhase::Closed;
	}
	AOSPlateSocket* Socket = Target.Get();
	if (Phase == EPhase::Closed || !Socket)
	{
		Target = nullptr;
		GoDark();
		SetComponentTickInterval(1.0f / 24.0f);
		return;
	}
	if (Socket->bFilled && !bWasSealed && Phase == EPhase::Open)
	{
		SealAt = Now; // sealed under the torch: the ring pulses once and settles
	}
	bWasSealed = Socket->bFilled;
	const bool bAnimating = IsAnimating(Now);
	SetComponentTickInterval(bAnimating ? 0.0f : 1.0f / 24.0f);
	if (!bDeviceTick && !bAnimating)
	{
		return;
	}
	const float Wave = FMath::Clamp(float(LastVelocity.Size()) / 360.0f, 0.0f, 1.0f) * 3.0f + Jolt;
	Compose(*Socket, Now, Wave >= 0.5f ? Wave : 0.0f);
	if (Raster)
	{
		Raster->UpdateResource();
	}
	Aim(*Socket);
}

#if !UE_BUILD_SHIPPING
namespace
{
	/**
	 * os.Weld.Curves: the appear's progress curves from the projector's defaults, every 10 ms, to
	 * Saved/Logs/WeldCurves.csv - each group's flight (empty before it starts) and the signs typed.
	 */
	void RunWeldCurves()
	{
		const UOSWeldProjectorComponent* Look = GetDefault<UOSWeldProjectorComponent>();
		FString Csv = TEXT("t");
		for (int32 Group = 0; Group < UOSWeldProjectorComponent::GroupCount; ++Group)
		{
			Csv += FString::Printf(TEXT(",g%d"), Group);
		}
		Csv += TEXT(",signs\n");
		for (int32 Step = 0; Step <= 120; ++Step)
		{
			const float T = Step * 0.01f;
			Csv += FString::Printf(TEXT("%.2f"), T);
			for (int32 Group = 0; Group < UOSWeldProjectorComponent::GroupCount; ++Group)
			{
				const float Tau = (T - Look->GroupDelay(Group)) / Look->FlightSeconds;
				Csv += Tau <= 0.0f ? FString(TEXT(",")) : FString::Printf(TEXT(",%.4f"), UOSWeldProjectorComponent::FlightCurve(Tau, Look->Anticipation, Look->Overshoot));
			}
			Csv += FString::Printf(TEXT(",%d\n"), T < Look->AppearSeconds ? 0 : FMath::FloorToInt32((T - Look->AppearSeconds) * Look->TypeSignsPerSecond));
		}
		const FString Path = FPaths::ProjectLogDir() / TEXT("WeldCurves.csv");
		FFileHelper::SaveStringToFile(Csv, *Path);
		UE_LOG(LogMindKore, Log, TEXT("Weld curves: %s (appear %.2f s, flight %.2f s, anticipation %.2f, overshoot %.2f)"), *Path,
			Look->AppearSeconds, Look->FlightSeconds, Look->Anticipation, Look->Overshoot);
	}

	FAutoConsoleCommand GWeldCurves(TEXT("os.Weld.Curves"),
		TEXT("The weld projector's appear curves (each group's flight, the signs typed) every 10 ms to Saved/Logs/WeldCurves.csv."),
		FConsoleCommandDelegate::CreateStatic(&RunWeldCurves));
}
#endif
