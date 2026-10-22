#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameters.h"

/**
 * MindKore's bodycam view on a OS camera: the same wide field of view
 * (UMKCameraComponent::BodyCamFov, 134) and the same barrel lens pass
 * (M_MK_LensDistort at weight 1), which pulls the stretched edges of a
 * rectilinear 134-degree frame back in. Without the lens pass 134 looks warped.
 */
inline void OSApplyBodycamLens(UCameraComponent& Camera, float FieldOfView, bool bLensDistortion)
{
	Camera.SetFieldOfView(FieldOfView);
	if (!bLensDistortion)
	{
		return;
	}
	if (UMaterialInterface* Lens = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/MindKore/PostProcess/M_MK_LensDistort.M_MK_LensDistort")))
	{
		Camera.PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, Lens));
	}
}

/**
 * Where M_MK_LensDistort puts an undistorted screen point (UV 0..1): its BarrelDistortUV node,
 * "float2 da = d * float2(1.7778, 1.0); float r2 = dot(da, da); norm = (1 + k r2) / (1 + k 1.0402)",
 * inverted (MindKore's MKLensScreenUV). HUD marks drawn over the lensed view go through it.
 */
inline FVector2D OSLensScreenUV(const FVector2D& SourceUV, double K)
{
	const FVector2D S = SourceUV - FVector2D(0.5, 0.5);
	const double R = FMath::Square(S.X * 1.7778) + FMath::Square(S.Y);
	const double C = 1.0 + K * 1.0402;
	if (K <= 0.0 || !FMath::IsFinite(R))
	{
		return SourceUV;
	}
	// The lens only scales the offset from the centre: T + K R T^3 = C, Newton from above.
	double T = C;
	for (int32 Step = 0; Step < 12; ++Step)
	{
		T -= (T + K * R * T * T * T - C) / (1.0 + 3.0 * K * R * T * T);
	}
	return S * T + FVector2D(0.5, 0.5);
}

/** The camera's lens strength: M_MK_LensDistort's Distortion among its blendables, 0 without the lens. */
inline float OSLensK(const UCameraComponent& Camera)
{
	for (const FWeightedBlendable& Each : Camera.PostProcessSettings.WeightedBlendables.Array)
	{
		const UMaterialInterface* Lens = Cast<UMaterialInterface>(Each.Object);
		float K = 0.0f;
		if (Lens && Each.Weight > 0.0f && Lens->GetName().Contains(TEXT("LensDistort"))
			&& Lens->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Distortion")), K))
		{
			return K * Each.Weight;
		}
	}
	return 0.0f;
}
