// TestGameMode.cpp
#include "TestGameMode.h"
#include "EscapeRoomGameState.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/World.h"

// =========================== ATestMapGridStub ===========================

ATestMapGridStub::ATestMapGridStub()
{
	// 클라 PC도 찾을 수 있게 항상 복제, 생성 Tick은 쓰지 않음
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

void ATestMapGridStub::BeginPlay()
{
	// AWFCGridManager::BeginPlay는 맵 생성을 시작하므로 건너뛰고 AActor 것만 실행
	AActor::BeginPlay();

	// 서버/클라 각자: DungeonPlayerController가 IsMapGenerated()로 바로 로딩 종료 단계로 넘어가게 함
	bHasGenerated = true;

	if (HasAuthority())
	{
		bReplicatedGenComplete = true; // 클라: OnRep_GenComplete -> OnMapGenComplete
	}

	// PC가 스텁을 BeginPlay보다 먼저 찾아서 "생성 중"으로 판단하고 기다리는 경우 대비 - 한 번 더 완료 알림
	// (PC의 HandleGenComplete는 중복 실행 방지가 있어서 두 번 받아도 무해)
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, this, &ATestMapGridStub::BroadcastComplete, 0.2f, false);
}

void ATestMapGridStub::BroadcastComplete()
{
	OnMapGenComplete.Broadcast();
}

// =========================== ATestGameMode ===========================

void ATestGameMode::InitGameState()
{
	Super::InitGameState();

	UWorld* World = GetWorld();
	if (World && !UGameplayStatics::GetActorOfClass(World, AWFCGridManager::StaticClass()))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<ATestMapGridStub>(ATestMapGridStub::StaticClass(), FTransform::Identity, Params);
		UE_LOG(LogTemp, Log, TEXT("[TestGameMode] WFC 맵 생성 없이 스텁 GridManager 스폰 (UI 진행용)"));
	}
}

void ATestGameMode::EnsureStartPointsReady()
{
	if (bStartPointsReady)
	{
		return;
	}

	CollectStartPoints();     // 스텁엔 빅룸이 없으니 빈 목록 -> 레벨 PlayerStart로 대체 스폰
	bStartPointsReady = true; // 이후 PlayerCanRestart(부모)도 통과
}

void ATestGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	if (!NewPlayer)
	{
		return;
	}

	EnsureStartPointsReady();

	// 역할 판정(GetDefaultPawnClassForController)은 부모 그대로
	SpawnPlayerAtStartPoint(NewPlayer);

	// 게임 레벨에선 소품 배치 완료 시점에 시작되는 매치 시계(제한시간/UI 타이머)를 첫 접속 때 바로 시작.
	// (부모의 HandleMatchClockStarted가 이어서 제한시간 타이머를 건다. 두 번째 호출부턴 무시됨)
	if (AEscapeRoomGameState* GS = GetGameState<AEscapeRoomGameState>())
	{
		GS->StartMatchClock();
	}
}
