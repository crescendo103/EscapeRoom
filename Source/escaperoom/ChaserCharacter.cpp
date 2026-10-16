// ChaserCharacter.cpp
#include "ChaserCharacter.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/LocalPlayer.h"
#include "Net/UnrealNetwork.h"
#include "Engine/OverlapResult.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraComponent.h"
#include "DoorActor.h"
#include "CabinetActor.h"
#include "Interactable.h"
#include "WFCGridManager.h"
#include "RoomActor.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RunnerCharacter.h"
#include "DungeonPlayerController.h"
#include "MatchWidget.h"
#include "GameFramework/GameStateBase.h"
#include "EngineUtils.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundAttenuation.h"

AChaserCharacter::AChaserCharacter()
{
    // CabinetActor::CanInteract_Implementation 등이 ActorHasTag(ChaserTag)로 술래를 구분하므로
    // 반드시 이 태그를 붙여줘야 함 (기본값은 CabinetActor::ChaserTag = "Chaser")
    Tags.Add(TEXT("Chaser"));

    // 목조르기 연출 때 러너가 스냅될 위치 마커. 손 소켓에 붙여두고, 실제 몽타주 보면서
    // 에디터에서 상대 위치(Relative Location/Rotation)를 튜닝하면 됨
    ChokeGrabPoint = CreateDefaultSubobject<USceneComponent>(TEXT("ChokeGrabPoint"));
    ChokeGrabPoint->SetupAttachment(GetMesh(), GrabSocketName);

    // 공포 반경 사운드 - 자동 재생하지 않고 RefreshTerrorRadiusAudio()가 머신별로 켤지 결정
    TerrorRadiusAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("TerrorRadiusAudio"));
    TerrorRadiusAudio->SetupAttachment(RootComponent);
    TerrorRadiusAudio->bAutoActivate = false;
}

void AChaserCharacter::BeginPlay()
{
    Super::BeginPlay();

    // 공포 반경 사운드 1차 판단. 클라에서는 이 시점에 아직 컨트롤러가 복제 안 돼서 술래 본인인데도
    // IsLocallyControlled()가 false일 수 있음 -> 잠깐 켜졌다가 NotifyControllerChanged에서 다시 꺼짐
    RefreshTerrorRadiusAudio();

    // [리슨] 술래 전용 IMC 추가는 AddLocalInputMappings(NotifyControllerChanged 경유)로 이동.
    // 리슨 호스트에서는 이 시점에 아직 빙의 전이라 GetController()가 null -> 키가 하나도 안 먹었음

    // 부감 스킬용 천장 컬링: 맵 생성 매니저를 찾아서 생성 완료 델리게이트에 바인딩해둔다.
    // OnMapGenComplete는 OnRep_GenComplete()를 통해 서버/모든 클라이언트에서 각자 정확히 한 번씩 호출된다
    if (AWFCGridManager* GridManager = Cast<AWFCGridManager>(UGameplayStatics::GetActorOfClass(GetWorld(), AWFCGridManager::StaticClass())))
    {
        CachedGridManager = GridManager;
        GridManager->OnMapGenComplete.AddUObject(this, &AChaserCharacter::CacheAllCeilings);

        // 맵 생성이 이미 끝난 뒤에 스폰된(늦게 접속한) 클라이언트를 위한 보정
        if (GridManager->IsMapGenerated())
        {
            CacheAllCeilings();
        }
    }
}

void AChaserCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    // 부모(AescaperoomCharacter)의 Move/Look/Jump 바인딩을 그대로 유지
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        if (GrabAction)
        {
            EIC->BindAction(GrabAction, ETriggerEvent::Started, this, &AChaserCharacter::Grab);
        }
        if (InteractAction)
        {
            EIC->BindAction(InteractAction, ETriggerEvent::Started, this, &AChaserCharacter::Interact);
            // 홀드가 필요한 상호작용 도중에 키를 놓으면 취소되도록 Completed/Canceled 모두 바인딩
            // (러너와 동일한 패턴 - 지금은 모든 대상이 홀드 0이라 실질적으로 안 쓰이지만, 나중에
            // 홀드가 필요한 술래 전용 상호작용이 추가되면 바로 동작함)
            EIC->BindAction(InteractAction, ETriggerEvent::Completed, this, &AChaserCharacter::CancelHoldInteract);
            EIC->BindAction(InteractAction, ETriggerEvent::Canceled, this, &AChaserCharacter::CancelHoldInteract);
        }
        if (OverheadViewAction)
        {
            // 홀드가 아니라 토글이라 Started 한 번만 바인딩하면 됨 (뗄 때는 아무 것도 안 함)
            EIC->BindAction(OverheadViewAction, ETriggerEvent::Started, this, &AChaserCharacter::ToggleOverheadView);
        }
    }
}

bool AChaserCharacter::CanLook() const
{
    // 홀드 상호작용 중(캐비넷 열기 채널링 + 문 부수기 차지 둘 다)에는 시점 회전(좌우+상하)을 막음.
    // bIsInteracting은 술래 본인 클라의 로컬 홀드 상태라 Look 입력을 처리하는 곳과 같은 머신
    //
    // 홀드가 끝난 뒤에도 이어지는 몽타주 동안 계속 고정:
    //   - KickDoor 몽타주(발로 차는 모션) - CanMove()와 동일한 조건
    //   - CabinetOpen 몽타주(홀드 후 남은 여는 모션), CabinetEmpty 몽타주(빈 캐비넷 확인)
    //
    // Grab 동작(bIsGrab 펄스, GrabPulseDuration 동안) 중에도 시점 회전을 막음 - CanMove()와 동일한 조건.
    // bIsGrab은 서버 권위 + 복제 값이라 입력 직후 서버 왕복 시간만큼은 아주 잠깐 회전이 들어갈 수 있음
    return Super::CanLook() && !bIsGrab && !bIsInteracting
        && !IsPlayingChokeGrabMontage() && !IsPlayingKickDoorMontage()
        && !IsPlayingCabinetOpenMontage() && !IsPlayingCabinetEmptyMontage();
}

bool AChaserCharacter::IsPlayingCabinetOpenMontage() const
{
    if (!CabinetOpenMontage)
    {
        return false;
    }

    if (USkeletalMeshComponent* MeshComp = GetMesh())
    {
        if (UAnimInstance* AnimInstance = MeshComp->GetAnimInstance())
        {
            return AnimInstance->Montage_IsPlaying(CabinetOpenMontage);
        }
    }
    return false;
}

bool AChaserCharacter::IsPlayingCabinetEmptyMontage() const
{
    if (!CabinetEmptyMontage)
    {
        return false;
    }

    if (USkeletalMeshComponent* MeshComp = GetMesh())
    {
        if (UAnimInstance* AnimInstance = MeshComp->GetAnimInstance())
        {
            return AnimInstance->Montage_IsPlaying(CabinetEmptyMontage);
        }
    }
    return false;
}

void AChaserCharacter::MulticastBeginCabinetOpen_Implementation()
{
    // 속도 보정/루프 없이 한 번만 재생 (홀드 시간과의 길이 맞춤은 몽타주 에셋 Rate Scale로 조절)
    if (CabinetOpenMontage && GetMesh())
    {
        if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
        {
            AnimInstance->Montage_Play(CabinetOpenMontage);
        }
    }
}

void AChaserCharacter::MulticastStopCabinetOpen_Implementation()
{
    if (CabinetOpenMontage && GetMesh())
    {
        if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
        {
            if (AnimInstance->Montage_IsPlaying(CabinetOpenMontage))
            {
                AnimInstance->Montage_Stop(CabinetOpenMontage->BlendOut.GetBlendTime(), CabinetOpenMontage);
            }
        }
    }
}

void AChaserCharacter::MulticastPlayCabinetEmpty_Implementation()
{
    if (CabinetEmptyMontage && GetMesh())
    {
        if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
        {
            AnimInstance->Montage_Play(CabinetEmptyMontage);
        }
    }
}

bool AChaserCharacter::CanMove() const
{
    // Grab 애니메이션 또는 KickDoor 몽타주 재생 중에는 이동 불가. 캐비닛 채널링 등 홀드 상호작용
    // 중(bIsInteracting)에도 제자리에 멈춰있어야 함 (ARunnerCharacter와 동일한 패턴)
    // 부감(목 늘리기) 스킬 사용 중에도 이동 불가 (시점 회전은 허용)
    return Super::CanMove() && !bIsGrab && !IsPlayingKickDoorMontage() && !IsPlayingChokeGrabMontage()
        && !IsPlayingCabinetEmptyMontage() && !bIsInteracting && !IsOverheadMoveLocked();
}

bool AChaserCharacter::IsOverheadMoveLocked() const
{
    if (bIsOverheadViewActive)
    {
        return true;
    }

    // 한 번도 안 썼으면(0) 잠금 없음. 끝난 시각 + 추가 잠금 시간 전이면 아직 이동 불가
    // (OverheadActiveEndServerTime은 EndOverheadView에서 실제 종료 시각으로 다시 맞춰서 복제됨)
    return OverheadActiveEndServerTime > 0.f
        && GetServerTimeSeconds() < OverheadActiveEndServerTime + OverheadMoveLockAfterEnd;
}

bool AChaserCharacter::IsPlayingKickDoorMontage() const
{
    if (!KickDoorMontage)
    {
        return false;
    }

    if (USkeletalMeshComponent* MeshComp = GetMesh())
    {
        if (UAnimInstance* AnimInstance = MeshComp->GetAnimInstance())
        {
            return AnimInstance->Montage_IsPlaying(KickDoorMontage);
        }
    }

    return false;
}

bool AChaserCharacter::IsPlayingChokeGrabMontage() const
{
    if (!ChokeGrabMontage)
    {
        return false;
    }

    if (USkeletalMeshComponent* MeshComp = GetMesh())
    {
        if (UAnimInstance* AnimInstance = MeshComp->GetAnimInstance())
        {
            return AnimInstance->Montage_IsPlaying(ChokeGrabMontage);
        }
    }

    return false;
}

ADoorActor* AChaserCharacter::FindDoorTargetByCameraTrace() const
{
    // [변경] 카메라 라인 트레이스 -> 몸 정면 박스. 박스 안의 문만 대상으로 (문 우선 탐색)
    return Cast<ADoorActor>(FindInteractableInFrontBox(ADoorActor::StaticClass()));
}

void AChaserCharacter::Grab(const FInputActionValue& Value)
{
    if (bIsGrab || IsPlayingKickDoorMontage())
    {
        return;
    }

    // 대상이 없거나 허탕이어도 잡기 동작(bIsGrab 펄스)은 항상 재생. 판정은 서버가 직접 함
    ServerStartGrabPulse();
}

void AChaserCharacter::GetGrabBox(FVector& OutCenter, FQuat& OutRotation) const
{
    const FRotator YawOnly(0.f, GetActorRotation().Yaw, 0.f);
    OutRotation = YawOnly.Quaternion();
    OutCenter = GetActorLocation() + YawOnly.Vector() * GrabBoxForwardOffset;
}

void AChaserCharacter::PerformGrabBoxCheck()
{
    if (!HasAuthority() || !GetWorld())
    {
        return;
    }

    FVector Center; FQuat Rot;
    GetGrabBox(Center, Rot);

    // 박스 안의 폰만 검사 (순간 판정 - 컴포넌트를 만들지 않음)
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ChaserGrabBox), false, this);
    TArray<FOverlapResult> Overlaps;
    GetWorld()->OverlapMultiByObjectType(Overlaps, Center, Rot,
        FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeBox(GrabBoxExtent), Params);

    // 잡을 수 있는 러너 중 술래와 가장 가까운 한 명만
    ARunnerCharacter* Best = nullptr;
    float BestDistSq = TNumericLimits<float>::Max();
    for (const FOverlapResult& Result : Overlaps)
    {
        ARunnerCharacter* Runner = Cast<ARunnerCharacter>(Result.GetActor());
        if (!Runner || !CanStartChokeGrab(Runner))
        {
            continue;
        }
        const float DistSq = FVector::DistSquared(GetActorLocation(), Runner->GetActorLocation());
        if (DistSq < BestDistSq)
        {
            BestDistSq = DistSq;
            Best = Runner;
        }
    }

    if (Best)
    {
        // 실제 연출 시작은 캐비닛 열기와 공통인 StartChokeGrab()이 담당
        StartChokeGrab(Best);
    }
}

bool AChaserCharacter::CanStartChokeGrab(const ARunnerCharacter* Runner) const
{
    // 서버 전용. 이미 잡혔거나 목조르기 중인 러너, 또는 이 술래가 이미 다른 러너를 조르는 중이면 시작 불가
    return HasAuthority() && Runner && !Runner->IsCaught() && !Runner->IsBeingCaught() && !Runner->IsEscaped() && !PendingChokeRunner.IsValid();
}

void AChaserCharacter::SnapForChokeGrab(const FVector& Location, const FRotator& Rotation)
{
    if (!HasAuthority())
    {
        return;
    }

    // Multicast는 서버에서 호출하면 서버 자신에게도 즉시 실행되므로, 이 줄 직후 StartChokeGrab()이
    // 읽는 ChokeGrabPoint 위치는 이미 옮겨진 위치 기준
    MulticastSnapTransform(Location, Rotation);

    // 1인칭 캐릭터는 몸 Yaw가 컨트롤러 회전을 따라가므로(bUseControllerRotationYaw), 액터만 돌리면 다음
    // 틱에 원래 카메라 방향으로 되돌아감 → 서버의 컨트롤러 + 술래 본인 클라이언트 양쪽 회전을 같이 맞춤.
    // 연출 중에는 CanLook()이 마우스 회전을 막으므로 이 방향이 그대로 유지됨
    if (AController* C = GetController())
    {
        C->SetControlRotation(Rotation);
        if (APlayerController* PC = Cast<APlayerController>(C))
        {
            PC->ClientSetRotation(Rotation);
        }
    }
}

void AChaserCharacter::MulticastSnapTransform_Implementation(FVector Location, FRotator Rotation)
{
    if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
    {
        MoveComp->StopMovementImmediately();
    }
    SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
}

bool AChaserCharacter::StartChokeGrab(ARunnerCharacter* Runner)
{
    if (!CanStartChokeGrab(Runner))
    {
        return false;
    }

    // 러너를 ChokeGrabPoint(술래 손 소켓에 붙은, 판정 콜리전과는 별개인 위치 마커)로 스냅시키고,
    // 술래/러너 양쪽에서 각자의 목조르기 몽타주를 동시에(제자리 재생) 재생시킴. 즉시 감옥으로 보내던
    // 예전 방식과 달리, Choke Capture 노티파이(또는 안전장치 타이머) 시점에야 실제로 Capture()를 호출함
    const FVector TargetLocation = ChokeGrabPoint ? ChokeGrabPoint->GetComponentLocation() : GetActorLocation();
    const FRotator TargetRotation = ChokeGrabPoint ? ChokeGrabPoint->GetComponentRotation() : GetActorRotation();
    Runner->MulticastBeginChoked(TargetLocation, TargetRotation);

    // 러너도 1인칭이라 몸 Yaw가 컨트롤러 회전을 따라감 → 액터만 스냅하면 다음 틱에 원래 카메라 방향으로
    // 되돌아가므로, 러너의 컨트롤러 회전(서버 + 러너 본인 클라)도 스냅 방향(Yaw만)으로 맞춤.
    // 연출 중에는 러너의 CanLook()이 마우스 회전을 막아서 이 방향이 유지됨
    if (AController* RunnerController = Runner->GetController())
    {
        const FRotator RunnerControlRotation(0.f, TargetRotation.Yaw, 0.f);
        RunnerController->SetControlRotation(RunnerControlRotation);
        if (APlayerController* RunnerPC = Cast<APlayerController>(RunnerController))
        {
            RunnerPC->ClientSetRotation(RunnerControlRotation);
        }
    }
    MulticastPlayChokeGrabMontage();

    const float ChokeDuration = ChokeGrabMontage ? ChokeGrabMontage->GetPlayLength() : 0.f;
    if (ChokeDuration <= 0.f)
    {
        // 몽타주가 없으면(에셋 미지정) 연출 없이 바로 감옥으로 보냄 - 안전한 폴백
        Runner->Capture();
        return true;
    }

    // 실제 감옥 이동은 몽타주의 Choke Capture 노티파이(NotifyChokeCapture) 타이밍에 처리됨.
    // 이 타이머는 노티파이를 안 넣었거나 몽타주가 중간에 끊겼을 때를 위한 안전장치(몽타주 길이 후 강제 처리)
    PendingChokeRunner = Runner;
    GetWorldTimerManager().SetTimer(ChokeGrabTimerHandle, this, &AChaserCharacter::CompleteChokeGrab, ChokeDuration, false);
    return true;
}

void AChaserCharacter::MulticastPlayChokeGrabMontage_Implementation()
{
    if (ChokeGrabMontage && GetMesh())
    {
        if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
        {
            AnimInstance->Montage_Play(ChokeGrabMontage);
        }
    }
}

void AChaserCharacter::CompleteChokeGrab()
{
    // 노티파이/타이머 중 먼저 온 쪽이 처리 → 나머지 경로는 여기서 할 일이 없도록 정리
    GetWorldTimerManager().ClearTimer(ChokeGrabTimerHandle);

    ARunnerCharacter* Runner = PendingChokeRunner.Get();
    PendingChokeRunner = nullptr;

    if (Runner)
    {
        Runner->Capture();
    }
}

void AChaserCharacter::NotifyChokeCapture()
{
    // ChokeGrabMontage는 MulticastPlayChokeGrabMontage()로 모든 머신에서 재생되므로 이 함수도 각자 불림.
    // PendingChokeRunner는 서버(PerformGrabBoxCheck)에서만 세팅되고 Capture()도 서버 전용이라
    // 서버만 처리하면 됨 - 클라이언트는 무시(러너 감옥 이동/포즈 해제는 Capture 쪽 복제/Multicast로 전달됨)
    if (!HasAuthority())
    {
        return;
    }

    if (!PendingChokeRunner.IsValid())
    {
        return; // 이미 처리됐거나(타이머가 먼저 옴) 잡은 러너가 없음
    }

    CompleteChokeGrab();
}

void AChaserCharacter::ServerStartGrabPulse_Implementation()
{
    // 이미 잡기 동작 중이면 무시 (연타로 판정이 여러 번 나가지 않게)
    if (bIsGrab)
    {
        return;
    }

    StartGrabPulse();

    if (GrabHitDelay > 0.f)
    {
        GetWorldTimerManager().SetTimer(GrabHitDelayTimerHandle, this, &AChaserCharacter::PerformGrabBoxCheck, GrabHitDelay, false);
    }
    else
    {
        PerformGrabBoxCheck();
    }
}

void AChaserCharacter::ServerPlayKickDoorMontage_Implementation()
{
    MulticastPlayKickDoorMontage();
}

void AChaserCharacter::MulticastPlayKickDoorMontage_Implementation()
{
    if (KickDoorMontage)
    {
        PlayAnimMontage(KickDoorMontage);
    }
}

void AChaserCharacter::MulticastBeginKickCharge_Implementation()
{
    // KickChargeMontage는 KickHoldTime(3초)과 길이가 정확히 맞게 만들어졌으므로 재생 속도 보정 없이
    // 1배속으로 재생 - 홀드가 끝나는 시점에 자연스럽게 몽타주도 같이 끝남
    if (KickChargeMontage && GetMesh())
    {
        if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
        {
            AnimInstance->Montage_Play(KickChargeMontage);
        }
    }
}

void AChaserCharacter::MulticastCancelKickCharge_Implementation()
{
    if (KickChargeMontage && GetMesh())
    {
        if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
        {
            AnimInstance->Montage_Stop(0.2f, KickChargeMontage);
        }
    }
}

void AChaserCharacter::ServerBreakDoor_Implementation(ADoorActor* Door)
{
    // 서버에서 다시 한번 유효성 검사 (클라이언트가 알려준 대상을 맹신하지 않음)
    if (!Door)
    {
        return;
    }

    // 실제로 부술 수 있는지(닫혀있고 아직 안 부서졌는지) 검증. KickDoor 몽타주는 Interact() 쪽에서
    // 이미 트리거되었으므로 여기서는 문 파괴 자체의 성공 여부만 판단하면 됨
    if (!Door->CanBreak())
    {
        return;
    }

    Door->Break();
}

void AChaserCharacter::NotifyKickDoorHit()
{
    // KickDoor 몽타주는 MulticastPlayKickDoorMontage()를 통해 술래 본인을 포함한 모든 클라이언트에서
    // 재생되므로, 이 함수(AnimNotify가 호출하는 지점)도 각 클라이언트에서 각자 호출된다. 하지만 실제
    // 파괴 요청(ServerBreakDoor)은 이 Interact 입력을 발생시킨 로컬 컨트롤 클라이언트(그리고 그 클라이언트가
    // 곧 서버인 리슨 서버)만 보내면 됨 - 원격 관전 중인 다른 클라이언트는 이 액터에 대한 Server RPC를
    // 보낼 권한 자체가 없어 호출해도 무시되고, PendingKickDoorTarget도 애초에 그쪽에서는 채워지지 않는다
    if (!IsLocallyControlled())
    {
        return;
    }

    ADoorActor* Door = PendingKickDoorTarget.Get();
    PendingKickDoorTarget = nullptr;

    if (!Door)
    {
        return;
    }

    // 나이아가라 타격 이펙트는 이제 DoorActor::ActivateBrokenMesh()(SpawnKickImpactEffect)에서 스폰함.
    // Break()(서버)/OnRep_IsBroken()(모든 클라) 양쪽 경로에서 다 호출되므로 다른 클라에서도 보임 -
    // 여기서는 파괴 요청만 서버로 보내면 됨
    ServerBreakDoor(Door);
}

AActor* AChaserCharacter::FindInteractableTarget() const
{
    // [변경] 카메라 라인 트레이스 -> 몸 정면 박스 (AescaperoomCharacter::FindInteractableInFrontBox)
    return FindInteractableInFrontBox();
}

void AChaserCharacter::Interact(const FInputActionValue& Value)
{
    if (bIsGrab || IsPlayingKickDoorMontage())
    {
        return;
    }

    // 문 부수기(KickDoor) 대상은 정면 박스 안의 문 중에서 먼저 찾음 (문 우선)
    ADoorActor* Door = FindDoorTargetByCameraTrace();

    // 문을 찾았으면 그 문을 Target으로 취급해서 캐비닛 등 다른 IInteractable 대상과 완전히 동일한
    // 홀드 파이프라인(아래)을 탄다 - DoorActor::GetInteractHoldTime_Implementation이 술래에게는
    // KickHoldTime(기본 3초)을 반환하므로 자동으로 홀드+프로그레스바 대상이 됨
    AActor* Target = Door;
    if (!Target)
    {
        Target = FindInteractableTarget();
    }

    if (!Target)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Interact][Chaser] No target found (Door=%s)"), *GetNameSafe(Door));
        return;
    }

    // 서버가 최종 검증하긴 하지만(ServerInteractWithTarget/ServerSetInteractHold), 애초에
    // 상호작용 불가능한 대상(예: 이미 열려있는 문을 술래가 다시 차려는 경우)이면 여기서 걸러서
    // 헛되이 홀드 타이머/게이지/몽타주가 시작되지 않게 함 - InteractionRange 프롬프트도
    // CanInteractInternal 기준으로 뜨므로 자연스럽게 서로 맞아떨어짐
    const bool bTargetCanInteract = IInteractable::Execute_CanInteract(Target, this);
    UE_LOG(LogTemp, Warning, TEXT("[Interact][Chaser] Target=%s (IsDoor=%d) CanInteract=%d"),
        *GetNameSafe(Target), Door != nullptr ? 1 : 0, bTargetCanInteract ? 1 : 0);
    // [TEMP DEBUG] CanInteract=0 원인 확인 + Chaser 태그가 실제로 붙어있는지 확인용 - 원인 찾으면 지워도 됨
    UE_LOG(LogTemp, Warning, TEXT("[Interact][Chaser] HasChaserTag=%d"), ActorHasTag(TEXT("Chaser")) ? 1 : 0);
    if (Door)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Interact][Chaser] DoorState IsOpen=%d IsBroken=%d"),
            Door->IsOpen() ? 1 : 0, Door->IsBroken() ? 1 : 0);
    }

    if (!bTargetCanInteract)
    {
        return;
    }

    const float HoldTime = IInteractable::Execute_GetInteractHoldTime(Target, this);
    const FText InteractHoldText = IInteractable::Execute_GetInteractionHoldText(Target, this);

    if (HoldTime <= 0.f)
    {
        if (Door)
        {
            // 문은 홀드시간이 0으로 설정돼 있어도 일반 OnInteract가 아니라 킥 몽타주로 처리해야 함.
            // 실제 파괴는 몽타주에 심어둔 AnimNotify(UAnimNotify_ChaserKickDoor)가 발로 문을 차는
            // 프레임에서 NotifyKickDoorHit()을 호출하는 시점에 실행된다
            PendingKickDoorTarget = Door;
            ServerPlayKickDoorMontage();
            return;
        }

        // 즉시 처리 (홀드 시간이 0인 대상만 해당). 실제 판정/처리는 서버에서 다시 검증됨
        ServerInteractWithTarget(Target);
        return;
    }

    if (Door)
    {
        // 몽타주 재생은 홀드가 끝난 뒤(CompleteHoldInteract)에 요청하지만, 그 시점에 어느 문인지
        // 알아야 하므로 미리 기억해둠 - NotifyKickDoorHit()이 나중에 이 값을 꺼내 씀
        PendingKickDoorTarget = Door;
    }

    // 홀드가 필요한 상호작용(캐비닛 채널링, 이제 킥도어도 포함): 러너와 동일하게 로컬 타이머로
    // HoldTime을 직접 재고, 다 채우면 CompleteHoldInteract()가 대상에 맞는 완료 처리를 한다.
    // 그동안 CanMove()는 bIsInteracting을 보고 이동을 막음
    PendingHoldTarget = Target;
    bIsInteracting = true;
    GetWorldTimerManager().SetTimer(InteractHoldTimerHandle, this, &AChaserCharacter::CompleteHoldInteract, HoldTime, false);

    // 홀드가 시작됐음을 서버에 알려서 "여는 중"/"부수는 중" 연출을 시작시킴
    ServerSetInteractHold(Target, true);

    // 홀드 게이지 바를 보여주기 위해 MatchWidget의 Tick을 켬 (키를 떼거나 홀드가 끝나면 다시 끔).
    // 텍스트도 같이 넘겨서 "문 부수는 중..."처럼 게이지 옆에 무엇을 하는 중인지 보여줌
    SetMatchWidgetHoldGaugeActive(true, InteractHoldText);
}

void AChaserCharacter::CancelHoldInteract(const FInputActionValue& Value)
{
    if (GetWorldTimerManager().IsTimerActive(InteractHoldTimerHandle))
    {
        GetWorldTimerManager().ClearTimer(InteractHoldTimerHandle);

        if (AActor* Target = PendingHoldTarget.Get())
        {
            ServerSetInteractHold(Target, false);
        }

        // 킥도어 홀드 도중 취소된 경우, 몽타주 자체가 재생되지 않으므로 문 파괴로 이어지면 안 됨
        PendingKickDoorTarget = nullptr;

        PendingHoldTarget = nullptr;
        bIsInteracting = false;
        SetMatchWidgetHoldGaugeActive(false);
    }
}

void AChaserCharacter::CompleteHoldInteract()
{
    // ARunnerCharacter::CompleteHoldInteract()와 동일한 이유로 순서를 바꿈: 정리(게이지 끄기 포함)를
    // 먼저 하고 나서 서버 RPC를 보낸다 (리슨 서버/스탠드얼론에서 RPC가 동기적으로 즉시 처리되면서
    // Controller가 다른 폰으로 Possess-교체되어 GetController()가 먼저 nullptr이 되는 것을 방지)
    AActor* Target = PendingHoldTarget.Get();

    PendingHoldTarget = nullptr;
    bIsInteracting = false;
    SetMatchWidgetHoldGaugeActive(false);

    if (!Target)
    {
        return;
    }

    if (Cast<ADoorActor>(Target))
    {
        // 문은 일반 OnInteract가 아니라 킥 몽타주 재생을 요청함 - PendingKickDoorTarget은 Interact()에서
        // 이미 세팅해뒀으므로, 몽타주 재생 후 발이 닿는 타이밍(AnimNotify)에 NotifyKickDoorHit()가
        // 실제 파괴(ServerBreakDoor)를 트리거한다
        ServerPlayKickDoorMontage();
        return;
    }

    ServerInteractWithTarget(Target);
}

float AChaserCharacter::GetInteractHoldPercent() const
{
    // ARunnerCharacter::GetInteractHoldPercent()와 완전히 동일한 패턴 (로컬 타이머 기준 클라이언트 예측)
    if (!GetWorldTimerManager().IsTimerActive(InteractHoldTimerHandle))
    {
        return 0.f;
    }

    const float Duration = GetWorldTimerManager().GetTimerRate(InteractHoldTimerHandle);
    if (Duration <= 0.f)
    {
        return 0.f;
    }

    const float Elapsed = GetWorldTimerManager().GetTimerElapsed(InteractHoldTimerHandle);
    return FMath::Clamp(Elapsed / Duration, 0.f, 1.f);
}

void AChaserCharacter::ServerInteractWithTarget_Implementation(AActor* Target)
{
    // 서버에서 다시 한번 유효성 검사 (클라이언트가 알려준 대상을 맹신하지 않음)
    if (!Target || !Target->GetClass()->ImplementsInterface(UInteractable::StaticClass()))
    {
        return;
    }

    if (!IInteractable::Execute_CanInteract(Target, this))
    {
        return;
    }

    IInteractable::Execute_OnInteract(Target, this);
}

void AChaserCharacter::ServerSetInteractHold_Implementation(AActor* Target, bool bHolding)
{
    if (!Target || !Target->GetClass()->ImplementsInterface(UInteractable::StaticClass()))
    {
        return;
    }

    if (bHolding && !IInteractable::Execute_CanInteract(Target, this))
    {
        return;
    }

    IInteractable::Execute_OnInteractHoldUpdate(Target, this, bHolding);
}

void AChaserCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // 스킬이 꺼진 직후에도(목이 아직 다 안 내려온 애니메이션 도중) neck이 천장보다 높은 동안에는
    // 계속 체크해서, 실제로 높이가 낮아졌을 때 자연스럽게 원복되도록 한다 (스킬 off 신호로 즉시
    // 강제 원복하지 않음)
    // [수정] 천장 투명화는 술래 본인 화면에서만. 목 애니메이션(bIsOverheadViewActive)은 모든 클라에 복제되므로
    // 이 체크가 없으면 러너 PC에 있는 술래 캐릭터도 "목이 천장보다 높다"고 판정해서 러너 화면 천장까지 사라졌음
    const bool bLocalChaserView = IsLocallyControlled();

    if (bLocalChaserView && (bIsOverheadViewActive || bCeilingsFaded))
    {
        UpdateCeilingFade();
    }

    // 천장 FadeAmount를 목표값까지 서서히 이동 (스킬이 끝난 뒤 복귀 페이드도 여기서 마저 진행)
    if (bLocalChaserView && !FMath::IsNearlyEqual(CurrentCeilingFade, TargetCeilingFade))
    {
        TickCeilingFade(DeltaSeconds);
    }

    // 술래 본인 화면: 사용 중이면 "목 늘어나는 중 (N)" 남은 초 갱신
    if (bIsOverheadViewActive && IsLocallyControlled())
    {
        UpdateOverheadPromptText();
    }
}

void AChaserCharacter::CacheAllCeilings()
{
    AllCeilings.Reset();
    OverheadHiddenMeshes.Reset();

    // 서버에서만 채워지는 GridManager->GetSpawnedRooms() 대신, 이 머신(서버든 클라든)의 월드에 실제로
    // 존재하는 모든 액터에서 "Ceiling" 컴포넌트 태그가 붙은 메시를 직접 수집 (일반 방 + 빅룸 공통)
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor)
        {
            continue;
        }

        TInlineComponentArray<UMeshComponent*> Prims;
        Actor->GetComponents(Prims);

        for (UMeshComponent* Comp : Prims)
        {
            if (Comp && Comp->ComponentTags.Contains(TEXT("Ceiling")))
            {
                AllCeilings.Add(Comp);
            }
            else if (Comp && Comp->ComponentTags.Contains(TEXT("HideInOverhead")))
            {
                OverheadHiddenMeshes.Add(Comp);
            }
        }
    }

    // 맵이 평면 그리드라 모든 방의 천장이 같은 Z에 있다는 전제 하에, 찾은 천장 중 하나의 Z만
    // 뽑아서 기준값으로 고정해둔다. 매 틱 트레이스/오버랩으로 다시 찾을 필요가 없어짐
    if (AllCeilings.Num() > 0)
    {
        if (UMeshComponent* FirstCeiling = AllCeilings[0].Get())
        {
            CachedCeilingZ = FirstCeiling->GetComponentLocation().Z;
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[Ceiling] %s collected %d ceilings (CeilingZ=%.1f)"),
        HasAuthority() ? TEXT("Server") : TEXT("Client"), AllCeilings.Num(), CachedCeilingZ);
}

void AChaserCharacter::RefreshFadedCeilings()
{
    if (!bCeilingsFaded)
    {
        GetWorldTimerManager().ClearTimer(CeilingRefreshTimerHandle);
        return;
    }

    // 새로 복제되어 들어온 방까지 다시 모아서 전부 투명 상태로 맞춤
    CacheAllCeilings();
    SetAllCeilingsFaded(true);
}

void AChaserCharacter::UpdateCeilingFade()
{
    if (!GetMesh())
    {
        return;
    }

    // 아직 한 번도 못 모았으면(맵 생성 완료 이벤트보다 늦게/일찍 불린 경우 등) 지금 모아봄
    if (AllCeilings.Num() == 0)
    {
        CacheAllCeilings();
        if (AllCeilings.Num() == 0)
        {
            return;
        }
    }

    const float NeckZ = GetMesh()->GetBoneLocation(NeckBoneName).Z;
    const bool bShouldFade = (NeckZ > CachedCeilingZ);

    if (bShouldFade != bCeilingsFaded)
    {
        bCeilingsFaded = bShouldFade;

        // 투명화 시작 순간엔 지금까지 복제되어 들어온 방 천장을 새로 모은 뒤 적용
        if (bCeilingsFaded)
        {
            CacheAllCeilings();
        }
        SetAllCeilingsFaded(bCeilingsFaded);

        // 투명화 중에는 늦게 복제된 방 천장도 따라잡도록 주기적으로 재수집
        if (bCeilingsFaded)
        {
            GetWorldTimerManager().SetTimer(CeilingRefreshTimerHandle, this, &AChaserCharacter::RefreshFadedCeilings, 0.5f, true);
        }
        else
        {
            GetWorldTimerManager().ClearTimer(CeilingRefreshTimerHandle);
        }
    }
}

void AChaserCharacter::SetAllCeilingsFaded(bool bFaded)
{
    // 즉시 바꾸지 않고 목표값만 설정 -> Tick(TickCeilingFade)에서 CeilingFadeDuration에 걸쳐 서서히 이동
    TargetCeilingFade = bFaded ? 1.f : 0.f;

    // 새로 수집된(늦게 복제된) 천장도 지금 진행 중인 값에 바로 맞춰줌
    ApplyCeilingFadeValue(CurrentCeilingFade);

    if (CeilingFadeDuration <= 0.f)
    {
        CurrentCeilingFade = TargetCeilingFade;
        ApplyCeilingFadeValue(CurrentCeilingFade);
    }

    // 램프 등 매달린 소품: 천장이 돌아올 때는 바로 다시 보이게 하고,
    // 사라질 때는 천장이 완전히 투명해진 순간(TickCeilingFade) 숨김. 이미 다 투명한 상태면 즉시 숨김
    if (!bFaded)
    {
        SetOverheadMeshesHidden(false);
    }
    else if (CurrentCeilingFade >= 1.f)
    {
        SetOverheadMeshesHidden(true);
    }
}

void AChaserCharacter::TickCeilingFade(float DeltaSeconds)
{
    const float Step = (CeilingFadeDuration > 0.f) ? DeltaSeconds / CeilingFadeDuration : 1.f;
    CurrentCeilingFade = FMath::FInterpConstantTo(CurrentCeilingFade, TargetCeilingFade, 1.f, Step);
    ApplyCeilingFadeValue(CurrentCeilingFade);

    // 천장이 완전히 투명해진 순간 매달린 소품도 숨김
    if (TargetCeilingFade >= 1.f && CurrentCeilingFade >= 1.f)
    {
        SetOverheadMeshesHidden(true);
    }
}

void AChaserCharacter::ApplyCeilingFadeValue(float Value)
{
    for (const TWeakObjectPtr<UMeshComponent>& WeakComp : AllCeilings)
    {
        if (UMeshComponent* Comp = WeakComp.Get())
        {
            UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Comp->GetMaterial(0));
            if (!MID)
            {
                MID = Comp->CreateAndSetMaterialInstanceDynamic(0);
            }

            if (MID)
            {
                MID->SetScalarParameterValue(TEXT("FadeAmount"), Value);
            }
        }
    }
}

void AChaserCharacter::SetOverheadMeshesHidden(bool bHideMeshes)
{
    // 천장에 매달린 소품(램프 등)은 투명화 대신 숨김/표시. 자식 컴포넌트(라이트)에는 전파하지 않음
    for (const TWeakObjectPtr<UMeshComponent>& WeakComp : OverheadHiddenMeshes)
    {
        if (UMeshComponent* Comp = WeakComp.Get())
        {
            Comp->SetVisibility(!bHideMeshes, false);
        }
    }
}

void AChaserCharacter::ToggleOverheadView(const FInputActionValue& Value)
{
    // 로컬 게이트: 사용 중/쿨타임 중이면 서버에 요청조차 안 보냄 (최종 판정은 서버가 다시 함)
    if (bIsOverheadViewActive || GetOverheadCooldownRemaining() > 0.f)
    {
        return;
    }

    ServerRequestOverheadView();
}

void AChaserCharacter::ServerRequestOverheadView_Implementation()
{
    // 사용 중이거나 쿨타임 중이면 무시
    if (bIsOverheadViewActive || GetServerTimeSeconds() < OverheadCooldownEndServerTime)
    {
        return;
    }

    const float Now = GetServerTimeSeconds();
    bIsOverheadViewActive = true;
    OverheadActiveEndServerTime = Now + OverheadViewDuration;
    OverheadCooldownEndServerTime = OverheadActiveEndServerTime + OverheadViewCooldown;

    GetWorldTimerManager().SetTimer(OverheadViewTimerHandle, this, &AChaserCharacter::EndOverheadView, OverheadViewDuration, false);

    // 리슨 서버(에디터 PIE 등) 자신의 화면에도 즉시 반영. 순수 데디서버에서는 렌더링이 없어 사실상 no-op
    OnRep_IsOverheadViewActive();
    ForceNetUpdate();
}

void AChaserCharacter::EndOverheadView()
{
    if (!HasAuthority() || !bIsOverheadViewActive)
    {
        return;
    }

    // 끄기 -> BP necklong 타임라인이 이 값을 보고 목을 다시 줄임. 쿨타임 종료 시각은 켤 때 이미 계산해둠
    // (타이머 오차로 조금 일찍/늦게 끝나도 쿨타임이 "지금부터 15초"가 되도록 여기서 다시 맞춤)
    bIsOverheadViewActive = false;
    OverheadActiveEndServerTime = GetServerTimeSeconds();
    OverheadCooldownEndServerTime = OverheadActiveEndServerTime + OverheadViewCooldown;

    OnRep_IsOverheadViewActive();
    ForceNetUpdate();
}

void AChaserCharacter::OnRep_IsOverheadViewActive()
{
    // 실제 목 늘어나는 애니메이션은 BP_ChaserCharacter(necklong 타임라인)/AnimBP가 이 값을 보고 처리

    // 술래 본인 화면: MatchWidget 홀드 문구 자리에 "목 늘어나는 중 (N)" 표시/해제
    if (IsLocallyControlled())
    {
        if (bIsOverheadViewActive)
        {
            LastShownOverheadSeconds = -1; // 다음 Tick에서 바로 문구를 씀
            UpdateOverheadPromptText();
        }
        else
        {
            LastShownOverheadSeconds = -1;
            SetOverheadPromptText(false);
        }
    }

    // 스킬이 꺼져도 천장을 여기서 바로 원복하지 않는다 - Tick()이 bCeilingsFaded인 동안은 계속
    // UpdateCeilingFade()를 돌려서, neck이 실제로 천장 높이 아래로 내려왔을 때 자연스럽게 원복된다
}

float AChaserCharacter::GetServerTimeSeconds() const
{
    if (const UWorld* World = GetWorld())
    {
        if (const AGameStateBase* GS = World->GetGameState())
        {
            return GS->GetServerWorldTimeSeconds();
        }
        return World->GetTimeSeconds();
    }
    return 0.f;
}

float AChaserCharacter::GetOverheadActiveRemaining() const
{
    if (!bIsOverheadViewActive)
    {
        return 0.f;
    }
    return FMath::Max(0.f, OverheadActiveEndServerTime - GetServerTimeSeconds());
}

float AChaserCharacter::GetOverheadCooldownRemaining() const
{
    if (bIsOverheadViewActive)
    {
        return 0.f; // 사용 중엔 쿨타임 표시 안 함
    }
    return FMath::Max(0.f, OverheadCooldownEndServerTime - GetServerTimeSeconds());
}

float AChaserCharacter::GetOverheadCooldownFraction() const
{
    if (OverheadViewCooldown <= 0.f)
    {
        return 0.f;
    }
    return FMath::Clamp(GetOverheadCooldownRemaining() / OverheadViewCooldown, 0.f, 1.f);
}

void AChaserCharacter::UpdateOverheadPromptText()
{
    // 남은 초를 올림해서 5,4,3,2,1로 표시 - 값이 바뀔 때만 위젯에 다시 씀
    const int32 Seconds = FMath::Max(1, FMath::CeilToInt(GetOverheadActiveRemaining()));
    if (Seconds == LastShownOverheadSeconds)
    {
        return;
    }
    LastShownOverheadSeconds = Seconds;

    SetOverheadPromptText(true, FText::Format(OverheadActiveTextFormat, FText::AsNumber(Seconds)));
}

void AChaserCharacter::SetOverheadPromptText(bool bActive, const FText& Text)
{
    // 홀드 문구와 같은 자리(HoldActionText)를 쓰는 "문구 전용" 상태. 도중에 문 부수기 등 홀드가 시작되면
    // 홀드 문구가 우선 표시되고, 홀드가 끝나면 이 문구로 자동 복귀함 (UMatchWidget::RefreshHoldActionText)
    if (ADungeonPlayerController* DPC = Cast<ADungeonPlayerController>(GetController()))
    {
        if (UMatchWidget* Widget = DPC->GetMatchWidget())
        {
            Widget->SetPromptTextActive(bActive, Text);
        }
    }
}

void AChaserCharacter::StartGrabPulse()
{
    bIsGrab = true;

    // 리슨 서버(에디터 PIE 등) 자신의 화면에도 즉시 반영. 순수 데디서버에서는 렌더링이 없어 사실상 no-op
    OnRep_IsGrab();

    GetWorldTimerManager().SetTimer(GrabPulseTimerHandle, this, &AChaserCharacter::EndGrabPulse, GrabPulseDuration, false);
}

void AChaserCharacter::EndGrabPulse()
{
    bIsGrab = false;

    OnRep_IsGrab();
}

void AChaserCharacter::OnRep_IsGrab()
{
    // TODO: 실제 잡기 애니메이션 재생은 AnimBP에서 이 bIsGrab 값을 참고해서 처리
}

void AChaserCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(AChaserCharacter, bIsOverheadViewActive);
    DOREPLIFETIME(AChaserCharacter, bIsGrab);
    DOREPLIFETIME(AChaserCharacter, OverheadActiveEndServerTime);
    DOREPLIFETIME(AChaserCharacter, OverheadCooldownEndServerTime);
}

// =========================== 공포 반경 사운드 ===========================

void AChaserCharacter::NotifyControllerChanged()
{
    Super::NotifyControllerChanged(); // 부모에서 로컬 플레이어면 AddLocalInputMappings 호출
    RefreshTerrorRadiusAudio();
}

void AChaserCharacter::AddLocalInputMappings(UEnhancedInputLocalPlayerSubsystem* Subsystem)
{
    Super::AddLocalInputMappings(Subsystem); // 공통 IMC (우선순위 0)

    // 술래 전용 IMC를 공통(Move/Look/Jump) 컨텍스트 위에 추가
    if (ChaserMappingContext)
    {
        Subsystem->AddMappingContext(ChaserMappingContext, 1);
    }
}

float AChaserCharacter::GetTerrorRadius() const
{
    if (TerrorRadiusOverride > 0.f)
    {
        return TerrorRadiusOverride;
    }

    if (TerrorRadiusAudio)
    {
        if (const FSoundAttenuationSettings* Settings = TerrorRadiusAudio->GetAttenuationSettingsToApply())
        {
            return Settings->GetMaxDimension();
        }
    }
    return 0.f;
}

void AChaserCharacter::RefreshTerrorRadiusAudio()
{
    if (!TerrorRadiusAudio)
    {
        return;
    }

    // 데디서버는 소리를 낼 일이 없고, 술래 본인은 자기 아우라를 듣지 않음
    const bool bShouldPlay = GetNetMode() != NM_DedicatedServer && !IsLocallyControlled();

    if (bShouldPlay)
    {
        if (!TerrorRadiusAudio->IsPlaying() && TerrorRadiusAudio->Sound)
        {
            TerrorRadiusAudio->Play();
        }
    }
    else if (TerrorRadiusAudio->IsPlaying())
    {
        TerrorRadiusAudio->Stop();
    }
}
