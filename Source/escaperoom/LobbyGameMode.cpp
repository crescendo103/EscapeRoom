#include "LobbyGameMode.h"
#include "EscapeRoomGameState.h"
#include "EscapeRoomPlayerState.h"
#include "StartScreenController.h"
#include "Algo/AllOf.h"
#include "TimerManager.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"

ALobbyGameMode::ALobbyGameMode()
{
	GameStateClass = AEscapeRoomGameState::StaticClass();
	PlayerStateClass = AEscapeRoomPlayerState::StaticClass();
}

int32 ALobbyGameMode::CreateRoom(APlayerState* Host, const FString& RoomName, int32 MaxMembers)
{
	auto* GS = GetGameState<AEscapeRoomGameState>();
	if (!GS || !Host) return -1;

	FRoomInfo NewRoom;
	NewRoom.RoomId = NextRoomId++;
	NewRoom.RoomName = RoomName;
	// 로비 방 최대 인원은 5명(술래 1 + 도망자 최대 4)으로 제한 - 서버 권위 검증이라 클라가 조작해도 못 넘김
	NewRoom.MaxMembers = FMath::Clamp(MaxMembers, 1, 5);
	NewRoom.Members.Add(Host);
	GS->Rooms.Add(NewRoom);
	UE_LOG(LogTemp, Log, TEXT("[Lobby] Room %d ('%s') created, max %d"), NewRoom.RoomId, *RoomName, NewRoom.MaxMembers);

	if (auto* EP = Cast<AEscapeRoomPlayerState>(Host))
	{
		EP->CurrentRoomId = NewRoom.RoomId;
	}
	return NewRoom.RoomId;
}

bool ALobbyGameMode::JoinRoom(int32 RoomId, APlayerState* Player)
{
	auto* GS = GetGameState<AEscapeRoomGameState>();
	FRoomInfo* Room = GS ? GS->FindRoom(RoomId) : nullptr;
	if (!Room || Player == nullptr) return false;
	if (Room->bMatchStarted || Room->Members.Num() >= Room->MaxMembers) return false;

	Room->Members.Add(Player);
	if (auto* EP = Cast<AEscapeRoomPlayerState>(Player))
	{
		EP->CurrentRoomId = RoomId;
	}
	return true;
}

void ALobbyGameMode::LeaveRoom(APlayerState* Player)
{
	auto* EP = Cast<AEscapeRoomPlayerState>(Player);
	if (!EP || EP->CurrentRoomId < 0) return;

	auto* GS = GetGameState<AEscapeRoomGameState>();
	const int32 RoomId = EP->CurrentRoomId;
	FRoomInfo* Room = GS ? GS->FindRoom(RoomId) : nullptr;

	if (Room)
	{
		Room->Members.RemoveAll([Player](const TObjectPtr<APlayerState>& P) { return P == Player; });
		EP->bIsReady = false;
	}
	EP->CurrentRoomId = -1;

	if (Room && Room->Members.Num() == 0)
	{
		GS->Rooms.RemoveAll([RoomId](const FRoomInfo& R) { return R.RoomId == RoomId; });
		CancelRoomCountdown(RoomId);
	}
	else if (Room)
	{
		CheckRoomReadyState(RoomId);
	}
}

void ALobbyGameMode::SetReady(APlayerState* Player, bool bReady)
{
	auto* EP = Cast<AEscapeRoomPlayerState>(Player);
	if (!EP || EP->CurrentRoomId < 0) return;

	EP->bIsReady = bReady;
	CheckRoomReadyState(EP->CurrentRoomId);
}

void ALobbyGameMode::CheckRoomReadyState(int32 RoomId)
{
	auto* GS = GetGameState<AEscapeRoomGameState>();
	FRoomInfo* Room = GS ? GS->FindRoom(RoomId) : nullptr;
	if (!Room || Room->bMatchStarted) return;

	const bool bEnough = Room->Members.Num() >= 2; // adjust minimum player count as needed
	const bool bAllReady = Algo::AllOf(Room->Members, [](const TObjectPtr<APlayerState>& P)
	{
		auto* EP = Cast<AEscapeRoomPlayerState>(P);
		return EP && EP->bIsReady;
	});

	if (bEnough && bAllReady)
	{
		// [변경] 5초 카운트다운 제거 - 전원 Ready 되는 순간 효과음과 함께 바로 매치 서버를 띄움
		// (CountdownSeconds를 0으로 두면 SetTimer가 아예 안 돌아서 직접 호출로 바꿈)
		UE_LOG(LogTemp, Log, TEXT("[Lobby] Room %d: all ready, starting match immediately"), RoomId);

		for (TObjectPtr<APlayerState> Member : Room->Members)
		{
			if (auto* PC = Cast<AStartScreenController>(Member ? Member->GetPlayerController() : nullptr))
			{
				PC->Client_PlayAllReadySound();
			}
		}

		// 위에서 bMatchStarted면 return하므로, 여기서 true가 되면 이후 입장/퇴장/Ready 변경으로 재시작되지 않음
		OnRoomCountdownFinished(RoomId);
	}
	else
	{
		if (Room->bCountdownActive)
		{
			UE_LOG(LogTemp, Log, TEXT("[Lobby] Room %d: countdown cancelled (not enough / someone unready)"), RoomId);
		}
		CancelRoomCountdown(RoomId);
	}
}

void ALobbyGameMode::CancelRoomCountdown(int32 RoomId)
{
	auto* GS = GetGameState<AEscapeRoomGameState>();
	if (FRoomInfo* Room = GS ? GS->FindRoom(RoomId) : nullptr)
	{
		Room->bCountdownActive = false;
		Room->CountdownEndServerTime = 0.f;
	}
	if (FTimerHandle* Handle = RoomCountdownTimers.Find(RoomId))
	{
		GetWorldTimerManager().ClearTimer(*Handle);
	}
}

int32 ALobbyGameMode::AcquireFreeMatchPort()
{
	for (int32 i = 0; i < MaxMatchServers; ++i)
	{
		const int32 Port = BasePort + i;
		FProcHandle* Existing = PortProcesses.Find(Port);
		if (!Existing)
		{
			return Port;
		}

		// 이 포트를 쓰던 매치 서버가 이미 끝났으면(전원 나가서 스스로 종료) 핸들 정리 후 재사용
		if (!FPlatformProcess::IsProcRunning(*Existing))
		{
			FPlatformProcess::CloseProc(*Existing);
			PortProcesses.Remove(Port);
			return Port;
		}
	}
	return -1;
}

FString ALobbyGameMode::GetPublicAddressForClients() const
{
	FString PublicIP;
	if (FParse::Value(FCommandLine::Get(), TEXT("PublicIP="), PublicIP) && !PublicIP.IsEmpty())
	{
		return PublicIP;
	}

	// 인자를 빼먹으면 클라가 매치 서버에 못 들어가므로 크게 경고 (같은 PC에서 돌릴 때만 동작하는 127.0.0.1로 대체)
	UE_LOG(LogTemp, Error, TEXT("[Lobby] -PublicIP= 실행 인자가 없음! 127.0.0.1로 대체 - 다른 PC의 클라는 매치 서버에 접속 못 함. ")
		TEXT("로비를 'escaperoomServer.exe LovyLv -port=7777 -PublicIP=<EC2 탄력적 IP> -log' 형태로 실행할 것"));
	return TEXT("127.0.0.1");
}

int32 ALobbyGameMode::LaunchMatchServerProcess(int32 RoomId)
{
	// [로컬 테스트 시절 - AWS 전환으로 주석처리] 포트를 계속 늘리기만 했음
	// const int32 Port = NextPort++;
	const int32 Port = AcquireFreeMatchPort();
	if (Port < 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[Lobby] Room %d: 남은 매치 서버 포트 없음 (최대 %d개 동시 진행)"), RoomId, MaxMatchServers);
		return -1;
	}

	// 로비 서버와 같은 exe(escaperoomServer)로 매치 맵을 띄움
	const FString ExePath = FPlatformProcess::ExecutablePath();
	// 방 인원수를 같이 넘겨서, 매치 서버가 이 인원이 다 접속한 뒤에 맵 생성을 시작하게 함
	// (로딩바가 클라 화면에서 실제로 차오르는 연출용 - AescaperoomGameMode::PostLogin 참고)
	int32 ExpectedPlayers = 0;
	if (auto* LobbyGS = GetGameState<AEscapeRoomGameState>())
	{
		if (const FRoomInfo* Room = LobbyGS->FindRoom(RoomId))
		{
			ExpectedPlayers = Room->Members.Num();
		}
	}
	const FString Args = FString::Printf(TEXT("GameLV -server -port=%d -ExpectedPlayers=%d -log"), Port, ExpectedPlayers);

	FProcHandle Handle = FPlatformProcess::CreateProc(
		*ExePath, *Args,
		true, false, false,
		nullptr, 0, nullptr, nullptr);

	if (!Handle.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[Lobby] Room %d: 매치 서버 프로세스 실행 실패 (%s %s)"), RoomId, *ExePath, *Args);
		return -1;
	}

	// [로컬 테스트 시절] RoomServerProcesses.Add(RoomId, Handle);
	PortProcesses.Add(Port, Handle);
	UE_LOG(LogTemp, Log, TEXT("[Lobby] Room %d: match server process started on port %d"), RoomId, Port);
	return Port;
}

void ALobbyGameMode::RedirectRoomMembersToMatchServer(int32 RoomId)
{
	auto* GS = GetGameState<AEscapeRoomGameState>();
	FRoomInfo* Room = GS ? GS->FindRoom(RoomId) : nullptr;
	if (!Room) return;

	for (TObjectPtr<APlayerState> Member : Room->Members)
	{
		if (auto* PC = Cast<AStartScreenController>(Member->GetPlayerController()))
		{
			PC->Client_ConnectToMatch(Room->MatchServerAddress);
		}
	}
}

void ALobbyGameMode::OnRoomCountdownFinished(int32 RoomId)
{
	auto* GS = GetGameState<AEscapeRoomGameState>();
	FRoomInfo* Room = GS ? GS->FindRoom(RoomId) : nullptr;
	if (!Room) return;

	Room->bCountdownActive = false;
	Room->bMatchStarted = true;

	const int32 Port = LaunchMatchServerProcess(RoomId);
	if (Port < 0)
	{
		// 매치 서버를 못 띄움 -> 방을 대기 상태로 되돌리고 Ready를 풀어서, 다시 Ready하면 재시도되게 함
		Room->bMatchStarted = false;
		for (TObjectPtr<APlayerState> Member : Room->Members)
		{
			if (auto* EP = Cast<AEscapeRoomPlayerState>(Member))
			{
				EP->bIsReady = false;
			}
		}
		return;
	}

	// ===== [로컬 테스트 전용 - AWS 전환으로 주석처리] =====
	// 127.0.0.1은 "접속하는 사람 자신의 PC"라는 뜻이라, 클라이언트/로비/매치 서버가
	// 전부 같은 PC일 때만 동작함.
	// [AWS] EC2로 옮길 때 반드시 수정: 매치 서버는 로비와 같은 EC2에서 실행되므로 여기를 EC2 공인 IP로
	//       바꿔야 클라이언트가 접속할 수 있음. 방법 예시:
	//       1) 로비 실행 인자로 받기 - 로비를 "-PublicIP=3.35.x.x"로 실행하고
	//          FParse::Value(FCommandLine::Get(), TEXT("PublicIP="), PublicIP)로 읽어서 사용 (탄력적 IP면 이걸로 충분)
	//       2) EC2 메타데이터(http://169.254.169.254/latest/meta-data/public-ipv4)로 자동 조회
	//       + EC2 보안 그룹에서 로비 포트(7777)와 매치 서버 포트 범위(BasePort~)를 UDP로 열어둘 것
	// Room->MatchServerAddress = FString::Printf(TEXT("127.0.0.1:%d"), Port);

	// ===== [AWS] 로비 실행 인자 -PublicIP= 로 받은 EC2 공인 IP를 클라에게 알려줌 =====
	Room->MatchServerAddress = FString::Printf(TEXT("%s:%d"), *GetPublicAddressForClients(), Port);
	UE_LOG(LogTemp, Log, TEXT("[Lobby] Room %d: countdown finished, match server launching on %s"), RoomId, *Room->MatchServerAddress);

	FTimerHandle DelayHandle;
	GetWorldTimerManager().SetTimer(DelayHandle, [this, RoomId]()
	{
		RedirectRoomMembersToMatchServer(RoomId);
	}, MatchServerBootDelaySeconds, false); // [로컬 테스트 시절] 3.0f
}
