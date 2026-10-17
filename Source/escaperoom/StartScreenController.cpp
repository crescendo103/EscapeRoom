// Fill out your copyright notice in the Description page of Project Settings.

#include "StartScreenController.h"
#include "StartScreenWidget.h"
#include "CreateJoinRoomWidget.h"
#include "RoomWidget.h"
#include "LobbyGameMode.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerState.h"
#include "EscapeRoomSessionSubsystem.h"
#include "Engine/NetConnection.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Components/AudioComponent.h"
#include "EscapeFadeWidget.h"
#include "ResultRoomActor.h"

void AStartScreenController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController()) return; // Skip on non-local (server-side) controller instances

	// 클라: 내 화면(LocalPlayer) 연결이 BeginPlay보다 늦게 올 수 있음 -> 그 경우 ReceivedPlayer()에서 초기화
	if (!GetLocalPlayer())
	{
		return;
	}
	InitLocalLobby();
}

void AStartScreenController::ReceivedPlayer()
{
	Super::ReceivedPlayer();

	if (HasActorBegunPlay() && IsLocalController() && GetLocalPlayer())
	{
		InitLocalLobby();
	}
}

void AStartScreenController::InitLocalLobby()
{
	if (bLocalLobbyInitialized)
	{
		return;
	}
	bLocalLobbyInitialized = true;

	// [리슨] 매치(술래 PC) 접속이 끊겨서 엔진이 이 맵을 혼자 모드로 다시 연 경우: 로비 재접속
	// (자동 접속/시작 화면보다 먼저 처리. 결과가 저장돼 있으면 로비에 도착한 뒤 아래에서 엔딩 재생)
	if (GetNetMode() == NM_Standalone && HandleMatchDisconnectResult())
	{
		return;
	}

	// [AWS] 패키징된 클라를 그냥 실행하면 로비 맵(LovyLv)이 서버 없이 혼자(Standalone) 열림 ->
	// 설정된 로비 서버(EC2)로 자동 접속. 에디터 PIE에서는 기존 테스트 방식 그대로 두기 위해 건너뜀
	if (GetNetMode() == NM_Standalone && GetWorld() && !GetWorld()->IsPlayInEditor())
	{
		UEscapeRoomSessionSubsystem* Session = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEscapeRoomSessionSubsystem>() : nullptr;
		if (Session && !Session->bTriedAutoConnect)
		{
			const FString LobbyAddress = UEscapeRoomSessionSubsystem::GetConfiguredLobbyAddress();
			if (!LobbyAddress.IsEmpty())
			{
				Session->bTriedAutoConnect = true;
				UE_LOG(LogTemp, Log, TEXT("[Session] Auto-connecting to lobby server: %s"), *LobbyAddress);
				ClientTravel(LobbyAddress, TRAVEL_Absolute);
				return;
			}
			UE_LOG(LogTemp, Warning, TEXT("[Session] 로비 주소 설정이 비어있음 (DefaultGame.ini [EscapeRoom.Network] LobbyAddress 또는 -LobbyAddress=) - 자동 접속 안 함"));
		}
	}

	// [로비 엔딩] 판이 끝나고 돌아온 경우: 시작 화면 대신 결과 방 엔딩 (버튼 누르면 FinishMatchResult -> 시작 화면)
	if (ShowPendingMatchResult())
	{
		return;
	}

	ShowStartScreenWidget();
	StartLobbyBGM();
}

void AStartScreenController::StartLobbyBGM()
{
	if (!LobbyBGM || LobbyBGMComponent)
	{
		return;
	}

	// 2D 사운드: 위치/거리와 상관없이 일정하게 들림.
	// CreateSound2D는 자동 재생하지 않으므로 반복 처리 바인딩 후 직접 재생/페이드인.
	// bPersistAcrossLevelTransition=false -> 게임 맵으로 이동하면 자동 정지, bAutoDestroy=false -> 곡이 끝나도 컴포넌트 유지(반복용)
	LobbyBGMComponent = UGameplayStatics::CreateSound2D(this, LobbyBGM, LobbyBGMVolume, 1.f, 0.f, nullptr, false, false);
	if (!LobbyBGMComponent)
	{
		return;
	}

	LobbyBGMComponent->OnAudioFinished.AddDynamic(this, &AStartScreenController::HandleLobbyBGMFinished);

	if (LobbyBGMFadeInTime > 0.f)
	{
		LobbyBGMComponent->FadeIn(LobbyBGMFadeInTime, 1.f);
	}
	else
	{
		LobbyBGMComponent->Play();
	}
}

void AStartScreenController::HandleLobbyBGMFinished()
{
	// 루프가 꺼진 에셋이라도 로비에 있는 동안은 계속 반복
	if (LobbyBGMComponent && IsValid(this))
	{
		LobbyBGMComponent->Play();
	}
}

void AStartScreenController::SwitchToWidget(TSubclassOf<UUserWidget> WidgetClass)
{
	if (!WidgetClass) return;

	if (ActiveWidget)
	{
		ActiveWidget->RemoveFromParent();
		ActiveWidget = nullptr;
	}

	ActiveWidget = CreateWidget<UUserWidget>(this, WidgetClass);
	if (ActiveWidget)
	{
		ActiveWidget->AddToViewport();

		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(ActiveWidget->TakeWidget());
		SetInputMode(InputMode);
		bShowMouseCursor = true;
	}
}

void AStartScreenController::ShowStartScreenWidget()
{
	SwitchToWidget(StartScreenWidgetClass);

	// 맵 이동용 검은 막이 남아있으면 걷음 (엔딩 없이 시작 화면으로 온 경우 등 - 검은 화면에 갇히지 않게)
	if (UEscapeRoomSessionSubsystem* Session = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEscapeRoomSessionSubsystem>() : nullptr)
	{
		Session->HideTravelBlackout();
	}
}

void AStartScreenController::ShowCreateJoinRoomWidget()
{
	SwitchToWidget(CreateJoinRoomWidgetClass);
}

void AStartScreenController::ShowRoomWidget()
{
	SwitchToWidget(RoomWidgetClass);
}

void AStartScreenController::OnRoomIdChanged(int32 NewRoomId)
{
	if (NewRoomId >= 0)
	{
		ShowRoomWidget();
	}
	else
	{
		ShowCreateJoinRoomWidget();
	}
}

void AStartScreenController::Server_CreateRoom_Implementation(const FString& RoomName, int32 MaxMembers)
{
	if (auto* GM = GetWorld()->GetAuthGameMode<ALobbyGameMode>())
	{
		GM->CreateRoom(PlayerState, RoomName, MaxMembers);
	}
}

void AStartScreenController::Server_JoinRoom_Implementation(int32 RoomId)
{
	if (auto* GM = GetWorld()->GetAuthGameMode<ALobbyGameMode>())
	{
		GM->JoinRoom(RoomId, PlayerState);
	}
}

void AStartScreenController::Server_LeaveRoom_Implementation()
{
	if (auto* GM = GetWorld()->GetAuthGameMode<ALobbyGameMode>())
	{
		GM->LeaveRoom(PlayerState);
	}
}

void AStartScreenController::Server_SetReady_Implementation(bool bReady)
{
	if (auto* GM = GetWorld()->GetAuthGameMode<ALobbyGameMode>())
	{
		GM->SetReady(PlayerState, bReady);
	}
}

FString AStartScreenController::GetUrlSafePlayerName() const
{
	// 매치(술래 PC)는 로비와 다른 서버라 로비의 PlayerState(이름 포함)가 따라가지 않음 ->
	// 로비 서버가 확정한 내 이름을 접속 URL 옵션(?PName=)으로 들고 감.
	// 매치 쪽 AescaperoomGameMode::InitNewPlayer가 이 값을 읽어서 PlayerName으로 세팅함
	// (술래는 OpenLevel URL에, 러너는 ClientTravel URL에 붙임)
	FString MyName = PlayerState ? PlayerState->GetPlayerName() : FString();

	// URL을 깨뜨리는 문자 제거 (서버에서도 한 번 더 거름)
	for (const TCHAR* Bad : { TEXT("?"), TEXT("="), TEXT("#"), TEXT(" "), TEXT("/"), TEXT("\\"), TEXT(":") })
	{
		MyName.ReplaceInline(Bad, TEXT("_"));
	}
	return MyName;
}

void AStartScreenController::RememberCurrentLobbyAddress()
{
	// 매치가 끝나면 돌아올 수 있도록, 떠나기 직전에 "지금 접속해 있는 로비 서버 주소"를 GameInstance
	// 메모장(UEscapeRoomSessionSubsystem)에 적어둠. 이 시점엔 아직 로비와 연결돼 있으므로 클라이언트의
	// NetConnection(=로비 서버와의 연결) URL이 곧 내가 접속한 로비 주소. 이동 이후엔 이 연결이
	// 끊기고 컨트롤러/위젯도 새로 만들어지지만 GameInstance는 프로그램이 꺼질 때까지 유지됨.
	// [AWS] 수정 불필요 - 로비에 EC2 공인 IP로 접속했다면 그 주소가 그대로 저장됨
	UGameInstance* GI = GetGameInstance();
	UEscapeRoomSessionSubsystem* Session = GI ? GI->GetSubsystem<UEscapeRoomSessionSubsystem>() : nullptr;
	if (!Session)
	{
		return;
	}

	FString Host;
	int32 Port = 0;
	if (const UNetConnection* Conn = GetNetConnection())
	{
		Host = Conn->URL.Host;
		Port = Conn->URL.Port;
	}
	if (Host.IsEmpty() && GetWorld())
	{
		Host = GetWorld()->URL.Host;
		Port = GetWorld()->URL.Port;
	}
	if (!Host.IsEmpty())
	{
		const FString LobbyAddress = FString::Printf(TEXT("%s:%d"), *Host, Port > 0 ? Port : FURL::UrlConfig.DefaultPort);
		Session->SetLobbyAddress(LobbyAddress);
		UE_LOG(LogTemp, Log, TEXT("[Session] Remembered lobby address: %s"), *LobbyAddress);
	}
}

void AStartScreenController::Client_ConnectToMatch_Implementation(const FString& ServerAddress)
{
	// [리슨] 러너: 술래 PC(리슨서버)로 접속
	RememberCurrentLobbyAddress();

	if (UEscapeRoomSessionSubsystem* Session = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEscapeRoomSessionSubsystem>() : nullptr)
	{
		Session->BeginMatch(/*bAsHost=*/false); // 이제부터 연결이 끊기면 "술래 이탈" 판정
	}

	FString TravelURL = ServerAddress;
	const FString MyName = GetUrlSafePlayerName();
	if (!MyName.IsEmpty())
	{
		TravelURL += FString::Printf(TEXT("?PName=%s"), *MyName);
	}

	UE_LOG(LogTemp, Log, TEXT("[Match] Runner -> connecting to chaser host: %s"), *TravelURL);
	ClientTravel(TravelURL, TRAVEL_Absolute);
}

void AStartScreenController::Client_HostMatch_Implementation(int32 Port, int32 ExpectedPlayers)
{
	// [리슨] 술래로 뽑힘: 내 PC에서 GameLV를 리슨서버로 열고 러너들을 기다림.
	// 로비와의 연결은 여기서 끊김(맵 이동) -> 로비 서버에선 Logout으로 방에서 빠짐
	RememberCurrentLobbyAddress();

	if (UEscapeRoomSessionSubsystem* Session = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEscapeRoomSessionSubsystem>() : nullptr)
	{
		Session->BeginMatch(/*bAsHost=*/true);
	}

	// 리슨 포트: 맵 URL에 포트를 직접 지정할 수 없어서, 절대 경로 이동 시 URL 기본 포트로 쓰이는
	// 전역 기본값(-port= 실행 인자와 같은 값)을 바꿔둠. 같은 PC에서 로비(7777)와 매치를 같이 띄워도 안 겹침.
	// 실제로 열린 포트는 매치 GameMode 로그 "[Match] Listen server up on port"로 확인 가능
	// (나중에 Steam으로 넘어가면 접속이 Steam ID 기반이라 포트는 의미 없어짐)
	if (Port > 0)
	{
		FURL::UrlConfig.DefaultPort = Port;
	}

	FString Options = FString::Printf(TEXT("listen?ExpectedPlayers=%d"), ExpectedPlayers);
	const FString MyName = GetUrlSafePlayerName();
	if (!MyName.IsEmpty())
	{
		Options += FString::Printf(TEXT("?PName=%s"), *MyName);
	}

	UE_LOG(LogTemp, Log, TEXT("[Match] I'm the chaser -> hosting %s?%s on port %d"), *MatchLevelName.ToString(), *Options, Port);
	UGameplayStatics::OpenLevel(this, MatchLevelName, true, Options);
}

bool AStartScreenController::HandleMatchDisconnectResult()
{
	UEscapeRoomSessionSubsystem* Session = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEscapeRoomSessionSubsystem>() : nullptr;
	if (!Session)
	{
		return false;
	}

	const EMatchDisconnectResult Result = Session->ConsumeDisconnectResult();
	switch (Result)
	{
	case EMatchDisconnectResult::ReturnToLobby:
		// 엔딩 후 끊김 / 술래 도중 이탈(러너 승리 결과가 저장됨) / 접속 실패 -> 로비로. 엔딩은 로비 도착 후 재생
		UE_LOG(LogTemp, Log, TEXT("[Match] Disconnected from match (pending result=%d) -> back to lobby"), Session->HasPendingMatchResult() ? 1 : 0);
		TravelBackToLobby();
		return true;

	default:
		return false;
	}
}

void AStartScreenController::TravelBackToLobby()
{
	UEscapeRoomSessionSubsystem* Session = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEscapeRoomSessionSubsystem>() : nullptr;
	const FString LobbyAddress = Session ? Session->GetLobbyAddressForReturn() : UEscapeRoomSessionSubsystem::GetConfiguredLobbyAddress();

	if (LobbyAddress.IsEmpty())
	{
		// 돌아갈 주소가 없음 -> 여기(혼자 모드 LovyLv)서 엔딩이 있으면 보여주고, 없으면 그냥 시작 화면
		UE_LOG(LogTemp, Warning, TEXT("[Match] 돌아갈 로비 주소가 없음 - 시작 화면 표시"));
		if (ShowPendingMatchResult())
		{
			return;
		}
		ShowStartScreenWidget();
		StartLobbyBGM();
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[Match] Returning to lobby: %s"), *LobbyAddress);
	ClientTravel(LobbyAddress, TRAVEL_Absolute);
}

void AStartScreenController::Debug_CreateRoom(const FString& RoomName)
{
	Server_CreateRoom(RoomName, 4);
}

void AStartScreenController::Debug_JoinRoom(int32 RoomId)
{
	Server_JoinRoom(RoomId);
}

void AStartScreenController::Debug_SetReady(bool bReady)
{
	Server_SetReady(bReady);
}

void AStartScreenController::Client_PlayAllReadySound_Implementation()
{
	if (AllReadySound)
	{
		UGameplayStatics::PlaySound2D(this, AllReadySound);
	}
}

// =========================== [로비 엔딩] 결과 방 연출 ===========================

bool AStartScreenController::ShowPendingMatchResult()
{
	UEscapeRoomSessionSubsystem* Session = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEscapeRoomSessionSubsystem>() : nullptr;
	if (!Session || !Session->HasPendingMatchResult())
	{
		return false;
	}

	if (!ResultFadeWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[Lobby][Result] ResultFadeWidgetClass가 비어있음 - StartScreenController BP Class Defaults에서 WBP_EscapeFade 지정 필요"));
		Session->ClearPendingMatchResult();
		return false;
	}

	const EMatchResultRoomType RoomType = Session->GetPendingResultRoomType();
	FText Message = Session->GetPendingResultMessage();
	if (Session->IsPendingResultHostLeft() && Message.IsEmpty())
	{
		Message = HostLeftResultMessage;
	}

	AResultRoomActor* Room = AResultRoomActor::FindRoom(this, RoomType);
	ActiveResultRoom = Room;
	if (!Room)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Lobby][Result] 결과 방(RoomType=%d)이 로비 맵(LovyLv)에 없음 - 검은 화면 결과로 대체"), (int32)RoomType);
	}

	// 시작 화면 등 다른 로비 위젯이 떠 있으면 정리
	if (ActiveWidget)
	{
		ActiveWidget->RemoveFromParent();
		ActiveWidget = nullptr;
	}

	ResultFadeWidget = CreateWidget<UEscapeFadeWidget>(this, ResultFadeWidgetClass);
	if (!ResultFadeWidget)
	{
		Session->ClearPendingMatchResult();
		return false;
	}
	ResultFadeWidget->AddToViewport(50);

	// 로비 풍경이 비치지 않게 완전히 검은 화면에서 시작 -> 결과 방 공개 (매치에서 하던 연출과 동일한 흐름)
	ResultFadeWidget->SnapToBlack();

	// 엔딩 위젯이 검은 화면으로 준비됨 = 컨트롤러 준비 완료 -> 맵 이동용 검은 막 제거 (아래가 이미 검어서 티 안 남)
	Session->HideTravelBlackout();
	ResultFadeWidget->OnMatchEndBlack.AddUObject(this, &AStartScreenController::HandleResultFadeBlack);

	// 버튼이 뜨기 전까지는 클릭/키 입력 무시 (버튼이 뜨면 위젯이 GameAndUI + 커서로 바꿔줌)
	SetInputMode(FInputModeUIOnly());
	bShowMouseCursor = false;

	UE_LOG(LogTemp, Log, TEXT("[Lobby][Result] Showing match result ending: RoomType=%d HostLeft=%d"),
		(int32)RoomType, Session->IsPendingResultHostLeft() ? 1 : 0);

	ResultFadeWidget->StartMatchEndFade(Message,
		Room ? Room->ResultTitle : FText::GetEmpty(),
		Room ? Room->ResultTitleColor : FLinearColor::White,
		/*bRevealResultRoom=*/Room != nullptr);
	return true;
}

void AStartScreenController::HandleResultFadeBlack()
{
	AResultRoomActor* Room = ActiveResultRoom.Get();
	if (!Room)
	{
		return;
	}

	// 매치에서 플레이한 내 캐릭터 외형 (GameInstance에 저장돼 있음 - 컨트롤러가 바뀌어도 남아있음)
	if (const UEscapeRoomSessionSubsystem* Session = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEscapeRoomSessionSubsystem>() : nullptr)
	{
		Room->ApplyCharacter(Session->GetResultMesh(), Session->GetResultMaterials());
	}

	bShowingResultRoom = true;
	SetViewTarget(Room);
}

void AStartScreenController::FinishMatchResult()
{
	if (UEscapeRoomSessionSubsystem* Session = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEscapeRoomSessionSubsystem>() : nullptr)
	{
		Session->ClearPendingMatchResult();
	}

	bShowingResultRoom = false;
	ActiveResultRoom = nullptr;

	if (ResultFadeWidget)
	{
		ResultFadeWidget->RemoveFromParent();
		ResultFadeWidget = nullptr;
	}

	// 로비 원래 시점으로 복귀 (레벨의 Auto Activate 카메라 / 폰 등 엔진 기본 규칙)
	AutoManageActiveCameraTarget(GetPawn());

	ShowStartScreenWidget();
	StartLobbyBGM();
}

void AStartScreenController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 서버 쪽 시점 전환(ClientSetViewTarget 등)이 늦게 도착해도 결과 방 시점 유지
	if (bShowingResultRoom && IsLocalController() && ActiveResultRoom.IsValid() && GetViewTarget() != ActiveResultRoom.Get())
	{
		SetViewTarget(ActiveResultRoom.Get());
	}
}

void AStartScreenController::AutoManageActiveCameraTarget(AActor* SuggestedTarget)
{
	if (bShowingResultRoom && ActiveResultRoom.IsValid())
	{
		if (GetViewTarget() != ActiveResultRoom.Get())
		{
			SetViewTarget(ActiveResultRoom.Get());
		}
		return;
	}

	Super::AutoManageActiveCameraTarget(SuggestedTarget);
}
