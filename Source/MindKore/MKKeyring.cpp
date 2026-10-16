#include "MKKeyring.h"

#include "GameFramework/Actor.h"
#include "MindKore.h"

UMKKeyringComponent::UMKKeyringComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UMKKeyringComponent::AddKey(FName KeyId)
{
	if (KeyId.IsNone())
	{
		return false;
	}
	bool bAlreadyHeld = false;
	Keys.Add(KeyId, &bAlreadyHeld);
	if (bAlreadyHeld)
	{
		return false;
	}
	UE_LOG(LogMindKore, Log, TEXT("Keyring %s: key '%s' added (%d held)"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("?"), *KeyId.ToString(), Keys.Num());
	return true;
}

bool UMKKeyringComponent::HasKey(FName KeyId) const
{
	return !KeyId.IsNone() && Keys.Contains(KeyId);
}

UMKKeyringComponent* MKFindKeyring(const AActor* Holder)
{
	return Holder ? Holder->FindComponentByClass<UMKKeyringComponent>() : nullptr;
}

bool MKHolderHasKey(const AActor* Holder, FName KeyId)
{
	const UMKKeyringComponent* Keyring = MKFindKeyring(Holder);
	return Keyring && Keyring->HasKey(KeyId);
}
