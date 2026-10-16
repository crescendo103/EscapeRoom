#pragma once

#include "CoreMinimal.h"
#include "RoomTypes.generated.h"

class APlayerState;

USTRUCT(BlueprintType)
struct FRoomInfo
{
	GENERATED_BODY()

	UPROPERTY() int32 RoomId = -1;
	UPROPERTY() FString RoomName;
	UPROPERTY() TArray<TObjectPtr<APlayerState>> Members;
	UPROPERTY() int32 MaxMembers = 2;
	UPROPERTY() bool bCountdownActive = false;
	UPROPERTY() float CountdownEndServerTime = 0.f; // based on GetWorld()->GetTimeSeconds()
	UPROPERTY() bool bMatchStarted = false;
	UPROPERTY() FString MatchServerAddress; // e.g. "127.0.0.1:7778"
};
