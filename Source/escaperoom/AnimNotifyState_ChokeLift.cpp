// AnimNotifyState_ChokeLift.cpp
#include "AnimNotifyState_ChokeLift.h"
#include "Components/SkeletalMeshComponent.h"
#include "RunnerCharacter.h"

namespace
{
    // 에디터 몽타주 프리뷰에서는 Owner가 ARunnerCharacter가 아니라서 캐스트가 실패 → 그냥 무시됨
    ARunnerCharacter* GetChokeLiftRunner(USkeletalMeshComponent* MeshComp)
    {
        return MeshComp ? Cast<ARunnerCharacter>(MeshComp->GetOwner()) : nullptr;
    }
}

void UAnimNotifyState_ChokeLift::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
    Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

    if (ARunnerCharacter* Runner = GetChokeLiftRunner(MeshComp))
    {
        Runner->BeginChokeLift(LiftHeight, TotalDuration, EaseExponent, LiftCurve);
    }
}

void UAnimNotifyState_ChokeLift::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
    Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);

    if (ARunnerCharacter* Runner = GetChokeLiftRunner(MeshComp))
    {
        Runner->UpdateChokeLift(FrameDeltaTime);
    }
}

void UAnimNotifyState_ChokeLift::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
    Super::NotifyEnd(MeshComp, Animation, EventReference);

    if (ARunnerCharacter* Runner = GetChokeLiftRunner(MeshComp))
    {
        Runner->EndChokeLift();
    }
}

#if WITH_EDITOR
FString UAnimNotifyState_ChokeLift::GetNotifyName_Implementation() const
{
    return TEXT("Choke Lift");
}
#endif
