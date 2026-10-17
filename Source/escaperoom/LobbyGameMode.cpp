#include "LobbyGameMode.h"
#include "EscapeRoomGameState.h"
#include "EscapeRoomPlayerState.h"
#include "StartScreenController.h"
#include "Algo/AllOf.h"
#include "TimerManager.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Engine/NetConnection.h"
#include "GameFramework/PlayerController.h"

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
		// 이미 대기 중이면(입장/퇴장 등으로 다시 불림) 효과음/타이머를 다시 걸지 않음
		if (Room->bCountdownActive)
		{
			return;
		}

		// 전원 Ready -> 효과음(울음소리) -> AllReadyStartDelaySeconds 뒤 매치 시작 (효과음을 다 듣고 넘어가게).
		// 술래는 매치 시작 순간 맵을 옮기므로, 바로 시작하면 효과음이 끊겨서 안 들렸음
		UE_LOG(LogTemp, Log, TEXT("[Lobby] Room %d: all ready, starting match in %.1fs"), RoomId, AllReadyStartDelaySeconds);

		for (TObjectPtr<APlayerState> Member : Room->Members)
		{
			if (auto* PC = Cast<AStartScreenController>(Member ? Member->GetPlayerController() : nullptr))
			{
				PC->Client_PlayAllReadySound();
			}
		}

		if (AllReadyStartDelaySeconds <= 0.f)
		{
			OnRoomCountdownFinished(RoomId); // 대기 없이 바로
			return;
		}

		Room->bCountdownActive = true;
		Room->CountdownEndServerTime = GetWorld()->GetTimeSeconds() + AllReadyStartDelaySeconds;

		// OnRoomCountdownFinished에서 bMatchStarted = true가 되면 이후 입장/퇴장/Ready 변경으로 재시작되지 않음
		FTimerHandle& Handle = RoomCountdownTimers.FindOrAdd(RoomId);
		GetWorldTimerManager().SetTimer(Handle,
			FTimerDelegate::CreateUObject(this, &ALobbyGameMode::OnRoomCountdownFinished, RoomId),
			AllReadyStartDelaySeconds, false);
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

void ALobbyGameMode::Logout(AController* Exiting)
{
	// 로비를 떠나면(술래가 리슨서버를 열러 감 / 러너가 술래 PC로 감 / 게임 종료) 방 멤버에서 빼둠.
	// 매치가 시작된 방은 LeaveRoom -> CheckRoomReadyState가 bMatchStarted로 바로 리턴하므로 재시작 안 됨.
	// 마지막 멤버까지 나가면 방이 삭제되어 목록에 유령 방이 남지 않음
	if (Exiting)
	{
		LeaveRoom(Exiting->PlayerState);
	}
	Super::Logout(Exiting);
}

FString ALobbyGameMode::GetRemoteIPOf(APlayerController* PC)
{
	UNetConnection* Conn = PC ? PC->GetNetConnection() : nullptr;
	if (!Conn)
	{
		return FString();
	}
	// false = 포트 없이 IP만. 술래 PC가 열 리슨 포트는 로비가 따로 정해서 붙임
	FString IP = Conn->LowLevelGetRemoteAddress(false);

	// 로비와 같은 PC에서 접속한 클라는 IPv6 루프백/매핑 주소로 보일 수 있어서 IPv4로 정리
	if (IP == TEXT("::1"))
	{
		IP = TEXT("127.0.0.1");
	}
	IP.RemoveFromStart(TEXT("::ffff:"));
	return IP;
}

bool ALobbyGameMode::StartListenMatch(int32 RoomId)
{
	auto* GS = GetGameState<AEscapeRoomGameState>();
	FRoomInfo* Room = GS ? GS->FindRoom(RoomId) : nullptr;
	if (!Room)
	{
		return false;
	}

	// ---- 1) 술래 랜덤 선택 (유효한 멤버 중에서) ----
	TArray<AStartScreenController*> Candidates;
	for (const TObjectPtr<APlayerState>& Member : Room->Members)
	{
		if (auto* PC = Cast<AStartScreenController>(Member ? Member->GetPlayerController() : nullptr))
		{
			Candidates.Add(PC);
		}
	}
	if (Candidates.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[Lobby] Room %d: 술래 후보가 없음"), RoomId);
		return false;
	}

	AStartScreenController* ChaserPC = Candidates[FMath::RandRange(0, Candidates.Num() - 1)];

	// ---- 2) 러너들이 접속할 술래 주소 (술래가 로비를 떠나기 전에 미리 확보) ----
	const FString ChaserIP = GetRemoteIPOf(ChaserPC);
	if (ChaserIP.IsEmpty())
	{
		// 로비 서버 본인(리슨 로비/PIE 호스트)이 뽑히면 NetConnection이 없음 - 로비는 데디로 돌릴 것
		UE_LOG(LogTemp, Error, TEXT("[Lobby] Room %d: 술래(%s)의 접속 IP를 알 수 없음 - 로비를 데디서버로 실행했는지 확인"),
			RoomId, *GetNameSafe(ChaserPC->PlayerState));
		return false;
	}

	const int32 Port = BasePort + NextMatchPortOffset;
	NextMatchPortOffset = (NextMatchPortOffset + 1) % MaxMatchPorts;

	const int32 ExpectedPlayers = Room->Members.Num(); // 술래 포함
	Room->MatchServerAddress = FString::Printf(TEXT("%s:%d"), *ChaserIP, Port);

	UE_LOG(LogTemp, Log, TEXT("[Lobby] Room %d: chaser = %s -> hosting at %s (expected %d players)"),
		RoomId, *GetNameSafe(ChaserPC->PlayerState), *Room->MatchServerAddress, ExpectedPlayers);

	// ---- 3) 술래에게 리슨서버 열라고 지시 (술래는 이 직후 로비를 떠나 Logout -> 방에서 빠짐) ----
	TWeakObjectPtr<APlayerState> ChaserPS = ChaserPC->PlayerState;
	ChaserPC->Client_HostMatch(Port, ExpectedPlayers);

	// ---- 4) 술래 PC가 맵을 로드하고 리슨을 열 시간을 준 뒤 러너들을 보냄 ----
	FTimerHandle DelayHandle;
	GetWorldTimerManager().SetTimer(DelayHandle, [this, RoomId, ChaserPS]()
	{
		RedirectRoomMembersToMatchServer(RoomId, ChaserPS);
	}, MatchHostBootDelaySeconds, false);

	return true;
}

void ALobbyGameMode::RedirectRoomMembersToMatchServer(int32 RoomId, TWeakObjectPtr<APlayerState> ChaserPS)
{
	auto* GS = GetGameState<AEscapeRoomGameState>();
	FRoomInfo* Room = GS ? GS->FindRoom(RoomId) : nullptr;
	if (!Room) return; // 기다리는 사이 전원이 나가서 방이 사라짐

	for (TObjectPtr<APlayerState> Member : Room->Members)
	{
		// 술래는 이미 떠났지만(Logout으로 멤버에서 빠짐) 혹시 남아있어도 다시 보내지 않음.
		// 나간 플레이어의 PlayerState는 null일 수 있어서 반드시 확인
		if (!Member || Member == ChaserPS.Get())
		{
			continue;
		}
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

	// [리슨] 데디 매치 서버 프로세스를 띄우던 자리 -> 랜덤 술래에게 리슨서버 호스팅을 맡김
	if (!StartListenMatch(RoomId))
	{
		// 시작 실패 -> 방을 대기 상태로 되돌리고 Ready를 풀어서, 다시 Ready하면 재시도되게 함
		Room->bMatchStarted = false;
		for (TObjectPtr<APlayerState> Member : Room->Members)
		{
			if (auto* EP = Cast<AEscapeRoomPlayerState>(Member))
			{
				EP->bIsReady = false;
			}
		}
	}
}
