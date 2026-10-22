#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "OSGameMode.generated.h"

/** The starter's old walker mode (AOSCharacter and AOSHUD); no map uses it - every map runs AOSShipGameMode (Docs/ShipMode.md). */
UCLASS()
class MINDKORE_API AOSGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AOSGameMode();
};

/**
 * Debug-grade HUD: a crosshair dot, the interaction offer under it (verb, and
 * the reason when refused) and a state line - health/armour, carrying,
 * crowbar tag, bullet time.
 */
UCLASS()
class MINDKORE_API AOSHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};

/**
 * The zero-G mode: AOSZeroGPawn and AOSZeroGHUD. Set it as the GameMode
 * override of a zero-G map whose WorldSettings set global gravity to 0. On
 * StartPlay every AMKPhysicsProp loses its damping and gets a small random
 * drift and spin (seeded, the same every run), so the junk floats.
 */
UCLASS()
class MINDKORE_API AOSZeroGGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AOSZeroGGameMode();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG", meta = (ClampMin = "0.0"))
	float InitialDriftMaxCm = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG", meta = (ClampMin = "0.0"))
	float InitialSpinMaxDeg = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG")
	int32 DriftSeed = 1337;

	virtual void StartPlay() override;
};

/** Zero-G HUD: reticle, interaction offer, O2 bar, speed, held mass, push cooldown, health. */
UCLASS()
class MINDKORE_API AOSZeroGHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
