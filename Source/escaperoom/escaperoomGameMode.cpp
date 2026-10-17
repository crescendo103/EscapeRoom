// Copyright Epic Games, Inc. All Rights Reserved.

#include "escaperoomGameMode.h"
#include "escaperoomCharacter.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"
#include "HAL/PlatformMisc.h"
#include "EscapeRoomGameState.h"
#include "DiceStageDataAsset.h"
#include "MatchPlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "DungeonPlayerController.h"
#include "EngineUtils.h"
#include "WFCGridManager.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

AescaperoomGameMode::AescaperoomGameMode()
	: Super()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter"));
	DefaultPawnClass = PlayerPawnClassFinder.Class;

	// LobbyGameMode랑 동일한 GameState를 써야 TotalDiceCollected/RemainingRunnerCount 등이 실제로 동작함
	GameStateClass = AEscapeRoomGameState::StaticClass();

	// 역할(술래/러너)과 감옥 여부를 모든 클라에 복제하기 위한 매치 전용 PlayerState
	// (주의: 이 GameMode를 상속한 블루프린트가 있다면 그 BP의 Player State Class도 확인할 것 - BP 값이 우선함)
	PlayerStateClass = AMatchPlayerState::StaticClass();
}

FString AescaperoomGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
	const FString& Options, const FString& Portal)
{
	// 기본 처리(엔진 기본 ?Name= 처리, 기본 이름 "Player0" 부여 등)를 먼저 함
	FString ErrorMessage = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	if (!ErrorMessage.IsEmpty() || !NewPlayerController)
	{
		return ErrorMessage;
	}

	FString RequestedName = UGameplayStatics::ParseOption(Options, TEXT("PName"));

	// 클라가 보낸 값이라 서버에서 다시 정리: 앞뒤 공백 제거, URL/표시를 깨는 문자 치환, 길이 제한
	RequestedName.TrimStartAndEndInline();
	for (const TCHAR* Bad : { TEXT("?"), TEXT("="), TEXT("#"), TEXT("/"), TEXT("\\"), TEXT(":") })
	{
		RequestedName.ReplaceInline(Bad, TEXT("_"));
	}
	RequestedName = RequestedName.Left(MaxPlayerNameLength);

	if (!RequestedName.IsEmpty())
	{
		ChangeName(NewPlayerController, RequestedName, false);
		UE_LOG(LogTemp, Log, TEXT("[Match] Player joined with name '%s'"), *RequestedName);
	}

	return ErrorMessage;
}

void AescaperoomGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer); // 반드시 먼저 호출 - Pawn 스폰 등 기본 로그인 처리가 여기서 일어남

	if (AEscapeRoomGameState* GS = GetGameState<AEscapeRoomGameState>())
	{
		// 술래는 항상 1명이라 누가 술래인지 구분할 필요 없이, 접속자 수 - 1 = 러너 수
		const int32 ConnectedPlayers = GS->PlayerArray.Num();
		const int32 RunnerCount = FMath::Max(ConnectedPlayers - 1, 0);
		GS->InitRunnerCount(RunnerCount);

		// InitRunnerCount와 동일한 이유로 접속할 때마다 다시 계산됨 - 아직 인원이 다 안 모인
		// 상태에서도 매번 갱신되지만, 실제로 다이스를 먹기 시작하는 건 다 모이고 맵 생성까지 끝난
		// 뒤라 문제없음(마지막 로그인 시점의 최종 RunnerCount로 정착됨)
		if (DiceStageTable)
		{
			if (const TArray<FDiceStage>* Stages = DiceStageTable->FindStagesForRunnerCount(RunnerCount))
			{
				GS->DiceStages = *Stages;
				GS->CurrentDiceStageIndex = 0;
			}
		}
	}

	// 데디 매치 서버: 인원이 다 모이면 맵 생성 시작 (다이스 단계도 최종 인원 기준으로 정해진 뒤라 정확해짐)
	CheckAllPlayersJoined();
}

UClass* AescaperoomGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	// [디버그용] 두 클래스가 다 지정돼 있을 때만 역할 분배 - 아니면 기존 동작(DefaultPawnClass) 그대로
	if (!ChaserPawnClass || (!RunnerPawnClass && RunnerPawnClasses.Num() == 0) || !InController)
	{
		// 역할 분배가 꺼져 있으면 전원 러너로 취급 (플레이어 목록 UI에 다 보이도록)
		if (InController)
		{
			if (AMatchPlayerState* MPS = InController->GetPlayerState<AMatchPlayerState>())
			{
				MPS->SetMatchRole(EMatchPlayerRole::Runner);
			}
		}
		return Super::GetDefaultPawnClassForController_Implementation(InController);
	}

	// 접속 순서 = GameState의 PlayerArray 순서. PlayerState는 컨트롤러가 생성될 때(PostLogin/폰 스폰보다
	// 먼저) PlayerArray에 추가되므로, 이 시점에 이미 자기 자리가 들어가 있음
	int32 JoinIndex = INDEX_NONE;
	if (GameState && InController->PlayerState)
	{
		JoinIndex = GameState->PlayerArray.IndexOfByKey(InController->PlayerState);
	}

	// [리슨] 실제 매치: 리슨서버 호스트 본인(= 로비에서 랜덤으로 뽑힌 술래) = 술래.
	// 리슨서버에서 로컬 컨트롤러는 호스트 1명뿐이고, 호스트는 맵 로드 때 가장 먼저 로그인하므로 JoinIndex도 항상 0.
	// [디버그] PIE 등 다른 넷모드에서는 예전처럼 ChaserPlayerIndex번째 접속자 = 술래
	const bool bListenMatch = IsNetMode(NM_ListenServer);
	const int32 ChaserJoinIndex = bListenMatch ? 0 : ChaserPlayerIndex;
	const bool bIsChaser = bListenMatch ? InController->IsLocalController() : (JoinIndex == ChaserPlayerIndex);
	UE_LOG(LogTemp, Log, TEXT("[Roles]%s %s JoinIndex=%d -> %s"), bListenMatch ? TEXT("[Listen]") : TEXT("[Debug]"),
		*GetNameSafe(InController), JoinIndex, bIsChaser ? TEXT("Chaser") : TEXT("Runner"));

	// 역할을 PlayerState에도 기록 -> 모든 클라에 복제되어 MatchWidget 플레이어 목록에서 술래를 제외할 수 있음
	AMatchPlayerState* MPS = InController->GetPlayerState<AMatchPlayerState>();
	if (MPS)
	{
		MPS->SetMatchRole(bIsChaser ? EMatchPlayerRole::Chaser : EMatchPlayerRole::Runner);
	}

	if (bIsChaser)
	{
		return ChaserPawnClass.Get();
	}

	// ---- 러너별 캐릭터 배정 (러너끼리의 접속 순서) ----
	// 이 함수는 한 플레이어에 대해 여러 번 불리므로(시작지점 스폰 + RestartPlayer 내부),
	// 처음 정한 인덱스를 PlayerState에 고정해두고 이후엔 그 값을 그대로 씀.
	// (중간에 누가 나가서 PlayerArray 순서가 바뀌어도 이미 받은 캐릭터는 안 바뀜)
	if (RunnerPawnClasses.Num() > 0)
	{
		int32 VariantIndex = INDEX_NONE;
		if (MPS && MPS->HasRunnerVariant())
		{
			VariantIndex = MPS->GetRunnerVariantIndex();
		}
		else
		{
			// 내 앞에 접속한 사람 중 술래를 뺀 수 = 몇 번째 러너인지
			const int32 RunnerOrder = FMath::Max(JoinIndex - ((ChaserJoinIndex < JoinIndex) ? 1 : 0), 0);
			VariantIndex = RunnerOrder % RunnerPawnClasses.Num();
			if (MPS)
			{
				MPS->SetRunnerVariantIndex(VariantIndex);
			}
			UE_LOG(LogTemp, Log, TEXT("[Roles] %s RunnerOrder=%d -> RunnerPawnClasses[%d] (%s)"),
				*GetNameSafe(InController), RunnerOrder, VariantIndex, *GetNameSafe(RunnerPawnClasses[VariantIndex].Get()));
		}

		if (RunnerPawnClasses.IsValidIndex(VariantIndex) && RunnerPawnClasses[VariantIndex])
		{
			return RunnerPawnClasses[VariantIndex].Get();
		}

		UE_LOG(LogTemp, Warning, TEXT("[Roles] RunnerPawnClasses[%d]가 비어있음 - RunnerPawnClass로 대체"), VariantIndex);
	}

	return RunnerPawnClass ? RunnerPawnClass.Get() : Super::GetDefaultPawnClassForController_Implementation(InController);
}

void AescaperoomGameMode::InitGameState()
{
	Super::InitGameState();

	if (AEscapeRoomGameState* GS = GetGameState<AEscapeRoomGameState>())
	{
		// ---- 맵 시드 결정 (서버 전용, 매치당 1회) ----
		// InitGameState는 레벨 액터들의 BeginPlay보다 먼저 호출되므로, WFCGridManager/PropSpawnManager가
		// 생성을 시작하는 시점엔 시드가 이미 확정되어 있음
		if (DebugFixedMapSeed != 0)
		{
			GS->MapSeed = DebugFixedMapSeed;
		}
		else
		{
			// 시간 + 엔진 난수를 섞어서 서버마다/판마다 겹치지 않게
			GS->MapSeed = static_cast<int32>(HashCombine(GetTypeHash(FDateTime::Now().GetTicks()), GetTypeHash(FMath::Rand())));
		}
		UE_LOG(LogTemp, Warning, TEXT("[MapSeed] This match seed = %d%s"), GS->MapSeed,
			DebugFixedMapSeed != 0 ? TEXT(" (DebugFixedMapSeed)") : TEXT(""));

		GS->OnAllRunnersResolved.AddUObject(this, &AescaperoomGameMode::HandleAllRunnersResolved);

		// 제한시간을 GameState에 넘겨서 클라 UI도 같은 값으로 남은 시간을 계산하게 함
		GS->MatchTimeLimitSeconds = MatchTimeLimitSeconds;
		GS->OnMatchClockStarted.AddUObject(this, &AescaperoomGameMode::HandleMatchClockStarted);
	}

	// [리슨] 예상 인원은 InitGame에서 읽음 (여기선 OptionsString이 아직 비어있음)
}

void AescaperoomGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	// [리슨] 술래 PC: OpenLevel("GameLV", "listen?ExpectedPlayers=N?PName=...")
	ExpectedPlayers = UGameplayStatics::GetIntOption(Options, TEXT("ExpectedPlayers"), 0);

	// [데디 시절 호환] 매치 서버를 실행 인자 -ExpectedPlayers=N 로 띄운 경우
	if (ExpectedPlayers <= 0 && IsNetMode(NM_DedicatedServer))
	{
		FParse::Value(FCommandLine::Get(), TEXT("ExpectedPlayers="), ExpectedPlayers);
	}

	UE_LOG(LogTemp, Log, TEXT("[MatchStart] NetMode=%d ExpectedPlayers=%d (0 = no waiting)"), (int32)GetNetMode(), ExpectedPlayers);
	if (IsNetMode(NM_ListenServer) && GetWorld())
	{
		UE_LOG(LogTemp, Log, TEXT("[Match] Listen server up on port %d (host = chaser)"), GetWorld()->URL.Port);
	}
}

void AescaperoomGameMode::CheckAllPlayersJoined()
{
	if (ExpectedPlayers <= 0 || bMapGenerationRequested)
	{
		return; // 대기 기능 꺼짐(PIE 등) 또는 이미 시작함
	}

	const int32 Joined = GetNumPlayers();
	UE_LOG(LogTemp, Log, TEXT("[MatchStart] Players joined %d / %d"), Joined, ExpectedPlayers);

	// 첫 접속 시점부터 타임아웃 시작
	if (!GetWorldTimerManager().IsTimerActive(WaitForPlayersTimerHandle) && WaitForPlayersTimeout > 0.f)
	{
		GetWorldTimerManager().SetTimer(WaitForPlayersTimerHandle, [this]()
		{
			UE_LOG(LogTemp, Warning, TEXT("[MatchStart] Timeout - starting with %d / %d players"), GetNumPlayers(), ExpectedPlayers);
			StartMapGeneration();
		}, WaitForPlayersTimeout, false);
	}

	if (Joined >= ExpectedPlayers)
	{
		bMapGenerationRequested = true;
		GetWorldTimerManager().ClearTimer(WaitForPlayersTimerHandle);

		if (GenerationStartDelay > 0.f)
		{
			GetWorldTimerManager().SetTimer(GenerationStartTimerHandle, this, &AescaperoomGameMode::StartMapGeneration,
				GenerationStartDelay, false);
		}
		else
		{
			StartMapGeneration();
		}
	}
}

void AescaperoomGameMode::StartMapGeneration()
{
	bMapGenerationRequested = true;
	GetWorldTimerManager().ClearTimer(WaitForPlayersTimerHandle);

	AWFCGridManager* GridManager = Cast<AWFCGridManager>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AWFCGridManager::StaticClass()));
	if (!GridManager)
	{
		UE_LOG(LogTemp, Error, TEXT("[MatchStart] WFCGridManager를 찾지 못해 맵 생성을 시작할 수 없음"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[MatchStart] All players ready - starting map generation"));
	GridManager->TryGenerateMap(); // bHasGenerated로 중복 호출 무시됨
}

void AescaperoomGameMode::HandleMatchClockStarted()
{
	// 구조 + 소품 생성 완료 = 시작지점이 있는 빅룸이 다 깔린 시점 -> 대기 중이던 플레이어 일괄 스폰
	// (제한시간 0 이하일 때의 조기 return보다 반드시 먼저 실행해야 함)
	SpawnPendingPlayersAtStartPoints();

	if (MatchTimeLimitSeconds <= 0.f || bMatchEnded)
	{
		return;
	}

	GetWorldTimerManager().SetTimer(MatchTimeLimitTimerHandle, this, &AescaperoomGameMode::HandleMatchTimeUp,
		MatchTimeLimitSeconds, false);
	UE_LOG(LogTemp, Log, TEXT("[MatchTime] Time limit started: %.0fs"), MatchTimeLimitSeconds);
}

void AescaperoomGameMode::HandleMatchTimeUp()
{
	UE_LOG(LogTemp, Log, TEXT("[MatchTime] Time up - ending match"));
	HandleAllRunnersResolved(); // bMatchEnded로 중복 종료 방지됨
}

void AescaperoomGameMode::HandleAllRunnersResolved()
{
	if (bMatchEnded)
	{
		return;
	}
	bMatchEnded = true;
	GetWorldTimerManager().ClearTimer(MatchTimeLimitTimerHandle); // 시간 전에 끝났으면 제한시간 타이머 정리

	// 결과 집계 - PlayerState 기준 (탈출 > 감옥 순, 탈출한 사람은 감옥 여부와 무관)
	int32 EscapedCount = 0;
	int32 CaughtCount = 0;
	int32 TotalRunners = 0;
	if (GameState)
	{
		for (APlayerState* PS : GameState->PlayerArray)
		{
			const AMatchPlayerState* MPS = Cast<AMatchPlayerState>(PS);
			if (!MPS || !MPS->IsRunner())
			{
				continue;
			}
			++TotalRunners;
			if (MPS->HasEscaped())
			{
				++EscapedCount;
			}
			else if (MPS->IsInJail())
			{
				++CaughtCount;
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[MatchEnd] Escaped=%d Caught=%d Total=%d"), EscapedCount, CaughtCount, TotalRunners);

	// 모든 플레이어(술래/감옥 러너/관전자)를 게임 종료 화면으로
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ADungeonPlayerController* DPC = Cast<ADungeonPlayerController>(It->Get()))
		{
			DPC->StopSpectating();

			// 승패는 서버에서 판정 (서버 PlayerState 값은 항상 최신)
			const AMatchPlayerState* MPS = DPC->GetPlayerState<AMatchPlayerState>();
			const bool bIsChaser = MPS && MPS->IsChaser();
			const bool bWon = bIsChaser ? (EscapedCount < TotalRunners) : (MPS && MPS->HasEscaped());
			DPC->ClientShowMatchEnd(EscapedCount, CaughtCount, TotalRunners, bIsChaser, bWon);
		}
	}
}

void AescaperoomGameMode::Logout(AController* Exiting)
{
	PendingStartPlayers.Remove(TWeakObjectPtr<AController>(Exiting));

	// [리슨] 판 도중 나간 러너 처리: 아직 진행 중(안 잡힘 + 탈출 안 함)이던 러너였으면 남은 러너 수에서 뺀다.
	// 안 빼면 RemainingRunnerCount가 0이 안 돼서, 나머지 러너가 다 잡히거나 탈출해도 제한시간까지 판이 안 끝남.
	// Logout 시점엔 폰은 이미 지워졌을 수 있지만 PlayerState는 아직 살아있음(Super/엔진 정리 이후 삭제) -> PlayerState로 판단.
	// 매치 시작 전(로딩 중)엔 역할이 아직 Unassigned라서, 호스트(술래 = 로컬 컨트롤러)가 아니면 러너로 취급
	if (!bMatchEnded && Exiting)
	{
		const AMatchPlayerState* MPS = Exiting->GetPlayerState<AMatchPlayerState>();
		const bool bRunner = MPS && (MPS->IsRunner()
			|| (MPS->GetMatchRole() == EMatchPlayerRole::Unassigned && !Exiting->IsLocalController()));

		if (bRunner && !MPS->IsInJail() && !MPS->HasEscaped())
		{
			if (AEscapeRoomGameState* GS = GetGameState<AEscapeRoomGameState>())
			{
				UE_LOG(LogTemp, Log, TEXT("[Match] Runner '%s' left mid-match -> remaining runners %d -> %d"),
					*MPS->GetPlayerName(), GS->RemainingRunnerCount, FMath::Max(GS->RemainingRunnerCount - 1, 0));

				// 마지막 남은 러너였으면 여기서 바로 OnAllRunnersResolved -> HandleAllRunnersResolved(게임 종료)까지 이어짐.
				// 나간 사람은 결과 집계에서 "탈출 못 한 러너"로 계산됨
				GS->NotifyRunnerResolved();
			}
		}
	}

	Super::Logout(Exiting);

	// [리슨] 술래 PC(리슨서버)에서 러너가 나간 경우 - 호스트는 남아있으니 아무것도 안 함.
	// 매치 도중 나간 러너 처리는 기존 게임 로직 그대로
	if (!IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	FTimerHandle CheckEmptyHandle;
	GetWorldTimerManager().SetTimer(CheckEmptyHandle, [this]()
	{
		if (GameState && GameState->PlayerArray.Num() == 0)
		{
			FPlatformMisc::RequestExit(false);
		}
	}, 0.2f, false);
}

// =========================== 시작지점 ===========================

void AescaperoomGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	if (!NewPlayer)
	{
		return;
	}

	// 매치가 이미 시작됐으면(늦게 접속) 바로 남은 시작지점에 스폰
	if (bStartPointsReady)
	{
		SpawnPlayerAtStartPoint(NewPlayer);
		return;
	}

	// 아직 맵이 없음 - 폰 없이 대기 (로딩 위젯이 화면을 덮고 있어서 보이는 문제 없음).
	// Super를 부르지 않는 게 핵심: Super 안에서 RestartPlayer -> 레벨 PlayerStart 스폰이 일어남
	PendingStartPlayers.AddUnique(TWeakObjectPtr<AController>(NewPlayer));
	ApplyLoadingCamera(NewPlayer);
	UE_LOG(LogTemp, Log, TEXT("[StartPoint] %s waiting for map generation (pending=%d)"),
		*GetNameSafe(NewPlayer), PendingStartPlayers.Num());
}

void AescaperoomGameMode::ApplyLoadingCamera(APlayerController* PC)
{
	if (!PC || LoadingCameraTag.IsNone())
	{
		return;
	}

	TArray<AActor*> Cameras;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), LoadingCameraTag, Cameras);
	if (Cameras.Num() == 0 || !Cameras[0])
	{
		UE_LOG(LogTemp, Warning, TEXT("[StartPoint] '%s' 태그가 붙은 카메라가 레벨에 없음 - 로딩 중엔 월드 원점을 보게 됨"), *LoadingCameraTag.ToString());
		return;
	}

	// 서버에서 SetViewTarget을 부르면 원격 클라에는 ClientSetViewTarget으로 전달됨.
	// 되돌리는 코드는 필요 없음: 나중에 RestartPlayerAtTransform -> Possess 시 엔진이 시점을 폰으로 바꿈
	PC->SetViewTarget(Cameras[0]);
}

bool AescaperoomGameMode::PlayerCanRestart_Implementation(APlayerController* Player)
{
	if (!bStartPointsReady)
	{
		return false;
	}
	return Super::PlayerCanRestart_Implementation(Player);
}

void AescaperoomGameMode::CollectStartPoints()
{
	RunnerStartTransforms.Reset();
	ChaserStartTransforms.Reset();
	NextRunnerStartIndex = 0;
	NextChaserStartIndex = 0;

	AWFCGridManager* GridManager = Cast<AWFCGridManager>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AWFCGridManager::StaticClass()));
	if (!GridManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("[StartPoint] WFCGridManager를 찾지 못함 - 레벨 PlayerStart로 대체 스폰"));
		return;
	}

	// 감옥(FindJailTransform)과 동일: 스폰된 빅룸 안에서 컴포넌트 태그로 찾는다
	for (const TObjectPtr<AActor>& BigRoom : GridManager->GetSpawnedBigRoomActors())
	{
		if (!BigRoom)
		{
			continue;
		}

		TInlineComponentArray<USceneComponent*> Comps;
		BigRoom->GetComponents(Comps);

		for (USceneComponent* Comp : Comps)
		{
			if (!Comp)
			{
				continue;
			}

			// 위치 + Yaw만 사용 (컴포넌트/방의 스케일, 기울기가 폰에 묻지 않게)
			const FTransform StartTransform(
				FRotator(0.f, Comp->GetComponentRotation().Yaw, 0.f),
				Comp->GetComponentLocation());

			if (Comp->ComponentHasTag(RunnerStartTag))
			{
				RunnerStartTransforms.Add(StartTransform);
			}
			else if (Comp->ComponentHasTag(ChaserStartTag))
			{
				ChaserStartTransforms.Add(StartTransform);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[StartPoint] Collected RunnerStart=%d ChaserStart=%d"),
		RunnerStartTransforms.Num(), ChaserStartTransforms.Num());

	if (RunnerStartTransforms.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[StartPoint] '%s' 태그 컴포넌트가 빅룸에 없음 - 러너 시작 방 BP 확인 필요"), *RunnerStartTag.ToString());
	}
	if (ChaserStartTransforms.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[StartPoint] '%s' 태그 컴포넌트가 빅룸에 없음 - 술래 시작 방 BP 확인 필요"), *ChaserStartTag.ToString());
	}
}

void AescaperoomGameMode::SpawnPendingPlayersAtStartPoints()
{
	if (bStartPointsReady)
	{
		return; // 매치당 1회
	}

	CollectStartPoints();
	bStartPointsReady = true;

	// 접속 순서대로 스폰 (러너 시작지점도 접속 순서대로 하나씩 배정됨)
	TArray<TWeakObjectPtr<AController>> ToSpawn = MoveTemp(PendingStartPlayers);
	PendingStartPlayers.Reset();

	for (const TWeakObjectPtr<AController>& Weak : ToSpawn)
	{
		SpawnPlayerAtStartPoint(Weak.Get());
	}
}

void AescaperoomGameMode::SpawnPlayerAtStartPoint(AController* Controller)
{
	if (!Controller || Controller->GetPawn())
	{
		return;
	}

	// 역할 판정은 기존 디버그 규칙(GetDefaultPawnClassForController)을 그대로 따른다.
	// 나중에 실제 술래 선정 방식으로 바꿔도 이쪽은 수정할 필요 없음
	UClass* PawnClass = GetDefaultPawnClassForController(Controller);
	const bool bIsChaser = PawnClass && ChaserPawnClass && PawnClass->IsChildOf(ChaserPawnClass);

	TArray<FTransform>& Pool = bIsChaser ? ChaserStartTransforms : RunnerStartTransforms;
	int32& NextIndex = bIsChaser ? NextChaserStartIndex : NextRunnerStartIndex;

	if (!Pool.IsValidIndex(NextIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("[StartPoint] %s 시작지점 부족(%d개) - %s를 레벨 PlayerStart로 대체 스폰"),
			bIsChaser ? TEXT("술래") : TEXT("러너"), Pool.Num(), *GetNameSafe(Controller));
		RestartPlayer(Controller);
		return;
	}

	FTransform SpawnTransform = Pool[NextIndex++];

	// 컴포넌트는 바닥에 놓는다고 가정 -> 캡슐 반높이만큼 올려서 바닥에 파묻히지 않게
	if (PawnClass)
	{
		if (const ACharacter* CharacterCDO = Cast<ACharacter>(PawnClass->GetDefaultObject()))
		{
			if (const UCapsuleComponent* Capsule = CharacterCDO->GetCapsuleComponent())
			{
				SpawnTransform.AddToTranslation(FVector(0.f, 0.f, Capsule->GetScaledCapsuleHalfHeight()));
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[StartPoint] Spawn %s as %s at %s"),
		*GetNameSafe(Controller), bIsChaser ? TEXT("Chaser") : TEXT("Runner"), *SpawnTransform.GetLocation().ToString());

	RestartPlayerAtTransform(Controller, SpawnTransform);
}
