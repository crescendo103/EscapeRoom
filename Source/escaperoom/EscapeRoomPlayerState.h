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

	// [EOS] 이 플레이어가 술래로 뽑혔을 때 러너들이 접속할 P2P 주소 ("EOS:<PUID>:GameNetDriver:<채널>").
	// 클라가 로비 접속 직후 Server_RegisterEOSAddress로 보냄. 로비 서버에서만 쓰므로 복제하지 않음
	FString EOSMatchAddress;

	UFUNCTION()
	void OnRep_RoomId();

	UFUNCTION()
	void OnRep_Ready();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
