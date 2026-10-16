// EscapeRoomSessionSubsystem.h
//
// [설계 메모] 서버 이동(ClientTravel)을 해도 사라지지 않고 유지되어야 하는 클라이언트 쪽 정보 보관소.
// GameInstance는 레벨/서버를 옮겨 다녀도 게임이 켜져 있는 동안 계속 살아있고, 그 서브시스템은
// 엔진이 자동으로 생성해주므로 프로젝트 설정(GameInstance 클래스)을 바꿀 필요가 없다.
//
// 지금은 "돌아갈 로비 서버 주소" 하나만 들고 있음:
//   AStartScreenController::Client_ConnectToMatch가 매치 서버로 떠나기 직전에, 지금 접속해 있는
//   로비 서버 주소를 여기 저장 -> 매치가 끝나면 ADungeonPlayerController::ReturnToLobby가 꺼내 씀.
// 주소를 코드에 박아두지 않으므로 로컬 테스트(127.0.0.1)든 EC2(공인 IP)든 그대로 동작함.
// [AWS] 이 파일은 수정할 필요 없음 - 클라이언트가 실제로 접속했던 로비 주소를 그대로 기억하는 방식이라
//       로비가 EC2로 옮겨가도 자동으로 EC2 주소가 저장됨.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "EscapeRoomSessionSubsystem.generated.h"

UCLASS()
class ESCAPEROOM_API UEscapeRoomSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
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

	// 게임 실행 후 로비 자동 접속을 이미 시도했는지. 접속 실패로 시작 맵이 다시 열려도
	// 무한 재접속하지 않게 한 번만 시도함
	bool bTriedAutoConnect = false;

private:
	// "호스트:포트" 형식 (예: 127.0.0.1:7777). 로비를 거치지 않고 매치에 들어왔으면(PIE 등) 비어있음
	FString LobbyAddress;
};
