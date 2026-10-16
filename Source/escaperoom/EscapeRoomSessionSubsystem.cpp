// EscapeRoomSessionSubsystem.cpp

#include "EscapeRoomSessionSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"

void UEscapeRoomSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UEscapeRoomSessionSubsystem::HandleNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UEscapeRoomSessionSubsystem::HandleTravelFailure);
	}
}

void UEscapeRoomSessionSubsystem::Deinitialize()
{
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	Super::Deinitialize();
}

void UEscapeRoomSessionSubsystem::BeginMatch(bool bAsHost)
{
	bInMatch = true;
	bIsMatchHost = bAsHost;
	bMatchResultReceived = false;
	PendingDisconnectResult = EMatchDisconnectResult::None;
	UE_LOG(LogTemp, Log, TEXT("[Session] Begin match as %s"), bAsHost ? TEXT("HOST (Chaser)") : TEXT("client (Runner)"));
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

	PendingDisconnectResult = bMatchResultReceived
		? EMatchDisconnectResult::ReturnToLobby       // 엔딩을 이미 봤음 -> 조용히 로비로
		: EMatchDisconnectResult::HostLeftRunnerWin;  // 매치 도중 술래가 나감 -> 러너 승리
	bInMatch = false;

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
