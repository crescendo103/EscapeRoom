// Interactable.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interactable.generated.h"

UINTERFACE(BlueprintType)
class ESCAPEROOM_API UInteractable : public UInterface
{
    GENERATED_BODY()
};

/**
 * 상호작용 가능한 오브젝트(문, 장롱 등)가 공통으로 구현하는 인터페이스.
 * 캐릭터의 Interact()는 라인 트레이스로 이 인터페이스를 구현한 액터를 찾아
 * 필요한 홀드 시간(GetInteractHoldTime)만큼 키가 눌려있으면
 * 서버 RPC를 통해 서버 권한으로 OnInteract를 호출한다.
 */
class ESCAPEROOM_API IInteractable
{
    GENERATED_BODY()

public:
    // 지금 이 폰이 이 오브젝트와 상호작용할 수 있는지 (프롬프트 표시 및 서버 검증용)
    UFUNCTION(BlueprintNativeEvent, Category = "Interaction")
    bool CanInteract(APawn* Interactor);

    // 실제 상호작용 처리. 반드시 서버(권한 있는 쪽)에서만 의미있는 상태 변경을 해야 하며,
    // 구현부는 HasAuthority() 체크로 스스로를 보호해야 한다.
    UFUNCTION(BlueprintNativeEvent, Category = "Interaction")
    void OnInteract(APawn* Interactor);

    // 상호작용 키를 몇 초 눌러야 OnInteract가 발동되는지. 기본값 0 = 즉시 발동(문 등 기존 오브젝트).
    UFUNCTION(BlueprintNativeEvent, Category = "Interaction")
    float GetInteractHoldTime(APawn* Interactor);

    // 홀드 중일 때 MatchWidget에 표시할 문구 (예: "캐비닛 열기", "문 부수기"). 홀드 시간이 0(즉시 발동)인
    // 오브젝트는 애초에 홀드 게이지 자체가 안 뜨므로 이 값도 쓰이지 않음. 기본 구현(AMiscActor)은
    // InteractionText 컴포넌트(UInteractionTextComponent)의 HoldText를 그대로 반환함.
    UFUNCTION(BlueprintNativeEvent, Category = "Interaction")
    FText GetInteractionHoldText(APawn* Interactor);

    // 상호작용 키를 홀드하는 동안 호출됨 (true=홀드 시작/유지, false=취소 또는 홀드 완료).
    // 뚜껑이 열리는 미리보기 애니메이션처럼, 완료되기 전에도 진행 상황을 보여주고
    // 취소되면 원상복구해야 하는 연출에 사용. 기본 구현은 아무것도 하지 않아도 됨(옵션).
    UFUNCTION(BlueprintNativeEvent, Category = "Interaction")
    void OnInteractHoldUpdate(APawn* Interactor, bool bHolding);
};
