// AnimNotify_ClimbStep.cpp
#include "AnimNotify_ClimbStep.h"
#include "Components/SkeletalMeshComponent.h"
#include "RunnerCharacter.h"

void UAnimNotify_ClimbStep::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
    Super::Notify(MeshComp, Animation, EventReference);

    if (!MeshComp)
    {
        return;
    }

    if (ARunnerCharacter* Runner = Cast<ARunnerCharacter>(MeshComp->GetOwner()))
    {
        Runner->NotifyClimbStep(StepHeight);
    }
}

#if WITH_EDITOR
FString UAnimNotify_ClimbStep::GetNotifyName_Implementation() const
{
    return TEXT("Climb Step");
}
#endif
