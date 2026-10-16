// AnimNotify_ChaserChokeCapture.cpp
#include "AnimNotify_ChaserChokeCapture.h"
#include "Components/SkeletalMeshComponent.h"
#include "ChaserCharacter.h"

void UAnimNotify_ChaserChokeCapture::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
    Super::Notify(MeshComp, Animation, EventReference);

    if (!MeshComp)
    {
        return;
    }

    // 에디터 몽타주 프리뷰에서는 Owner가 AChaserCharacter가 아니라서 캐스트가 실패 → 그냥 무시됨
    if (AChaserCharacter* Chaser = Cast<AChaserCharacter>(MeshComp->GetOwner()))
    {
        Chaser->NotifyChokeCapture();
    }
}

#if WITH_EDITOR
FString UAnimNotify_ChaserChokeCapture::GetNotifyName_Implementation() const
{
    return TEXT("Choke Capture");
}
#endif
