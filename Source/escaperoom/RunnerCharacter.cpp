// RunnerCharacter.cpp
#include "RunnerCharacter.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/LocalPlayer.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Curves/CurveFloat.h"
#include "Net/UnrealNetwork.h"
#include "Interactable.h"
#include "CabinetActor.h"
#include "ExitActor.h"
#include "DoorActor.h"
#include "WFCGridManager.h"
#include "EscapeRoomGameState.h"
#include "Kismet/GameplayStatics.h"
#include "MatchPlayerState.h"
#include "DungeonPlayerController.h"

ARunnerCharacter::ARunnerCharacter()
{
    // TODO: 러너 전용 초기값이 필요하면 여기서 설정 (예: 기본 이동속도 등)
}

void ARunnerCharacter::BeginPlay()
{
    Super::BeginPlay();

    // 서버/모든 클라이언트의 시작 속도를 WalkSpeed로 통일 (CharacterMovement BP 기본값이 WalkSpeed와 달라도
    // 서버-클라 속도가 어긋나지 않도록)
    SetSprintingLocal(false);

    // 어떤 PlayerController가 빙의했든 상관없이 동작하도록 APlayerController로 캐스팅
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            if (RunnerMappingContext)
            {
                // 우선순위 1 : 컨트롤러가 추가하는 공통(Move/Look/Jump) 컨텍스트 위에 추가로 쌓음
                Subsystem->AddMappingContext(RunnerMappingContext, 1);
            }
        }
    }
}

void ARunnerCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // 상호작용(홀드 포함) 중에는 카메라/이동 등 무엇이 원인이든 캐릭터 회전이 절대 일어나지 않도록
    // 매 프레임 상호작용 시작 시점의 회전값으로 강제로 되돌린다
    if (bIsInteracting)
    {
        SetActorRotation(InteractLockedRotation);
    }
}

void ARunnerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    // 부모(AescaperoomCharacter)의 Move/Look/Jump 바인딩을 그대로 유지
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        if (SprintAction)
        {
            EIC->BindAction(SprintAction, ETriggerEvent::Started, this, &ARunnerCharacter::StartSprint);
            EIC->BindAction(SprintAction, ETriggerEvent::Completed, this, &ARunnerCharacter::StopSprint);
        }
        if (CrouchAction)
        {
            EIC->BindAction(CrouchAction, ETriggerEvent::Started, this, &ARunnerCharacter::StartCrouch);
            EIC->BindAction(CrouchAction, ETriggerEvent::Completed, this, &ARunnerCharacter::StopCrouch);
        }
        if (InteractAction)
        {
            EIC->BindAction(InteractAction, ETriggerEvent::Started, this, &ARunnerCharacter::Interact);
            // 홀드가 필요한 상호작용(캐비넷 등) 도중에 키를 놓으면 취소되도록 Completed/Canceled 모두 바인딩
            EIC->BindAction(InteractAction, ETriggerEvent::Completed, this, &ARunnerCharacter::CancelHoldInteract);
            EIC->BindAction(InteractAction, ETriggerEvent::Canceled, this, &ARunnerCharacter::CancelHoldInteract);
        }
        if (EmoteAction)
        {
            // Completed(키 뗌)는 더 이상 바인딩하지 않음 - 짧게 탭해도 애니메이션이 끝까지 재생되도록 함
            // (취소는 이동 시에만 Move()에서 처리)
            EIC->BindAction(EmoteAction, ETriggerEvent::Started, this, &ARunnerCharacter::PlayEmote);
        }
        if (EmoteAction2)
        {
            EIC->BindAction(EmoteAction2, ETriggerEvent::Started, this, &ARunnerCharacter::PlayEmote2);
        }
    }
}

void ARunnerCharacter::StartSprint(const FInputActionValue& Value)
{
    // 상호작용/이모트/웅크리기 중에는 스프린트 금지
    if (bIsInteracting || bIsEmoting || bIsCrouching)
    {
        return;
    }

    // 전방(Y) 입력이 좌우(X) 입력보다 확실히 크고, 실제로 앞으로 가는 중일 때만 스프린트 허용
    // (뒤로 가거나 옆으로 걸을 때는 스프린트 속도가 붙지 않음)
    if (FMath::Abs(LastMoveInput.Y) > FMath::Abs(LastMoveInput.X) && LastMoveInput.Y > 0.1f)
    {
        // 로컬에서 바로 적용(반응성) + 서버에도 같은 속도 적용 요청
        SetSprintingLocal(true);
        ServerSetSprinting(true);
    }
}

void ARunnerCharacter::StopSprint(const FInputActionValue& Value)
{
    SetSprintingLocal(false);
    ServerSetSprinting(false);
}

void ARunnerCharacter::SetSprintingLocal(bool bNewSprinting)
{
    bIsSprinting = bNewSprinting;
    ApplyMoveSpeed();
}

void ARunnerCharacter::ApplyMoveSpeed()
{
    if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
    {
        MoveComp->MaxWalkSpeed = bIsCrouching ? CrouchSpeed : (bIsSprinting ? SprintSpeed : WalkSpeed);
    }
}

void ARunnerCharacter::OnRep_IsSprinting()
{
    // 다른 클라이언트 화면의 이 러너(시뮬레이티드 프록시)도 속도/애니메이션을 맞춤
    SetSprintingLocal(bIsSprinting);
}

void ARunnerCharacter::ServerSetSprinting_Implementation(bool bNewSprinting)
{
    // 서버가 확실히 아는 상태(숨음/탈출/웅크림)에서는 달리기 시작을 거부. 끄기(false)는 항상 허용
    if (bNewSprinting && (bIsHiding || bIsEscaped || bIsCrouching))
    {
        return;
    }

    SetSprintingLocal(bNewSprinting);
}

void ARunnerCharacter::ServerSetCrouching_Implementation(bool bNewCrouching)
{
    bIsCrouching = bNewCrouching;
    ApplyMoveSpeed(); // 서버도 같은 속도로 맞춰야 위치 보정(버벅임)이 안 생김
}

void ARunnerCharacter::StartCrouch(const FInputActionValue& Value)
{
    // 상호작용/이모트 중에는 웅크리기 금지
    if (bIsInteracting || bIsEmoting)
    {
        return;
    }

    // 토글이 아니라 키를 누르고 있는 동안만 true. ACharacter 내장 Crouch()는 CharacterMovement의
    // "Can Crouch" 옵션이 꺼져있으면 조용히 무시되는 문제가 있어서 커스텀 bool로 직접 관리.
    bIsCrouching = true;
    ApplyMoveSpeed(); // 본인 화면 즉시 반영
    ServerSetCrouching(true);
}

void ARunnerCharacter::StopCrouch(const FInputActionValue& Value)
{
    bIsCrouching = false;
    ApplyMoveSpeed();
    ServerSetCrouching(false);
}

AActor* ARunnerCharacter::FindInteractableTarget() const
{
    // [변경] 카메라 라인 트레이스 -> 몸 정면 박스 (AescaperoomCharacter::FindInteractableInFrontBox)
    return FindInteractableInFrontBox();
}

void ARunnerCharacter::PawnClientRestart()
{
    Super::PawnClientRestart();

    InteractBlockedUntil = GetWorld()->GetTimeSeconds() + InteractBlockAfterPossess;
}

void ARunnerCharacter::Interact(const FInputActionValue& Value)
{
    // 캐비넷에서 막 나온 직후, 계속 누르고 있던 나가기 키가 상호작용으로 다시 들어오는 것 무시
    if (GetWorld()->GetTimeSeconds() < InteractBlockedUntil)
    {
        return;
    }

    // 이미 상호작용/이모트 중이면 중복 트리거 방지 (탈출한 뒤에는 아예 상호작용 불가)
    if (bIsInteracting || bIsEmoting || bIsEscaped)
    {
        return;
    }

    AActor* Target = FindInteractableTarget();
    if (!Target)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Interact][Runner] No target found"));
        return;
    }

    const float HoldTime = IInteractable::Execute_GetInteractHoldTime(Target, this);
    const FText InteractHoldText = IInteractable::Execute_GetInteractionHoldText(Target, this);
    UE_LOG(LogTemp, Warning, TEXT("[Interact][Runner] Target=%s CanInteract=%d HoldTime=%.2f"),
        *GetNameSafe(Target), IInteractable::Execute_CanInteract(Target, this) ? 1 : 0, HoldTime);
    // [TEMP DEBUG] CanInteract=0 원인이 "이미 부서짐/열림 상태"인지 직접 확인용 - 원인 찾으면 지워도 됨
    if (const ADoorActor* DebugDoor = Cast<ADoorActor>(Target))
    {
        UE_LOG(LogTemp, Warning, TEXT("[Interact][Runner] DoorState IsOpen=%d IsBroken=%d"),
            DebugDoor->IsOpen() ? 1 : 0, DebugDoor->IsBroken() ? 1 : 0);
    }

    // 탈출구는 이미 쓰였거나 다른 러너가 오르는 중이면(복제된 bIsOpen/CurrentClimber 기준) 홀드 자체를
    // 시작하지 않음 - 안 막으면 서버는 거절했는데 내 화면에서만 5.9초 게이지가 차는 상황이 생김
    if (Cast<AExitActor>(Target) && !IInteractable::Execute_CanInteract(Target, this))
    {
        return;
    }

    bIsInteracting = true;
    InteractLockedRotation = GetActorRotation(); // Tick()이 상호작용 중 이 값으로 회전을 고정함

    // 몽타주 재생은 즉시/홀드 상호작용 공통 연출.
    // 탈출구(사다리)는 여기서 로컬로 미리 재생하지 않는다 - ClimbMontage는 서버가 홀드 시작을 승인한
    // 뒤(OnInteractHoldUpdateInternal -> MulticastBeginLadderHold)에 서버 자신을 포함한 모든 클라이언트가
    // 동시에 재생해야 한다. 그 몽타주의 AnimNotify(NotifyClimbStep)가 만드는 위치 변화가 서버의 권위있는
    // 위치로도 인정되어야 CharacterMovementComponent의 클라-서버 교차검증에서 원위치로 되돌아가지 않기
    // 때문 - 로컬로만 먼저 재생해버리면 서버는 이 이동을 전혀 모르는 상태가 되어 결국 보정으로 되돌려짐
    const bool bIsClimbTarget = (Cast<AExitActor>(Target) != nullptr);
    UAnimMontage* MontageToPlay = bIsClimbTarget ? nullptr : InteractMontage;

    bool bPlayedMontage = bIsClimbTarget; // 사다리는 곧 서버 승인을 거쳐 재생될 예정이므로 대체 펄스 타이머 불필요
    if (MontageToPlay)
    {
        // [NetMulticast로 변경] 예전엔 여기서 로컬로만 Montage_Play해서 본인 화면에만 보이고 다른
        // 클라이언트에는 안 보였음. 술래의 KickDoor 몽타주와 동일하게 서버를 거쳐 Multicast로
        // 재생시켜서 본인 포함 모든 클라이언트에서 동일하게 보이게 함 (종료 델리게이트 바인딩도
        // MulticastPlayInteractMontage_Implementation 쪽으로 같이 옮김)
        ServerPlayInteractMontage();
        bPlayedMontage = true;
    }

    if (HoldTime <= 0.f)
    {
        // 즉시 처리 (문 등 기존 오브젝트). 실제 판정/처리는 서버에서 다시 검증됨
        ServerInteractWithTarget(Target);

        if (!bPlayedMontage)
        {
            // 몽타주가 없을 때를 위한 대체 동작 (기존 타이머 방식)
            GetWorldTimerManager().SetTimer(InteractPulseTimerHandle, this, &ARunnerCharacter::EndInteractPulse, InteractPulseDuration, false);
        }
        return;
    }

    // 홀드가 필요한 상호작용 (캐비넷 숨기 등): 키를 뗄 때까지 / 시간이 찰 때까지 대기
    PendingHoldTarget = Target;
    GetWorldTimerManager().SetTimer(InteractHoldTimerHandle, this, &ARunnerCharacter::CompleteHoldInteract, HoldTime, false);

    // 홀드가 시작됐음을 서버에 알려서 미리보기 연출(뚜껑 열리는 애니메이션 등)을 시작시킴
    ServerSetInteractHold(Target, true);

    // 홀드 게이지 바를 보여주기 위해 MatchWidget의 Tick을 켬 (키를 떼거나 홀드가 끝나면 다시 끔).
    // 텍스트도 같이 넘겨서 "캐비닛 열기"처럼 게이지 옆에 무엇을 하는 중인지 보여줌
    SetMatchWidgetHoldGaugeActive(true, InteractHoldText);
}

void ARunnerCharacter::CancelHoldInteract(const FInputActionValue& Value)
{
    CancelHoldInteractInternal();
}

void ARunnerCharacter::CancelHoldInteractInternal()
{
    if (GetWorldTimerManager().IsTimerActive(InteractHoldTimerHandle))
    {
        GetWorldTimerManager().ClearTimer(InteractHoldTimerHandle);

        // 홀드가 취소됐음을 서버에 알려서 미리보기 연출을 원상복구시킴 (타이머를 지우기 전에 대상을 미리 캡처)
        if (AActor* Target = PendingHoldTarget.Get())
        {
            ServerSetInteractHold(Target, false);
        }

        PendingHoldTarget = nullptr;
        bIsInteracting = false;
        SetMatchWidgetHoldGaugeActive(false);

        // 클라이밍 미리보기 때문에 Flying으로 바꿔뒀었다면(사다리를 실제로 타지 않고 홀드만 취소된
        // 경우) 다시 정상 걷기 모드로 복구
        if (bClimbFlightModeActive)
        {
            if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
            {
                MoveComp->SetMovementMode(MOVE_Walking);
            }
            bClimbFlightModeActive = false;
        }

        // 홀드 도중 취소되면 재생 중이던 몽타주도 같이 정지 (블렌드 아웃). InteractMontage/ClimbMontage
        // 둘 중 어느 게 재생 중이었는지 여기서 굳이 구분하지 않고 둘 다 정지 시도 - 재생 중이 아닌
        // 쪽은 Montage_Stop을 불러도 아무 일도 안 일어나서 안전함
        if (GetMesh())
        {
            if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
            {
                if (InteractMontage)
                {
                    AnimInstance->Montage_Stop(0.2f, InteractMontage);
                }
                if (ClimbMontage)
                {
                    AnimInstance->Montage_Stop(0.2f, ClimbMontage);
                }
            }
        }
    }
}

void ARunnerCharacter::CompleteHoldInteract()
{
    // 게이지를 먼저 끄고 상태를 정리한 다음에 서버로 완료를 통지한다 - 순서가 반대였으면(먼저 RPC를
    // 보내고 나중에 정리) 리슨 서버/스탠드얼론에서는 이 RPC가 즉시(같은 프레임 안에서) 처리돼서 캐비넷
    // 등이 Controller->Possess()로 조종권을 가져가버릴 수 있고, 그러면 이 시점의 GetController()가
    // 이미 nullptr이 되어 SetMatchWidgetHoldGaugeActive(false)가 컨트롤러를 못 찾아 조용히 무시되고
    // 게이지 바가 화면에 계속 남는 버그가 생긴다. 그래서 Controller가 확실히 붙어있는 지금 먼저 정리함
    AActor* Target = PendingHoldTarget.Get();

    PendingHoldTarget = nullptr;
    bIsInteracting = false;
    SetMatchWidgetHoldGaugeActive(false);

    if (Target)
    {
        ServerInteractWithTarget(Target);
    }
}

float ARunnerCharacter::GetInteractHoldPercent() const
{
    // Interact()가 건 로컬 타이머(서버 응답을 기다리지 않는 클라이언트 예측)를 그대로 진행률 소스로 사용.
    // 홀드 중이 아니면(취소/완료로 타이머가 지워졌으면) 0
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

void ARunnerCharacter::EndInteractPulse()
{
    bIsInteracting = false;
}

void ARunnerCharacter::OnInteractMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
    bIsInteracting = false;
}

void ARunnerCharacter::OnClimbMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
    // 홀드 취소(MulticastCancelLadderHold)로 끊긴 경우엔 그쪽에서 이미 Walking으로 복구했을 수
    // 있지만, bClimbFlightModeActive 체크가 있어서 두 번 불려도 안전함(이미 꺼져있으면 아무 일도
    // 안 함). 다 올라간 뒤 탈출 완료 판정이 따로 생기기 전까지는, 일단 몽타주가 끝나는 시점 =
    // 클라이밍이 끝나는 시점으로 보고 여기서 Walking으로 되돌린다.
    // 단, 탈출에 성공해서 몽타주가 끊긴 경우엔 OnRep_IsEscaped가 이미 이동을 꺼뒀으므로(MOVE_None) 건드리지 않음
    if (bIsEscaped)
    {
        bClimbFlightModeActive = false;
        return;
    }

    if (bClimbFlightModeActive)
    {
        if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
        {
            MoveComp->SetMovementMode(MOVE_Walking);
        }
        bClimbFlightModeActive = false;
    }
}

void ARunnerCharacter::PlayEmote(const FInputActionValue& Value)
{
    StartEmote(1);
}

void ARunnerCharacter::PlayEmote2(const FInputActionValue& Value)
{
    StartEmote(2);
}

void ARunnerCharacter::StartEmote(int32 EmoteIndex)
{
    // 로컬 게이트: 이미 상호작용/이모트 중이면 서버에 요청조차 보내지 않음 (스팸 방지용 - 실제 승인은 서버가 함)
    if (bIsInteracting || ActiveEmoteIndex != 0 || bIsEscaped)
    {
        return;
    }

    // ActiveEmoteIndex는 일반 Replicated 프로퍼티라(몽타주 리플리케이션과 달리 COND_SkipOwner가 아님)
    // 서버가 값을 바꾸면 본인 포함 전 클라이언트에 자동으로 뿌려짐. 실제 애니메이션 재생/전환은
    // AnimBP 스테이트 머신이 이 값은 보고 알아서 처리하므로 여기선 값 변경 요청만 하면 됨
    ServerPlayEmote(EmoteIndex);
}

void ARunnerCharacter::EndEmote(int32 EmoteIndex)
{
    // 자신이 재생시킨 이모트가 아니면 무시 (그 사이에 취소되고 다른 이모트가 시작됐을 수 있음)
    if (ActiveEmoteIndex != EmoteIndex)
    {
        return;
    }

    // 이동 입력처럼 로컬에서 즉시 반응해야 하는 취소이므로, 서버 왕복을 기다리지 않고 내 화면부터 바로 정리
    // (서버가 뒤늦게 0으로 되돌려 다시 리플리케이트해줘도 이미 같은 값이라 무해함)
    ActiveEmoteIndex = 0;
    OnRep_ActiveEmoteIndex();

    // 다른 클라이언트 화면도 같이 정리되도록 서버에 알림
    ServerStopEmote();
}

void ARunnerCharacter::ServerPlayEmote_Implementation(int32 EmoteIndex)
{
    // EmoteIndex는 1 또는 2만 유효 - 클라이언트가 보낸 값을 그대로 믿지 않고 재검증
    if (EmoteIndex != 1 && EmoteIndex != 2)
    {
        return;
    }

    // 서버 쪽 재검증: bIsInteracting은 클라이언트 로컬 전용 플래그라 서버에서는 신뢰할 수 없지만,
    // ActiveEmoteIndex는 서버 자신이 직접 바꾸는 값이라 이 시점에 서버가 들고 있는 값은 신뢰할 수 있음
    if (ActiveEmoteIndex != 0)
    {
        return;
    }

    ActiveEmoteIndex = EmoteIndex;

    // 리슨 서버 자신의 화면에도 즉시 반영 (OnRep은 원격 클라이언트에서만 자동 호출됨 - bIsCaught 등과 동일한 이유)
    OnRep_ActiveEmoteIndex();
}

void ARunnerCharacter::ServerStopEmote_Implementation()
{
    // 취소는 항상 통과시킴 (ServerSetInteractHold의 bHolding=false 케이스와 동일한 이유)
    ActiveEmoteIndex = 0;
    OnRep_ActiveEmoteIndex();
}

void ARunnerCharacter::OnRep_ActiveEmoteIndex()
{
    // bIsEmoting은 ActiveEmoteIndex를 그대로 미러링만 함 - 실제 어떤 애니메이션을 재생/전환할지는
    // AnimBP 스테이트 머신이 ActiveEmoteIndex 값(0/1/2)을 직접 보고 판단함 (몽타주/Notify 없이
    // 순수 AnimSequence + Time Remaining 전환 조건으로 처리 - bIsCrouching/bIsSprinting과 같은 성격)
    bIsEmoting = (ActiveEmoteIndex != 0);
}

void ARunnerCharacter::Move(const FInputActionValue& Value)
{
    // 이동 입력이 들어오는 순간 재생 중인 이모트가 있으면 즉시 취소 (EndEmote가 로컬 정지 + 서버 알림을
    // 함께 처리). 이동 자체는 막지 않고 그대로 Super::Move()로 넘겨서 처리
    if (ActiveEmoteIndex != 0)
    {
        EndEmote(ActiveEmoteIndex);
    }

    // 홀드 상호작용 중에 이동 입력이 들어오면 홀드를 취소한다. CanMove()가 bIsInteracting 동안
    // 실제 이동은 이미 막아주지만, 그건 "제자리에 멈춰있게"만 할 뿐 홀드 타이머 자체는 계속 돌아가므로
    // 여기서 명시적으로 취소해야 함(이동하려고 하면 홀드가 깨지는 게 자연스러운 동작)
    if (GetWorldTimerManager().IsTimerActive(InteractHoldTimerHandle))
    {
        CancelHoldInteractInternal();
    }

    Super::Move(Value);
}

bool ARunnerCharacter::CanLook() const
{
    return Super::CanLook() && !bIsBeingCaught && !bIsEscaped;
}

bool ARunnerCharacter::CanMove() const
{
    // 이모트는 더 이상 이동을 막지 않음 - 움직이기 시작하면 Move()에서 바로 취소되기 때문.
    // 상호작용/장롱 은신/목조르기 연출 중에는 여전히 이동을 막아서 그 자리에 멈춰있도록 함
    return !bIsInteracting && !bIsHiding && !bIsBeingCaught && !bIsEscaped;
}

void ARunnerCharacter::ServerInteractWithTarget_Implementation(AActor* Target)
{
    // 서버에서 다시 한번 유효성 검사 (클라이언트가 알려준 대상/홀드 완료 여부를 맹신하지 않음)
    if (!Target || !Target->GetClass()->ImplementsInterface(UInteractable::StaticClass()))
    {
        return;
    }

    if (!IInteractable::Execute_CanInteract(Target, this))
    {
        return;
    }

    // InteractMontage 재생은 이제 Interact()에서 ServerPlayInteractMontage() -> MulticastPlayInteractMontage()
    // 경로로 처리됨 (본인 포함 모든 클라이언트에서 동일하게 보이도록). 여기서 다시 재생할 필요 없음
    IInteractable::Execute_OnInteract(Target, this);
}

void ARunnerCharacter::ServerPlayInteractMontage_Implementation()
{
    MulticastPlayInteractMontage();
}

void ARunnerCharacter::MulticastPlayInteractMontage_Implementation()
{
    if (!InteractMontage || !GetMesh())
    {
        return;
    }
    if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
    {
        AnimInstance->Montage_Play(InteractMontage);

        // 몽타주가 끝나는 시점(정상 종료든 다른 몽타주에 의해 중단되든)에 맞춰 bIsInteracting을 false로 복귀
        FOnMontageEnded EndDelegate;
        EndDelegate.BindUObject(this, &ARunnerCharacter::OnInteractMontageEnded);
        AnimInstance->Montage_SetEndDelegate(EndDelegate, InteractMontage);
    }
}

void ARunnerCharacter::ServerSetInteractHold_Implementation(AActor* Target, bool bHolding)
{
    if (!Target || !Target->GetClass()->ImplementsInterface(UInteractable::StaticClass()))
    {
        return;
    }

    // 홀드를 "시작"할 때만 서버에서 다시 유효성 검사. 취소(false)는 항상 통과시켜서
    // 미리보기 연출이 어떤 경우에도 확실히 원상복구되도록 함
    if (bHolding && !IInteractable::Execute_CanInteract(Target, this))
    {
        return;
    }

    IInteractable::Execute_OnInteractHoldUpdate(Target, this, bHolding);
}

void ARunnerCharacter::EnterCabinet(ACabinetActor* Cabinet)
{
    // 상태를 바꾸는 쪽은 항상 서버 (ACabinetActor::StartHiding에서 서버 권한으로만 호출됨)
    if (!HasAuthority() || !Cabinet || bIsHiding)
    {
        return;
    }

    // 나중에 나갈 때 이 위치로 복귀시키기 위해 숨기 직전 위치/회전을 저장
    PreHideTransform = GetActorTransform();

    CurrentCabinet = Cabinet;
    bIsHiding = true;

    // 술래가 부딪히거나 감지하지 못하도록 콜리전을 끄고 이동을 정지
    if (UCapsuleComponent* Capsule = GetCapsuleComponent())
    {
        Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    if (UCharacterMovementComponent* Movement = GetCharacterMovement())
    {
        Movement->StopMovementImmediately();
        Movement->DisableMovement();
    }

    // 캐비넷 위치에 겹치도록 이동 (ParkPoint가 있으면 그 위치/방향, 없으면 캐비넷 액터 위치)
    if (USceneComponent* Park = Cabinet->GetParkPoint())
    {
        SetActorLocationAndRotation(Park->GetComponentLocation(), Park->GetComponentRotation());
    }
    else
    {
        SetActorLocation(Cabinet->GetActorLocation());
    }

    // 리슨 서버 자신의 화면에서도 즉시 반영 (OnRep은 원격 클라이언트에서만 자동 호출됨)
    OnRep_IsHiding();
}

void ARunnerCharacter::ExitCabinet(bool bRestoreOriginalPosition)
{
    if (!HasAuthority() || !bIsHiding)
    {
        return;
    }

    // 콜리전을 다시 켜기 전에 먼저 위치부터 복귀시켜서, 다른 물체와 겹친 채로 콜리전이 켜지는 것을 방지
    if (bRestoreOriginalPosition)
    {
        SetActorLocationAndRotation(PreHideTransform.GetLocation(), PreHideTransform.GetRotation());
    }

    if (UCapsuleComponent* Capsule = GetCapsuleComponent())
    {
        Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    }
    if (UCharacterMovementComponent* Movement = GetCharacterMovement())
    {
        Movement->SetMovementMode(MOVE_Walking);
    }

    bIsHiding = false;
    CurrentCabinet = nullptr;

    OnRep_IsHiding();
}

void ARunnerCharacter::OnRep_IsHiding()
{
    // 실제 시각 연출(숨어있는 동안 캡슐/메시 숨김 등)이 필요하면 AnimBP/블루프린트에서
    // 이 bIsHiding 값을 참고해서 처리 (숨어있는 동안은 이 캐릭터가 Possess되어 있지 않음)
}

bool ARunnerCharacter::FindJailTransform(FTransform& OutTransform) const
{
    AWFCGridManager* GridManager = Cast<AWFCGridManager>(UGameplayStatics::GetActorOfClass(GetWorld(), AWFCGridManager::StaticClass()));
    if (!GridManager)
    {
        return false;
    }

    // 감옥은 WFC 빅룸(예: Scientest) 안에 이미 배치된 메시에 "Jail" 컴포넌트 태그만 붙여서 찾는다 -
    // AChaserCharacter::CacheAllCeilings()가 "Ceiling" 태그를 찾는 것과 동일한 패턴
    for (const TObjectPtr<AActor>& BigRoom : GridManager->GetSpawnedBigRoomActors())
    {
        if (!BigRoom)
        {
            continue;
        }

        TInlineComponentArray<USceneComponent*> Comps;
        BigRoom->GetComponents(Comps);

        for (USceneComponent* Comp : Comps)
        {
            if (Comp && Comp->ComponentTags.Contains(TEXT("Jail")))
            {
                OutTransform = Comp->GetComponentTransform();
                return true;
            }
        }
    }

    return false;
}

void ARunnerCharacter::Capture()
{
    if (!HasAuthority() || bIsCaught)
    {
        return;
    }

    FTransform JailTransform;
    if (!FindJailTransform(JailTransform))
    {
        // "Jail" 컴포넌트 태그가 붙은 액터를 못 찾음 - WFC Scientest 빅룸 안 감옥 메시에 태그가
        // 붙어있는지 확인 필요. 못 찾으면 잡힌 처리를 하지 않는다(이동 없이 bIsCaught/
        // NotifyRunnerResolved만 반영되어 상태가 어긋나는 것을 막기 위함)
        UE_LOG(LogTemp, Warning, TEXT("ARunnerCharacter::Capture: \"Jail\" 태그를 찾지 못해 %s를 감옥으로 보내지 못함"), *GetName());

        // [중요] 여기서 그냥 return하면, 목조르기 연출(MulticastBeginChoked)로 켜놨던 bIsBeingCaught가
        // true로 영원히 남아 CanMove()가 계속 이동을 막아버림(감옥을 못 찾았을 뿐인데 러너가 영구
        // 고정되는 버그) - 실패 케이스에서도 반드시 풀어줘야 함
        bIsBeingCaught = false;
        MulticastStopBeingChoked(); // 마지막 프레임에 멈춰둔 목졸림 포즈/들어올리기/이동 잠금도 실패 케이스에서 풀어줘야 함
        return;
    }

    bIsBeingCaught = false; // 목조르기 연출이 끝나서 여기 도달한 것이므로 이동 잠금 해제
    MulticastStopBeingChoked(); // 마지막 프레임에 멈춰둔 BeingChokedMontage를 Stop + 이동모드/잠금 복구 (모든 머신)

    // EnterCabinet()과 달리 콜리전/이동은 건드리지 않음 - 기획상 감옥 안에서는 계속 자유롭게
    // 움직일 수 있어야 하므로 위치/회전만 옮긴다. 캡슐 콜리전은 켜진 채로 유지되므로, 여러 명이
    // 동시에 같은 지점에 도착해도 CharacterMovementComponent가 겹친 만큼 알아서 밀어내며 벌어진다
    //
    // bIsCaught = true / 남은 러너 수 갱신은 여기서 하지 않음 - 순간이동하는 순간 감옥 구역 Box
    // (UJailGateComponent::JailAreas)의 BeginOverlap이 서버에서 SetCaught(true)를 불러서 처리한다.
    // 그래서 JailTransform(도착 지점)은 반드시 그 Box 안쪽이어야 함
    SetActorLocationAndRotation(JailTransform.GetLocation(), JailTransform.GetRotation());
}

void ARunnerCharacter::SetCaught(bool bNewCaught)
{
    if (!HasAuthority() || bIsCaught == bNewCaught)
    {
        return;
    }

    bIsCaught = bNewCaught;

    // 리슨 서버 자신의 화면에도 즉시 반영 (OnRep은 원격 클라이언트에서만 자동 호출됨)
    OnRep_IsCaught();

    // PlayerState에도 복사 - 캐릭터는 거리 때문에 다른 클라에 복제가 끊길 수 있지만 PlayerState는
    // 항상 전원에게 복제되므로, MatchWidget의 플레이어 목록(감옥 이미지)은 이 값을 읽음
    if (AMatchPlayerState* MPS = GetPlayerState<AMatchPlayerState>())
    {
        MPS->SetInJail(bIsCaught);
    }

    // 남은 러너 수 갱신 - 잡히면 -1, 풀려나면 +1
    if (AEscapeRoomGameState* GS = GetWorld()->GetGameState<AEscapeRoomGameState>())
    {
        if (bIsCaught)
        {
            GS->NotifyRunnerResolved();

            // 알림: "닉네임님이 감옥에 갇혔습니다" - 위의 bIsCaught == bNewCaught 가드 덕분에
            // 감옥 박스 오버랩이 여러 번 들어와도 실제로 상태가 바뀔 때 한 번만 나감
            const APlayerState* PS = GetPlayerState();
            GS->MulticastMatchNotify(FText::Format(
                NSLOCTEXT("MatchNotify", "RunnerJailed", "{0} has been jailed!"),
                FText::FromString(PS ? PS->GetPlayerName() : GetName())));
        }
        else
        {
            GS->NotifyRunnerRestored();
        }
    }
}

void ARunnerCharacter::MulticastBeginChoked_Implementation(FVector TargetLocation, FRotator TargetRotation)
{
    // 사다리와 달리 여기서는 위치를 완전히 스냅시켜도 문제없음(쌓아올린 상태 같은 게 없음) -
    // 술래의 ChokeGrabPoint 자리로 정확히 옮겨야 두 몽타주가 애니메이터 의도대로 맞물림
    SetActorLocationAndRotation(TargetLocation, TargetRotation);
    bIsBeingCaught = true;

    // 들어올리기(ChokeLift)의 기준 위치 + 이전 상태 초기화
    ChokeLiftBaseLocation = GetActorLocation();
    ChokeLiftOffset = 0.f;
    bChokeLiftActive = false;

    // Walking이면 CMC가 매 틱 캡슐을 바닥으로 되돌려서 들어올리기가 무효화되고, Flying도 관성/중력 처리가
    // 남아있음 → 연출 동안은 MOVE_None으로 CMC의 위치 개입을 완전히 끔 (MulticastStopBeingChoked에서 복구)
    if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
    {
        MoveComp->StopMovementImmediately();
        MoveComp->SetMovementMode(MOVE_None);
        bChokeMovementDisabled = true;
    }

    if (BeingChokedMontage && GetMesh())
    {
        if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
        {
            // 마지막 프레임 유지는 몽타주 에셋의 "Enable Auto Blend Out" 해제로 처리 (타이머/Pause 불필요)
            AnimInstance->Montage_Play(BeingChokedMontage);
        }
    }
}

void ARunnerCharacter::BeginChokeLift(float InLiftHeight, float InDuration, float InEaseExponent, UCurveFloat* InLiftCurve)
{
    // 연출이 이미 끝났으면(Capture 이후 블렌드아웃 중 등) 무시 - 감옥에 간 러너를 다시 움직이지 않도록
    if (!bIsBeingCaught)
    {
        return;
    }

    bChokeLiftActive = true;
    // Notify를 여러 개 이어 붙여도(올렸다가 음수로 내리기 등) 끊김 없이 이어지도록 현재 높이 기준으로 시작
    ChokeLiftStartOffset = ChokeLiftOffset;
    ChokeLiftHeight = InLiftHeight;
    ChokeLiftElapsed = 0.f;
    ChokeLiftDuration = FMath::Max(InDuration, KINDA_SMALL_NUMBER);
    ChokeLiftEaseExponent = FMath::Max(InEaseExponent, 1.f);
    ChokeLiftCurve = InLiftCurve;

    // 구간이 끝났을 때의 최종 높이 (커브면 X=1 지점의 Y값 기준 - 커브가 1에서 안 끝나도 정확히 그 값으로 끝남)
    ChokeLiftTargetOffset = ChokeLiftStartOffset + ChokeLiftHeight * EvaluateChokeLiftFraction(1.f);
}

void ARunnerCharacter::UpdateChokeLift(float DeltaTime)
{
    if (!bChokeLiftActive || !bIsBeingCaught)
    {
        return;
    }

    ChokeLiftElapsed += DeltaTime;
    const float Alpha = FMath::Clamp(ChokeLiftElapsed / ChokeLiftDuration, 0.f, 1.f);
    ChokeLiftOffset = ChokeLiftStartOffset + ChokeLiftHeight * EvaluateChokeLiftFraction(Alpha);

    // 누적 이동(AddActorWorldOffset)이 아니라 기준 위치 + 현재 높이로 절대 세팅 → 프레임 오차가 쌓이지 않음
    SetActorLocation(ChokeLiftBaseLocation + FVector(0.f, 0.f, ChokeLiftOffset), false, nullptr, ETeleportType::TeleportPhysics);
}

float ARunnerCharacter::EvaluateChokeLiftFraction(float Alpha) const
{
    if (ChokeLiftCurve)
    {
        // 커브 값은 클램프하지 않음 - 1을 넘기거나(살짝 더 올렸다 내려오기) 음수도 의도적으로 쓸 수 있게
        return ChokeLiftCurve->GetFloatValue(Alpha);
    }
    return FMath::InterpEaseInOut(0.f, 1.f, Alpha, ChokeLiftEaseExponent);
}

void ARunnerCharacter::EndChokeLift()
{
    if (!bChokeLiftActive)
    {
        return;
    }
    bChokeLiftActive = false;

    if (!bIsBeingCaught)
    {
        return;
    }

    // 프레임 오차로 목표 높이에 딱 안 닿는 경우 방지 - 끝나면 정확히 최종 높이로 맞추고 그대로 유지
    // (Capture()에서 감옥으로 보낼 때까지 공중에 떠 있음)
    ChokeLiftOffset = ChokeLiftTargetOffset;
    SetActorLocation(ChokeLiftBaseLocation + FVector(0.f, 0.f, ChokeLiftOffset), false, nullptr, ETeleportType::TeleportPhysics);
}

void ARunnerCharacter::MulticastStopBeingChoked_Implementation()
{
    // [FIX] 예전엔 bIsBeingCaught를 서버의 Capture()에서만 false로 되돌려서, 러너 본인 클라이언트
    // (CanMove()를 실제로 검사하는 곳)에서는 계속 true로 남아 감옥에서 못 움직이는 문제가 있었음.
    // 모든 머신에서 실행되는 이 Multicast에서 같이 풀어줌
    bIsBeingCaught = false;

    // 들어올리기 종료 (높이 자체는 성공 시 서버가 곧바로 감옥으로 순간이동시키고, 실패 시엔 아래
    // Walking 복구로 공중에서 자연스럽게 떨어짐)
    bChokeLiftActive = false;
    ChokeLiftOffset = 0.f;
    ChokeLiftCurve = nullptr;

    if (bChokeMovementDisabled)
    {
        if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
        {
            MoveComp->SetMovementMode(MOVE_Walking);
        }
        bChokeMovementDisabled = false;
    }

    if (BeingChokedMontage && GetMesh())
    {
        if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
        {
            // Auto Blend Out을 꺼둬서 마지막 프레임에 멈춰있던(혹은 아직 재생 중이던) 포즈를 풀고
            // 몽타주에 설정된 블렌드아웃 시간으로 로코모션에 되돌아가게 함
            AnimInstance->Montage_Stop(BeingChokedMontage->BlendOut.GetBlendTime(), BeingChokedMontage);
        }
    }
}

void ARunnerCharacter::MulticastBeginLadderClimb_Implementation(FVector TargetLocation, FRotator TargetRotation)
{
    // 예전엔 여기서 TargetLocation(ClimbStartPoint)으로 위치를 강제 스냅시켰는데, 그러면 홀드하는
    // 동안 MulticastBeginLadderHold의 루프 노티파이로 쌓아올린 높이가 이 순간 전부 사라지고 처음
    // 시작 지점으로 되돌아가버렸다. 그래서 위치는 더 이상 건드리지 않고 "홀드가 끝난 그 자리"를
    // 그대로 이어받는다. 각도만 사다리를 정면으로 보도록 맞춰줌(높이와 달리 각도는 노티파이가
    // 바꾸는 값이 아니라서 스냅시켜도 홀드 중 쌓인 걸 지우지 않음)
    SetActorRotation(TargetRotation);

    // 실제 클라이밍 구간도 Walking 모드면 AnimNotify_ClimbStep의 상승이 매 틱 무효화되므로 계속
    // Flying 유지 (홀드 단계에서 이미 켜져있었겠지만, 방어적으로 여기서도 확실히 켜둠). 다 올라간 뒤
    // 다시 Walking으로 되돌리는 처리는 탈출 완료 판정 로직이 생기면 그때 같이 추가하면 됨 (아직 미구현)
    if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
    {
        MoveComp->SetMovementMode(MOVE_Flying);
        bClimbFlightModeActive = true;
    }

    // 몽타주는 여기서 더 이상 건드리지 않는다. 홀드 단계(MulticastBeginLadderHold)에서 이미
    // ClimbMontage 전체(고개 두리번 + 실제 클라이밍)를 한 번 재생했고, InteractHoldTime을 그
    // 몽타주 길이에 맞춰뒀으므로 홀드가 끝나는 시점 = 몽타주가 끝나는 시점이어야 한다. 여기서
    // Montage_Play를 다시 부르면(IsPlaying 체크를 넣어도) 그 시점의 타이밍 오차 때문에 처음부터
    // 다시 재생되는 경우가 생겨서, 아예 재생 자체를 시도하지 않는다
}

void ARunnerCharacter::MulticastBeginLadderHold_Implementation()
{
    // AExitActor::OnInteractHoldUpdateInternal이 홀드 시작(bHolding=true)을 서버에서 승인한 뒤 호출.
    // Multicast라 서버 자신 + 모든 클라이언트에서 동시에 재생됨 - 서버 자신도 이 몽타주를 재생하고
    // 있어야, 그 안의 AnimNotify(NotifyClimbStep)가 서버의 권위있는 위치를 직접 움직이게 되어
    // 클라-서버 교차검증에서 더 이상 원위치로 되돌아가지 않는다
    if (!ClimbMontage || !GetMesh())
    {
        return;
    }

    if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
    {
        // 루프시키지 않고 한 번만 재생 - 시작/끝 프레임 포즈가 안 맞아서 루프 지점마다 캐릭터가
        // 튀는 문제가 있었음(Default 섹션을 자기 자신으로 되돌리던 코드를 제거). InteractHoldTime을
        // 이 몽타주 재생 시간과 맞춰뒀으므로(AExitActor::InteractHoldTime = 5.9f) 홀드가 끝날 때쯤
        // 몽타주도 한 번 다 재생되고 끝난다
        AnimInstance->Montage_Play(ClimbMontage);

        // 몽타주가 끝나는 시점(정상 종료/중단 둘 다)에 Flying 모드를 정리하기 위한 델리게이트.
        // 안 걸어두면 몽타주가 끝난 뒤에도 Flying이 계속 켜진 채로 남아서 캐릭터가 허공에 뜨고,
        // 이동하면 Walking 특유의 바닥 마찰이 없어서 미끄러지는 것처럼 보인다
        FOnMontageEnded EndDelegate;
        EndDelegate.BindUObject(this, &ARunnerCharacter::OnClimbMontageEnded);
        AnimInstance->Montage_SetEndDelegate(EndDelegate, ClimbMontage);
    }

    // Walking 모드에서는 CharacterMovementComponent가 매 틱 캡슐을 바닥 높이로 되돌려버려서
    // AnimNotify_ClimbStep의 AddActorWorldOffset이 무효화된다. Flying으로 바꿔서 그 바닥 스냅
    // 로직을 끈다. 취소되면 MulticastCancelLadderHold에서 다시 Walking으로 복구함
    if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
    {
        MoveComp->SetMovementMode(MOVE_Flying);
        bClimbFlightModeActive = true;
    }
}

void ARunnerCharacter::MulticastCancelLadderHold_Implementation()
{
    // AExitActor::OnInteractHoldUpdateInternal이 홀드 취소(bHolding=false)를 서버에서 처리한 뒤 호출.
    // MulticastBeginLadderHold로 모든 클라이언트에서 시작시켰던 걸 다시 모든 클라이언트에서 원상복구함
    if (ClimbMontage && GetMesh())
    {
        if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
        {
            AnimInstance->Montage_Stop(0.2f, ClimbMontage);
        }
    }

    if (bClimbFlightModeActive)
    {
        if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
        {
            MoveComp->SetMovementMode(MOVE_Walking);
        }
        bClimbFlightModeActive = false;
    }
}

void ARunnerCharacter::NotifyClimbStep(float HeightDelta)
{
    // Multicast로 모든 머신에서 동일하게 재생되는 몽타주에 심긴 Notify가 각자 로컬에서 이걸 부르므로,
    // 서버/모든 클라이언트가 각자 독립적으로 같은 만큼 이동시켜 결과적으로 다 같이 맞게 보인다
    // (DiceActor 흔들림 등 기존 순수 비주얼 연출과 동일한 방식 - HasAuthority 체크 불필요)
    AddActorWorldOffset(FVector(0.f, 0.f, HeightDelta), false);

    // CharacterMovementComponent는 자기 이동 파이프라인을 거치지 않은 위치 변화를 다음 틱에
    // "어긋났다"고 판단해서 원래 위치로 되돌려버린다 (ACharacter::TeleportTo가 내부적으로
    // bJustTeleported를 켜주는 것과 같은 이유). 그래서 매 노티파이마다 이 플래그를 켜서
    // "방금 그 위치 변화는 의도된 것이니 보정하지 마라"고 CMC에 알려줘야 함
    if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
    {
        MoveComp->bJustTeleported = true;
    }
}

void ARunnerCharacter::OnRep_IsCaught()
{
    // TODO: 잡힌 뒤 시각 연출(예: 손이 묶인 애니메이션, UI 표시 등)이 필요하면 이 값을 참고해서 처리
}

void ARunnerCharacter::SetEscaped()
{
    if (!HasAuthority() || bIsEscaped || bIsCaught)
    {
        return;
    }

    bIsEscaped = true;

    // 서버 자신에게도 즉시 반영 (OnRep은 원격 클라이언트에서만 자동 호출됨 - SetCaught와 동일한 이유)
    OnRep_IsEscaped();

    // PlayerState에도 복사 - 플레이어 목록 UI(탈출 이미지)와 관전 대상 선정은 이 값을 읽음
    if (AMatchPlayerState* MPS = GetPlayerState<AMatchPlayerState>())
    {
        MPS->SetEscaped(true);
    }

    // 남은 러너 수 -1 (탈출은 되돌아오지 않으므로 Restore 짝이 없음)
    if (AEscapeRoomGameState* GS = GetWorld()->GetGameState<AEscapeRoomGameState>())
    {
        GS->NotifyRunnerResolved();

        // 알림: "닉네임님이 탈출했습니다" (남은 탈출구 알림은 이 직후 AExitActor가 보냄 -
        // 같은 액터의 Reliable 멀티캐스트라 클라에서도 이 순서 그대로 도착함)
        const APlayerState* PS = GetPlayerState();
        GS->MulticastMatchNotify(FText::Format(
            NSLOCTEXT("MatchNotify", "RunnerEscaped", "{0} has escaped!"),
            FText::FromString(PS ? PS->GetPlayerName() : GetName())));
    }

    // 본인 화면: 비네트 페이드 -> 관전 모드. 폰은 그대로 빙의된 채로 두고(숨김+조작 차단 상태),
    // 시점만 컨트롤러의 ViewTarget으로 다른 폰에 붙인다
    if (ADungeonPlayerController* DPC = Cast<ADungeonPlayerController>(GetController()))
    {
        DPC->ClientBeginEscape();
    }

    ForceNetUpdate();
}

void ARunnerCharacter::OnRep_IsEscaped()
{
    if (!bIsEscaped)
    {
        return;
    }

    // 홀드/상호작용 로컬 상태 정리 (본인 클라이언트 기준 - 다른 머신에선 원래 비어있어서 무해함)
    GetWorldTimerManager().ClearTimer(InteractHoldTimerHandle);
    PendingHoldTarget = nullptr;
    bIsInteracting = false;
    if (IsLocallyControlled())
    {
        SetMatchWidgetHoldGaugeActive(false);
    }

    // 사다리 몽타주 등 재생 중이던 몽타주 정지 (OnClimbMontageEnded는 bIsEscaped를 보고 Walking 복구를 건너뜀)
    if (GetMesh())
    {
        if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
        {
            AnimInstance->StopAllMontages(0.f);
        }
    }
    bClimbFlightModeActive = false;

    // 이동 완전 정지 (MOVE_None - 점프/중력/이동 입력 전부 무효)
    if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
    {
        MoveComp->StopMovementImmediately();
        MoveComp->DisableMovement();
    }

    // 모든 화면에서 사라지게 + 술래 잡기/상호작용 트레이스에도 안 걸리게
    SetActorHiddenInGame(true);
    SetActorEnableCollision(false);
}

void ARunnerCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(ARunnerCharacter, bIsHiding);
    DOREPLIFETIME(ARunnerCharacter, bIsCaught);
    DOREPLIFETIME(ARunnerCharacter, bIsEscaped);
    DOREPLIFETIME(ARunnerCharacter, ActiveEmoteIndex);
    DOREPLIFETIME_CONDITION(ARunnerCharacter, bIsSprinting, COND_SkipOwner);
    DOREPLIFETIME_CONDITION(ARunnerCharacter, bIsCrouching, COND_SkipOwner);
}
