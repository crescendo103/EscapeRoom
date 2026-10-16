// AnimNotify_ChaserKickDoor.cpp
#include "AnimNotify_ChaserKickDoor.h"
#include "Components/SkeletalMeshComponent.h"
#include "ChaserCharacter.h"

void UAnimNotify_ChaserKickDoor::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
    Super::Notify(MeshComp, Animation, EventReference);

    if (!MeshComp)
    {
        return;
    }

    if (AChaserCharacter* Chaser = Cast<AChaserCharacter>(MeshComp->GetOwner()))
    {
        Chaser->NotifyKickDoorHit();
    }
}

#if WITH_EDITOR
FString UAnimNotify_ChaserKickDoor::GetNotifyName_Implementation() const
{
    return TEXT("Chaser Kick Door");
}
#endif
