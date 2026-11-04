// EscapeRoomSessionSubsystem.h
//
// [설계 메모] 서버 이동(ClientTravel/OpenLevel)을 해도 사라지지 않고 유지되어야 하는 클라이언트 쪽 정보 보관소.
// GameInstance는 레벨/서버를 옮겨 다녀도 게임이 켜져 있는 동안 계속 살아있고, 그 서브시스템은
// 엔진이 자동으로 생성해주므로 프로젝트 설정(GameInstance 클래스)을 바꿀 필요가 없다.
//
// [구조] 로비 = EC2 데디서버 / 매치(GameLV) = 술래 PC 리슨서버
//   - 매치로 떠나기 직전(러너: Client_ConnectToMatch, 술래: Client_HostMatch) 지금 접속해 있는 로비 주소를 저장
//     -> 매치가 끝나면 ADungeonPlayerController::ReturnToLobby가 꺼내 씀
//   - [로비 엔딩] 판이 끝나면 결과(결과 방 종류 + 문구)를 여기 저장해두고 전원 로비로 이동 ->
//     로비 맵(LovyLv)의 AStartScreenController가 저장된 결과로 결과 방 엔딩을 재생 (술래 PC에 의존하지 않음)
//   - 매치 도중 호스트(술래) PC와 연결이 끊기면 엔진이 강제로 기본 맵(LovyLv, 혼자 모드)으로 보냄.
//     이때 "엔딩을 이미 받았는지"를 보고
//     - 엔딩 전: 러너 승리(술래 이탈) 결과를 저장
//     - 엔딩 후: 이미 저장된 결과 그대로
//     어느 쪽이든 LovyLv에서 로비로 재접속한 뒤 저장된 결과로 엔딩을 보여줌

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/EngineBaseTypes.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "EscapeRoomSessionSubsystem.generated.h"

class UNetDriver;
class SWidget;
class USkeletalMesh;
class UMaterialInterface;
class FUniqueNetId;
enum class EMatchResultRoomType : uint8; // ResultRoomActor.h

// 매치 접속이 끊긴 뒤 시작 맵(LovyLv)에서 처리할 일
enum class EMatchDisconnectResult : uint8
{
	None,               // 처리할 것 없음 (평소 실행)
	ReturnToLobby,      // 매치 접속이 끊김(엔딩 전/후, 술래 PC 접속 실패) -> 로비 재접속 (결과가 저장돼 있으면 로비에서 엔딩)
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

	// ---------------- [EOS] P2P 로그인 ----------------
	// 술래 PC(리슨서버)는 집 공유기(NAT) 뒤에 있어서 러너가 IP로 직접 접속할 수 없음 ->
	// EOS(Epic Online Services) P2P를 사용. EIK NetDriver가 NAT 뚫기를 시도하고, 실패하면 Epic 중계 서버로 연결해줌.
	// 계정 없이 이 PC 전용 익명 계정(Device ID)으로 로그인 -> 내 Product User ID(PUID)로
	// "EOS:<PUID>:GameNetDriver:<채널>" 주소를 만들어 로비 서버에 등록해 둠 (술래로 뽑히면 이 주소로 러너들이 접속)
	DECLARE_DELEGATE_OneParam(FOnEOSLoginFinished, bool /*bLoggedIn*/);

	// 로그인 시작. 이미 로그인돼 있으면 바로 성공 콜백. 실패/타임아웃이어도 콜백은 반드시 한 번 불림
	// -> 호출 쪽은 결과와 상관없이 로비 접속을 이어가면 됨 (EOS가 안 되면 로비가 기존 IP 방식으로 매치를 연결)
	void LoginEOS(FOnEOSLoginFinished OnFinished);

	bool IsEOSLoggedIn() const { return !EOSMatchAddress.IsEmpty(); }

	// 내가 술래가 됐을 때 러너들이 접속할 주소. 로그인 전이면 빈 문자열
	const FString& GetEOSMatchAddress() const { return EOSMatchAddress; }

	// ---------------- 매치 상태 ----------------
	// 로비를 떠나 매치로 갈 때 호출 (러너: 술래 PC 접속 직전 / 술래: 리슨서버 열기 직전)
	void BeginMatch(bool bAsHost);

	// ClientShowMatchEnd를 받았을 때 호출 - 이후 끊김은 "정상 종료 후 끊김"으로 취급
	void MarkMatchResultReceived() { bMatchResultReceived = true; }

	// 스스로 로비로 돌아갈 때(ReturnToLobby) 호출 - 이후 끊김 감지 안 함
	void EndMatch();

	bool IsInMatch() const { return bInMatch; }
	bool IsMatchHost() const { return bIsMatchHost; }

	// ---------------- 결과 방 캐릭터 외형 ----------------
	// 로비 결과 방에 세울 내 캐릭터 외형. 매치 중 빙의 시점에 저장해두면, 로비로 옮겨가도 남아있음
	void SetResultCharacter(USkeletalMesh* Mesh, const TArray<TObjectPtr<UMaterialInterface>>& Materials);
	USkeletalMesh* GetResultMesh() const { return ResultMesh; }
	const TArray<TObjectPtr<UMaterialInterface>>& GetResultMaterials() const { return ResultMaterials; }

	// ---------------- 로비 엔딩용 결과 ----------------
	// 판 결과 저장 (ClientShowMatchEnd / 술래 이탈 감지 시). bHostLeft=true면 문구는 로비 쪽 HostLeftResultMessage를 씀
	void SetPendingMatchResult(EMatchResultRoomType RoomType, const FText& Message, bool bHostLeft = false);
	bool HasPendingMatchResult() const { return bHasPendingMatchResult; }
	EMatchResultRoomType GetPendingResultRoomType() const { return PendingResultRoomType; }
	const FText& GetPendingResultMessage() const { return PendingResultMessage; }
	bool IsPendingResultHostLeft() const { return bPendingResultHostLeft; }
	// 로비에서 엔딩을 다 보고 Exit Game을 눌렀을 때 (또는 새 매치 시작 시) 비움
	void ClearPendingMatchResult();

	// ---------------- 맵 이동용 검은 막 ----------------
	// 판이 끝나 로비로 이동할 때, 로비 맵이 로드된 직후 ~ 로비 컨트롤러가 엔딩 위젯을 띄우기 전까지
	// 로비 화면(시작 화면 카메라)이 몇 프레임 비치는 걸 막는 화면 전체 검은 막.
	// UMG가 아니라 Slate 위젯을 GameViewportClient에 직접 붙이므로 컨트롤러 없이도 표시되고,
	// 맵 로드 때 엔진이 뷰포트 위젯을 지워도 PostLoadMapWithWorld에서 즉시 다시 붙여서 맵 이동 내내 유지됨.
	// 걷는 쪽은 로비 컨트롤러(AStartScreenController: 엔딩 위젯 준비 직후 / 시작 화면 표시 시).
	void ShowTravelBlackout();
	void HideTravelBlackout();
	bool IsTravelBlackoutActive() const { return bBlackoutRequested; }

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

	bool bHasPendingMatchResult = false;
	bool bPendingResultHostLeft = false;
	EMatchResultRoomType PendingResultRoomType{};
	FText PendingResultMessage;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> ResultMesh;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> ResultMaterials;

	// ---- [EOS] ----
	FString EOSMatchAddress;
	bool bEOSLoginInProgress = false;
	TArray<FOnEOSLoginFinished> PendingEOSLoginCallbacks;
	FDelegateHandle EOSLoginCompleteHandle;
	FTimerHandle EOSLoginTimeoutHandle;

	// Epic 서버가 응답하지 않을 때(네트워크 차단 등) 로비 접속이 무한정 막히지 않게 하는 상한
	static constexpr float EOSLoginTimeoutSeconds = 15.f;

	void HandleEOSLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error);
	void FinishEOSLogin(bool bSuccess);

	// EIK NetDriver와 같은 규칙으로 리슨 소켓 주소를 만듦 (자세한 설명은 cpp)
	static FString MakeEOSMatchAddress(const FString& ProductUserId);

	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
	FDelegateHandle PostLoadMapHandle;

	// ---- 맵 이동용 검은 막 ----
	TSharedPtr<SWidget> BlackoutWidget;
	bool bBlackoutRequested = false;
	FTimerHandle BlackoutSafetyTimer;

	// 컨트롤러가 끝내 걷지 못하는 경우(접속 실패 등) 이 시간이 지나면 자동으로 걷음
	static constexpr float BlackoutMaxSeconds = 30.f;

	void AddBlackoutToViewport();
	void RemoveBlackoutFromViewport();

	// 맵 로드 완료(새 맵 첫 프레임 전) - 막이 요청된 상태면 다시 붙임
	void HandlePostLoadMap(UWorld* LoadedWorld);

	// 접속 중이던 서버(술래 PC)와 연결이 끊김
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);

	// 술래 PC로 접속 자체가 실패함 (술래가 리슨서버를 열기 전에 꺼진 경우 등)
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

	// 이 이벤트가 내 GameInstance의 월드에서 난 건지 (PIE 창이 여러 개면 엔진 델리게이트를 같이 받음)
	bool IsMyWorld(const UWorld* World) const;
};
