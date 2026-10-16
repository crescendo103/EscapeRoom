#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/TimerHandle.h"
#include "HAL/PlatformProcess.h"
#include "LobbyGameMode.generated.h"

class APlayerState;

UCLASS()
class ESCAPEROOM_API ALobbyGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ALobbyGameMode();

	int32 CreateRoom(APlayerState* Host, const FString& RoomName, int32 MaxMembers);
	bool  JoinRoom(int32 RoomId, APlayerState* Player);
	void  LeaveRoom(APlayerState* Player);
	void  SetReady(APlayerState* Player, bool bReady);

private:
	void CheckRoomReadyState(int32 RoomId);
	void OnRoomCountdownFinished(int32 RoomId);
	void CancelRoomCountdown(int32 RoomId);

	int32 LaunchMatchServerProcess(int32 RoomId);
	void  RedirectRoomMembersToMatchServer(int32 RoomId);

	// [AWS] 비어있는 매치 서버 포트 하나를 고름 (끝난 매치 서버의 포트는 재사용). 다 차있으면 -1
	int32 AcquireFreeMatchPort();

	// [AWS] 클라이언트에게 알려줄 이 서버의 공인 IP. 로비 실행 인자 -PublicIP=x.x.x.x 로 받음
	FString GetPublicAddressForClients() const;

	// [변경] 카운트다운 제거로 미사용 - 전원 Ready 시 바로 시작 (LobbyGameMode.cpp CheckRoomReadyState)
	// static constexpr float CountdownSeconds = 5.f;

	// 매치 서버 포트 범위 = BasePort ~ BasePort + MaxMatchServers - 1 (7778~7787).
	// EC2 보안 그룹 인바운드에서 이 범위를 UDP로 열어둬야 함
	static constexpr int32 BasePort = 7778;
	static constexpr int32 MaxMatchServers = 10;

	// 매치 서버 프로세스를 띄운 뒤 클라이언트를 보내기까지 기다리는 시간.
	// 로컬에선 3초로 충분했지만 EC2에선 맵 로드가 더 걸릴 수 있어서 늘림
	static constexpr float MatchServerBootDelaySeconds = 10.f;

	TMap<int32, FTimerHandle> RoomCountdownTimers;

	// [로컬 테스트 시절] 방 ID -> 프로세스, 포트는 계속 증가만 했음 (판이 거듭되면 보안 그룹 범위를 벗어남)
	// TMap<int32, FProcHandle>  RoomServerProcesses;
	// int32 NextPort = BasePort;

	// [AWS] 포트 -> 그 포트에서 돌고 있는 매치 서버 프로세스. 빈 매치 서버는 스스로 종료하므로
	// 프로세스가 끝난 포트는 다음 방에서 다시 씀
	TMap<int32, FProcHandle> PortProcesses;

	int32 NextRoomId = 0;
};
