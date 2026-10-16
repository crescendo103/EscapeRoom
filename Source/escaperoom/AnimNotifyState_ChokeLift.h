// AnimNotifyState_ChokeLift.h
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_ChokeLift.generated.h"

class UCurveFloat;

/**
 * 러너의 목졸림 몽타주(ARunnerCharacter::BeingChokedMontage)에 심어두는 AnimNotifyState.
 * 이 구간(시작~끝) 동안 러너 캡슐(액터)을 LiftHeight만큼 월드 Z로 천천히 들어올린다.
 * 러너 자신의 몽타주라 MeshComp의 Owner가 곧 러너 → 술래 정보 없이 스스로 올라감.
 * 캡슐이 올라가므로 러너 본인 1인칭 카메라도 같이 올라감.
 *
 * 구간이 끝나면 최종 높이를 그대로 유지함(감옥으로 보내질 때까지). 다시 내려오게 하고 싶으면
 * LiftHeight를 음수로 준 Notify를 하나 더 배치하면 됨(시작값을 "현재 높이" 기준으로 잡으므로 이어 붙이기 가능).
 *
 * [주의] NotifyState UObject는 몽타주 애셋에 하나만 존재하고 이 몽타주를 재생하는 모든 메시가
 * 공유하므로, 진행 상태(경과시간 등)를 여기 멤버에 저장하면 안 됨 → 전부 ARunnerCharacter에 저장
 */
UCLASS()
class ESCAPEROOM_API UAnimNotifyState_ChokeLift : public UAnimNotifyState
{
    GENERATED_BODY()

public:
    // 이 구간이 끝났을 때 추가로 올라가 있을 높이 (cm). 몽타주 에디터에서 Notify별로 조정 가능
    UPROPERTY(EditAnywhere, Category = "Choke")
    float LiftHeight = 60.f;

    // 이징 강도 (1 = 선형, 2 이상이면 천천히 시작해서 천천히 끝남). LiftCurve를 지정하면 무시됨
    UPROPERTY(EditAnywhere, Category = "Choke", meta = (ClampMin = "1.0"))
    float EaseExponent = 2.f;

    // (선택) 올라가는 모양을 직접 그린 커브. 콘텐츠 브라우저 → Miscellaneous → Curve → CurveFloat로 생성.
    //  - X축: 0~1 = 이 노티파이 구간의 진행률 (0 = 구간 시작, 1 = 구간 끝) → 구간 길이를 바꿔도 다시 안 그려도 됨
    //  - Y축: 0~1 = LiftHeight 중 몇 %까지 올라가 있을지 (1을 넘기면 LiftHeight보다 더 올라감, 음수면 아래로)
    // 지정하면 EaseExponent 대신 이 커브 모양대로 올라가고, 비워두면 EaseExponent 방식 사용.
    // 전체 속도(더 천천히)는 커브가 아니라 구간 길이로 조절함 - 커브는 "모양"만 바꿈
    UPROPERTY(EditAnywhere, Category = "Choke")
    TObjectPtr<UCurveFloat> LiftCurve;

    virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
    virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;
    virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

#if WITH_EDITOR
    virtual FString GetNotifyName_Implementation() const override;
#endif
};
