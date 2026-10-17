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

bool UEscapeRoomSessionSubsystem::IsHostLeftResultWorld(const UWorld* World)
{
	return World && World->URL.HasOption(HostLeftResultOption);
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
