#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraComponent.h"
#include "Materials/MaterialInterface.h"

/**
 * MindKore's bodycam view on a jam camera: the same wide field of view
 * (UMKCameraComponent::BodyCamFov, 134) and the same barrel lens pass
 * (M_MK_LensDistort at weight 1), which pulls the stretched edges of a
 * rectilinear 134-degree frame back in. Without the lens pass 134 looks warped.
 */
inline void JamApplyBodycamLens(UCameraComponent& Camera, float FieldOfView, bool bLensDistortion)
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
