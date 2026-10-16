// [설계 메모] 매치 서버(GameLV) 전용 PlayerState.
// 로비의 AEscapeRoomPlayerState(방 ID/레디)와는 목적이 달라서 분리함.
//
// 캐릭터(ARunnerCharacter::bIsCaught 등)에도 같은 정보가 있지만, 캐릭터(Pawn)는 거리가 멀면
// 다른 클라이언트에 리플리케이션이 끊길(Net Relevancy) 수 있어서 UI가 믿고 읽기 어렵다.
// PlayerState는 항상 모든 클라이언트에 복제되므로 MatchWidget의 플레이어 목록은 여기 값만 읽는다.
//
// 값은 전부 서버에서만 세팅:
//   MatchRole - AescaperoomGameMode::GetDefaultPawnClassForController (폰 클래스 정할 때)
//   bIsInJail - ARunnerCharacter::SetCaught (감옥 구역 진입/탈출 시)
//   bHasEscaped - ARunnerCharacter::SetEscaped (탈출구 홀드 완료 시, 한 번 true가 되면 되돌아가지 않음)
//   RunnerVariantIndex - AescaperoomGameMode::GetDefaultPawnClassForController (러너 외형 배정, 한 번 정하면 고정)
// 이름은 엔진 기본 PlayerName을 그대로 씀 (AescaperoomGameMode::InitNewPlayer에서 ?PName= 으로 세팅)

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "MatchPlayerState.generated.h"

UENUM(BlueprintType)
enum class EMatchPlayerRole : uint8
{
	Unassigned, // 아직 폰이 안 정해짐 (접속 직후 잠깐) - UI에 표시하지 않음
	Chaser,
	Runner
};

UCLASS()
class ESCAPEROOM_API AMatchPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	EMatchPlayerRole GetMatchRole() const { return MatchRole; }
	bool IsChaser() const { return MatchRole == EMatchPlayerRole::Chaser; }
	bool IsRunner() const { return MatchRole == EMatchPlayerRole::Runner; }
	bool IsInJail() const { return bIsInJail; }
	bool HasEscaped() const { return bHasEscaped; }
	int32 GetRunnerVariantIndex() const { return RunnerVariantIndex; }
	bool HasRunnerVariant() const { return RunnerVariantIndex != INDEX_NONE; }

	// 서버 전용
	void SetMatchRole(EMatchPlayerRole NewRole);
	void SetInJail(bool bNewInJail);
	void SetEscaped(bool bNewEscaped);
	void SetRunnerVariantIndex(int32 NewIndex);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	EMatchPlayerRole MatchRole = EMatchPlayerRole::Unassigned;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	bool bIsInJail = false;

	// 탈출 성공 여부. 러너 캐릭터(폰)는 탈출 후 숨겨지고 거리 때문에 복제가 끊길 수도 있어서,
	// 플레이어 목록 UI(탈출 이미지)와 관전 대상 선정은 전부 이 값을 기준으로 판단함
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	bool bHasEscaped = false;

	// 몇 번째 러너 캐릭터(GameMode의 RunnerPawnClasses 인덱스)를 받았는지. 술래/미배정이면 INDEX_NONE.
	// GetDefaultPawnClassForController가 한 플레이어에 대해 여러 번 불려도 같은 캐릭터가 나오도록 처음 값을 고정해두는 용도이고,
	// 모든 클라에 복제되므로 나중에 플레이어 목록 UI에 캐릭터 초상화를 띄울 때도 이 값을 쓰면 됨
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	int32 RunnerVariantIndex = INDEX_NONE;
};
