// LampActor.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/TimelineComponent.h"
#include "LampActor.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UCurveFloat;
class USoundBase;
class USoundAttenuation;
class UAudioComponent;

UCLASS()
class ESCAPEROOM_API ALampActor : public AActor
{
    GENERATED_BODY()

public:
    ALampActor();

    // ARoomActor가 방 시드로 굴려서 호출: 메시/라이트 숨기고 트리거/콜리전/틱/소리 전부 끔
    void DisableLamp();

    // Called by ARoomActor (same seed on every machine): keep the lamp visible as a normal, always-on light.
    // Motion-sensing is turned off: trigger/overlap, collision, timers, timeline, on/off sounds and noise.
    void SetConstantLight();

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    // 감지 범위 (바운딩 박스)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lamp")
    UBoxComponent* TriggerBox;

    // 조명 기구 메시
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lamp")
    UStaticMeshComponent* LightMesh;

    // 실제로 방을 비추는 포인트라이트
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lamp")
    UPointLightComponent* PointLight;

    // Element 1 슬롯 인덱스 (전구 부분 머티리얼 슬롯)
    UPROPERTY(EditAnywhere, Category = "Lamp")
    int32 EmissiveMaterialSlot = 1;

    // 불 켜졌을 때 쓸 머티리얼 인스턴스
    UPROPERTY(EditAnywhere, Category = "Lamp")
    UMaterialInterface* OnMaterialInstance;

    // 불 꺼졌을 때 쓸 머티리얼 인스턴스
    UPROPERTY(EditAnywhere, Category = "Lamp")
    UMaterialInterface* OffMaterialInstance;

    // 켜져 있는 동안 밝기를 조절할 커브 (예: 0~5000 사이 값)
    UPROPERTY(EditAnywhere, Category = "Lamp")
    UCurveFloat* BrightnessCurve;

    // 커브 값이 이 값 이상이면 On 머티리얼, 미만이면 Off 머티리얼로 전환
    UPROPERTY(EditAnywhere, Category = "Lamp")
    float MaterialOnThreshold = 100.0f;

    // 커브를 반복 재생할지 (깜빡임 효과 등에 사용)
    UPROPERTY(EditAnywhere, Category = "Lamp")
    bool bLoopBrightnessCurve = false;

    // 켜져 있는 지속 시간(초)
    UPROPERTY(EditAnywhere, Category = "Lamp")
    float LitDuration = 5.0f;

    // Base (idle) brightness. Normal always-on lamps (SetConstantLight) stay at this value,
    // and motion-sensing lamps also stay at this value while idle (instead of 0) and return to it after turning off
    UPROPERTY(EditAnywhere, Category = "Lamp|Constant", meta = (ClampMin = "0.0"))
    float ConstantLightIntensity = 1500.0f;

    // 이 태그가 붙은 폰(술래)은 랜턴을 발동시키지 않음. ChaserCharacter 생성자의 Tags.Add("Chaser")와 동일한 값
    UPROPERTY(EditAnywhere, Category = "Lamp")
    FName ChaserTag = TEXT("Chaser");

    // ---- 사운드 ----
    // 램프가 켜지는 순간(꺼짐 -> 켜짐) 재생. 타임라인 깜빡임 중에도 켜질 때마다 재생됨
    UPROPERTY(EditAnywhere, Category = "Lamp|Sound")
    USoundBase* TurnOnSound = nullptr;

    // 램프가 꺼지는 순간(켜짐 -> 꺼짐) 재생. 깜빡임 중 꺼질 때 + LitDuration 종료 시
    UPROPERTY(EditAnywhere, Category = "Lamp|Sound")
    USoundBase* TurnOffSound = nullptr;

    // 들리는 범위 (SA_Default 또는 랜턴 전용 SoundAttenuation 지정)
    UPROPERTY(EditAnywhere, Category = "Lamp|Sound")
    USoundAttenuation* LampSoundAttenuation = nullptr;

    UPROPERTY(EditAnywhere, Category = "Lamp|Sound", meta = (ClampMin = "0.0"))
    float LampSoundVolume = 1.0f;

    // 소리 사이 최소 간격(초). 커브가 아주 빠르게 깜빡일 때 소리가 겹치는 것 방지. 0이면 제한 없음
    UPROPERTY(EditAnywhere, Category = "Lamp|Sound", meta = (ClampMin = "0.0"))
    float MinSoundInterval = 0.05f;

    // ---- 배경 노이즈 (러너 감지로 켜질 때 1번 시작 -> 램프가 꺼질 때까지 루프) ----
    // 깜빡임(타임라인)과 무관하게 계속 재생됨. 사운드 에셋에 Looping이 꺼져 있어도 끝나면 다시 재생해서 루프 유지
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lamp|Noise")
    UAudioComponent* NoiseAudio;

    UPROPERTY(EditAnywhere, Category = "Lamp|Noise")
    USoundBase* NoiseSound = nullptr;

    // 비워두면 LampSoundAttenuation을 그대로 사용
    UPROPERTY(EditAnywhere, Category = "Lamp|Noise")
    USoundAttenuation* NoiseAttenuation = nullptr;

    UPROPERTY(EditAnywhere, Category = "Lamp|Noise", meta = (ClampMin = "0.0"))
    float NoiseVolume = 0.5f;

    // 시작/종료 시 페이드 시간(초). 0이면 즉시
    UPROPERTY(EditAnywhere, Category = "Lamp|Noise", meta = (ClampMin = "0.0"))
    float NoiseFadeInTime = 0.2f;

    UPROPERTY(EditAnywhere, Category = "Lamp|Noise", meta = (ClampMin = "0.0"))
    float NoiseFadeOutTime = 0.3f;

    UFUNCTION()
    void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
        bool bFromSweep, const FHitResult& SweepResult);

    void TurnOnLight();
    void TurnOffLight();

    // 러너 감지로 램프 발동 (켜기 + 노이즈 + 꺼짐 타이머 재시작)
    void ActivateByRunner();

    // 웅크린 채 트리거 안에 들어온 러너가 일어섰는지 주기적으로 확인 (트리거 안에 웅크린 러너가 있을 때만)
    void CheckCrouchedRunnersInside();

    // 웅크린 러너는 램프를 발동시키지 않음. 판정 대상(폰이고 술래가 아님)인지 + 웅크리지 않았는지
    bool IsValidTriggerPawn(const AActor* Actor, bool& bOutCrouching) const;

    // 웅크린 러너가 트리거 안에 있는 동안 확인 주기(초)
    UPROPERTY(EditAnywhere, Category = "Lamp", meta = (ClampMin = "0.02"))
    float CrouchRecheckInterval = 0.1f;

    // DisableLamp()로 꺼진 램프 (BeginPlay가 나중에 돌아도 다시 켜지지 않게)
    bool bLampDisabled = false;

    // 켜짐/꺼짐 상태가 실제로 바뀌었을 때만 호출 (로컬 재생 - 각 머신에서 오버랩/타임라인이 따로 돌기 때문에 RPC 불필요)
    void PlayLampSound(USoundBase* Sound);

    void StartNoise();
    void StopNoise();

    // 루프 플래그가 없는 사운드도 램프가 켜져 있는 동안 계속 반복되도록 재시작
    UFUNCTION()
    void HandleNoiseFinished();

    UFUNCTION()
    void HandleBrightnessUpdate(float Value);

private:
    FTimeline BrightnessTimeline;
    FOnTimelineFloat BrightnessInterpFunction;

    FTimerHandle TurnOffTimerHandle;

    // 현재 On 머티리얼 상태인지 (매 틱마다 SetMaterial 호출하지 않기 위한 캐시)
    bool bIsMaterialOn = false;

    // 마지막으로 소리를 재생한 시각 (MinSoundInterval 판정용)
    float LastLampSoundTime = -1000.f;

    // 노이즈가 재생 중이어야 하는 상태인지 (러너 감지 ~ TurnOffLight 사이)
    bool bNoiseActive = false;

    // 웅크린 러너가 트리거 안에 있어 재확인 중인지
    bool bWatchingCrouchedRunner = false;
    float CrouchRecheckTimer = 0.f;
};
