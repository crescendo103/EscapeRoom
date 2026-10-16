#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "EscapeRoomPlayerState.generated.h"

UCLASS()
class ESCAPEROOM_API AEscapeRoomPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	UPROPERTY(ReplicatedUsing = OnRep_RoomId)
	int32 CurrentRoomId = -1;

	UPROPERTY(ReplicatedUsing = OnRep_Ready)
	bool bIsReady = false;

	UFUNCTION()
	void OnRep_RoomId();

	UFUNCTION()
	void OnRep_Ready();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
