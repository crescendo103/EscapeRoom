#include "EscapeRoomGameState.h"
#include "Net/UnrealNetwork.h"

void AEscapeRoomGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEscapeRoomGameState, Rooms);
	DOREPLIFETIME(AEscapeRoomGameState, MapSeed);
	DOREPLIFETIME(AEscapeRoomGameState, TotalDiceCollected);
	DOREPLIFETIME(AEscapeRoomGameState, DiceStages);
	DOREPLIFETIME(AEscapeRoomGameState, CurrentDiceStageIndex);
	DOREPLIFETIME(AEscapeRoomGameState, RemainingRunnerCount);
	DOREPLIFETIME(AEscapeRoomGameState, TotalRunnerCount);
	DOREPLIFETIME(AEscapeRoomGameState, ActiveExitCount);
	DOREPLIFETIME(AEscapeRoomGameState, MatchStartServerTime);
	DOREPLIFETIME(AEscapeRoomGameState, MatchTimeLimitSeconds);
}

// ---- 공용 게임 시계 ----

void AEscapeRoomGameState::StartMatchClock()
{
	if (!HasAuthority() || HasMatchClockStarted())
	{
		return;
	}

	MatchStartServerTime = GetServerWorldTimeSeconds();
	OnRep_MatchStartServerTime(); // 서버 자신에게도 알림 (기존 패턴과 동일)
}

void AEscapeRoomGameState::OnRep_MatchStartServerTime()
{
	if (HasMatchClockStarted())
	{
		OnMatchClockStarted.Broadcast();
	}
}

float AEscapeRoomGameState::GetMatchElapsedSeconds() const
{
	if (!HasMatchClockStarted())
	{
		return 0.f;
	}
	return static_cast<float>(FMath::Max(0.0, GetServerWorldTimeSeconds() - MatchStartServerTime));
}

FText AEscapeRoomGameState::GetMatchTimeText() const
{
	const int32 TotalSeconds = FMath::FloorToInt(GetMatchElapsedSeconds());
	return FText::FromString(FString::Printf(TEXT("%02d:%02d"), TotalSeconds / 60, TotalSeconds % 60));
}

float AEscapeRoomGameState::GetMatchRemainingSeconds() const
{
	if (MatchTimeLimitSeconds <= 0.f)
	{
		return 0.f;
	}
	return FMath::Max(0.f, MatchTimeLimitSeconds - GetMatchElapsedSeconds());
}

FText AEscapeRoomGameState::GetMatchRemainingTimeText() const
{
	if (HasMatchClockStarted() && MatchTimeLimitSeconds > 0.f && GetMatchRemainingSeconds() <= 0.f)
	{
		return NSLOCTEXT("Match", "TimeOut", "Time Out");
	}

	// 올림 처리: 남은 0.4초도 00:01로 보이다가 정확히 0이 될 때 Time Out으로 넘어가도록
	const int32 TotalSeconds = FMath::CeilToInt(GetMatchRemainingSeconds());
	return FText::FromString(FString::Printf(TEXT("%02d:%02d"), TotalSeconds / 60, TotalSeconds % 60));
}

void AEscapeRoomGameState::MulticastMatchNotify_Implementation(const FText& Message)
{
	// 여기는 이미 각 머신(서버/클라 각각)에서 실행 중 - 이 머신의 위젯에만 로컬로 전달.
	// 데디서버에는 구독한 위젯이 없으니 아무 일도 일어나지 않음
	OnMatchNotification.Broadcast(Message);
}

void AEscapeRoomGameState::OnRep_ActiveExitCount()
{
	OnActiveExitCountChanged.Broadcast(ActiveExitCount);
}

void AEscapeRoomGameState::OnRep_TotalDiceCollected()
{
	OnDiceProgressChanged.Broadcast(TotalDiceCollected, GetCurrentStageDiceRequirement());
}

void AEscapeRoomGameState::OnRep_DiceStageIndex()
{
	// 단계가 바뀌면 "이번 단계 요구량"도 같이 바뀌니 진행률 UI를 다시 갱신
	OnDiceProgressChanged.Broadcast(TotalDiceCollected, GetCurrentStageDiceRequirement());
}

void AEscapeRoomGameState::OnRep_RemainingRunnerCount()
{
	// UI 쪽에 값이 바뀔 때마다(폴링 없이) 알림 - MatchWidget 등에서 구독
	OnRunnerCountChanged.Broadcast(RemainingRunnerCount, TotalRunnerCount);

	// TotalRunnerCount > 0: 첫 접속자가 술래라 러너 수가 0으로 초기화되는 순간 "전원 처리됨"으로
	// 오판해서 게임이 바로 끝나버리는 것 방지
	if (!bAllRunnersResolvedBroadcastDone && TotalRunnerCount > 0 && AreAllRunnersResolved())
	{
		bAllRunnersResolvedBroadcastDone = true;
		OnAllRunnersResolved.Broadcast();
	}
}
