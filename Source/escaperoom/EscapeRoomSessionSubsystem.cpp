// EscapeRoomSessionSubsystem.cpp

#include "EscapeRoomSessionSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "ResultRoomActor.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/Layout/SBorder.h"
#include "Styling/CoreStyle.h"
#include "UObject/UObjectGlobals.h"
#include "TimerManager.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineIdentityInterface.h"

void UEscapeRoomSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UEscapeRoomSessionSubsystem::HandleNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UEscapeRoomSessionSubsystem::HandleTravelFailure);
	}

	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UEscapeRoomSessionSubsystem::HandlePostLoadMap);
}

void UEscapeRoomSessionSubsystem::Deinitialize()
{
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);

	bBlackoutRequested = false;
	RemoveBlackoutFromViewport();
	BlackoutWidget.Reset();

	Super::Deinitialize();
}

void UEscapeRoomSessionSubsystem::BeginMatch(bool bAsHost)
{
	bInMatch = true;
	bIsMatchHost = bAsHost;
	bMatchResultReceived = false;
	PendingDisconnectResult = EMatchDisconnectResult::None;
	ClearPendingMatchResult(); // 지난 판 결과가 남아있지 않게
	UE_LOG(LogTemp, Log, TEXT("[Session] Begin match as %s"), bAsHost ? TEXT("HOST (Chaser)") : TEXT("client (Runner)"));
}

void UEscapeRoomSessionSubsystem::SetPendingMatchResult(EMatchResultRoomType RoomType, const FText& Message, bool bHostLeft)
{
	bHasPendingMatchResult = true;
	PendingResultRoomType = RoomType;
	PendingResultMessage = Message;
	bPendingResultHostLeft = bHostLeft;
	UE_LOG(LogTemp, Log, TEXT("[Session] Pending match result saved: RoomType=%d HostLeft=%d"), (int32)RoomType, bHostLeft ? 1 : 0);
}

void UEscapeRoomSessionSubsystem::ClearPendingMatchResult()
{
	bHasPendingMatchResult = false;
	bPendingResultHostLeft = false;
	PendingResultRoomType = EMatchResultRoomType{};
	PendingResultMessage = FText::GetEmpty();
}

void UEscapeRoomSessionSubsystem::EndMatch()
{
	bInMatch = false;
	bIsMatchHost = false;
	bMatchResultReceived = false;
}

void UEscapeRoomSessionSubsystem::SetResultCharacter(USkeletalMesh* Mesh, const TArray<TObjectPtr<UMaterialInterface>>& Materials)
{
	ResultMesh = Mesh;
	ResultMaterials = Materials;
}

bool UEscapeRoomSessionSubsystem::IsMyWorld(const UWorld* World) const
{
	return World && World->GetGameInstance() == GetGameInstance();
}

void UEscapeRoomSessionSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	// 매치에 러너(클라)로 접속해 있을 때만 관심 있음.
	// 호스트(술래) 본인은 서버라서 러너가 나가도 여기 안 옴. 로비 연결 끊김(bInMatch=false)도 무시
	if (!IsMyWorld(World) || !bInMatch || bIsMatchHost)
	{
		return;
	}
	if (NetDriver && NetDriver->NetDriverName != NAME_GameNetDriver)
	{
		return; // 데모(리플레이) 등 다른 넷드라이버
	}

	// [로비 엔딩] 엔딩을 이미 받았으면 결과는 ClientShowMatchEnd에서 저장돼 있음 -> 그대로 로비로.
	// 엔딩 전에 끊겼으면 = 매치 도중 술래가 나감 -> 러너 승리 결과를 저장 (로비에서 RunnerWin 결과 방 엔딩)
	if (!bMatchResultReceived)
	{
		SetPendingMatchResult(EMatchResultRoomType::RunnerWin, FText::GetEmpty(), /*bHostLeft=*/true);
	}
	PendingDisconnectResult = EMatchDisconnectResult::ReturnToLobby;
	bInMatch = false;

	// 이제 혼자 모드 로비 맵 -> 로비 서버 재접속을 거쳐 로비에서 엔딩을 보게 됨 - 그동안 로비 화면이 비치지 않게 검은 막
	ShowTravelBlackout();

	UE_LOG(LogTemp, Warning, TEXT("[Session] Lost connection to match host (%s: %s) -> %s"),
		ENetworkFailure::ToString(FailureType), *ErrorString,
		bMatchResultReceived ? TEXT("return to lobby") : TEXT("HOST LEFT - runners win"));
	// 이후 엔진이 기본 맵(LovyLv)을 혼자 모드로 열고, AStartScreenController::BeginPlay가 결과를 처리함
}

void UEscapeRoomSessionSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	if (!IsMyWorld(World) || !bInMatch || bIsMatchHost)
	{
		return;
	}

	// 술래 PC에 들어가지도 못함 = 판이 성립 안 됨 -> 엔딩 없이 로비로
	PendingDisconnectResult = EMatchDisconnectResult::ReturnToLobby;
	bInMatch = false;

	UE_LOG(LogTemp, Warning, TEXT("[Session] Could not join match host (%s: %s) -> return to lobby"),
		ETravelFailure::ToString(FailureType), *ErrorString);
}

// =========================== 맵 이동용 검은 막 ===========================

void UEscapeRoomSessionSubsystem::ShowTravelBlackout()
{
	bBlackoutRequested = true;
	AddBlackoutToViewport();

	// 안전장치: 누가 끝내 걷지 않아도 일정 시간 뒤 자동으로 걷음 (GameInstance 타이머 - 맵이 바뀌어도 유지)
	if (UGameInstance* GI = GetGameInstance())
	{
		GI->GetTimerManager().SetTimer(BlackoutSafetyTimer,
			FTimerDelegate::CreateUObject(this, &UEscapeRoomSessionSubsystem::HideTravelBlackout),
			BlackoutMaxSeconds, false);
	}
	UE_LOG(LogTemp, Log, TEXT("[Session] Travel blackout ON"));
}

void UEscapeRoomSessionSubsystem::HideTravelBlackout()
{
	if (!bBlackoutRequested && !BlackoutWidget.IsValid())
	{
		return;
	}
	bBlackoutRequested = false;
	RemoveBlackoutFromViewport();

	if (UGameInstance* GI = GetGameInstance())
	{
		GI->GetTimerManager().ClearTimer(BlackoutSafetyTimer);
	}
	UE_LOG(LogTemp, Log, TEXT("[Session] Travel blackout OFF"));
}

void UEscapeRoomSessionSubsystem::AddBlackoutToViewport()
{
	UGameInstance* GI = GetGameInstance();
	UGameViewportClient* VC = GI ? GI->GetGameViewportClient() : nullptr; // 내 창 (PIE면 창마다 따로)
	if (!VC)
	{
		return; // 데디서버 등 화면 없음
	}

	if (!BlackoutWidget.IsValid())
	{
		// 화면 전체를 채우는 검은 사각형 (Slate - PlayerController 없이도 표시 가능)
		BlackoutWidget = SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor::Black)
			.Visibility(EVisibility::HitTestInvisible);
	}

	// 이미 붙어있으면 중복으로 쌓이지 않게 한 번 떼고 다시 붙임. ZOrder를 크게 줘서 모든 UI보다 위
	VC->RemoveViewportWidgetContent(BlackoutWidget.ToSharedRef());
	VC->AddViewportWidgetContent(BlackoutWidget.ToSharedRef(), 10000);
}

void UEscapeRoomSessionSubsystem::RemoveBlackoutFromViewport()
{
	UGameInstance* GI = GetGameInstance();
	UGameViewportClient* VC = GI ? GI->GetGameViewportClient() : nullptr;
	if (VC && BlackoutWidget.IsValid())
	{
		VC->RemoveViewportWidgetContent(BlackoutWidget.ToSharedRef());
	}
}

void UEscapeRoomSessionSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	// 맵 로드 때 엔진이 뷰포트 위젯을 정리하므로, 막이 요청된 상태면 새 맵 첫 프레임 전에 다시 붙임
	if (bBlackoutRequested && IsMyWorld(LoadedWorld))
	{
		AddBlackoutToViewport();
	}
}

// ======================== [EOS] P2P 로그인 ========================

void UEscapeRoomSessionSubsystem::LoginEOS(FOnEOSLoginFinished OnFinished)
{
	if (IsEOSLoggedIn())
	{
		OnFinished.ExecuteIfBound(true);
		return;
	}

	// 로비 데디서버는 EOS를 쓰지 않음 (EIK NetDriver도 데디서버에선 항상 IP 방식으로 동작)
	if (IsRunningDedicatedServer())
	{
		OnFinished.ExecuteIfBound(false);
		return;
	}

	PendingEOSLoginCallbacks.Add(MoveTemp(OnFinished));
	if (bEOSLoginInProgress)
	{
		return; // 이미 진행 중 - 끝나면 같이 불림
	}

	IOnlineSubsystem* OSS = IOnlineSubsystem::Get(TEXT("EIK"));
	IOnlineIdentityPtr Identity;
	if (OSS)
	{
		Identity = OSS->GetIdentityInterface();
	}
	if (!Identity.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[EOS] EIK online subsystem is not available - continuing without EOS (IP fallback)"));
		FinishEOSLogin(false);
		return;
	}

	// 이미 로그인된 상태면(타임아웃 뒤 늦게 성공한 경우 등) 그 ID로 바로 주소를 만듦
	if (Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn)
	{
		if (FUniqueNetIdPtr ExistingId = Identity->GetUniquePlayerId(0))
		{
			HandleEOSLoginComplete(0, true, *ExistingId, FString());
			return;
		}
	}

	bEOSLoginInProgress = true;
	EOSLoginCompleteHandle = Identity->AddOnLoginCompleteDelegate_Handle(0,
		FOnLoginCompleteDelegate::CreateUObject(this, &UEscapeRoomSessionSubsystem::HandleEOSLoginComplete));

	if (UGameInstance* GI = GetGameInstance())
	{
		GI->GetTimerManager().SetTimer(EOSLoginTimeoutHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UE_LOG(LogTemp, Warning, TEXT("[EOS] Login timed out after %.0fs - continuing without EOS (IP fallback)"), EOSLoginTimeoutSeconds);
			FinishEOSLogin(false);
		}), EOSLoginTimeoutSeconds, false);
	}

	// EIK 규칙: "noeas_+_<외부 인증 종류>" = Epic 계정(EAS) 없이 Connect 인터페이스로 로그인.
	// Device ID 계정이 아직 없으면 EIK가 자동으로 만든 뒤 다시 로그인함. Id = 표시 이름(비면 안 됨)
	FOnlineAccountCredentials Credentials;
	Credentials.Type = TEXT("noeas_+_EIK_ECT_DEVICEID_ACCESS_TOKEN");
	Credentials.Id = TEXT("Player");
	Credentials.Token = FString();

	UE_LOG(LogTemp, Log, TEXT("[EOS] Device ID login start"));
	if (!Identity->Login(0, Credentials))
	{
		UE_LOG(LogTemp, Warning, TEXT("[EOS] Login call was rejected - continuing without EOS (IP fallback)"));
		FinishEOSLogin(false);
	}
}

void UEscapeRoomSessionSubsystem::HandleEOSLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error)
{
	if (bWasSuccessful && UserId.IsValid())
	{
		// EIK의 FUniqueNetIdEOS::ToString() = "EpicAccountId|PUID", Epic 계정이 없으면(Device ID) PUID만 옴
		FString ProductUserId = UserId.ToString();
		FString AfterSeparator;
		if (ProductUserId.Split(TEXT("|"), nullptr, &AfterSeparator))
		{
			ProductUserId = AfterSeparator;
		}

		if (!ProductUserId.IsEmpty())
		{
			EOSMatchAddress = MakeEOSMatchAddress(ProductUserId);
			UE_LOG(LogTemp, Log, TEXT("[EOS] Login OK. PUID=%s, match address=%s"), *ProductUserId, *EOSMatchAddress);
			FinishEOSLogin(true);
			return;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[EOS] Login failed (%s) - continuing without EOS (IP fallback)"), *Error);
	FinishEOSLogin(false);
}

void UEscapeRoomSessionSubsystem::FinishEOSLogin(bool bSuccess)
{
	bEOSLoginInProgress = false;

	if (UGameInstance* GI = GetGameInstance())
	{
		GI->GetTimerManager().ClearTimer(EOSLoginTimeoutHandle);
	}

	if (EOSLoginCompleteHandle.IsValid())
	{
		if (IOnlineSubsystem* OSS = IOnlineSubsystem::Get(TEXT("EIK")))
		{
			if (IOnlineIdentityPtr Identity = OSS->GetIdentityInterface())
			{
				Identity->ClearOnLoginCompleteDelegate_Handle(0, EOSLoginCompleteHandle);
			}
		}
		EOSLoginCompleteHandle.Reset();
	}

	// 콜백 안에서 맵 이동(ClientTravel)을 하므로, 목록을 먼저 꺼내 비운 뒤 호출
	TArray<FOnEOSLoginFinished> Callbacks = MoveTemp(PendingEOSLoginCallbacks);
	PendingEOSLoginCallbacks.Reset();
	for (FOnEOSLoginFinished& Callback : Callbacks)
	{
		Callback.ExecuteIfBound(bSuccess && IsEOSLoggedIn());
	}
}

FString UEscapeRoomSessionSubsystem::MakeEOSMatchAddress(const FString& ProductUserId)
{
	// EIK NetDriver(UNetDriverEIKBase::InitBase)는 리슨 소켓 이름 = 넷드라이버 이름("GameNetDriver"),
	// 채널 = 그 이름의 해시를 uint8로 자른 값으로 정함. 러너가 같은 소켓으로 찾아오도록 똑같이 계산해서
	// "EOS:<PUID>:<소켓 이름>:<채널>" 형식으로 만듦 (FInternetAddrEOS::ToString과 같은 형식)
	const FString SocketName = TEXT("GameNetDriver");
	const uint8 Channel = static_cast<uint8>(GetTypeHash(SocketName));
	return FString::Printf(TEXT("EOS:%s:%s:%u"), *ProductUserId, *SocketName, static_cast<uint32>(Channel));
}
