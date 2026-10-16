// EscapeRoomSessionSubsystem.h
//
// [설계 메모] 서버 이동(ClientTravel/OpenLevel)을 해도 사라지지 않고 유지되어야 하는 클라이언트 쪽 정보 보관소.
// GameInstance는 레벨/서버를 옮겨 다녀도 게임이 켜져 있는 동안 계속 살아있고, 그 서브시스템은
// 엔진이 자동으로 생성해주므로 프로젝트 설정(GameInstance 클래스)을 바꿀 필요가 없다.
//
// [구조] 로비 = EC2 데디서버 / 매치(GameLV) = 술래 PC 리슨서버
//   - 매치로 떠나기 직전(러너: Client_ConnectToMatch, 술래: Client_HostMatch) 지금 접속해 있는 로비 주소를 저장
//     -> 매치가 끝나면 ADungeonPlayerController::ReturnToLobby가 꺼내 씀
//   - 매치 도중 호스트(술래) PC와 연결이 끊기면 엔진이 강제로 기본 맵(LovyLv, 혼자 모드)으로 보냄.
//     이때 "엔딩을 이미 받았는지"를 보고 결과를 남겨두면, LovyLv의 AStartScreenController::BeginPlay가
//     - 엔딩 전: GameLV를 혼자 모드 + ?HostLeftResult 로 다시 열어서 기존 결과 방/엔딩 UI로 러너 승리 연출
//     - 엔딩 후: 바로 로비로 재접속

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/EngineBaseTypes.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "EscapeRoomSessionSubsystem.generated.h"

class UNetDriver;
class USkeletalMesh;
class UMaterialInterface;

// 매치 접속이 끊긴 뒤 시작 맵(LovyLv)에서 처리할 일
enum class EMatchDisconnectResult : uint8
{
	None,               // 처리할 것 없음 (평소 실행)
	HostLeftRunnerWin,  // 엔딩 전에 호스트(술래)가 나감 -> GameLV 결과 모드로 러너 승리 연출 후 로비 복귀
	ReturnToLobby,      // 엔딩 후 끊김 / 술래 PC 접속 실패 -> 바로 로비 복귀
};

UCLASS()
class ESCAPEROOM_API UEscapeRoomSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ---------------- 로비 주소 ----------------
	void SetLobbyAddress(const FString& InAddress) { LobbyAddress = InAddress; }
	const FString& GetLobbyAddress() const { return LobbyAddress; }
	bool HasLobbyAddress() const { return !LobbyAddress.IsEmpty(); }

	// [AWS] 클라이언트가 처음 접속할 로비 서버 주소 ("IP:포트").
	// 우선순위: 실행 인자 -LobbyAddress=ip:port  >  Config/DefaultGame.ini 의 [EscapeRoom.Network] LobbyAddress
	// 둘 다 없으면 빈 문자열 (자동 접속 안 함)
	static FString GetConfiguredLobbyAddress()
	{
		FString Addr;
		if (FParse::Value(FCommandLine::Get(), TEXT("LobbyAddress="), Addr) && !Addr.IsEmpty())
		{
			return Addr;
		}
		if (GConfig)
		{
			GConfig->GetString(TEXT("EscapeRoom.Network"), TEXT("LobbyAddress"), Addr, GGameIni);
		}
		Addr.TrimStartAndEndInline();
		return Addr;
	}

	// 로비로 돌아갈 주소: 저장된 주소 > 설정 파일 주소
	FString GetLobbyAddressForReturn() const
	{
		return HasLobbyAddress() ? LobbyAddress : GetConfiguredLobbyAddress();
	}

	// 게임 실행 후 로비 자동 접속을 이미 시도했는지. 접속 실패로 시작 맵이 다시 열려도
	// 무한 재접속하지 않게 한 번만 시도함
	bool bTriedAutoConnect = false;

	// ---------------- 매치 상태 ----------------
	// 로비를 떠나 매치로 갈 때 호출 (러너: 술래 PC 접속 직전 / 술래: 리슨서버 열기 직전)
	void BeginMatch(bool bAsHost);

	// ClientShowMatchEnd를 받았을 때 호출 - 이후 끊김은 "정상 종료 후 끊김"으로 취급
	void MarkMatchResultReceived() { bMatchResultReceived = true; }

	// 스스로 로비로 돌아갈 때(ReturnToLobby) 호출 - 이후 끊김 감지 안 함
	void EndMatch();

	bool IsInMatch() const { return bInMatch; }
	bool IsMatchHost() const { return bIsMatchHost; }

	// ---------------- 술래 이탈 결과 모드 ----------------
	// GameLV를 이 옵션을 붙여 혼자 모드로 열면 맵 생성 없이 결과 방 연출만 함
	static constexpr const TCHAR* HostLeftResultOption = TEXT("HostLeftResult");

	// 이 월드가 "술래 이탈 결과 모드"로 열린 GameLV인지 (WFCGridManager / DungeonPlayerController 등이 확인)
	static bool IsHostLeftResultWorld(const UWorld* World);

	// 결과 방에 세울 내 캐릭터 외형. 매치 중 빙의 시점에 저장해두면, 튕겨서 컨트롤러가 새로 만들어져도 남아있음
	void SetResultCharacter(USkeletalMesh* Mesh, const TArray<TObjectPtr<UMaterialInterface>>& Materials);
	USkeletalMesh* GetResultMesh() const { return ResultMesh; }
	const TArray<TObjectPtr<UMaterialInterface>>& GetResultMaterials() const { return ResultMaterials; }

	// 시작 맵에서 한 번 꺼내 쓰고 비움
	EMatchDisconnectResult ConsumeDisconnectResult()
	{
		const EMatchDisconnectResult Result = PendingDisconnectResult;
		PendingDisconnectResult = EMatchDisconnectResult::None;
		return Result;
	}

private:
	// "호스트:포트" 형식 (예: 127.0.0.1:7777). 로비를 거치지 않고 매치에 들어왔으면(PIE 등) 비어있음
	FString LobbyAddress;

	bool bInMatch = false;
	bool bIsMatchHost = false;
	bool bMatchResultReceived = false;
	EMatchDisconnectResult PendingDisconnectResult = EMatchDisconnectResult::None;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> ResultMesh;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> ResultMaterials;

	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;

	// 접속 중이던 서버(술래 PC)와 연결이 끊김
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);

	// 술래 PC로 접속 자체가 실패함 (술래가 리슨서버를 열기 전에 꺼진 경우 등)
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

	// 이 이벤트가 내 GameInstance의 월드에서 난 건지 (PIE 창이 여러 개면 엔진 델리게이트를 같이 받음)
	bool IsMyWorld(const UWorld* World) const;
};
