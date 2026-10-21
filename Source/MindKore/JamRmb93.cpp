#include "JamWeapons.h"

#include "Engine/StaticMesh.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "JamRmb93"

namespace
{
	const TCHAR* const Rmb93Meshes = TEXT("/Game/JamArt/Weapons/RMB93/Meshes/");

	UStaticMesh* Rmb93Mesh(const TCHAR* Name)
	{
		return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%sSM_Jam_RMB93_%s.SM_Jam_RMB93_%s"), Rmb93Meshes, Name, Name));
	}
}

FJamWeapon JamMakeRmb93(const FJamWeapon& PumpShotgun)
{
	// For now the pump shotgun's handling on the RMB-93's meshes (Scripts/jam_rmb93_assets.py): the body
	// with the counter in material slot 1, the fore-end as the moving part, 5.5 cm of pump stroke.
	FJamWeapon Gun = PumpShotgun;
	Gun.Name = LOCTEXT("Name", "Рысь");
	Gun.ViewMesh = Rmb93Mesh(TEXT("ViewBody"));
	Gun.ActionMesh = Rmb93Mesh(TEXT("Action"));
	Gun.ActionPivot = FVector::ZeroVector;
	Gun.ActionTravelCm = 5.5f;
	Gun.NixieMaterialSlot = 1;
	return Gun;
}

FJamGunAim JamMakeRmb93Aim(const FJamGunAim& PumpAim)
{
	// Along the top of the receiver to the front sight.
	FJamGunAim Aim = PumpAim;
	Aim.SightRearCm = FVector(11.5f, 0.0f, 7.4f);
	Aim.SightFrontCm = FVector(51.5f, 0.0f, 7.4f);
	return Aim;
}

#undef LOCTEXT_NAMESPACE
