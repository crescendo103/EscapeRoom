#include "MatchPlayerState.h"
#include "Net/UnrealNetwork.h"

void AMatchPlayerState::SetMatchRole(EMatchPlayerRole NewRole)
{
	if (!HasAuthority())
	{
		return;
	}
	MatchRole = NewRole;
}

void AMatchPlayerState::SetInJail(bool bNewInJail)
{
	if (!HasAuthority())
	{
		return;
	}
	bIsInJail = bNewInJail;
}

void AMatchPlayerState::SetEscaped(bool bNewEscaped)
{
	if (!HasAuthority())
	{
		return;
	}
	bHasEscaped = bNewEscaped;
}

void AMatchPlayerState::SetRunnerVariantIndex(int32 NewIndex)
{
	if (!HasAuthority())
	{
		return;
	}
	RunnerVariantIndex = NewIndex;
}

void AMatchPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMatchPlayerState, MatchRole);
	DOREPLIFETIME(AMatchPlayerState, bIsInJail);
	DOREPLIFETIME(AMatchPlayerState, bHasEscaped);
	DOREPLIFETIME(AMatchPlayerState, RunnerVariantIndex);
}
