// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "StartScreenController.generated.h"

class USoundBase;
class UAudioComponent;

class UStartScreenWidget;
class UCreateJoinRoomWidget;
class URoomWidget;
class UUserWidget;
class UEscapeFadeWidget;
class AResultRoomActor;

/**
 * 
 */
UCLASS()
class ESCAPEROOM_API AStartScreenController : public APlayerController
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UStartScreenWidget> StartScreenWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UCreateJoinRoomWidget> CreateJoinRoomWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<URoomWidget> RoomWidgetClass;

	UPROPERTY()
	UUserWidget* ActiveWidget;

	virtual void BeginPlay() override;

	// 클라: 내 화면(LocalPlayer)이 연결되는 순간 - BeginPlay보다 늦으면 여기서 로비 UI 초기화
	virtual void ReceivedPlayer() override;

	// 시작 화면/자동 접속/엔딩 등 로컬 초기화 (BeginPlay 또는 ReceivedPlayer에서 1회)
	void InitLocalLobby();
	bool bLocalLobbyInitialized = false;

	void SwitchToWidget(TSubclassOf<UUserWidget> WidgetClass);

public:
	void ShowStartScreenWidget();
	void ShowCreateJoinRoomWidget();
	void ShowRoomWidget();

	// Called client-side (local player only) whenever CurrentRoomId replicates to a new value,
	// so the UI follows server-confirmed room membership instead of guessing on button click.
	void OnRoomIdChanged(int32 NewRoomId);

	// Room / lobby RPCs (server-authoritative, called from client UI).
	UFUNCTION(Server, Reliable)
	void Server_CreateRoom(const FString& RoomName, int32 MaxMembers);

	UFUNCTION(Server, Reliable)
	void Server_JoinRoom(int32 RoomId);

	UFUNCTION(Server, Reliable)
	void Server_LeaveRoom();

	UFUNCTION(Server, Reliable)
	void Server_SetReady(bool bReady);

	// [EOS] 클라 -> 로비 서버: 내가 술래가 됐을 때 러너들이 접속할 EOS P2P 주소 등록 (로비 접속 직후 1회)
	UFUNCTION(Server, Reliable)
	void Server_RegisterEOSAddress(const FString& EOSAddress);

	// [리슨] 로비 서버 -> 러너들: 술래 PC(리슨서버)로 접속해라
	UFUNCTION(Client, Reliable)
	void Client_ConnectToMatch(const FString& ServerAddress);

	// [리슨] 로비 서버 -> 랜덤으로 뽑힌 술래: 내 PC에서 GameLV를 리슨서버로 열어라.
	// Port: 리슨 포트 (같은 PC에서 로비+매치를 같이 테스트할 때 안 겹치게 로비가 정해줌)
	// ExpectedPlayers: 술래 포함 방 인원 - 다 모이면 맵 생성 시작 (AescaperoomGameMode::CheckAllPlayersJoined)
	// bUseEOSP2P: [EOS] true면 EOS P2P로 리슨(러너들이 EOS 주소로 접속), false면 기존 IP:Port로 리슨
	UFUNCTION(Client, Reliable)
	void Client_HostMatch(int32 Port, int32 ExpectedPlayers, bool bUseEOSP2P);

	// 매치 레벨 이름 (OpenLevel에 그대로 넘김)
	UPROPERTY(EditDefaultsOnly, Category = "Match")
	FName MatchLevelName = TEXT("GameLV");

	// 방 인원 전원이 Ready가 되는 순간(카운트다운 시작) 로비 서버가 방 멤버 전원에게 호출.
	// 로비 서버에서 재생하면 플레이어에게 안 들리므로 각 클라이언트가 자기 화면에서 2D로 재생
	UFUNCTION(Client, Reliable)
	void Client_PlayAllReadySound();

	// 전원 Ready 효과음 (BP Class Defaults에서 지정)
	UPROPERTY(EditDefaultsOnly, Category = "Lobby|Sound")
	TObjectPtr<USoundBase> AllReadySound;

	// ---- 로비 배경음악 (각 클라이언트가 자기 화면에서 2D로 재생, 게임 맵으로 이동하면 자동 정지) ----
	UPROPERTY(EditDefaultsOnly, Category = "Lobby|Sound")
	TObjectPtr<USoundBase> LobbyBGM;

	UPROPERTY(EditDefaultsOnly, Category = "Lobby|Sound", meta = (ClampMin = "0.0"))
	float LobbyBGMVolume = 0.6f;

	// 로비 진입 시 페이드인 시간(초). 0이면 바로 재생
	UPROPERTY(EditDefaultsOnly, Category = "Lobby|Sound", meta = (ClampMin = "0.0"))
	float LobbyBGMFadeInTime = 1.5f;

	// ===================== [로비 엔딩] 게임 결과 방 연출 =====================
	// 판이 끝나면 결과가 GameInstance(UEscapeRoomSessionSubsystem)에 저장된 채로 로비로 돌아옴 ->
	// 시작 화면 대신 LovyLv에 배치된 결과 방(AResultRoomActor)으로 엔딩을 재생하고, 버튼을 누르면 시작 화면으로.
	// 술래 PC(리슨서버)가 먼저 꺼져도 각자 자기 PC에서 재생하므로 엔딩이 끊기지 않음

	// 엔딩 화면 위젯 (매치에서 쓰던 WBP_EscapeFade 그대로 지정하면 됨 - BP Class Defaults)
	UPROPERTY(EditDefaultsOnly, Category = "Lobby|Result")
	TSubclassOf<UEscapeFadeWidget> ResultFadeWidgetClass;

	// 술래가 매치 도중 나가서 러너가 이겼을 때 결과 문구 (결과 방 제목은 RunnerWin 방의 ResultTitle)
	UPROPERTY(EditDefaultsOnly, Category = "Lobby|Result")
	FText HostLeftResultMessage = NSLOCTEXT("MatchEnd", "HostLeft", "The chaser left the game");

	// 엔딩 위젯의 버튼이 호출 - 결과 지우고 시작 화면 + 로비 BGM
	void FinishMatchResult();

	// 결과 방 연출 중 엔진이 시점을 다른 곳(로비 기본 카메라/폰)으로 옮겨도 결과 방으로 되돌림
	virtual void Tick(float DeltaSeconds) override;
	virtual void AutoManageActiveCameraTarget(AActor* SuggestedTarget) override;

private:
	UPROPERTY()
	TObjectPtr<UAudioComponent> LobbyBGMComponent;

	// 저장된 결과가 있으면 엔딩 시작 후 true (없으면 false -> 평소처럼 시작 화면)
	bool ShowPendingMatchResult();

	// 엔딩 위젯이 검은 화면이 된 순간 - 내 캐릭터 외형 적용 + 결과 방 카메라로 전환
	void HandleResultFadeBlack();

	UPROPERTY()
	TObjectPtr<UEscapeFadeWidget> ResultFadeWidget;

	TWeakObjectPtr<AResultRoomActor> ActiveResultRoom;
	bool bShowingResultRoom = false;

	// 매치로 떠나기 직전에 지금 접속해 있는 로비 주소를 서브시스템에 저장 (러너/술래 공통)
	void RememberCurrentLobbyAddress();

	// 로비 이름에서 URL을 깨뜨리는 문자를 치환한 내 이름 (?PName= 용)
	FString GetUrlSafePlayerName() const;

	// 매치 접속이 끊겨 시작 맵으로 돌아왔을 때 처리. 처리했으면 true (이후 BeginPlay 기본 흐름 생략)
	bool HandleMatchDisconnectResult();

	// 저장된 로비 주소로 재접속
	void TravelBackToLobby();

	void StartLobbyBGM();

	// 사운드 에셋에 Looping이 꺼져 있어도 곡이 끝나면 다시 재생해서 반복 유지
	UFUNCTION()
	void HandleLobbyBGMFinished();

public:

	// Debug-only console commands, kept for quick testing (~ console in a client window).
	UFUNCTION(Exec)
	void Debug_CreateRoom(const FString& RoomName);

	UFUNCTION(Exec)
	void Debug_JoinRoom(int32 RoomId);

	UFUNCTION(Exec)
	void Debug_SetReady(bool bReady);

	// [디버그] 서버 없이 로비 결과창 확인. 콘솔: Debug_ShowEnding 0~3 (0=RunnerWin 1=RunnerLose 2=ChaserWin 3=ChaserLose)
	UFUNCTION(Exec)
	void Debug_ShowEnding(int32 RoomType);
};
