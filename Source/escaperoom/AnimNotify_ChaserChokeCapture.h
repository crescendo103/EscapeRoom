// AnimNotify_ChaserChokeCapture.h
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_ChaserChokeCapture.generated.h"

/**
 * 술래(Chaser)의 목조르기 몽타주(ChokeGrabMontage)에 심어두는 AnimNotify.
 * 몽타주 에디터에서 러너를 놓아주는(감옥으로 보낼) 프레임에 배치해두면, 그 프레임에 도달했을 때
 * AChaserCharacter::NotifyChokeCapture()를 호출해서 잡고 있던 러너를 감옥으로 보낸다(Capture).
 *
 * UAnimNotify_ChaserKickDoor와 같은 구조 - 이 클래스는 "몽타주의 어느 타이밍에 트리거할지"만 담당하고,
 * 실제 처리(서버 권한 체크, 대상 러너 확인, Capture)는 AChaserCharacter 쪽에서 한다.
 * 이 Notify를 배치하지 않아도 StartChokeGrab에서 걸어둔 몽타주 길이 타이머가 안전장치로 대신 보냄.
 */
UCLASS()
class ESCAPEROOM_API UAnimNotify_ChaserChokeCapture : public UAnimNotify
{
    GENERATED_BODY()

public:
    virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

#if WITH_EDITOR
    virtual FString GetNotifyName_Implementation() const override;
#endif
};
