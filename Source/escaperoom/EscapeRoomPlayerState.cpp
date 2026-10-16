#include "EscapeRoomPlayerState.h"
#include "Net/UnrealNetwork.h"
#include "StartScreenController.h"

void AEscapeRoomPlayerState::OnRep_RoomId()
{
	// Only the owning client's own controller should switch its own screen.
	if (APlayerController* PC = GetPlayerController())
	{
		if (PC->IsLocalController())
		{
			if (auto* SSC = Cast<AStartScreenController>(PC))
			{
				SSC->OnRoomIdChanged(CurrentRoomId);
			}
		}
	}
}

void AEscapeRoomPlayerState::OnRep_Ready()
{
	// Hook ready-check UI refresh here if needed (RoomWidget currently polls instead).
}

void AEscapeRoomPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEscapeRoomPlayerState, CurrentRoomId);
	DOREPLIFETIME(AEscapeRoomPlayerState, bIsReady);
}
