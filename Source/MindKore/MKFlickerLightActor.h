#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MKFlickerLightActor.generated.h"

class UPointLightComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UCLASS()
class MINDKORE_API AMKFlickerLightActor : public AActor
{
	GENERATED_BODY()

public:
	AMKFlickerLightActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Light")
	TObjectPtr<USceneComponent> SceneRoot = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Light")
	TObjectPtr<UPointLightComponent> FlickerLight = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Light")
	TObjectPtr<UStaticMeshComponent> FixtureMesh = nullptr;

	// Optional authored fixture. Only material slots exposing Brightness participate.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "MindKore|Light")
	TObjectPtr<AActor> EmissiveFixtureActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Light")
	float BaseIntensity = 2600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Light")
	float FlickerIntensity = 900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Light")
	float AttenuationRadius = 900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Light")
	float FlickerSpeed = 17.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Light")
	float StutterChancePerSecond = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Light")
	float StutterDuration = 0.055f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Light")
	float StutterIntensityMultiplier = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Light")
	FLinearColor LightColor = FLinearColor(0.65f, 0.82f, 1.0f);

	/** A wall switch: off is dark, with the fixture's glow gone too. Lit at level start. */
	UFUNCTION(BlueprintCallable, Category = "MindKore|Light")
	void SetLit(bool bLit);

	UFUNCTION(BlueprintPure, Category = "MindKore|Light")
	bool IsLit() const;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FixtureMaterials;
	TArray<float> FixturePeakBrightness;
	float TimeOffset = 0.0f;
	float StutterTimeRemaining = 0.0f;
};
