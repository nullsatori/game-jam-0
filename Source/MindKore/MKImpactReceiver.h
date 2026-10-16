#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MKImpactTypes.h"
#include "MKImpactReceiver.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UMKImpactReceiver : public UInterface
{
	GENERATED_BODY()
};

class MINDKORE_API IMKImpactReceiver
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MindKore|Impact")
	void ReceiveResolvedImpact(const FMKResolvedImpact& ResolvedImpact);
};
