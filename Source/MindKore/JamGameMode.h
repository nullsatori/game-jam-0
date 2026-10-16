#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "JamGameMode.generated.h"

/** The starter's game mode: AJamCharacter and AJamHUD. Config: GlobalDefaultGameMode. */
UCLASS()
class MINDKORE_API AJamGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AJamGameMode();
};

/**
 * Debug-grade HUD: a crosshair dot, the interaction offer under it (verb, and
 * the reason when refused) and a state line - health/armour, carrying,
 * crowbar tag, bullet time.
 */
UCLASS()
class MINDKORE_API AJamHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
