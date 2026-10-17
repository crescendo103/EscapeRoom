#include "DungeonPlayerController.h"
#include "WFCGridManager.h"
#include "PropSpawnManagerActor.h"
#include "LoadingScreenWidget.h"
#include "MatchWidget.h"
#include "EngineUtils.h" // TActorIterator
#include "EscapeFadeWidget.h"
#include "MatchPlayerState.h"
#include "escaperoomCharacter.h"
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

	// [리슨] 술래가 매치 도중 나가서 GameLV를 혼자 모드 결과 모드(?HostLeftResult)로 다시 연 경우:
	// 맵 생성/로딩 위젯 없이 바로 러너 승리 엔딩 (WFCGridManager도 이 모드에선 생성을 안 함)
	if (UEscapeRoomSessionSubsystem::IsHostLeftResultWorld(GetWorld()))
	{
		ShowHostLeftResult();
		return;
	}

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

	// 2. 입력 모드를 GameOnly로 복원하고 마우스 커서를 숨김
	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	bShowMouseCursor = false;

	// 3. 캐릭터 입력 활성화 (SetIgnoreMoveInput이 걸려있던 경우 대비)
	SetIgnoreMoveInput(false);
	SetIgnoreLookInput(false);

	// 4. 로딩이 끝난 시점에 매치 HUD(러너/주사위/탈출구) 표시 시작
	ShowMatchWidget();

	// 5. 배경음악 시작
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
	// 결과 방 연출 중에는 엔진이 시점을 폰/관전 쪽으로 옮기지 못하게 고정
	if (bShowingResultRoom && ActiveResultRoom.IsValid())
	{
		if (GetViewTarget() != ActiveResultRoom.Get())
		{
			SetViewTarget(ActiveResultRoom.Get());
		}
		return;
	}

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
}

void ADungeonPlayerController::AcknowledgePossession(APawn* P)
{
	Super::AcknowledgePossession(P);

	if (Cast<ACabinetActor>(P))
	{
		return;
	}

	// 러너/술래 폰에 빙의된 순간 내 외형 저장 (결과 방 연출용)
	CacheResultCharacter(P);

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

	// 결과 방 연출 중 서버 관전 전환(ClientSetViewTarget) 등이 늦게 도착해도 결과 방 시점 유지
	if (bShowingResultRoom && ActiveResultRoom.IsValid() && IsLocalController()
		&& GetViewTarget() != ActiveResultRoom.Get())
	{
		SetViewTarget(ActiveResultRoom.Get());
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
	const FText Result = FText::Format(MatchResultFormat, FText::AsNumber(EscapedCount), FText::AsNumber(CaughtCount), FText::AsNumber(TotalRunners));

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

	ShowLocalMatchEnd(Result, RoomType);
}

void ADungeonPlayerController::ShowLocalMatchEnd(const FText& ResultMessage, EMatchResultRoomType RoomType)
{
	// 정상 엔딩(ClientShowMatchEnd)과 술래 이탈 결과 모드(ShowHostLeftResult)가 같이 쓰는 엔딩 연출:
	// 결과 방 선택 -> EscapeFadeWidget 암전 -> 완전히 검어지면 HandleMatchEndBlack에서 결과 방으로 시점 전환
	const FText& Result = ResultMessage;

	AResultRoomActor* Room = AResultRoomActor::FindRoom(this, RoomType);
	ActiveResultRoom = Room;
	if (!Room)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MatchEnd] 결과 방(RoomType=%d)이 레벨에 없음 - 검은 화면 결과로 대체"), (int32)RoomType);
	}

	if (UEscapeFadeWidget* Widget = EnsureEscapeFadeWidget())
	{
		Widget->OnMatchEndBlack.RemoveAll(this);
		Widget->OnMatchEndBlack.AddUObject(this, &ADungeonPlayerController::HandleMatchEndBlack);
		Widget->StartMatchEndFade(Result,
			Room ? Room->ResultTitle : FText::GetEmpty(),
			Room ? Room->ResultTitleColor : FLinearColor::White,
			Room != nullptr);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[MatchEnd] EscapeFadeWidgetClass가 비어있어 게임 종료 화면을 띄울 수 없음"));
	}
}

void ADungeonPlayerController::ShowHostLeftResult()
{
	// 매치 중에 저장해둔 내 캐릭터 외형 (이 컨트롤러는 새로 만들어진 거라 CachedResultMesh가 비어있음)
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UEscapeRoomSessionSubsystem* Session = GI->GetSubsystem<UEscapeRoomSessionSubsystem>())
		{
			CachedResultMesh = Session->GetResultMesh();
			CachedResultMaterials = Session->GetResultMaterials();
		}
	}

	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);

	UE_LOG(LogTemp, Log, TEXT("[MatchEnd] Host-left result mode: showing RunnerWin (mesh=%s)"), *GetNameSafe(CachedResultMesh));
	ShowLocalMatchEnd(HostLeftResultMessage, EMatchResultRoomType::RunnerWin);
}

void ADungeonPlayerController::HandleMatchEndBlack()
{
	AResultRoomActor* Room = ActiveResultRoom.Get();
	if (!Room)
	{
		return;
	}

	// 관전 관련 로컬 처리 정리 (관전 대상 메시 숨김 복구, 폴링 타이머 정지)
	GetWorldTimerManager().ClearTimer(LocalSpectateVisualTimer);
	RestoreLocallyHiddenSpectateMesh();
	bLocalSpectateInputEnabled = false;

	// 결과 방에 매치 HUD가 겹치지 않게 숨김
	if (MatchWidget)
	{
		MatchWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 내가 플레이한 캐릭터 외형으로 교체 + 루프 애니 재생 -> 화면이 검은 지금 시점 전환 (컷이 안 보임)
	Room->ApplyCharacter(CachedResultMesh, CachedResultMaterials);
	bShowingResultRoom = true;
	SetViewTarget(Room);
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

	CachedResultMesh = MeshComp->GetSkeletalMeshAsset();
	CachedResultMaterials.Reset();
	for (int32 i = 0; i < MeshComp->GetNumMaterials(); ++i)
	{
		CachedResultMaterials.Add(MeshComp->GetMaterial(i));
	}
	UE_LOG(LogTemp, Log, TEXT("[MatchEnd] Cached result character: %s (%d materials)"),
		*GetNameSafe(CachedResultMesh), CachedResultMaterials.Num());

	// [리슨] 술래 이탈로 튕기면 이 컨트롤러가 사라지므로 GameInstance 쪽에도 저장 (결과 모드에서 꺼내 씀)
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEscapeRoomSessionSubsystem* Session = GI->GetSubsystem<UEscapeRoomSessionSubsystem>())
		{
			Session->SetResultCharacter(CachedResultMesh, CachedResultMaterials);
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

	// 매치로 넘어오기 직전에 AStartScreenController::Client_ConnectToMatch가 GameInstance 메모장에 적어둔
	// 로비 주소를 우선 사용. 메모가 없으면(로비를 거치지 않고 PIE로 바로 매치 맵을 실행한 경우 등)
	// 컨트롤러 BP의 LobbyServerAddress(예비값)를 씀
	FString LobbyAddress = LobbyServerAddress;
	if (LobbyAddress.IsEmpty())
	{
		LobbyAddress = UEscapeRoomSessionSubsystem::GetConfiguredLobbyAddress(); // [AWS] 설정 파일의 로비 주소
	}
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEscapeRoomSessionSubsystem* Session = GI->GetSubsystem<UEscapeRoomSessionSubsystem>())
		{
			if (Session->HasLobbyAddress())
			{
				LobbyAddress = Session->GetLobbyAddress();
			}
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
