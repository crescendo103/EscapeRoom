// AnimNotify_ChaserKickDoor.h
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_ChaserKickDoor.generated.h"

/**
 * 술래(Chaser)의 KickDoor(문 부수기) 애니메이션에 심어두는 AnimNotify.
 * 애니메이션 에디터에서 발로 문을 차는(닿는) 프레임에 이 Notify를 배치해두면,
 * 재생 중 그 프레임에 도달했을 때 애니메이션을 재생 중인 스켈레탈 메시의 Owner를
 * AChaserCharacter로 캐스팅해서 NotifyKickDoorHit()을 호출한다.
 *
 * 실제 문 파괴(서버 RPC 전송 및 DoorActor::CanBreak/Break를 통한 최종 검증)는
 * AChaserCharacter::NotifyKickDoorHit() 쪽에서 처리하며, 이 클래스는 "애니메이션(몽타주)의
 * 어느 타이밍에 그것을 트리거할지"만 담당한다. AChaserCharacter::Interact()는
 * 문을 찾는 즉시 파괴를 요청하지 않고, KickDoorMontage 재생만 시작시킨 뒤 대상 문을
 * PendingKickDoorTarget에 기억해뒀다가 이 Notify가 불릴 때 실제로 요청한다.
 */
UCLASS()
class ESCAPEROOM_API UAnimNotify_ChaserKickDoor : public UAnimNotify
{
    GENERATED_BODY()

public:
    virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

#if WITH_EDITOR
    virtual FString GetNotifyName_Implementation() const override;
#endif
};
