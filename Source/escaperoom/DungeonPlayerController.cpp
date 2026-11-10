#include "DungeonPlayerController.h"
#include "WFCGridManager.h"
#include "PropSpawnManagerActor.h"
#include "LoadingScreenWidget.h"
#include "MatchWidget.h"
#include "EngineUtils.h" // TActorIterator
#include "EscapeFadeWidget.h"
#include "MatchPlayerState.h"
#include "escaperoomCharacter.h"
#include "MatchIntroComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EscapeRoomSessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "ChaserCharacter.h"
#include "CabinetActor.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "ResultRoomActor.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"

void ADungeonPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// [HANG FIX] Only the local player should show/bind the loading UI.
	// On a server, PlayerControllers representing remote clients are NOT local,
	// and creating/assigning a widget to them logs
	// "Only Local Player Controllers can be assigned to widgets." and is undefined-ish.
	if (!IsLocalController())
	{
		return;
	}

	// 클라이언트는 컨트롤러가 복제돼서 BeginPlay가 먼저 불리고, 내 화면(LocalPlayer) 연결은 그 뒤에 옴.
	// 연결 전에 위젯을 만들면 "CreateWidget cannot be used on Player Controller with no attached player"로 실패
	// (로딩 화면이 안 뜸) -> 아직이면 ReceivedPlayer()에서 초기화
	if (!GetLocalPlayer())
	{
		return;
	}
	InitLocalUI();
}

void ADungeonPlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();

	// BeginPlay 이후에 LocalPlayer가 연결된 경우 (BeginPlay 전이면 BeginPlay가 처리)
	if (HasActorBegunPlay() && IsLocalController() && GetLocalPlayer())
	{
		InitLocalUI();
	}
}

void ADungeonPlayerController::InitLocalUI()
{
	if (bLocalUIInitialized)
	{
		return;
	}
	bLocalUIInitialized = true;

	// 바로 찾기를 시도하고, 실패하면 찾을 때까지 타이머 재시도
	InitializeMapGeneratorBinding();
}

void ADungeonPlayerController::InitializeMapGeneratorBinding()
{
	MapGenerator = FindMapGenerator();

	// 아직 레벨에 WFCGridManager가 로드되지 않았다면 0.1초 뒤 다시 시도
	if (!MapGenerator)
	{
		GetWorldTimerManager().SetTimer(
			TimerHandle_FindMapGenerator,
			this,
			&ADungeonPlayerController::InitializeMapGeneratorBinding,
			0.1f,
			false // 0.1초 뒤 1회 실행
		);
		return;
	}

	// --- WFCGridManager를 정상적으로 찾은 경우 ---

	// 타이머가 작동 중이었다면 정리
	GetWorldTimerManager().ClearTimer(TimerHandle_FindMapGenerator);

	// 이벤트 바인딩
	MapGenerator->OnMapGenProgress.AddUObject(this, &ADungeonPlayerController::HandleProgress);
	MapGenerator->OnMapGenComplete.AddUObject(this, &ADungeonPlayerController::HandleGenComplete);

	// [중요 수정] IsMapGenerated() 대신 실제로 남은 작업(Tick)이 없는지 직접 검사
	if (MapGenerator->IsMapGenerated() && !GetWorldTimerManager().IsTimerActive(TimerHandle_FindMapGenerator))
	{
		// 이전에 생성이 이미 완료된 상태라면 즉시 다음 단계로 전환
		HandleGenComplete();
		return;
	}

	// 아직 생성 중이라면 로딩창 띄우고 UI 대기 시작
	ShowLoadingWidget();
}

AWFCGridManager* ADungeonPlayerController::FindMapGenerator() const
{
	for (TActorIterator<AWFCGridManager> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

APropSpawnManagerActor* ADungeonPlayerController::FindPropSpawner() const
{
	for (TActorIterator<APropSpawnManagerActor> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void ADungeonPlayerController::ShowLoadingWidget()
{
	if (!LoadingWidgetClass) return;

	// LocalPlayer가 아직 없거나 이미 떨어진 상태면 CreateWidget이 실패함 (접속 중/맵 이동·종료 중)
	if (!GetLocalPlayer())
	{
		UE_LOG(LogTemp, Warning, TEXT("[UI] %s: LocalPlayer 없음 - 로딩 위젯 생성 건너뜀"), *GetName());
		return;
	}

	// UI 생성 및 등록
	if (!LoadingWidget)
	{
		LoadingWidget = CreateWidget<ULoadingScreenWidget>(this, LoadingWidgetClass);
	}

	if (LoadingWidget)
	{
		LoadingWidget->AddToViewport(100);
		LoadingWidget->SetProgress(0, 100);

		// 로딩 중에는 UI 전용 입력 모드로 전환 (필요 시)
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(LoadingWidget->GetCachedWidget());
		SetInputMode(InputMode);
		bShowMouseCursor = true;
	}
}

// 진행바 0~90% 구간: 구조(방/빅룸/문) 생성
void ADungeonPlayerController::HandleProgress(int32 Current, int32 Total)
{
	if (LoadingWidget)
	{
		const int32 Percent = (Total > 0) ? FMath::Clamp(Current * 90 / Total, 0, 90) : 0;
		LoadingWidget->SetProgress(Percent, 100);
	}
}

// 구조 생성이 끝나는 시점 - 소품 매니저가 있으면 그쪽 진행률을 이어서 구독하고,
// 없으면(이 레벨엔 소품 배치 기능이 없는 경우) 바로 로딩을 끝낸다.
// PropSpawner는 WFCGridManager::OnMapGenComplete에 스스로 바인딩해서 알아서 PopulateProps()를
// 시작하므로, 여기서 직접 호출하지 않는다 - 순수하게 진행률만 구독해서 UI에 반영한다.
void ADungeonPlayerController::HandleGenComplete()
{
	if (bGenCompleteHandled) return;
	bGenCompleteHandled = true;

	PropSpawner = FindPropSpawner();

	if (PropSpawner)
	{
		PropSpawner->OnPropSpawnProgress.AddUObject(this, &ADungeonPlayerController::HandlePropProgress);
		PropSpawner->OnPropSpawnComplete.AddUObject(this, &ADungeonPlayerController::HandlePropComplete);

		// 구독하기 전에 이미 소품 배치가 끝나 있었으면(완료 신호가 먼저 복제돼서 지나감) 바로 로딩 종료.
		// 이 체크가 없으면 로딩바가 90%에서 영원히 멈춤
		if (PropSpawner->IsPropSpawnComplete())
		{
			FinishLoading();
		}
	}
	else
	{
		FinishLoading();
	}
}

// 진행바 90~100% 구간: 소품(캐비닛/다이스/Exit) 배치
// NOTE: 90/100 분기점은 정확한 비율 계산이 아니라 임의로 정한 값.
void ADungeonPlayerController::HandlePropProgress(int32 Current, int32 Total)
{
	if (LoadingWidget)
	{
		const int32 Percent = 90 + ((Total > 0) ? FMath::Clamp(Current * 10 / Total, 0, 10) : 10);
		LoadingWidget->SetProgress(Percent, 100);
	}
}

void ADungeonPlayerController::HandlePropComplete()
{
	FinishLoading();
}

// 구조+소품 생성이 전부 끝나는 시점 - 실제로 로딩 위젯 제거 + 입력 모드 복귀
void ADungeonPlayerController::FinishLoading()
{
	if (bLoadingFinished) return;
	bLoadingFinished = true;

	// 1. 로딩 UI 제거
	if (LoadingWidget)
	{
		LoadingWidget->RemoveFromParent();
		LoadingWidget = nullptr;
	}

	// 2. 입력 모드를 GameOnly로 복원하고 마우스 커서를 숨김 (연출 중에도 커서가 보이지 않게 여기서 먼저)
	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	bShowMouseCursor = false;

	// 3. [시작 연출] 내 캐릭터 주위를 비추는 연출 -> 끝나면 StartMatchPlay (입력 허용 / HUD / 배경음악)
	TryStartMatchIntro();
}

// =========================== 시작 연출 ===========================

void ADungeonPlayerController::TryStartMatchIntro()
{
	if (bMatchIntroHandled || !bLoadingFinished)
	{
		return;
	}

	APawn* MyPawn = GetPawn();
	if (!MyPawn || Cast<ACabinetActor>(MyPawn))
	{
		// 아직 내 캐릭터가 복제되지 않음 -> AcknowledgePossession에서 다시 시도.
		// 끝내 안 오면(스폰 실패 등) 연출 없이 게임을 시작해서 화면이 멈춰 있지 않게
		if (!GetWorldTimerManager().IsTimerActive(MatchIntroPawnWaitTimer))
		{
			GetWorldTimerManager().SetTimer(MatchIntroPawnWaitTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (!bMatchIntroHandled)
				{
					UE_LOG(LogTemp, Warning, TEXT("[Intro] No pawn after %.0fs - starting without intro"), MatchIntroPawnWaitSeconds);
					bMatchIntroHandled = true;
					StartMatchPlay();
				}
			}), MatchIntroPawnWaitSeconds, false);
		}
		return;
	}

	bMatchIntroHandled = true;
	GetWorldTimerManager().ClearTimer(MatchIntroPawnWaitTimer);

	UMatchIntroComponent* Intro = MyPawn->FindComponentByClass<UMatchIntroComponent>();
	if (!Intro || !Intro->IsIntroEnabled())
	{
		StartMatchPlay(); // 연출이 없는 캐릭터 -> 바로 시작
		return;
	}

	// 연출 중에는 조작 불가 (이동/시점 + 상호작용 등 캐릭터 입력 전부)
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
	MyPawn->DisableInput(this);
	MatchIntroPawn = MyPawn;

	Intro->Play(FSimpleDelegate::CreateUObject(this, &ADungeonPlayerController::HandleMatchIntroFinished));
}

void ADungeonPlayerController::HandleMatchIntroFinished()
{
	if (APawn* IntroPawn = MatchIntroPawn.Get())
	{
		IntroPawn->EnableInput(this);
	}
	MatchIntroPawn.Reset();

	StartMatchPlay();
}

void ADungeonPlayerController::StartMatchPlay()
{
	if (bMatchPlayStarted)
	{
		return;
	}
	bMatchPlayStarted = true;

	// 1. 캐릭터 입력 활성화 (연출 중 막아둔 것 / SetIgnoreMoveInput이 걸려있던 경우 대비)
	SetIgnoreMoveInput(false);
	SetIgnoreLookInput(false);

	// 2. 매치 HUD(러너/주사위/탈출구) 표시 시작
	ShowMatchWidget();

	// 3. 배경음악 시작
	StartMatchBGM();
}

// =========================== 로딩 카메라 ===========================

AActor* ADungeonPlayerController::FindLoadingCamera()
{
	if (!CachedLoadingCamera.IsValid() && !LoadingCameraTag.IsNone())
	{
		TArray<AActor*> Cameras;
		UGameplayStatics::GetAllActorsWithTag(GetWorld(), LoadingCameraTag, Cameras);
		if (Cameras.Num() > 0)
		{
			CachedLoadingCamera = Cameras[0];
		}
	}
	return CachedLoadingCamera.Get();
}

void ADungeonPlayerController::AutoManageActiveCameraTarget(AActor* SuggestedTarget)
{
	// 로딩 중(아직 러너/술래 폰을 Possess하기 전): 폰 없는 컨트롤러는 엔진이 자동으로 관전 상태로 만들고
	// SpectatorPawn을 스폰해서 시점을 그쪽으로 옮기는데, 그걸 막고 LoadingCamera를 유지한다
	if (bAutoManageActiveCameraTarget && !GetPawn())
	{
		if (AActor* LoadingCamera = FindLoadingCamera())
		{
			if (GetViewTarget() != LoadingCamera)
			{
				SetViewTarget(LoadingCamera);
			}
			return;
		}
	}

	Super::AutoManageActiveCameraTarget(SuggestedTarget);

	if (Cast<ACabinetActor>(GetPawn()))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CabinetDiag] AutoManageActiveCameraTarget %s: Suggested=%s, ViewTarget=%s, bAutoManage=%d"),
			*GetName(), *GetNameSafe(SuggestedTarget), *GetNameSafe(GetViewTarget()), bAutoManageActiveCameraTarget ? 1 : 0);
		if (SuggestedTarget == this)
		{
			// [진단] 캐비넷 조종 중인데 시점이 컨트롤러로 되돌아감 -> 누가 불렀는지 콜스택 출력
			FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		}
	}
}

void ADungeonPlayerController::AcknowledgePossession(APawn* P)
{
	Super::AcknowledgePossession(P);

	UE_LOG(LogTemp, Warning, TEXT("[CabinetDiag] AcknowledgePossession %s: Pawn=%s, NetMode=%d"),
		*GetName(), *GetNameSafe(P), (int32)GetNetMode());

	if (Cast<ACabinetActor>(P))
	{
		return;
	}

	// 러너/술래 폰에 빙의된 순간 내 외형 저장 (결과 방 연출용)
	CacheResultCharacter(P);

	// [시작 연출] 로딩이 먼저 끝나서 캐릭터를 기다리던 중이면 지금 시작
	if (bLoadingFinished && !bMatchIntroHandled)
	{
		TryStartMatchIntro();
	}

	for (TActorIterator<ACabinetActor> It(GetWorld()); It; ++It)
	{
		It->ReleaseLocalInputFor(this);
	}
}

void ADungeonPlayerController::PawnLeavingGame()
{
	// 숨어있던 도망자: 캐비넷 대신 도망자 캐릭터가 지워지도록 조종권부터 되돌림
	if (ACabinetActor* Cabinet = Cast<ACabinetActor>(GetPawn()))
	{
		Cabinet->ReleaseOccupantForLeavingPlayer(this);
	}

	// 엔진 기본: 지금 조종 중인 폰(= 도망자 캐릭터)을 Destroy
	Super::PawnLeavingGame();
}

// =========================== 배경음악 ===========================

void ADungeonPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsLocalController() && BGMAudio)
	{
		UpdateBGMForTerrorRadius();
	}
}

void ADungeonPlayerController::StartMatchBGM()
{
	if (!IsLocalController() || !MatchBGM || BGMAudio)
	{
		return;
	}

	// 일시정지/재개를 해야 하므로 자동 파괴되지 않는 컴포넌트로 생성해서 보관
	BGMAudio = UGameplayStatics::CreateSound2D(this, MatchBGM, 1.f, 1.f, 0.f, nullptr,
		/*bPersistAcrossLevelTransition*/ false, /*bAutoDestroy*/ false);
	if (BGMAudio)
	{
		BGMAudio->FadeIn(BGMFadeTime);
	}
}

void ADungeonPlayerController::UpdateBGMForTerrorRadius()
{
	// 술래는 한 명이라 한 번 찾아두고 재사용 (없거나 파괴됐을 때만 다시 검색)
	if (!CachedChaser.IsValid())
	{
		for (TActorIterator<AChaserCharacter> It(GetWorld()); It; ++It)
		{
			CachedChaser = *It;
			break;
		}
	}

	AChaserCharacter* Chaser = CachedChaser.Get();

	// 술래가 없거나, 내가 술래면(아우라를 안 들음) 항상 배경음악
	if (!Chaser || Chaser->IsLocallyControlled())
	{
		SetBGMSuppressed(false);
		return;
	}

	const float Radius = Chaser->GetTerrorRadius();
	if (Radius <= 0.f)
	{
		SetBGMSuppressed(false);
		return;
	}

	// 실제로 소리를 듣는 위치(관전 중이면 관전 대상 카메라, 캐비닛 안이면 캐비닛 카메라) 기준
	FVector ListenerLoc, FrontDir, RightDir;
	GetAudioListenerPosition(ListenerLoc, FrontDir, RightDir);
	const float Dist = FVector::Dist(ListenerLoc, Chaser->GetActorLocation());

	if (!bBGMSuppressedByTerror && Dist <= Radius)
	{
		SetBGMSuppressed(true);
	}
	else if (bBGMSuppressedByTerror && Dist > Radius + TerrorRadiusExitMargin)
	{
		SetBGMSuppressed(false);
	}
}

void ADungeonPlayerController::SetBGMSuppressed(bool bSuppress)
{
	if (!BGMAudio || bBGMSuppressedByTerror == bSuppress)
	{
		return;
	}
	bBGMSuppressedByTerror = bSuppress;

	if (bSuppress)
	{
		// 볼륨을 거의 0까지 줄인 뒤 일시정지 -> 재생 위치 유지
		// (FadeOut은 Stop이라 위치가 초기화되므로 쓰지 않음)
		BGMAudio->AdjustVolume(BGMFadeTime, 0.001f);
		GetWorldTimerManager().SetTimer(BGMPauseTimer, this, &ADungeonPlayerController::PauseBGMAfterFade, FMath::Max(BGMFadeTime, 0.01f), false);
	}
	else
	{
		// 페이드아웃 도중에 다시 나온 경우도 여기서 타이머만 취소하면 그대로 볼륨 복구
		GetWorldTimerManager().ClearTimer(BGMPauseTimer);
		BGMAudio->SetPaused(false);
		BGMAudio->AdjustVolume(BGMFadeTime, 1.f);
	}
}

void ADungeonPlayerController::PauseBGMAfterFade()
{
	if (BGMAudio && bBGMSuppressedByTerror)
	{
		BGMAudio->SetPaused(true);
	}
}

void ADungeonPlayerController::ShowMatchWidget()
{
	if (!MatchWidgetClass) return;

	// 시작 연출 도중 캐릭터가 사라지거나 맵 이동·종료 중이면 LocalPlayer가 이미 없을 수 있음
	if (!GetLocalPlayer())
	{
		UE_LOG(LogTemp, Warning, TEXT("[UI] %s: LocalPlayer 없음 - 매치 위젯 생성 건너뜀"), *GetName());
		return;
	}

	if (!MatchWidget)
	{
		MatchWidget = CreateWidget<UMatchWidget>(this, MatchWidgetClass);
	}

	if (MatchWidget && !MatchWidget->IsInViewport())
	{
		// 로딩 위젯(ZOrder 100)보다 낮은 값으로 깔아둠 - 상시 표시되는 인게임 HUD라
		// 입력 모드는 건드리지 않는다(로딩 위젯 쪽에서만 UI 전용 입력 모드를 잡았다 돌려놓음)
		MatchWidget->AddToViewport(0);
	}
}

// =====================================================================
// 탈출 후 페이드 + 관전
// =====================================================================

void ADungeonPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// 프로젝트 기본 입력 컴포넌트가 EnhancedInputComponent여야 함 (UE5 기본값)
	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (SpectateNextAction)
		{
			EIC->BindAction(SpectateNextAction, ETriggerEvent::Started, this, &ADungeonPlayerController::OnSpectateNextInput);
		}
	}
}

void ADungeonPlayerController::ClientBeginEscape_Implementation()
{
	// 관전 입력 컨텍스트를 폰 쪽 컨텍스트보다 높은 우선순위로 추가 - Space가 점프 대신 관전 전환으로 먹히게
	if (SpectateMappingContext)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(SpectateMappingContext, SpectateMappingPriority);
		}
	}

	UEscapeFadeWidget* Widget = EnsureEscapeFadeWidget();
	if (!Widget)
	{
		// 위젯 클래스가 안 지정돼 있으면 연출 없이 바로 관전 (로비 버튼도 없으니 반드시 지정할 것)
		UE_LOG(LogTemp, Error, TEXT("[Escape] EscapeFadeWidgetClass가 비어있음 - 컨트롤러 BP Class Defaults에서 지정 필요"));
		HandleEscapeFadeOutFinished();
		return;
	}

	Widget->OnFadeOutFinished.RemoveAll(this);
	Widget->OnFadeOutFinished.AddUObject(this, &ADungeonPlayerController::HandleEscapeFadeOutFinished);
	Widget->StartEscapeFade();
}

UEscapeFadeWidget* ADungeonPlayerController::EnsureEscapeFadeWidget()
{
	if (!EscapeFadeWidgetClass)
	{
		return nullptr;
	}

	if (!EscapeFadeWidget && !GetLocalPlayer())
	{
		UE_LOG(LogTemp, Warning, TEXT("[UI] %s: LocalPlayer 없음 - 탈출 위젯 생성 건너뜀"), *GetName());
		return nullptr;
	}

	if (!EscapeFadeWidget)
	{
		EscapeFadeWidget = CreateWidget<UEscapeFadeWidget>(this, EscapeFadeWidgetClass);
	}

	// MatchWidget(0) 위, 로딩 위젯(100) 아래
	if (EscapeFadeWidget && !EscapeFadeWidget->IsInViewport())
	{
		EscapeFadeWidget->AddToViewport(50);
	}
	return EscapeFadeWidget;
}

void ADungeonPlayerController::ClientShowMatchEnd_Implementation(int32 EscapedCount, int32 CaughtCount, int32 TotalRunners, bool bIsChaser, bool bWon)
{
	// [리슨] 엔딩을 받았음 -> 이후 술래(호스트)가 먼저 나가서 끊겨도 "술래 이탈 러너 승리"가 아니라 조용히 로비로
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEscapeRoomSessionSubsystem* Session = GI->GetSubsystem<UEscapeRoomSessionSubsystem>())
		{
			Session->MarkMatchResultReceived();
		}
	}

	// 관전 전환(Space) 중지 + 이동/시점 입력 차단 (화면은 곧 완전히 검어짐)
	bLocalSpectateInputEnabled = false;
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);

	// 진행 중이던 홀드 게이지 등 HUD는 가려지므로 따로 정리하지 않음
	// 통계(탈출/잡힘 수)는 술래 화면에만 표시, 러너 엔딩 UI는 빈칸 (게임 맵 엔딩/로비 엔딩 공통)
	const FText Result = bIsChaser
		? FText::Format(MatchResultFormat, FText::AsNumber(EscapedCount), FText::AsNumber(CaughtCount), FText::AsNumber(TotalRunners))
		: FText::GetEmpty();

	// ---- 내 승패 -> 결과 방 선택 ----
	// 승패는 서버(AescaperoomGameMode::HandleAllRunnersResolved)가 판정해서 보내준 값 사용
	// 러너: 탈출했으면 승리. 술래: 탈출 못 한 러너가 한 명이라도 있으면 승리 (전원 탈출이면 패배)
	EMatchResultRoomType RoomType;
	if (bIsChaser)
	{
		RoomType = bWon ? EMatchResultRoomType::ChaserWin : EMatchResultRoomType::ChaserLose;
	}
	else
	{
		RoomType = bWon ? EMatchResultRoomType::RunnerWin : EMatchResultRoomType::RunnerLose;
	}
	UE_LOG(LogTemp, Log, TEXT("[MatchEnd] Client result: Chaser=%d Won=%d -> RoomType=%d"), bIsChaser ? 1 : 0, bWon ? 1 : 0, (int32)RoomType);

	// [테스트용] 러너 승리 / 술래 패배 연출 강제
	if (bDebugForceRunnerWinResult)
	{
		const AMatchPlayerState* DebugPS = GetPlayerState<AMatchPlayerState>();
		RoomType = (DebugPS && DebugPS->IsChaser()) ? EMatchResultRoomType::ChaserLose : EMatchResultRoomType::RunnerWin;
		UE_LOG(LogTemp, Warning, TEXT("[MatchEnd][Debug] bDebugForceRunnerWinResult ON -> RoomType=%d"), (int32)RoomType);
	}

	// [로비 엔딩] 결과를 GameInstance에 저장 - 이후 술래(호스트)가 먼저 나가서 연결이 끊기든, 내가 직접 로비로
	// 이동하든 로비 맵에서 이 결과로 엔딩을 재생함 (내 캐릭터 외형은 빙의 시점에 이미 저장돼 있음)
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEscapeRoomSessionSubsystem* Session = GI->GetSubsystem<UEscapeRoomSessionSubsystem>())
		{
			Session->SetPendingMatchResult(RoomType, Result);
		}
	}

	ShowLocalMatchEnd();
}

void ADungeonPlayerController::ShowLocalMatchEnd()
{
	// [로비 엔딩] 여기서는 암전만 하고, 완전히 검어지면(HandleMatchEndBlack) 로비로 이동.
	// 결과 방 연출은 로비 맵에서 AStartScreenController가 저장된 결과로 재생함 -> 술래 PC(리슨서버)가 먼저 꺼져도 엔딩이 안 끊김
	PendingLobbyTravelAddress = ResolveLobbyAddress();
	bTravelToLobbyOnMatchEndBlack = !PendingLobbyTravelAddress.IsEmpty();
	if (!bTravelToLobbyOnMatchEndBlack)
	{
		// 로비 주소가 없음(PIE로 GameLV를 바로 실행 등) - 돌아갈 곳이 없으니 암전만 하고 끝
		UE_LOG(LogTemp, Warning, TEXT("[MatchEnd] 로비 주소가 없어서 암전만 함 (엔딩은 로비에서만 재생)"));
	}

	if (UEscapeFadeWidget* Widget = EnsureEscapeFadeWidget())
	{
		Widget->OnMatchEndBlack.RemoveAll(this);
		Widget->OnMatchEndBlack.AddUObject(this, &ADungeonPlayerController::HandleMatchEndBlack);
		// 암전만 (문구/버튼 없음). 완전히 검어진 순간 OnMatchEndBlack -> 로비 이동, 이후로도 검은 화면 유지
		// (서버 주소로 이동할 땐 접속이 끝날 때까지 GameLV가 계속 돌아가므로, 다시 열리면 안 됨)
		Widget->StartFadeToBlackOnly();
	}
	else
	{
		TravelToLobbyForResult(); // 위젯이 없으면 연출 없이 바로 이동 (주소가 없으면 아무것도 안 함)
	}
}

void ADungeonPlayerController::HandleMatchEndBlack()
{
	// [로비 엔딩] 화면이 완전히 검은 지금 로비로 이동 (결과 방은 로비에서)
	if (bTravelToLobbyOnMatchEndBlack)
	{
		TravelToLobbyForResult();
	}
}

void ADungeonPlayerController::CacheResultCharacter(APawn* P)
{
	if (!P)
	{
		return;
	}

	USkeletalMeshComponent* MeshComp = nullptr;
	if (const ACharacter* Char = Cast<ACharacter>(P))
	{
		MeshComp = Char->GetMesh();
	}
	if (!MeshComp)
	{
		MeshComp = P->FindComponentByClass<USkeletalMeshComponent>();
	}
	if (!MeshComp || !MeshComp->GetSkeletalMeshAsset())
	{
		return;
	}

	USkeletalMesh* Mesh = MeshComp->GetSkeletalMeshAsset();
	TArray<TObjectPtr<UMaterialInterface>> Materials;
	for (int32 i = 0; i < MeshComp->GetNumMaterials(); ++i)
	{
		Materials.Add(MeshComp->GetMaterial(i));
	}
	UE_LOG(LogTemp, Log, TEXT("[MatchEnd] Cached result character: %s (%d materials)"),
		*GetNameSafe(Mesh), Materials.Num());

	// 로비로 옮겨가면 이 컨트롤러는 사라지므로 GameInstance 쪽에 저장 (로비 결과 방에서 꺼내 씀)
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEscapeRoomSessionSubsystem* Session = GI->GetSubsystem<UEscapeRoomSessionSubsystem>())
		{
			Session->SetResultCharacter(Mesh, Materials);
		}
	}
}

void ADungeonPlayerController::StopSpectating()
{
	bIsSpectating = false;
	GetWorldTimerManager().ClearTimer(SpectateValidateTimer);
}

void ADungeonPlayerController::ReturnToLobby()
{
	if (!IsLocalController())
	{
		return;
	}

	// [탈출 후 Exit Game] 탈출한 러너는 이 시점에 이미 승리가 확정이므로, 시작 화면으로 바로 가지 않고
	// 판이 끝났을 때(ClientShowMatchEnd)와 같은 흐름을 탐: 결과(러너 승리) 저장 -> 암전 -> 로비 이동 ->
	// 로비에서 결과 방 엔딩 -> 엔딩의 버튼으로 시작 화면. 판 자체는 남은 사람들끼리 계속 진행됨
	// (탈출한 러너는 Logout에서 남은 러너 수에 반영되지 않음)
	const AMatchPlayerState* MyMPS = GetPlayerState<AMatchPlayerState>();
	if (MyMPS && MyMPS->HasEscaped())
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UEscapeRoomSessionSubsystem* Session = GI->GetSubsystem<UEscapeRoomSessionSubsystem>())
			{
				// 러너 엔딩 UI는 통계 빈칸 규칙 그대로 (ClientShowMatchEnd와 동일)
				Session->SetPendingMatchResult(EMatchResultRoomType::RunnerWin, FText::GetEmpty());
			}
		}

		// 관전 전환/입력 정리 후 암전 -> 완전히 검어지면 TravelToLobbyForResult (EndMatch도 거기서 처리)
		bLocalSpectateInputEnabled = false;
		SetIgnoreMoveInput(true);
		SetIgnoreLookInput(true);
		UE_LOG(LogTemp, Log, TEXT("[Lobby] Escaped runner leaving -> lobby ending (RunnerWin)"));
		ShowLocalMatchEnd();
		return;
	}

	const FString LobbyAddress = ResolveLobbyAddress();
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEscapeRoomSessionSubsystem* Session = GI->GetSubsystem<UEscapeRoomSessionSubsystem>())
		{
			// 스스로 나가는 것이므로 이후 연결 끊김은 "술래 이탈"로 보지 않음
			Session->EndMatch();
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Lobby] Returning to lobby: %s"), *LobbyAddress);

	// 로비 맵의 StartScreenController가 BeginPlay에서 시작 화면을 띄우므로 UI 정리는 따로 안 해도 됨.
	// [리슨] 러너: 술래 PC에서 Logout 처리됨.
	//        술래(호스트): 내가 나가면 리슨서버가 닫혀서 아직 엔딩을 보던 러너도 로비로 돌아감 (엔딩을 이미 받았으니 조용히 복귀)
	RestoreLocallyHiddenSpectateMesh();
	ClientTravel(LobbyAddress, TRAVEL_Absolute);
}

FString ADungeonPlayerController::ResolveLobbyAddress() const
{
	// 매치로 넘어오기 직전에 AStartScreenController가 GameInstance 메모장에 적어둔 로비 주소를 우선 사용.
	// 메모가 없으면(로비를 거치지 않고 PIE로 바로 매치 맵을 실행한 경우 등) 설정 파일 -> BP 예비값 순
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UEscapeRoomSessionSubsystem* Session = GI->GetSubsystem<UEscapeRoomSessionSubsystem>())
		{
			if (Session->HasLobbyAddress())
			{
				return Session->GetLobbyAddress();
			}
		}
	}

	FString LobbyAddress = LobbyServerAddress;
	if (LobbyAddress.IsEmpty())
	{
		LobbyAddress = UEscapeRoomSessionSubsystem::GetConfiguredLobbyAddress(); // [AWS] 설정 파일의 로비 주소
	}
	return LobbyAddress;
}

void ADungeonPlayerController::TravelToLobbyForResult()
{
	if (!IsLocalController() || PendingLobbyTravelAddress.IsEmpty())
	{
		return;
	}
	bTravelToLobbyOnMatchEndBlack = false; // 중복 이동 방지

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEscapeRoomSessionSubsystem* Session = GI->GetSubsystem<UEscapeRoomSessionSubsystem>())
		{
			// 스스로 나가는 것이므로 이후 연결 끊김은 신경 안 씀 (결과는 ClientShowMatchEnd에서 이미 저장됨)
			Session->EndMatch();

			// 로비 맵이 로드된 직후 ~ 엔딩 위젯이 뜨기 전까지 로비 화면이 비치지 않게 검은 막 (로비 컨트롤러가 걷음)
			Session->ShowTravelBlackout();
		}
	}

	GetWorldTimerManager().ClearTimer(LocalSpectateVisualTimer);
	RestoreLocallyHiddenSpectateMesh();

	UE_LOG(LogTemp, Log, TEXT("[MatchEnd] Traveling to lobby for result ending: %s"), *PendingLobbyTravelAddress);
	ClientTravel(PendingLobbyTravelAddress, TRAVEL_Absolute);
}

void ADungeonPlayerController::HandleEscapeFadeOutFinished()
{
	// 화면이 완전히 검은 지금 첫 관전 대상으로 전환 요청 (검은 화면 뒤에서 시점이 바뀌어서 튀는 게 안 보임)
	bLocalSpectateInputEnabled = true;
	ServerSpectateNext();

	// 관전 대상 메시 숨김 처리는 내 화면 전용이라 클라 타이머로 폴링
	GetWorldTimerManager().SetTimer(LocalSpectateVisualTimer, this, &ADungeonPlayerController::UpdateLocalSpectateVisuals, 0.1f, true);
}

void ADungeonPlayerController::OnSpectateNextInput(const FInputActionValue& Value)
{
	if (!bLocalSpectateInputEnabled)
	{
		return;
	}
	ServerSpectateNext();
}

void ADungeonPlayerController::ServerSpectateNext_Implementation()
{
	// 탈출한 플레이어만 관전 가능 (클라가 임의로 보내는 요청 방어)
	if (!IsEscapedPlayer())
	{
		return;
	}

	bIsSpectating = true;
	SpectatedPlayerState = FindNextSpectateTarget(SpectatedPlayerState.Get());
	ApplySpectateViewTarget();

	if (!GetWorldTimerManager().IsTimerActive(SpectateValidateTimer))
	{
		GetWorldTimerManager().SetTimer(SpectateValidateTimer, this, &ADungeonPlayerController::ValidateSpectateTarget, 0.5f, true);
	}
}

bool ADungeonPlayerController::IsEscapedPlayer() const
{
	const AMatchPlayerState* MPS = GetPlayerState<AMatchPlayerState>();
	return MPS && MPS->HasEscaped();
}

bool ADungeonPlayerController::IsValidSpectateTarget(const APlayerState* PS) const
{
	if (!PS || PS == PlayerState.Get())
	{
		return false;
	}

	if (const AMatchPlayerState* MPS = Cast<AMatchPlayerState>(PS))
	{
		// 이미 탈출한 사람(폰이 숨겨져 있음)과 역할 미정(접속 직후)은 제외
		if (MPS->HasEscaped() || MPS->GetMatchRole() == EMatchPlayerRole::Unassigned)
		{
			return false;
		}
	}

	return IsValid(PS->GetPawn());
}

APlayerState* ADungeonPlayerController::FindNextSpectateTarget(const APlayerState* Current) const
{
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!GS || GS->PlayerArray.Num() == 0)
	{
		return nullptr;
	}

	const TArray<TObjectPtr<APlayerState>>& Players = GS->PlayerArray;
	const int32 Num = Players.Num();
	int32 StartIndex = INDEX_NONE;
	for (int32 i = 0; Current && i < Num; ++i)
	{
		if (Players[i].Get() == Current)
		{
			StartIndex = i;
			break;
		}
	}

	// 현재 대상 다음 칸부터 한 바퀴 돌면서 유효한 첫 대상 (현재 대상 자신은 마지막에 다시 후보가 됨)
	for (int32 Step = 1; Step <= Num; ++Step)
	{
		const int32 Index = (StartIndex == INDEX_NONE) ? (Step - 1) : (StartIndex + Step) % Num;
		APlayerState* Candidate = Players[Index].Get();
		if (IsValidSpectateTarget(Candidate))
		{
			return Candidate;
		}
	}
	return nullptr;
}

void ADungeonPlayerController::ApplySpectateViewTarget()
{
	const APlayerState* PS = SpectatedPlayerState.Get();
	APawn* DesiredPawn = PS ? PS->GetPawn() : nullptr;
	if (!DesiredPawn)
	{
		return; // 볼 대상이 하나도 없으면 지금 시점 유지
	}

	if (GetViewTarget() != DesiredPawn)
	{
		// 서버에서 호출하면 PlayerCameraManager가 원격 클라에 ClientSetViewTarget으로 알려줌.
		// 폰에 CameraComponent가 있으면 그 카메라(=그 플레이어의 1인칭 시점)를 그대로 씀
		SetViewTargetWithBlend(DesiredPawn, SpectateBlendTime);
	}
}

void ADungeonPlayerController::ValidateSpectateTarget()
{
	if (!bIsSpectating)
	{
		GetWorldTimerManager().ClearTimer(SpectateValidateTimer);
		return;
	}

	// 보던 사람이 탈출했거나 나갔으면 다음 사람으로
	if (!IsValidSpectateTarget(SpectatedPlayerState.Get()))
	{
		SpectatedPlayerState = FindNextSpectateTarget(SpectatedPlayerState.Get());
	}

	// 같은 사람이어도 조종 폰이 바뀌었을 수 있음(캐비닛에 숨으면 캐비닛 폰) - 매번 맞춰줌
	ApplySpectateViewTarget();
}

void ADungeonPlayerController::UpdateLocalSpectateVisuals()
{
	ACharacter* ViewCharacter = Cast<ACharacter>(GetViewTarget());
	if (ViewCharacter == GetPawn())
	{
		ViewCharacter = nullptr; // 아직 서버가 시점을 안 옮겨줬으면 아무것도 안 함
	}

	// 관전 대상의 카메라가 자기 캡슐 안쪽(1인칭)에 있으면 몸통 메시가 화면을 가리므로 숨김.
	// 캡슐 밖(3인칭/부감)이면 숨기지 않음. SetRenderInMainPass(false)라 그림자는 그대로 남음 (내 화면에서만 적용)
	bool bShouldHide = false;
	if (bHideSpectatedMeshInFirstPerson)
	{
		if (const AescaperoomCharacter* EC = Cast<AescaperoomCharacter>(ViewCharacter))
		{
			const UCameraComponent* Cam = EC->GetFirstPersonCameraComponent();
			const UCapsuleComponent* Capsule = EC->GetCapsuleComponent();
			if (Cam && Capsule)
			{
				const FVector Offset = Cam->GetComponentLocation() - Capsule->GetComponentLocation();
				const bool bInsideRadius = Offset.Size2D() <= Capsule->GetScaledCapsuleRadius() + HideSpectatedMeshCapsuleMargin;
				const bool bInsideHeight = FMath::Abs(Offset.Z) <= Capsule->GetScaledCapsuleHalfHeight() + HideSpectatedMeshCapsuleMargin;
				bShouldHide = bInsideRadius && bInsideHeight;
			}
		}
	}

	ACharacter* CurrentlyHidden = LocallyHiddenSpectateCharacter.Get();
	if (CurrentlyHidden && (CurrentlyHidden != ViewCharacter || !bShouldHide))
	{
		RestoreLocallyHiddenSpectateMesh();
	}

	if (ViewCharacter && bShouldHide && LocallyHiddenSpectateCharacter.Get() != ViewCharacter)
	{
		if (USkeletalMeshComponent* Mesh = ViewCharacter->GetMesh())
		{
			Mesh->SetRenderInMainPass(false);
			LocallyHiddenSpectateCharacter = ViewCharacter;
		}
	}
}

void ADungeonPlayerController::RestoreLocallyHiddenSpectateMesh()
{
	if (ACharacter* Hidden = LocallyHiddenSpectateCharacter.Get())
	{
		if (USkeletalMeshComponent* Mesh = Hidden->GetMesh())
		{
			Mesh->SetRenderInMainPass(true);
		}
	}
	LocallyHiddenSpectateCharacter = nullptr;
}
