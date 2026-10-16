// LampActor.cpp
#include "LampActor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/Pawn.h"
#include "RunnerCharacter.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Components/AudioComponent.h"

ALampActor::ALampActor()
{
    PrimaryActorTick.bCanEverTick = true; // Timeline 갱신을 위해 필요

    LightMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LightMesh"));
    RootComponent = LightMesh;
    // 술래 부감(목 늘리기) 중 천장이 투명해질 때 램프 메시도 같이 숨김 (ChaserCharacter::CacheAllCeilings 참고)
    LightMesh->ComponentTags.Add(TEXT("HideInOverhead"));

    PointLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PointLight"));
    PointLight->SetupAttachment(RootComponent);
    PointLight->SetIntensity(0.0f);

    TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
    TriggerBox->SetupAttachment(RootComponent);
    TriggerBox->SetCollisionProfileName(TEXT("Trigger"));
    TriggerBox->SetGenerateOverlapEvents(true);

    NoiseAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("NoiseAudio"));
    NoiseAudio->SetupAttachment(RootComponent);
    NoiseAudio->bAutoActivate = false;
}

void ALampActor::BeginPlay()
{
    Super::BeginPlay();

    // 방(ARoomActor)이 먼저 꺼버린 램프면 초기화 건너뜀
    if (bLampDisabled)
    {
        return;
    }

    // 시작은 꺼진 상태 머티리얼로
    if (OffMaterialInstance)
    {
        LightMesh->SetMaterial(EmissiveMaterialSlot, OffMaterialInstance);
    }
    bIsMaterialOn = false;

    // Motion-sensing lamp: idle at the base brightness instead of fully dark
    if (PointLight)
    {
        PointLight->SetIntensity(ConstantLightIntensity);
    }

    TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ALampActor::OnOverlapBegin);

    if (NoiseAudio)
    {
        NoiseAudio->OnAudioFinished.AddDynamic(this, &ALampActor::HandleNoiseFinished);
    }

    // Timeline 설정
    if (BrightnessCurve)
    {
        BrightnessInterpFunction.BindUFunction(this, FName("HandleBrightnessUpdate"));
        BrightnessTimeline.AddInterpFloat(BrightnessCurve, BrightnessInterpFunction);
        BrightnessTimeline.SetLooping(bLoopBrightnessCurve);
    }
}

void ALampActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    BrightnessTimeline.TickTimeline(DeltaTime);

    // 웅크린 러너가 트리거 안에 있을 때만 일정 간격으로 일어섰는지 확인
    if (bWatchingCrouchedRunner)
    {
        CrouchRecheckTimer += DeltaTime;
        if (CrouchRecheckTimer >= CrouchRecheckInterval)
        {
            CrouchRecheckTimer = 0.f;
            CheckCrouchedRunnersInside();
        }
    }
}

void ALampActor::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    bool bCrouching = false;
    if (!IsValidTriggerPawn(OtherActor, bCrouching))
    {
        return; // 소품/술래 등은 무시
    }

    if (bCrouching)
    {
        // 웅크린 채 들어오면 발동하지 않고, 트리거 안에서 일어서는지 지켜봄
        bWatchingCrouchedRunner = true;
        CrouchRecheckTimer = 0.f;
        return;
    }

    ActivateByRunner();
}

bool ALampActor::IsValidTriggerPawn(const AActor* Actor, bool& bOutCrouching) const
{
    bOutCrouching = false;
    if (!Actor || Actor == this)
    {
        return false;
    }

    // 러너 캐릭터만 반응. 문/캐비닛/주사위/출구도 AMiscActor(APawn)라서 Pawn 판정만 하면
    // 근처 소품이 "일어선 러너"로 오인되어 불이 켜지는 문제가 있었음.
    // 술래 태그 검사는 혹시 모를 상속 구조 변경 대비로 유지
    const ARunnerCharacter* Runner = Cast<ARunnerCharacter>(Actor);
    if (!Runner || Runner->ActorHasTag(ChaserTag))
    {
        return false;
    }

    bOutCrouching = Runner->IsCrouching();
    return true;
}

void ALampActor::ActivateByRunner()
{
    // 이미 켜져 있는 동안(꺼짐 타이머가 도는 중)에는 다시 들어와도 아무것도 하지 않음.
    // - 깜빡임 커브(BrightnessTimeline)를 처음부터 다시 재생하지 않음
    // - 꺼짐 타이머도 연장하지 않음 -> 처음 켜진 시점 기준으로 LitDuration 뒤에 무조건 꺼짐
    if (GetWorldTimerManager().IsTimerActive(TurnOffTimerHandle))
    {
        return;
    }

    // [Debug] who triggered this lamp (to track lamps turning on for the chaser)
    TArray<AActor*> DebugOverlaps;
    if (TriggerBox)
    {
        TriggerBox->GetOverlappingActors(DebugOverlaps);
    }
    for (const AActor* A : DebugOverlaps)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LampDebug] %s activated. Overlapping: %s (Class=%s, RunnerCast=%d, ChaserTag=%d)"),
            *GetName(), *GetNameSafe(A), *GetNameSafe(A ? A->GetClass() : nullptr),
            Cast<ARunnerCharacter>(A) != nullptr, A ? (int32)A->ActorHasTag(ChaserTag) : 0);
    }

    TurnOnLight();
    StartNoise(); // 이미 재생 중이면 무시 (재진입 시 처음부터 다시 틀지 않음)

    GetWorldTimerManager().ClearTimer(TurnOffTimerHandle);
    GetWorldTimerManager().SetTimer(TurnOffTimerHandle, this, &ALampActor::TurnOffLight, LitDuration, false);
}

void ALampActor::CheckCrouchedRunnersInside()
{
    TArray<AActor*> Overlapping;
    TriggerBox->GetOverlappingActors(Overlapping, ARunnerCharacter::StaticClass());

    bool bAnyCrouchedInside = false;
    for (AActor* Actor : Overlapping)
    {
        bool bCrouching = false;
        if (!IsValidTriggerPawn(Actor, bCrouching))
        {
            continue;
        }

        if (bCrouching)
        {
            bAnyCrouchedInside = true;
        }
        else
        {
            // 트리거 안에서 일어선 러너 -> 발동
            ActivateByRunner();
            bWatchingCrouchedRunner = false;
            return;
        }
    }

    // 웅크린 러너가 다 나갔으면 감시 종료
    bWatchingCrouchedRunner = bAnyCrouchedInside;
}

void ALampActor::TurnOnLight()
{
    if (BrightnessCurve)
    {
        BrightnessTimeline.PlayFromStart();
    }
    else
    {
        // 커브가 없으면 그냥 즉시 On 상태로
        if (OnMaterialInstance)
        {
            LightMesh->SetMaterial(EmissiveMaterialSlot, OnMaterialInstance);
        }
        if (!bIsMaterialOn)
        {
            PlayLampSound(TurnOnSound);
        }
        bIsMaterialOn = true;
        PointLight->SetIntensity(5000.0f);
    }
}

void ALampActor::TurnOffLight()
{
    BrightnessTimeline.Stop();
    StopNoise();

    // 켜져 있던 상태에서 꺼질 때만 꺼짐 소리 (이미 꺼진 상태면 중복 재생 안 함)
    if (bIsMaterialOn)
    {
        PlayLampSound(TurnOffSound);
    }

    if (OffMaterialInstance)
    {
        LightMesh->SetMaterial(EmissiveMaterialSlot, OffMaterialInstance);
    }
    bIsMaterialOn = false;

    // Back to the base brightness (not fully dark)
    PointLight->SetIntensity(ConstantLightIntensity);
}

void ALampActor::HandleBrightnessUpdate(float Value)
{
    PointLight->SetIntensity(Value);

    // 밝기 값이 임계값을 넘는지에 따라 On/Off 머티리얼 전환 (상태가 바뀔 때만 SetMaterial 호출)
    const bool bShouldBeOn = (Value >= MaterialOnThreshold);
    if (bShouldBeOn != bIsMaterialOn)
    {
        bIsMaterialOn = bShouldBeOn;
        if (bIsMaterialOn)
        {
            if (OnMaterialInstance)
            {
                LightMesh->SetMaterial(EmissiveMaterialSlot, OnMaterialInstance);
            }
            PlayLampSound(TurnOnSound);   // 깜빡임: 꺼짐 -> 켜짐 순간
        }
        else
        {
            if (OffMaterialInstance)
            {
                LightMesh->SetMaterial(EmissiveMaterialSlot, OffMaterialInstance);
            }
            PlayLampSound(TurnOffSound);  // 깜빡임: 켜짐 -> 꺼짐 순간
        }
    }
}

void ALampActor::PlayLampSound(USoundBase* Sound)
{
    if (!Sound || !GetWorld())
    {
        return;
    }

    const float Now = GetWorld()->GetTimeSeconds();
    if (MinSoundInterval > 0.f && (Now - LastLampSoundTime) < MinSoundInterval)
    {
        return;
    }
    LastLampSoundTime = Now;

    UGameplayStatics::PlaySoundAtLocation(this, Sound, PointLight->GetComponentLocation(),
        FRotator::ZeroRotator, LampSoundVolume, 1.f, 0.f, LampSoundAttenuation);
}

void ALampActor::StartNoise()
{
    if (bNoiseActive || !NoiseAudio || !NoiseSound)
    {
        return;
    }
    bNoiseActive = true;

    NoiseAudio->SetSound(NoiseSound);
    NoiseAudio->AttenuationSettings = NoiseAttenuation ? NoiseAttenuation : LampSoundAttenuation;
    NoiseAudio->SetVolumeMultiplier(NoiseVolume);

    if (NoiseFadeInTime > 0.f)
    {
        NoiseAudio->FadeIn(NoiseFadeInTime, 1.f);
    }
    else
    {
        NoiseAudio->Play();
    }
}

void ALampActor::StopNoise()
{
    if (!bNoiseActive || !NoiseAudio)
    {
        return;
    }
    bNoiseActive = false; // 먼저 내려야 페이드아웃 종료 시 HandleNoiseFinished에서 다시 재생하지 않음

    if (NoiseFadeOutTime > 0.f)
    {
        NoiseAudio->FadeOut(NoiseFadeOutTime, 0.f);
    }
    else
    {
        NoiseAudio->Stop();
    }
}

void ALampActor::HandleNoiseFinished()
{
    // 루프 플래그가 없는 사운드가 끝났는데 램프가 아직 켜져 있으면 다시 재생
    if (bNoiseActive && NoiseAudio)
    {
        NoiseAudio->Play();
    }
}

void ALampActor::SetConstantLight()
{
    // bLampDisabled also makes BeginPlay skip the motion-sensing setup (overlap binding, timeline)
    bLampDisabled = true;

    // Visible, but no collision / trigger
    SetActorHiddenInGame(false);
    SetActorEnableCollision(false);
    if (TriggerBox)
    {
        TriggerBox->SetGenerateOverlapEvents(false);
        TriggerBox->OnComponentBeginOverlap.RemoveAll(this);
    }

    // No sensing logic / sounds
    SetActorTickEnabled(false);
    BrightnessTimeline.Stop();
    GetWorldTimerManager().ClearTimer(TurnOffTimerHandle);
    bWatchingCrouchedRunner = false;
    bNoiseActive = false;
    if (NoiseAudio)
    {
        NoiseAudio->Stop();
    }

    // Always on: fixed brightness + emissive "on" material
    if (PointLight)
    {
        PointLight->SetVisibility(true);
        PointLight->SetIntensity(ConstantLightIntensity);
    }
    if (OnMaterialInstance && LightMesh)
    {
        LightMesh->SetMaterial(EmissiveMaterialSlot, OnMaterialInstance);
    }
    bIsMaterialOn = true;
}

void ALampActor::DisableLamp()
{
    bLampDisabled = true;

    SetActorHiddenInGame(true);
    SetActorEnableCollision(false);
    SetActorTickEnabled(false);
    GetWorldTimerManager().ClearTimer(TurnOffTimerHandle);

    if (PointLight)
    {
        PointLight->SetIntensity(0.f);
        PointLight->SetVisibility(false);
    }
    if (TriggerBox)
    {
        TriggerBox->SetGenerateOverlapEvents(false);
    }
    if (NoiseAudio)
    {
        NoiseAudio->Stop();
    }
}
