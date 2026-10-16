// AnimNotify_ClimbStep.h
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_ClimbStep.generated.h"

/**
 * 사다리 오르기(ClimbMontage)에 심어두는 AnimNotify.
 * 이 몽타주 자체는 (루프용이라) 캐릭터를 실제로 위로 이동시키지 않으므로, 발/손이 발판을 딛는
 * 타이밍에 맞춰 이 Notify를 배치해두면 그 프레임에 캐릭터를 StepHeight만큼 Z축으로 실제 이동시킨다.
 * StepHeight는 Notify 인스턴스마다(몽타주 에디터에서 배치한 각 Notify마다) 다르게 줄 수 있어서
 * 한 사이클에 여러 번 배치해 계단식으로 나눠 올리는 것도 가능함.
 */
UCLASS()
class ESCAPEROOM_API UAnimNotify_ClimbStep : public UAnimNotify
{
    GENERATED_BODY()

public:
    // 이 Notify가 호출되는 순간 캐릭터를 Z축으로 이만큼(cm) 이동시킴. 몽타주 에디터에서 이 Notify를
    // 선택하면 Details 패널에서 개별적으로 조정 가능
    UPROPERTY(EditAnywhere, Category = "Climb")
    float StepHeight = 30.f;

    virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

#if WITH_EDITOR
    virtual FString GetNotifyName_Implementation() const override;
#endif
};
