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

void AStartScreenController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController()) return; // Skip on non-local (server-side) controller instances

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

void AStartScreenController::Client_ConnectToMatch_Implementation(const FString& ServerAddress)
{
	// 매치 서버는 별도 프로세스라 로비의 PlayerState(이름 포함)가 따라가지 않음 ->
	// 로비 서버가 확정한 내 이름을 접속 URL 옵션(?PName=)으로 들고 감.
	// 매치 서버의 AescaperoomGameMode::InitNewPlayer가 이 값을 읽어서 PlayerName으로 세팅함.
	FString MyName = PlayerState ? PlayerState->GetPlayerName() : FString();

	// URL을 깨뜨리는 문자 제거 (서버에서도 한 번 더 거름)
	for (const TCHAR* Bad : { TEXT("?"), TEXT("="), TEXT("#"), TEXT(" "), TEXT("/"), TEXT("\\"), TEXT(":") })
	{
		MyName.ReplaceInline(Bad, TEXT("_"));
	}

	// 매치가 끝나면 돌아올 수 있도록, 떠나기 직전에 "지금 접속해 있는 로비 서버 주소"를 GameInstance
	// 메모장(UEscapeRoomSessionSubsystem)에 적어둠. 이 시점엔 아직 로비와 연결돼 있으므로 클라이언트의
	// NetConnection(=로비 서버와의 연결) URL이 곧 내가 접속한 로비 주소. ClientTravel 이후엔 이 연결이
	// 끊기고 컨트롤러/위젯도 새로 만들어지지만 GameInstance는 프로그램이 꺼질 때까지 유지됨.
	// [AWS] 수정 불필요 - 로비에 EC2 공인 IP로 접속했다면 그 주소가 그대로 저장됨
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEscapeRoomSessionSubsystem* Session = GI->GetSubsystem<UEscapeRoomSessionSubsystem>())
		{
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
	}

	FString TravelURL = ServerAddress;
	if (!MyName.IsEmpty())
	{
		TravelURL += FString::Printf(TEXT("?PName=%s"), *MyName);
	}

	ClientTravel(TravelURL, TRAVEL_Absolute);
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
