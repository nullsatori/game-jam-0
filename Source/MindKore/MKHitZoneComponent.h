#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "MKImpactTypes.h"
#include "MKHitZoneComponent.generated.h"

UCLASS(ClassGroup = (MindKore), meta = (BlueprintSpawnableComponent))
class MINDKORE_API UMKHitZoneComponent : public UBoxComponent
{
	GENERATED_BODY()

public:
	UMKHitZoneComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|HitZone")
	EMKHitZone HitZone = EMKHitZone::Default;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|HitZone")
	EMKSurfaceKind SurfaceKind = EMKSurfaceKind::Default;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|HitZone")
	float DamageMultiplierOverride = -1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|HitZone")
	EMKHitReaction ReactionOverride = EMKHitReaction::None;

	UFUNCTION(BlueprintPure, Category = "MindKore|HitZone")
	EMKSurfaceKind GetEffectiveSurfaceKind() const;

	// < 0 = no override; >= 0 applies (including 0 = zero damage).
	UFUNCTION(BlueprintPure, Category = "MindKore|HitZone")
	bool HasDamageMultiplierOverride() const { return DamageMultiplierOverride >= 0.0f; }

	UFUNCTION(BlueprintPure, Category = "MindKore|HitZone")
	bool HasReactionOverride() const { return ReactionOverride != EMKHitReaction::None; }
};
