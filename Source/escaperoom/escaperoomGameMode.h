// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/TimerHandle.h"
#include "escaperoomGameMode.generated.h"

class UDiceStageDataAsset;

UCLASS(minimalapi)
class AescaperoomGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AescaperoomGameMode();

	// 도망자 수(N)별 다이스/탈출구 단계 표. 콘텐츠 브라우저에서 만든 DiceStageDataAsset 애셋을
	// 여기 연결해두면 됨(블루프린트 클래스 디폴트 패널에서 지정)
	UPROPERTY(EditDefaultsOnly, Category = "Match|DiceStages")
	TObjectPtr<UDiceStageDataAsset> DiceStageTable;

	// ===== 맵 시드 =====
	// 0이면 매 판 서버가 랜덤으로 시드를 뽑는다(기본). 0이 아니면 그 값으로 고정 -
	// 로그에 찍힌 "[MapSeed] ..." 값을 여기 넣으면 그 판의 맵 + 소품 배치를 그대로 재현할 수 있음 (디버그용)
	UPROPERTY(EditDefaultsOnly, Category = "Debug|Map")
	int32 DebugFixedMapSeed = 0;

	// ===== [디버그용] 역할별 폰 분배 =====
	// 접속 순서로 술래/러너를 나눠서 스폰함 (에디터 PIE에서 창 여러 개로 테스트하기 위한 임시 규칙).
	// ChaserPlayerIndex번째로 접속한 플레이어(0 = 처음 들어온 사람 = PIE 첫 번째 창/호스트)만 술래,
	// 나머지는 전부 러너. 두 클래스 중 하나라도 비어있으면 이 기능은 꺼지고 예전처럼 DefaultPawnClass 사용.
	// 나중에 실제 매치(랜덤 술래/로비 선택 등)로 바꿀 때는 GetDefaultPawnClassForController의 판정만 교체하면 됨
	UPROPERTY(EditDefaultsOnly, Category = "Debug|Roles")
	TSubclassOf<APawn> ChaserPawnClass;

	// 러너 기본 클래스. RunnerPawnClasses가 비어있거나 해당 칸이 비어있을 때 대신 사용됨
	UPROPERTY(EditDefaultsOnly, Category = "Debug|Roles")
	TSubclassOf<APawn> RunnerPawnClass;

	// 러너별 캐릭터(외형만 다른 러너 BP의 자식 BP들). 러너끼리의 접속 순서대로 배정:
	// 첫 번째 러너 = [0], 두 번째 러너 = [1] ... (술래는 순서에서 제외)
	// 칸 수보다 러너가 많으면 처음부터 다시 돌아가며 배정(i % Num)
	UPROPERTY(EditDefaultsOnly, Category = "Debug|Roles")
	TArray<TSubclassOf<APawn>> RunnerPawnClasses;

	// 몇 번째로 접속한 플레이어를 술래로 할지 (0부터). 1로 바꾸면 두 번째 창이 술래가 됨
	// [리슨] 실제 매치(술래 PC가 리슨서버)에서는 이 값과 상관없이 "호스트 본인 = 술래".
	//        이 값은 에디터 PIE(데디/Standalone 넷모드) 테스트에서만 쓰임
	UPROPERTY(EditDefaultsOnly, Category = "Debug|Roles", meta = (ClampMin = "0"))
	int32 ChaserPlayerIndex = 0;

	// [리슨] 로비가 넘겨준 인원이 다 모일 때까지 맵 생성을 미루는 중인지.
	// AWFCGridManager::BeginPlay가 이걸 보고 자동 생성을 건너뜀 (InitGame이 레벨 액터 BeginPlay보다 먼저라 안전)
	bool IsWaitingForPlayers() const { return ExpectedPlayers > 0 && !bMapGenerationRequested; }

	// 스폰할 폰 클래스를 플레이어마다 결정 (엔진이 RestartPlayer → SpawnDefaultPawnFor 과정에서 호출)
	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;

protected:
	// [리슨] 맵 URL 옵션 읽기 (?ExpectedPlayers=N). 술래가 OpenLevel("GameLV", "listen?ExpectedPlayers=N")로 엶.
	// InitGameState는 GameMode 스폰 도중(InitGame보다 먼저) 불려서 그 시점엔 OptionsString이 비어있음 -> 여기서 읽음
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

	// GameState가 만들어진 직후 - 게임 종료 이벤트(OnAllRunnersResolved) 구독
	virtual void InitGameState() override;

	// 게임 종료 처리 (서버 전용, 매치당 1회): 결과 집계 후 모든 플레이어를 EscapeFadeWidget의
	// 게임 종료 화면으로 보냄 (ADungeonPlayerController::ClientShowMatchEnd)
	void HandleAllRunnersResolved();

	bool bMatchEnded = false;

	// ---- 제한시간 ----
	// 매치 제한시간(초). 매치 시계가 시작된 뒤(맵 + 소품 생성 완료) 이 시간이 지나면 기존 게임 종료 처리를 그대로 호출.
	// 0 이하면 제한시간 없음
	UPROPERTY(EditDefaultsOnly, Category = "Match|Time", meta = (ClampMin = "0.0"))
	float MatchTimeLimitSeconds = 1200.f;

	FTimerHandle MatchTimeLimitTimerHandle;

	// GameState::OnMatchClockStarted (서버) -> 제한시간 타이머 시작
	void HandleMatchClockStarted();

	// 제한시간 종료 -> HandleAllRunnersResolved()와 동일한 결과 집계 + 종료 화면
	void HandleMatchTimeUp();

	// [데디 매치 서버일 때만] 전원이 나가면 프로세스 종료.
	// [리슨] 술래 PC에서는 절대 종료하면 안 됨(호스트 본인 게임이 꺼짐) -> NM_DedicatedServer일 때만 동작
	virtual void Logout(AController* Exiting) override;

	// 플레이어가 접속을 완료할 때마다 호출됨(서버 전용). 술래는 항상 1명 고정이므로,
	// 접속자 수 - 1을 러너 수로 다시 계산해서 GameState에 반영한다. 아직 인원이 다 안 모인 상태에서도
	// 매번 재계산되지만, 실제 추격전은 다 모인 뒤에 시작되니 문제없음. 다이스 단계 표도 같은 이유로
	// 매번 다시 조회해서 채워넣음(늦게 들어오는 인원까지 반영된 최종 RunnerCount로 정착됨)
	virtual void PostLogin(APlayerController* NewPlayer) override;

	// 접속 URL의 ?PName= 옵션(로비에서 쓰던 이름)을 읽어서 PlayerName으로 세팅.
	// 엔진 순서가 Login -> InitNewPlayer -> PostLogin이라 PostLogin 시점엔 이미 이름이 들어가 있음.
	// (나중에 AWS GameLift로 가면 여기서 PName 대신 PlayerSessionId를 검증/조회하도록 바꾸면 됨)
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
		const FString& Options, const FString& Portal = TEXT("")) override;

	// PName 최대 길이 (서버 기준으로 잘라냄)
	static constexpr int32 MaxPlayerNameLength = 16;

	// ===== 시작지점 =====
	// 맵(WFC)은 접속 이후 비동기로 생성되므로, 접속 즉시 폰을 스폰하지 않고 대기시켰다가
	// 구조 + 소품 생성이 끝난 순간(GameState::OnMatchClockStarted)에 빅룸 안 시작지점으로 일괄 스폰한다.
	// 시작지점 = 빅룸 블루프린트 안의 씬/Arrow 컴포넌트에 아래 컴포넌트 태그를 붙인 것
	// (감옥 "Jail" 태그와 같은 패턴 - ARunnerCharacter::FindJailTransform 참고).
	// 컴포넌트는 바닥 위에 두면 되고(캡슐 반높이만큼 자동으로 올려서 스폰), 회전은 Yaw만 사용한다.
	UPROPERTY(EditDefaultsOnly, Category = "Match|Spawn")
	FName RunnerStartTag = TEXT("RunnerStart");

	UPROPERTY(EditDefaultsOnly, Category = "Match|Spawn")
	FName ChaserStartTag = TEXT("ChaserStart");

	// 맵 생성(로딩) 동안 대기 중인 플레이어가 바라볼 카메라. 레벨에 CameraActor를 놓고 이 태그(Actor Tags)를 붙이면 됨.
	// 없으면 엔진 기본대로 월드 원점(0,0,0)에서 +X 방향을 봄. 폰이 Possess되면 엔진이 자동으로 폰 시점으로 바꿔줌
	UPROPERTY(EditDefaultsOnly, Category = "Match|Spawn")
	FName LoadingCameraTag = TEXT("LoadingCamera");

	// 대기 중인 플레이어 시점을 LoadingCameraTag 카메라로 고정 (서버에서 호출 -> 원격 클라에도 자동 전달)
	void ApplyLoadingCamera(APlayerController* PC);

	// 접속은 했지만 아직 맵이 없어서 스폰을 미뤄둔 컨트롤러들
	TArray<TWeakObjectPtr<AController>> PendingStartPlayers;

	// 시작지점 수집이 끝났는지 (= 매치 시작 이후). 이후에 접속한 플레이어는 바로 남은 시작지점에 스폰
	bool bStartPointsReady = false;

	TArray<FTransform> RunnerStartTransforms;
	TArray<FTransform> ChaserStartTransforms;
	int32 NextRunnerStartIndex = 0;
	int32 NextChaserStartIndex = 0;

	// ===== 맵 생성 시작 시점 =====
	// [리슨] 술래가 연 맵 URL의 ?ExpectedPlayers=N (술래 포함 방 인원). 0이면(에디터 PIE 등) 대기하지 않고
	// WFCGridManager가 BeginPlay에서 예전처럼 바로 생성한다. (데디 시절엔 -ExpectedPlayers= 실행 인자)
	int32 ExpectedPlayers = 0;

	// 첫 플레이어 접속 후 이 시간(초)이 지나도 인원이 다 안 모이면 있는 인원으로 생성 시작 (이동 중 튕긴 사람 대비)
	UPROPERTY(EditDefaultsOnly, Category = "Match|Start", meta = (ClampMin = "0.0"))
	float WaitForPlayersTimeout = 20.f;

	// 마지막 인원이 들어온 뒤 생성 시작까지 여유(초). 클라가 로딩 위젯/진행률 구독을 준비할 시간
	UPROPERTY(EditDefaultsOnly, Category = "Match|Start", meta = (ClampMin = "0.0"))
	float GenerationStartDelay = 1.0f;

	FTimerHandle WaitForPlayersTimerHandle;
	FTimerHandle GenerationStartTimerHandle;
	bool bMapGenerationRequested = false;

	// 접속할 때마다 인원 체크 -> 다 모이면 생성 예약
	void CheckAllPlayersJoined();

	// 실제로 WFCGridManager::TryGenerateMap() 호출 (매치당 1회)
	void StartMapGeneration();

	// 엔진 기본은 여기서 바로 RestartPlayer(=PlayerStart에 스폰)를 하는데, 매치 시작 전이면 대기열에만 넣는다
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

	// 매치 시작 전에는 클라의 ServerRestartPlayer 등으로 폰이 먼저 생기지 않도록 막음
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override;

	// 스폰된 빅룸들에서 RunnerStartTag / ChaserStartTag 컴포넌트 위치를 모음 (서버 전용)
	void CollectStartPoints();

	// 대기 중이던 플레이어 전원을 시작지점에 스폰
	void SpawnPendingPlayersAtStartPoints();

	// 역할(술래/러너)에 맞는 시작지점에 한 명 스폰. 시작지점이 부족/없으면 경고 후 기존 PlayerStart로 대체
	void SpawnPlayerAtStartPoint(AController* Controller);
};
