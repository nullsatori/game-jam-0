#pragma once

#include "CoreMinimal.h"
#include "MKImpactTarget.h"
#include "MKImpactDummy.generated.h"

class UMKHitZoneComponent;
class UStaticMeshComponent;

UCLASS(BlueprintType, meta = (DisplayName = "MindKore Box Impact Dummy"))
class MINDKORE_API AMKImpactDummy : public AMKImpactTarget
{
	GENERATED_BODY()

public:
	AMKImpactDummy();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Dummy")
	TObjectPtr<USceneComponent> VisualRoot = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Dummy|Visual")
	TObjectPtr<UStaticMeshComponent> Mesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Dummy|Visual")
	TObjectPtr<UStaticMeshComponent> HeadMesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Dummy|Visual")
	TObjectPtr<UStaticMeshComponent> LeftArmMesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Dummy|Visual")
	TObjectPtr<UStaticMeshComponent> RightArmMesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Dummy|Visual")
	TObjectPtr<UStaticMeshComponent> LeftLegMesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Dummy|Visual")
	TObjectPtr<UStaticMeshComponent> RightLegMesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Dummy|Visual")
	TObjectPtr<UStaticMeshComponent> ArmorPlateMesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Dummy|HitZones")
	TObjectPtr<UMKHitZoneComponent> TorsoHitZone = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Dummy|HitZones")
	TObjectPtr<UMKHitZoneComponent> HeadHitZone = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Dummy|HitZones")
	TObjectPtr<UMKHitZoneComponent> LeftArmHitZone = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Dummy|HitZones")
	TObjectPtr<UMKHitZoneComponent> RightArmHitZone = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Dummy|HitZones")
	TObjectPtr<UMKHitZoneComponent> LeftLegHitZone = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Dummy|HitZones")
	TObjectPtr<UMKHitZoneComponent> RightLegHitZone = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Dummy|HitZones")
	TObjectPtr<UMKHitZoneComponent> ArmorPlate = nullptr;

private:
	void ConfigureVisualPart(UStaticMeshComponent* Component, UStaticMesh* MeshAsset, const FVector& RelativeLocation, const FVector& RelativeScale);
	void ConfigureHitZone(UMKHitZoneComponent* HitZone, EMKHitZone HitZoneType, EMKSurfaceKind SurfaceKind, const FVector& RelativeLocation, const FVector& BoxExtent);
};
