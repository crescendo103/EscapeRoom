#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/TimerHandle.h"
#include "LobbyGameMode.generated.h"

class APlayerState;
class APlayerController;

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

	// ===== [리슨] 매치 = 랜덤으로 뽑힌 술래의 PC가 리슨서버로 호스팅 =====
	// 전원 Ready -> 방 멤버 중 랜덤 1명을 술래로 뽑아 Client_HostMatch(리슨서버 열기) 지시
	// -> MatchHostBootDelaySeconds 뒤 나머지(러너)에게 Client_ConnectToMatch(술래 주소) 지시
	// 성공하면 true. 술래 주소를 못 얻는 등 실패하면 false (방을 대기 상태로 되돌림)
	bool StartListenMatch(int32 RoomId);

	void RedirectRoomMembersToMatchServer(int32 RoomId, TWeakObjectPtr<APlayerState> ChaserPS);

	// 러너들이 접속할 술래 PC 주소의 IP 부분. 로비가 본 술래의 접속 IP를 그대로 씀
	// (같은 PC 테스트 = 127.0.0.1, 같은 공유기 = 내부 IP. 인터넷 너머는 Steam 단계에서 Steam ID로 교체)
	static FString GetRemoteIPOf(APlayerController* PC);

	// 로비 퇴장(술래가 리슨서버를 열러 떠남, 러너가 술래 PC로 떠남, 게임 종료 등) -> 방에서 빼기
	virtual void Logout(AController* Exiting) override;

	// 전원 Ready 효과음(울음소리)을 다 듣고 매치로 넘어가도록 기다리는 시간(초). 0이면 바로 시작.
	// 대기 중 누가 Ready를 풀거나 나가면 취소됨 (CancelRoomCountdown). 효과음 길이에 맞춰 조절
	static constexpr float AllReadyStartDelaySeconds = 6.f;

	// 술래 리슨 포트 범위 = BasePort ~ BasePort + MaxMatchPorts - 1 (7778~7787), 매치마다 돌아가며 사용.
	// 같은 PC에서 로비(7777)와 여러 매치를 동시에 테스트할 때 포트가 안 겹치게 하려는 용도.
	// 실제 서비스에선 술래마다 다른 PC라 겹칠 일 없음 (Steam 단계에선 포트 자체가 의미 없어짐)
	static constexpr int32 BasePort = 7778;
	static constexpr int32 MaxMatchPorts = 10;
	int32 NextMatchPortOffset = 0;

	// 술래에게 호스트 지시 후 러너를 보내기까지 기다리는 시간 (술래 PC가 GameLV를 로드하고 리슨을 열 때까지)
	static constexpr float MatchHostBootDelaySeconds = 8.f;

	TMap<int32, FTimerHandle> RoomCountdownTimers;

	// ===== [데디 매치 서버 시절 - 리슨 전환으로 삭제] =====
	// int32 LaunchMatchServerProcess(int32 RoomId);   // 로비 EC2에서 방마다 매치 서버 프로세스 실행
	// int32 AcquireFreeMatchPort();                    // 프로세스가 끝난 포트 재사용
	// FString GetPublicAddressForClients() const;      // -PublicIP= 로 EC2 공인 IP 받기
	// TMap<int32, FProcHandle> PortProcesses;          // 포트 -> 매치 서버 프로세스

	int32 NextRoomId = 0;
};
