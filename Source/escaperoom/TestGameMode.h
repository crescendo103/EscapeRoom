// TestGameMode.h
// 테스트맵 전용 게임모드: escaperoomGameMode의 매치 로직(술래/러너 배정, 목조르기, 감옥 등)은 그대로 쓰고,
// WFC 맵 생성 없이 접속 즉시 레벨의 PlayerStart에 스폰한다.
// 매치 UI(로딩 종료 → 시작 연출 → MatchWidget → BGM)는 DungeonPlayerController가 "생성 완료된 WFCGridManager"를
// 찾아야 진행되므로, 아무것도 생성하지 않고 완료 상태만 가진 스텁(ATestMapGridStub)을 레벨에 하나 스폰해준다.
#pragma once

#include "CoreMinimal.h"
#include "escaperoomGameMode.h"
#include "WFCGridManager.h"
#include "TestGameMode.generated.h"

// 맵을 생성하지 않고 "생성 완료" 상태만 알려주는 WFCGridManager. TestGameMode가 서버에서 스폰 → 모든 클라에 복제됨
UCLASS(NotPlaceable)
class ATestMapGridStub : public AWFCGridManager
{
	GENERATED_BODY()

public:
	ATestMapGridStub();

	// 부모 BeginPlay(맵 생성 시작)를 건너뛰고 완료 상태로만 만든다
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override {}

private:
	void BroadcastComplete();
};

UCLASS()
class ATestGameMode : public AescaperoomGameMode
{
	GENERATED_BODY()

protected:
	// 레벨에 WFCGridManager가 없으면 스텁을 스폰
	virtual void InitGameState() override;

	// 부모는 맵 생성 완료(OnMatchClockStarted)까지 대기열에만 넣는데, 여기선 바로 스폰 + 매치 시계 시작
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

private:
	// 시작지점 준비(=부모의 bStartPointsReady)를 접속 시점에 바로 끝냄.
	// 빅룸 시작지점이 0개라 SpawnPlayerAtStartPoint가 레벨 PlayerStart로 대체 스폰함
	void EnsureStartPointsReady();
};
