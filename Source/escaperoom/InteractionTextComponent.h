// InteractionTextComponent.h
//
// [설계 메모] 상호작용 관련 표시 문구만 들고 있는 순수 데이터 컴포넌트. 렌더링도, 월드 배치도 하지
// 않고 그냥 FText 프로퍼티만 가지고 있어서, 각 상호작용 오브젝트(Door/Cabinet/Dice/Exit) 블루프린트가
// 자기 컴포넌트 계층에서 이 컴포넌트를 선택해 문구를 직접(수기로) 채워넣으면 됨.
//
// PromptText는 범위 진입 시 MatchWidget(HoldActionText 자리)에, HoldText는 홀드 중 MatchWidget에 표시됨
// (IInteractable::GetInteractionHoldText 경유). 월드 스페이스 "Press E" 위젯은 제거됨.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractionTextComponent.generated.h"

UCLASS(ClassGroup = (Interaction), meta = (BlueprintSpawnableComponent))
class ESCAPEROOM_API UInteractionTextComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// 상호작용이 가능해진 시점(오버랩 등)에 보여줄 문구 - 도망자용/술래용을 따로 둠(같은 대상이라도
	// 역할에 따라 할 수 있는 상호작용이 다르므로: 예) 문은 러너에게 "문 열기", 술래에게 "문 부수기").
	// Press E 프롬프트 위젯에 실제로 연결하는 작업은 나중에 별도로 진행 예정 - 지금은 데이터만 들고 있음.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	FText RunnerPromptText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	FText ChaserPromptText;

	// 홀드 중일 때 MatchWidget에 표시할 문구 - 위와 동일한 이유로 역할별로 분리
	// (예: 러너는 "문 여는 중...", 술래는 "문 부수는 중...").
	// PromptText와는 다른 시점(홀드 진행 중)에 쓰이므로 별도 필드로 분리함.
	// 블루프린트 디테일 패널에서 오브젝트별로 직접 입력.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	FText RunnerHoldText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	FText ChaserHoldText;
};
