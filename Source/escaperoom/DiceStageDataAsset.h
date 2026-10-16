// DiceStageDataAsset.h
//
// [설계 메모] 도망자 수(N)별로 "다이스 몇 개 모으면 탈출구 몇 개가 열리는지"를 단계별로 정의해두는
// 데이터 애셋. 콘텐츠 브라우저에서 Add > Miscellaneous > Data Asset > DiceStageDataAsset으로
// 애셋을 만들어 StageTableByRunnerCount를 채우고, escaperoomGameMode의 DiceStageTable 슬롯에
// 연결해서 쓴다. 실제 값은 AescaperoomGameMode::PostLogin에서 RunnerCount로 조회해
// GameState->DiceStages로 복사되고, 그 이후 진행(0->1->2단계 이동)은
// AEscapeRoomGameState::AddDice()가 담당한다. 숫자를 바꾸고 싶으면 이 애셋만 열어서 고치면 되고
// C++ 재컴파일이 필요 없다.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DiceStageDataAsset.generated.h"

// 단계 하나: 이번 단계에서 모아야 하는 다이스 개수 + 클리어 시 열리는 탈출구 개수
USTRUCT(BlueprintType)
struct FDiceStage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice Stage")
	int32 DiceRequired = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice Stage")
	int32 ExitsToSpawn = 0;
};

// TMap의 Value 자리에 TArray를 바로 못 넣어서(컨테이너 중첩은 UHT가 지원 안 함) 감싸는 래퍼
USTRUCT(BlueprintType)
struct FDiceStageArray
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice Stage")
	TArray<FDiceStage> Stages;
};

UCLASS(BlueprintType)
class ESCAPEROOM_API UDiceStageDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// 도망자 수(Key) -> 그 인원용 단계 배열(Value). 애셋 디테일 패널에서 직접 채우면 됨
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice Stage")
	TMap<int32, FDiceStageArray> StageTableByRunnerCount;

	// RunnerCount에 해당하는 단계 배열을 찾아 반환. 표에 없으면 nullptr
	const TArray<FDiceStage>* FindStagesForRunnerCount(int32 RunnerCount) const
	{
		if (const FDiceStageArray* Found = StageTableByRunnerCount.Find(RunnerCount))
		{
			return &Found->Stages;
		}
		return nullptr;
	}
};
