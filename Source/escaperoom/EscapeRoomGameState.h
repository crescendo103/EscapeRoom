#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "RoomTypes.h"
#include "DiceStageDataAsset.h"
#include "EscapeRoomGameState.generated.h"

UCLASS()
class ESCAPEROOM_API AEscapeRoomGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	UPROPERTY(Replicated)
	TArray<FRoomInfo> Rooms;

	// 이번 판 맵 시드. 서버(AescaperoomGameMode::InitGameState)가 모든 액터 BeginPlay보다 먼저 한 번 정한다.
	// 구조(WFCGridManager)와 소품(PropSpawnManagerActor)이 전부 이 값 하나로 생성되므로,
	// 같은 시드를 다시 넣으면 맵 + 소품 배치까지 그대로 재현됨.
	// 생성 자체는 서버에서만 하므로 클라는 이 값이 필요 없지만, 디버그 HUD 표시용으로 복제해 둠
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Map")
	int32 MapSeed = 0;

	// 지금 단계 안에서 러너들이 상호작용해서 모은 다이스 개수(전체 합산이 아니라 "이번 단계 몫"만).
	// 단계가 넘어갈 때마다 0으로 리셋됨. 서버 권위 값이라 반드시 AddDice()를 통해서만, 그리고
	// 서버 코드 경로(GameMode, Server RPC의 _Implementation 등)에서만 증가시켜야 함
	UPROPERTY(ReplicatedUsing = OnRep_TotalDiceCollected)
	int32 TotalDiceCollected = 0;

	// 이번 판에 실제로 쓰이는 단계 배열. AescaperoomGameMode::PostLogin이 도망자 수로
	// DiceStageDataAsset을 조회해서 여기다 복사해 넣는다. 클라이언트도 "N/M개 모음" UI를
	// 그리려면 이 값을 알아야 하니 Replicated로 열어둠
	UPROPERTY(Replicated)
	TArray<FDiceStage> DiceStages;

	// DiceStages에서 지금 몇 번째 칸을 진행 중인지. 인원수와 무관하게 항상 0부터 시작해서
	// 단계를 클리어할 때마다 1씩 올라감
	UPROPERTY(ReplicatedUsing = OnRep_DiceStageIndex)
	int32 CurrentDiceStageIndex = 0;

	// ---- 매치 알림 (MatchWidget의 NotificationText에 표시) ----
	// 서버에서 완성된 문장을 만들어 호출하면 서버 + 모든 클라이언트에서 _Implementation이 실행되고,
	// 각 머신에서 OnMatchNotification을 로컬 브로드캐스트한다. GameState는 항상 모든 클라에
	// relevant하므로 거리와 상관없이 전원에게 도착함. 반드시 서버 권위 코드에서만 호출할 것
	// (클라에서 부르면 네트워크로 안 나가고 자기 자신에서만 실행됨)
	UFUNCTION(NetMulticast, Reliable)
	void MulticastMatchNotify(const FText& Message);

	// MatchWidget이 구독 - 알림 문장을 받아서 큐에 넣고 순서대로 연출
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMatchNotification, const FText& /*Message*/);
	FOnMatchNotification OnMatchNotification;

	FRoomInfo* FindRoom(int32 RoomId)
	{
		return Rooms.FindByPredicate([RoomId](const FRoomInfo& R) { return R.RoomId == RoomId; });
	}

	// 지금 단계를 클리어하는 데 필요한 다이스 개수 (표에 없거나 모든 단계가 끝났으면 0)
	int32 GetCurrentStageDiceRequirement() const
	{
		return DiceStages.IsValidIndex(CurrentDiceStageIndex) ? DiceStages[CurrentDiceStageIndex].DiceRequired : 0;
	}

	// 다이스 단계가 하나 클리어될 때마다(매치당 여러 번 발동 가능) 브로드캐스트 - 이번에 열어야 할
	// 탈출구 개수를 실어보냄. AddDice() 안에서만(=서버 권위 코드에서만) 발동되므로
	// PropSpawnManagerActor 같은 서버 전용 스폰 로직에서 구독하면 됨
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnDiceStageCleared, int32 /*ExitsToSpawn*/);
	FOnDiceStageCleared OnDiceStageCleared;

	// UI 갱신용 - TotalDiceCollected/CurrentDiceStageIndex가 바뀔 때마다(서버 자신 + 각 클라 로컬에서)
	// 호출됨. MatchWidget 등에서 구독해서 "N/M" 표시를 갱신하면 됨
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnDiceProgressChanged, int32 /*Collected*/, int32 /*Required*/);
	FOnDiceProgressChanged OnDiceProgressChanged;

	// 다이스를 Amount개 먹었을 때 호출. 반드시 서버 권위 코드에서만 호출할 것.
	// 이번 단계 요구량을 넘기면 단계를 넘겨서(넘친 만큼은 다음 단계로 이월) OnDiceStageCleared를
	// 쏘고, 그래도 남으면 while로 계속 다음 단계까지 확인한다(한 번에 여러 단계를 건너뛰는 극단적인
	// 경우 대비 - 보통은 한 번에 1개씩만 들어오니 한 번만 돔)
	void AddDice(int32 Amount = 1)
	{
		TotalDiceCollected += Amount;
		bool bStageCleared = false;

		while (DiceStages.IsValidIndex(CurrentDiceStageIndex) &&
			TotalDiceCollected >= DiceStages[CurrentDiceStageIndex].DiceRequired)
		{
			const int32 ExitsToSpawn = DiceStages[CurrentDiceStageIndex].ExitsToSpawn;
			TotalDiceCollected -= DiceStages[CurrentDiceStageIndex].DiceRequired;
			++CurrentDiceStageIndex;

			OnRep_DiceStageIndex(); // 서버 자신에게도 알림(리슨서버 배려, 기존 패턴과 동일)
			OnDiceStageCleared.Broadcast(ExitsToSpawn);

			// 알림: 탈출구 개방
			MulticastMatchNotify(FText::Format(
				NSLOCTEXT("MatchNotify", "ExitsOpened", "Exits opened: {0}!"),
				FText::AsNumber(ExitsToSpawn)));
			bStageCleared = true;
		}

		OnRep_TotalDiceCollected(); // 서버 자신에게도 알림

		// 알림: 다음 탈출구까지 남은 주사위 - 이번에 단계가 넘어갔으면(탈출구 개방 알림으로 대체),
		// 남은 게 0이거나 모든 단계가 끝났으면(요구량 0) 보내지 않음
		const int32 RemainingDice = GetCurrentStageDiceRequirement() - TotalDiceCollected;
		if (!bStageCleared && RemainingDice > 0)
		{
			MulticastMatchNotify(FText::Format(
				NSLOCTEXT("MatchNotify", "DiceRemaining", "{0} more dice until the next exit"),
				FText::AsNumber(RemainingDice)));
		}
	}

	// ---- 현재 맵에 남아있는(아직 안 쓰인) 탈출구 수 ----
	// AExitActor가 서버 BeginPlay에서 +1, 파괴될 때(EndPlay - 러너가 탈출에 써서 Destroy) -1 한다.
	// 복제되므로 클라 HUD(MatchWidget)도 OnActiveExitCountChanged를 구독해서 개수를 표시함
	UPROPERTY(ReplicatedUsing = OnRep_ActiveExitCount)
	int32 ActiveExitCount = 0;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnActiveExitCountChanged, int32 /*Count*/);
	FOnActiveExitCountChanged OnActiveExitCountChanged;

	// 반드시 서버 권위 코드에서만 호출할 것
	void AddActiveExitCount(int32 Delta)
	{
		ActiveExitCount = FMath::Max(0, ActiveExitCount + Delta);
		OnRep_ActiveExitCount(); // 서버 자신에게도 알림
	}

	// ---- 남은 러너 수 관리 (틀만 - 실제 호출부는 잡기/탈출 판정 쪽에서 나중에 연결) ----

	// 아직 잡히지도, 탈출하지도 않은("진행 중인") 러너 수. 매치 시작 시 InitRunnerCount()로 초기값을
	// 세팅하고, 러너가 잡히거나 탈출해서 더 이상 진행 중이 아니게 될 때마다 NotifyRunnerResolved()를
	// 호출해 감소시킨다 (호출부는 아직 없음 - 잡기/탈출 판정 쪽에서 완성되면 거기서 부르면 됨)
	UPROPERTY(ReplicatedUsing = OnRep_RemainingRunnerCount)
	int32 RemainingRunnerCount = 0;

	// 매치 시작 시의 총 러너 수 (분모용, InitRunnerCount() 이후로는 안 바뀜). UI에서
	// "안 잡힌 인원/전체 인원"(RemainingRunnerCount / TotalRunnerCount)을 표시할 때 이 값을 분모로 쓰면 됨
	UPROPERTY(Replicated)
	int32 TotalRunnerCount = 0;

	// RemainingRunnerCount/TotalRunnerCount가 바뀔 때마다(초기화 포함) 매번 브로드캐스트되는 델리게이트.
	// UI(MatchWidget 등)에서 폴링 대신 이걸 구독해서 값이 실제로 바뀔 때만 텍스트를 갱신하면 됨.
	// OnRep_RemainingRunnerCount() 안에서 호출되므로 서버(수동 호출)/클라(리플리케이션 자동 호출) 양쪽 다 동작함
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRunnerCountChanged, int32 /*Remaining*/, int32 /*Total*/);
	FOnRunnerCountChanged OnRunnerCountChanged;

	// 매치 시작 시 총 러너 수로 초기화. 반드시 서버 권위 코드에서만 호출할 것
	void InitRunnerCount(int32 TotalRunners)
	{
		RemainingRunnerCount = TotalRunners;
		TotalRunnerCount = TotalRunners;
		bAllRunnersResolvedBroadcastDone = false;
		OnRep_RemainingRunnerCount();
	}

	// 러너 한 명이 잡히거나 탈출해서 더 이상 "진행 중"이 아니게 됐을 때 호출 (원인은 구분하지 않음 -
	// 잡힘/탈출 둘 다 "남은 인원에서 제외"라는 점은 동일하니까). 반드시 서버 권위 코드에서만 호출할 것
	void NotifyRunnerResolved()
	{
		if (RemainingRunnerCount <= 0)
		{
			return;
		}

		--RemainingRunnerCount;
		OnRep_RemainingRunnerCount();
	}

	// NotifyRunnerResolved()의 반대 - 잡혔던 러너가 감옥에서 풀려나 다시 "진행 중"이 됐을 때 호출
	// (ARunnerCharacter::SetCaught(false)). 전체 러너 수를 넘지 않음. 반드시 서버 권위 코드에서만 호출할 것
	void NotifyRunnerRestored()
	{
		if (RemainingRunnerCount >= TotalRunnerCount)
		{
			return;
		}

		++RemainingRunnerCount;
		OnRep_RemainingRunnerCount();
	}

	// 남은 러너가 0인지(=전원 잡히거나 탈출해서 이번 판이 끝났는지) 판별
	bool AreAllRunnersResolved() const
	{
		return RemainingRunnerCount <= 0;
	}

	// 남은 러너가 0이 되는 시점에 딱 한 번만 브로드캐스트되는 델리게이트.
	// GameMode 등에서 바인딩해서 라운드 종료/결과 판정을 처리하면 됨
	DECLARE_MULTICAST_DELEGATE(FOnAllRunnersResolved);
	FOnAllRunnersResolved OnAllRunnersResolved;

	// ---- 공용 게임 시계 ----
	// 서버가 매치 시작 순간(구조 + 소품 생성 완료)에 StartMatchClock()으로 한 번 기록한다.
	// 클라는 매 초 값을 받지 않고, "서버 시간 - 시작 시각"을 각자 계산하므로 네트워크 비용이 없고
	// 늦게 들어온 플레이어도 바로 같은 시간이 나온다. 화면 타이머/안개 등 시간이 필요한 곳은 전부 여기를 쓸 것

	// 모든 머신에서 같은 기준의 현재 서버 시간 (초)
	UFUNCTION(BlueprintPure, Category = "Match|Time")
	double GetServerTime() const { return GetServerWorldTimeSeconds(); }

	// 매치 시계가 시작됐는지 (로딩 중에는 false)
	UFUNCTION(BlueprintPure, Category = "Match|Time")
	bool HasMatchClockStarted() const { return MatchStartServerTime >= 0.0; }

	// 매치 시작 후 경과 시간 (초). 시작 전에는 0
	UFUNCTION(BlueprintPure, Category = "Match|Time")
	float GetMatchElapsedSeconds() const;

	// 경과 시간을 "MM:SS" 텍스트로 (예: 03:27). MatchWidget 타이머에 바로 쓰면 됨
	UFUNCTION(BlueprintPure, Category = "Match|Time")
	FText GetMatchTimeText() const;

	// 매치 시계 시작. 반드시 서버 권위 코드에서만 호출할 것 (두 번째 호출부터는 무시)
	void StartMatchClock();

	// ---- 제한시간 ----
	// 제한시간(초). GameMode::InitGameState에서 GameMode의 MatchTimeLimitSeconds 값으로 채워지고 클라로 복제됨.
	// 0 이하면 제한시간 없음
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match|Time")
	float MatchTimeLimitSeconds = 1200.f;

	// 남은 시간(초). 시계 시작 전에는 제한시간 전체, 끝나면 0. 클라도 각자 계산하므로 네트워크 비용 없음
	UFUNCTION(BlueprintPure, Category = "Match|Time")
	float GetMatchRemainingSeconds() const;

	// 남은 시간을 "MM:SS"로. 시간이 다 되면 "Time Out"
	UFUNCTION(BlueprintPure, Category = "Match|Time")
	FText GetMatchRemainingTimeText() const;

	// 시계가 시작되는 순간 (서버 + 각 클라에서 한 번씩) 브로드캐스트
	DECLARE_MULTICAST_DELEGATE(FOnMatchClockStarted);
	FOnMatchClockStarted OnMatchClockStarted;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	// 매치 시작 시각 (서버 시간 기준). -1 = 아직 시작 안 함
	UPROPERTY(ReplicatedUsing = OnRep_MatchStartServerTime)
	double MatchStartServerTime = -1.0;

	UFUNCTION()
	void OnRep_MatchStartServerTime();

	UFUNCTION()
	void OnRep_TotalDiceCollected();

	UFUNCTION()
	void OnRep_DiceStageIndex();

	UFUNCTION()
	void OnRep_RemainingRunnerCount();

	UFUNCTION()
	void OnRep_ActiveExitCount();

private:
	// OnAllRunnersResolved를 이미 한 번 쐈는지 (0에서 더 안 내려가므로 재발동 방지)
	bool bAllRunnersResolvedBroadcastDone = false;
};
