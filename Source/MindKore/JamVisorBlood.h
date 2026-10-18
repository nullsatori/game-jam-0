#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "JamVisorBlood.generated.h"

class UCameraComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

/**
 * Blood on the visor: MindKore's lens blood (M_MK_LensBlood, the blot that
 * lands, runs and fades - UMKCameraComponent's) on a jam pawn's camera.
 * Splash() throws a blot; while one lasts the material is the FIRST blendable
 * of the owner's camera (the blood is on the outside of the glass, so the lens
 * pass bends it), and it leaves the chain when the last blot is gone.
 *
 * Material contract (as MindKore's): "BlotA"/"BlotB" <- (centre U, centre V,
 * half width / frame height, age s), "SeedA"/"SeedB", "Life".
 * Added on demand by whoever splashes (FindOrAdd), so the pawn needs no edit.
 */
UCLASS(ClassGroup = (Jam), meta = (BlueprintSpawnableComponent))
class MINDKORE_API UJamVisorBloodComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UJamVisorBloodComponent();

	/** Seconds from the splash until the blot is gone (the material fades it over the last part). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|VisorBlood", meta = (ClampMin = "0.1"))
	float LifeSeconds = 3.5f;

	/** Half the blot's width as a share of the frame's height at strength 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|VisorBlood", meta = (ClampMin = "0.01"))
	float BlotSize = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|VisorBlood")
	TSoftObjectPtr<UMaterialInterface> Material = TSoftObjectPtr<UMaterialInterface>(
		FSoftObjectPath(TEXT("/Game/MindKore/PostProcess/M_MK_LensBlood.M_MK_LensBlood")));

	/** The owner's component, created and registered when it has none. */
	static UJamVisorBloodComponent* FindOrAdd(AActor* Owner);

	/**
	 * A blot of Strength (0..1 of BlotSize) off the middle of the frame - below
	 * and to a side, never on the point of aim. Returns its centre (viewport UV).
	 */
	FVector2D Splash(float Strength);

	int32 GetBlotCount() const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FBlot
	{
		FVector2D Centre = FVector2D(0.5, 0.5);
		float Size = 0.0f;
		float Age = 0.0f;
		float Seed = 0.0f;
	};
	FBlot Blots[2];

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MID = nullptr;

	bool bInChain = false;
	UCameraComponent* Camera() const;
	void SetInChain(bool bWanted);
};
