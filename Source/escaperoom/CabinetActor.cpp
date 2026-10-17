// CabinetActor.cpp
#include "CabinetActor.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "RunnerCharacter.h"
#include "ChaserCharacter.h"
#include "DungeonPlayerController.h"
#include "MatchWidget.h"
#include "ActorSequenceComponent.h"
#include "ActorSequencePlayer.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"

ACabinetActor::ACabinetActor()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;
    AutoPossessPlayer = EAutoReceiveInput::Disabled;
    AutoPossessAI = EAutoPossessAI::Disabled;

    // 루트(AMiscActor::Root, 순수 SceneComponent)는 이미 부모 생성자에서 만들어졌으므로,
    // 메시/파크포인트/피크암은 전부 그 밑에 형제로 붙이기만 하면 됨
    // (메시 위치를 나중에 아트 정렬용으로 옮기거나 다른 메시로 바꿔도 나머지가 안 끌려가게)
    CabinetMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CabinetMesh"));
    CabinetMesh->SetupAttachment(Root);

    ParkPoint = CreateDefaultSubobject<USceneComponent>(TEXT("ParkPoint"));
    ParkPoint->SetupAttachment(Root);

    // 잡았을 때 술래가 옮겨질 위치 마커 - 기본값은 캐비넷 정면(X축) 100cm 앞, 캐비넷을 바라보는 방향.
    // 실제 위치/방향은 블루프린트에서 조정
    ChaserCatchPoint = CreateDefaultSubobject<USceneComponent>(TEXT("ChaserCatchPoint"));
    ChaserCatchPoint->SetupAttachment(Root);
    ChaserCatchPoint->SetRelativeLocation(FVector(100.f, 0.f, 0.f));
    ChaserCatchPoint->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));

    // 문틈 훔쳐보기: 아주 짧은 스프링암을 회전시키면 그 지점을 중심으로 도는 효과가 그대로 나옴
    PeekArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("PeekArm"));
    PeekArm->SetupAttachment(Root);
    PeekArm->TargetArmLength = 15.f;
    PeekArm->bUsePawnControlRotation = false;
    PeekArm->bDoCollisionTest = false;
    PeekArm->bInheritPitch = true;
    PeekArm->bInheritYaw = true;
    PeekArm->bInheritRoll = false;

    PeekCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("PeekCamera"));
    PeekCamera->SetupAttachment(PeekArm, USpringArmComponent::SocketName);
}

void ACabinetActor::BeginPlay()
{
    Super::BeginPlay();

    // 에디터에서 PeekArm을 캐비넷 문틈이 실제로 바라보는 방향으로 돌려뒀을 것이므로,
    // 그 값을 "정면" 기준으로 캐싱해두고 Look 입력은 여기에 오프셋으로만 더함
    if (PeekArm)
    {
        PeekHomeRotation = PeekArm->GetRelativeRotation();
    }

    // 블루프린트에 직접 추가된 ActorSequenceComponent를 찾아 캐싱 (없으면 시퀀스 연출만 생략됨)
    OpeningSequence = FindComponentByClass<UActorSequenceComponent>();
    if (OpeningSequence && OpeningSequence->GetSequencePlayer())
    {
        OpeningSequence->GetSequencePlayer()->OnFinished.AddUniqueDynamic(this, &ACabinetActor::OnOpeningSequenceFinished);
    }
}

void ACabinetActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(ACabinetActor, OccupyingRunner);
    DOREPLIFETIME(ACabinetActor, bIsBeingOpened);
    DOREPLIFETIME(ACabinetActor, bOpenCompleted);
    DOREPLIFETIME(ACabinetActor, bIsHoldPreview);
    DOREPLIFETIME(ACabinetActor, bIsExitHoldPreview);
}

bool ACabinetActor::CanInteractInternal(const APawn* Interactor) const
{
    if (!Interactor) return false;

    if (IsChaser(Interactor))
    {
        // 술래: 이미 여는 중이 아니면 언제든 열기 시도 가능 (비어있어도 "허탕" 상호작용은 허용).
        // 단, 지금 여는 중인 그 술래 본인(CurrentOpener)의 완료 요청은 항상 통과시켜야 함 -
        // 그렇지 않으면 홀드 완료 시점(bIsBeingOpened==true)에 자기 자신이 막혀서 절대 안 열림
        return !bIsBeingOpened || CurrentOpener == Interactor;
    }

    // 도망자: 비어있고 술래가 여는 중이 아니고, 다른 도망자가 먼저 홀드를 시작한 상태가 아닐
    // 때만 숨기 위해 상호작용 가능(본인이 이미 홀드 중인 재시도는 통과시킴 - CurrentOpener와 동일한 이유).
    // (나가기는 이 인터페이스를 쓰지 않고, 캐비넷을 조종 중일 때의 전용 Exit 입력으로 처리됨)
    return OccupyingRunner == nullptr && !bIsBeingOpened && (PendingHider == nullptr || PendingHider == Interactor);
}

float ACabinetActor::GetInteractHoldTime_Implementation(APawn* Interactor)
{
    if (Interactor && IsChaser(Interactor))
    {
        // 술래는 OpenDuration만큼 직접 홀드해야 함(즉시 발동 아님) - 홀드 시작/취소가
        // OnInteractHoldUpdate_Implementation을 통해 그대로 StartOpening/CancelOpening에 연결됨
        return OpenDuration;
    }

    return RequiredHoldTime;
}

void ACabinetActor::OnInteract_Implementation(APawn* Interactor)
{
    // 실제 상태 변경은 서버에서만. 호출부(캐릭터)는 서버 RPC를 거쳐서 여기 도달해야 함
    if (!HasAuthority() || !Interactor) return;

    if (IsChaser(Interactor))
    {
        // 술래 클라이언트의 홀드 타이머(AChaserCharacter::InteractHoldTimerHandle)가 OpenDuration을
        // 다 채우고 나서 여기 도달한 것 - 채널링 완료를 확정하고 즉시 판정한다
        bOpenCompleted = true; // 취소가 아니라 완료로 끝났음 (OnRep에서 열자마자 닫는 연출 분기용)
        bIsBeingOpened = false;
        CurrentOpener = nullptr;
        OnRep_IsBeingOpened();

        // 열기 홀드 몽타주 정지 → 아래에서 목조르기(러너 있음) 또는 허탕(비어있음) 몽타주로 넘어감
        AChaserCharacter* OpenerChaser = Cast<AChaserCharacter>(Interactor);
        if (OpenerChaser)
        {
            OpenerChaser->MulticastStopCabinetOpen();
        }

        if (!OccupyingRunner)
        {
            // 허탕: 비어있는 캐비넷을 연 경우 허탕 몽타주 재생 (재생 중 술래 이동/시점 회전 잠금)
            if (OpenerChaser)
            {
                OpenerChaser->MulticastPlayCabinetEmpty();
            }
            return;
        }

        if (OccupyingRunner)
        {
            ARunnerCharacter* Caught = OccupyingRunner;

            // 조종권부터 원래 도망자에게 돌려준 다음 (캐비넷을 조종한 채로 잡히는 상태 방지) -
            // ExitCabinet으로 숨김 상태/콜리전/이동이 풀린 뒤라야 아래 목조르기 연출(MOVE_None 전환 +
            // ChokeGrabPoint 스냅)이 제대로 덮어씀
            ReturnControlToRunner();

            // Grab으로 잡았을 때와 동일한 목조르기 연출을 거침 (러너를 술래 손으로 스냅 → 양쪽 몽타주 →
            // Choke Capture 노티파이 시점에 Capture()로 감옥 이동). 이미 러너를 캐비넷에서 꺼냈으므로,
            // 연출을 시작하지 못하는 경우(술래 캐릭터가 아님/이미 다른 러너를 조르는 중 등)엔 바로 감옥으로 보냄
            AChaserCharacter* Chaser = Cast<AChaserCharacter>(Interactor);
            if (Chaser && Chaser->CanStartChokeGrab(Caught))
            {
                // 러너가 캐비넷 메시와 겹치지 않도록, 연출 전에 술래를 ChaserCatchPoint(블루프린트에서 배치)로
                // 옮기고 그 방향(Yaw)을 바라보게 함. 높이(Z)는 술래의 현재 높이 유지(바닥에 박히거나 뜨지 않도록).
                // 러너는 StartChokeGrab이 그 순간의 ChokeGrabPoint로 스냅하므로 술래만 옮기면 자동으로 따라옴
                if (ChaserCatchPoint)
                {
                    FVector ChaserLocation = ChaserCatchPoint->GetComponentLocation();
                    ChaserLocation.Z = Chaser->GetActorLocation().Z;
                    const FRotator ChaserRotation(0.f, ChaserCatchPoint->GetComponentRotation().Yaw, 0.f);

                    Chaser->SnapForChokeGrab(ChaserLocation, ChaserRotation);
                }
                Chaser->StartChokeGrab(Caught);
            }
            else
            {
                Caught->Capture();
            }

            // 연출용 훅(뚜껑 열림 애니메이션 등은 블루프린트에서 구현)
            Caught->OnCaughtInCabinet();
            OnOccupantCaught(Caught);
        }
        return;
    }

    // 홀드가 완료되어 여기 도달했으니 미리보기(뚜껑 열림) 연출 상태를 해제 (다음 상호작용을 위해 원상복구)
    SetHoldPreview(false);
    PendingHider = nullptr; // 이 도망자의 홀드 시도는 성공/실패 여부와 무관하게 끝났으므로 정리

    if (OccupyingRunner != nullptr) return; // 이미 다른 도망자가 숨어있음

    if (ARunnerCharacter* Runner = Cast<ARunnerCharacter>(Interactor))
    {
        StartHiding(Runner);
    }
}

void ACabinetActor::StartHiding(ARunnerCharacter* Runner)
{
    if (!Runner) return;

    // Possess로 조종권이 넘어가기 전에 원래 컨트롤러를 캐싱 (넘어간 뒤엔 Runner->GetController()가 null이 됨)
    APlayerController* PC = Cast<APlayerController>(Runner->GetController());

    OccupyingRunner = Runner;
    OccupyingController = PC;

    // 도망자를 캐비넷 위치로 겹쳐서 이동시키고 콜리전/이동 비활성화 (서버 권한 내부에서 처리)
    Runner->EnterCabinet(this);

    if (PC)
    {
        PC->Possess(this);
    }

    OnRep_OccupyingRunner();
}

void ACabinetActor::ReleaseOccupantForLeavingPlayer(APlayerController* LeavingPC)
{
    if (!HasAuthority() || !OccupyingRunner) return;

    ARunnerCharacter* Runner = OccupyingRunner;

    // 숨김 해제 + 조종권을 도망자에게 (캐비넷은 빈 상태로 맵에 남음)
    ReturnControlToRunner(false);

    // 혹시 Possess가 안 돼서 아직 캐비넷을 조종 중이면: 캐비넷은 놔두고 도망자 캐릭터만 직접 정리
    if (LeavingPC && LeavingPC->GetPawn() == this)
    {
        LeavingPC->UnPossess();
        if (IsValid(Runner))
        {
            Runner->Destroy();
        }
    }
}

void ACabinetActor::ReturnControlToRunner(bool bRestoreOriginalPosition)
{
    if (!HasAuthority() || !OccupyingRunner) return;

    ARunnerCharacter* Runner = OccupyingRunner;
    APlayerController* PC = OccupyingController;

    OccupyingRunner = nullptr;
    OccupyingController = nullptr;
    OnRep_OccupyingRunner();

    Runner->ExitCabinet(bRestoreOriginalPosition);

    if (PC)
    {
        PC->Possess(Runner);
    }
}

void ACabinetActor::StartOpening(APawn* Chaser)
{
    // 실제 시간은 술래 클라이언트가 재므로(AChaserCharacter::InteractHoldTimerHandle), 여기서는
    // "여는 중" 상태만 켜서 다른 클라이언트에도 연출이 보이게 함 - 타이머는 걸지 않음
    if (bIsBeingOpened) return;

    bOpenCompleted = false;
    bIsBeingOpened = true;
    CurrentOpener = Chaser;
    OnRep_IsBeingOpened();
}

void ACabinetActor::CancelOpening()
{
    // 홀드 도중 술래가 키를 떼서 취소된 경우 (여기도 타이머는 원래 없었으므로 상태만 원복)
    if (!HasAuthority() || !bIsBeingOpened) return;

    bOpenCompleted = false; // 완료가 아니라 취소
    bIsBeingOpened = false;
    CurrentOpener = nullptr;
    OnRep_IsBeingOpened();
}

void ACabinetActor::OnRep_OccupyingRunner()
{
    OnOccupancyChanged(OccupyingRunner != nullptr);
}

void ACabinetActor::OnRep_IsBeingOpened()
{
    HandleOpeningSequence(bIsBeingOpened);

    // 블루프린트 훅은 그대로 유지 (단, 열기 시퀀스 재생은 이제 C++이 하므로 블루프린트 쪽 재생 로직은 끊어둘 것)
    OnOpeningStateChanged(bIsBeingOpened);
}

void ACabinetActor::HandleOpeningSequence(bool bNowOpening)
{
    GetWorldTimerManager().ClearTimer(OpeningSequenceTimerHandle);
    bCloseWhenOpenFinished = false;

    UActorSequencePlayer* Player = OpeningSequence ? OpeningSequence->GetSequencePlayer() : nullptr;
    if (!Player)
    {
        bOpeningSequenceStarted = false;
        return;
    }

    if (bNowOpening)
    {
        bOpeningSequenceStarted = false;

        // 시퀀스 마지막 프레임이 홀드 끝(OpenDuration)과 맞도록: OpenDuration - 시퀀스 길이만큼 기다렸다가 재생
        const float SequenceLength = Player->GetDuration().AsSeconds();
        const float WaitTime = OpenDuration - SequenceLength;

        if (WaitTime > KINDA_SMALL_NUMBER)
        {
            GetWorldTimerManager().SetTimer(OpeningSequenceTimerHandle, this, &ACabinetActor::StartOpeningSequence, WaitTime, false);
        }
        else
        {
            // 시퀀스가 홀드 시간보다 길면 앞부분을 건너뛰고 바로 재생 → 끝나는 시점은 똑같이 홀드 끝
            Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(-WaitTime, EUpdatePositionMethod::Jump));
            Player->Play();
            PlayOpenSound();
            bOpeningSequenceStarted = true;
        }
        return;
    }

    // 열기가 끝남(완료 또는 취소)
    if (bOpeningSequenceStarted)
    {
        if (bOpenCompleted && Player->IsPlaying() && !Player->IsReversed())
        {
            // 완료인데 이 머신에서는 아직 정방향 재생이 조금 남음(네트워크 지연 등) → 끝까지 열린 걸
            // 보여준 뒤 OnOpeningSequenceFinished에서 바로 역재생해서 닫음
            bCloseWhenOpenFinished = true;
        }
        else
        {
            // 완료: 뚜껑이 다 열린 직후 바로 역재생해서 닫는 연출
            // 취소: 열리던 도중 그 자리에서 역재생해서 다시 닫힘
            Player->PlayReverse();
            PlayCloseSound();
        }
    }
    bOpeningSequenceStarted = false;
}

void ACabinetActor::OnOpeningSequenceFinished()
{
    // 정방향이 끝났을 때만 의미 있음 (역재생이 끝났을 때도 OnFinished가 불리지만 플래그가 꺼져 있어서 무시)
    if (!bCloseWhenOpenFinished)
    {
        return;
    }
    bCloseWhenOpenFinished = false;

    // [FIX] 여기서 바로 PlayReverse하면 안 됨: 시퀀스 플레이어는 재생이 끝날 때 OnFinished를 먼저 브로드캐스트하고
    // 그 뒤에 (지연 처리로) 정지 상태를 적용하는 경우가 있어서, 콜백 안에서 건 역재생이 그 정지에 덮여 무시됨
    // (예전 블루프린트는 Delay 1초를 거친 뒤 역재생해서 이 문제를 피했던 것) → 다음 틱으로 미뤄서 실행
    GetWorldTimerManager().SetTimerForNextTick(this, &ACabinetActor::PlayOpeningSequenceReverse);
}

void ACabinetActor::PlayOpeningSequenceReverse()
{
    // 그 사이 술래가 다시 열기를 시작했으면(새 열기 시퀀스 대기/재생 중) 닫지 않음
    if (bIsBeingOpened)
    {
        return;
    }

    if (UActorSequencePlayer* Player = OpeningSequence ? OpeningSequence->GetSequencePlayer() : nullptr)
    {
        Player->PlayReverse();
        PlayCloseSound();
    }
}

void ACabinetActor::StartOpeningSequence()
{
    UActorSequencePlayer* Player = OpeningSequence ? OpeningSequence->GetSequencePlayer() : nullptr;
    if (!Player || !bIsBeingOpened)
    {
        return;
    }

    Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(0.f, EUpdatePositionMethod::Jump));
    Player->Play();
    PlayOpenSound();
    bOpeningSequenceStarted = true;
}

void ACabinetActor::PlayOpenSound()
{
    if (OpenSound)
    {
        const FVector Loc = CabinetMesh ? CabinetMesh->GetComponentLocation() : GetActorLocation();
        UGameplayStatics::PlaySoundAtLocation(this, OpenSound, Loc, FRotator::ZeroRotator, 1.f, 1.f, 0.f, DoorSoundAttenuation);
    }
}

void ACabinetActor::PlayCloseSound()
{
    if (CloseSound)
    {
        const FVector Loc = CabinetMesh ? CabinetMesh->GetComponentLocation() : GetActorLocation();
        UGameplayStatics::PlaySoundAtLocation(this, CloseSound, Loc, FRotator::ZeroRotator, 1.f, 1.f, 0.f, DoorSoundAttenuation);
    }
}

void ACabinetActor::OnInteractHoldUpdate_Implementation(APawn* Interactor, bool bHolding)
{
    // 실제 상태 변경은 서버 권한에서만
    if (!HasAuthority()) return;

    if (Interactor && IsChaser(Interactor))
    {
        // 술래: 홀드 시작/취소 신호로 "여는 중" 연출 상태만 토글함 (StartOpening/CancelOpening 둘 다
        // 이제 타이머 없이 상태만 바꿈). 실제 완료 판정은 술래 클라이언트의 로컬 홀드 타이머가 끝나서
        // OnInteract가 호출될 때(OnInteract_Implementation) 처리됨
        if (bHolding)
        {
            StartOpening(Interactor);
        }
        else
        {
            CancelOpening();
        }

        // 술래 열기 홀드 몽타주: 시작 시 한 번 재생, 취소 시 정지 (DoorActor의 킥 충전 몽타주와 같은 방식)
        if (AChaserCharacter* Chaser = Cast<AChaserCharacter>(Interactor))
        {
            if (bHolding)
            {
                Chaser->MulticastBeginCabinetOpen();
            }
            else
            {
                Chaser->MulticastStopCabinetOpen();
            }
        }
        return;
    }

    // 도망자: 뚜껑 열림 미리보기 연출용 플래그 토글 + 다른 도망자의 동시 홀드 시도를 막기 위해
    // 지금 홀드 중인 도망자를 기억해둠(취소되면 비움). CanInteract 통과 후 여기 왔으므로
    // 홀드 시작 시점엔 PendingHider가 비어있거나 본인이었던 상태였음이 보장됨
    PendingHider = bHolding ? Interactor : nullptr;
    SetHoldPreview(bHolding);
}

void ACabinetActor::SetHoldPreview(bool bNewHoldPreview)
{
    if (bIsHoldPreview == bNewHoldPreview) return;

    bIsHoldPreview = bNewHoldPreview;

    // 리슨 서버 자신의 화면에도 즉시 반영 (OnRep은 원격 클라이언트에서만 자동 호출됨)
    OnRep_IsHoldPreview();
}

void ACabinetActor::OnRep_IsHoldPreview()
{
    // 숨기 홀드 시작 = 문 열림, 취소/완료(안으로 들어감) = 문 닫힘
    bIsHoldPreview ? PlayOpenSound() : PlayCloseSound();

    OnInteractHoldChanged(bIsHoldPreview);
}

void ACabinetActor::SetExitHoldPreview(bool bNewExitHoldPreview)
{
    if (bIsExitHoldPreview == bNewExitHoldPreview) return;

    bIsExitHoldPreview = bNewExitHoldPreview;

    // 리슨 서버 자신의 화면에도 즉시 반영 (OnRep은 원격 클라이언트에서만 자동 호출됨)
    OnRep_IsExitHoldPreview();
}

void ACabinetActor::OnRep_IsExitHoldPreview()
{
    // 나가기 홀드 시작 = 문 열림, 취소/완료(밖으로 나옴) = 문 닫힘
    bIsExitHoldPreview ? PlayOpenSound() : PlayCloseSound();

    OnExitHoldChanged(bIsExitHoldPreview);
}

void ACabinetActor::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);

    // 매번 새로 숨을 때마다 정면(문틈, PeekHomeRotation)부터 다시 보도록 초기화
    PeekYaw = 0.f;
    PeekPitch = 0.f;
    if (PeekArm)
    {
        PeekArm->SetRelativeRotation(PeekHomeRotation);
    }
}

void ACabinetActor::UnPossessed()
{
    // Super::UnPossessed()가 Controller를 null로 지워버리므로, 그 전에 먼저 정리 (ExitPromptText도 동일한 이유)
    SetMatchWidgetPromptTextActive(false);

    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            if (CabinetMappingContext)
            {
                Subsystem->RemoveMappingContext(CabinetMappingContext);
            }
        }
    }

    Super::UnPossessed();
}

void ACabinetActor::NotifyControllerChanged()
{
    Super::NotifyControllerChanged();

    // IMC를 추가했던 로컬 컨트롤러가 더 이상 이 캐비넷을 조종하지 않으면(나가기/잡힘) 그 로컬 화면의
    // 캐비넷 입력과 나가기 문구를 정리. 서버(리슨 호스트)는 UnPossessed에서도 정리하지만 두 번 불려도 무해
    if (APlayerController* OldPC = LocalInputController.Get())
    {
        if (GetController() != OldPC)
        {
            CleanupLocalCabinetInput(OldPC);
            LocalInputController = nullptr;
        }
    }
}

void ACabinetActor::CleanupLocalCabinetInput(APlayerController* PC)
{
    if (!PC)
    {
        return;
    }

    // 진행 중이던 나가기 홀드 타이머도 로컬 상태라 같이 정리
    GetWorldTimerManager().ClearTimer(ExitHoldTimerHandle);

    if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
        ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
    {
        if (CabinetMappingContext)
        {
            Subsystem->RemoveMappingContext(CabinetMappingContext);
        }
    }

    // SetMatchWidgetPromptTextActive/HoldGaugeActive는 GetController() 기준이라 이미 조종이 끝난 지금은
    // 못 씀 → 넘겨받은 PC 기준으로 직접 끔
    if (ADungeonPlayerController* DPC = Cast<ADungeonPlayerController>(PC))
    {
        if (UMatchWidget* Widget = DPC->GetMatchWidget())
        {
            Widget->SetPromptTextActive(false);
            Widget->SetHoldGaugeActive(false);
        }
    }
}

void ACabinetActor::PawnClientRestart()
{
    // 첫 빙의라면 여기(Super) 안에서 SetupPlayerInputComponent(키 바인딩)가 먼저 불림
    Super::PawnClientRestart();

    SetupLocalCabinetInput();
}

void ACabinetActor::ReleaseLocalInputFor(APlayerController* PC)
{
    if (PC && LocalInputController.Get() == PC)
    {
        CleanupLocalCabinetInput(PC);
        LocalInputController = nullptr;
    }
}

void ACabinetActor::SetupLocalCabinetInput()
{
    // 캐비넷을 조종하게 된 로컬 클라이언트에서만 IMC 추가됨 (GetLocalPlayer()가 원격 컨트롤러에서는 null).
    // 이미 들어있는 IMC를 다시 추가해도 중복되지 않으므로 매 빙의마다 호출해도 안전
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            if (CabinetMappingContext)
            {
                Subsystem->AddMappingContext(CabinetMappingContext, 1);

                // 로컬 플레이어가 있는(=이 화면에서 조종 중인) 컨트롤러만 기억 - 나갈 때 정리용
                LocalInputController = PC;
            }
        }
    }

    // 나가기 홀드를 시작하기 전까지는 ExitPromptText를 MatchWidget에 상시 표시 - UnPossessed/CleanupLocalCabinetInput에서 끔
    SetMatchWidgetPromptTextActive(true, ExitPromptText);
}

void ACabinetActor::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    // 키 바인딩만 담당 (클라이언트에서는 입력 컴포넌트가 처음 만들어질 때 한 번만 호출됨).
    // IMC 추가/나가기 문구는 매 빙의마다 필요하므로 PawnClientRestart -> SetupLocalCabinetInput에서 처리

    if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        if (CabinetLookAction)
        {
            EIC->BindAction(CabinetLookAction, ETriggerEvent::Triggered, this, &ACabinetActor::HandleLookInput);
        }
        if (CabinetExitAction)
        {
            EIC->BindAction(CabinetExitAction, ETriggerEvent::Started, this, &ACabinetActor::HandleExitInput);
            // 나가기도 들어올 때처럼 홀드 도중 키를 놓으면 취소되도록 Completed/Canceled 모두 바인딩
            EIC->BindAction(CabinetExitAction, ETriggerEvent::Completed, this, &ACabinetActor::HandleExitInputCanceled);
            EIC->BindAction(CabinetExitAction, ETriggerEvent::Canceled, this, &ACabinetActor::HandleExitInputCanceled);
        }
    }

}

void ACabinetActor::HandleLookInput(const FInputActionValue& Value)
{
    const FVector2D Axis = Value.Get<FVector2D>();

    // 부호가 반대로 느껴지면 IMC의 Look 액션에 Negate 모디파이어를 걸어서 조정하면 됨
    PeekYaw = FMath::Clamp(PeekYaw + Axis.X * PeekLookSensitivity, -PeekYawLimit, PeekYawLimit);
    PeekPitch = FMath::Clamp(PeekPitch + Axis.Y * PeekLookSensitivity, -PeekPitchLimit, PeekPitchLimit);

    if (PeekArm)
    {
        // 에디터에서 잡아둔 "정면" 방향(PeekHomeRotation)에 대한 상대 오프셋으로 적용
        PeekArm->SetRelativeRotation(PeekHomeRotation + FRotator(PeekPitch, PeekYaw, 0.f));
    }
}

void ACabinetActor::HandleExitInput(const FInputActionValue& Value)
{
    // 술래가 지금 이 캐비넷을 여는 중이면(bIsBeingOpened는 복제되므로 여기서도 최신값을 앎)
    // 나가기 시도 자체를 시작하지 않음 - 잡히기 직전에 빠져나가는 걸 막기 위함.
    // (진짜 방어는 서버 쪽 ServerRequestExit_Implementation에 있고, 이건 즉각적인 클라 피드백용)
    if (bIsBeingOpened) return;

    // 들어올 때(RequiredHoldTime)와 동일한 홀드 시간이 지나야 실제로 나가짐
    GetWorldTimerManager().SetTimer(ExitHoldTimerHandle, this, &ACabinetActor::CompleteExitHold, RequiredHoldTime, false);

    // 홀드가 시작됐음을 서버에 알려서 미리보기 연출(뚜껑 역재생)을 시작시킴
    ServerSetExitHold(true);

    // 나가기 게이지 바를 보여주기 위해 MatchWidget의 게이지를 켬 (키를 떼거나 홀드가 끝나면 다시 끔).
    // ExitHoldText도 같이 넘겨서 "나가는 중..."처럼 게이지 옆에 표시함
    SetMatchWidgetHoldGaugeActive(true, ExitHoldText);
}

void ACabinetActor::HandleExitInputCanceled(const FInputActionValue& Value)
{
    if (GetWorldTimerManager().IsTimerActive(ExitHoldTimerHandle))
    {
        GetWorldTimerManager().ClearTimer(ExitHoldTimerHandle);

        // 홀드가 취소됐음을 서버에 알려서 미리보기 연출을 원상복구시킴
        ServerSetExitHold(false);

        SetMatchWidgetHoldGaugeActive(false);
    }
}

void ACabinetActor::CompleteExitHold()
{
    // 게이지부터 끄고(Controller가 아직 이 캐비넷에 붙어있는 지금) 서버로 완료를 통지한다 - 순서가
    // 반대면 리슨 서버/스탠드얼론에서 ServerRequestExit이 동기적으로(같은 프레임 안에) 처리되면서
    // ReturnControlToRunner()의 Controller->Possess(Runner)가 먼저 실행돼버릴 수 있고, 그러면 이 캐비넷의
    // GetController()가 이미 nullptr이 되어 게이지를 못 끄는 채로 남는다 - ARunnerCharacter::
    // CompleteHoldInteract()에서 겪었던 것과 완전히 동일한 문제라 순서도 동일하게 맞춤
    SetMatchWidgetHoldGaugeActive(false);

    ServerRequestExit();
}

void ACabinetActor::ServerRequestExit_Implementation()
{
    // 홀드가 끝까지 채워져서 여기 도달했으니 미리보기 상태를 해제 (True/False 결과 상관없이 최종적으로 닫힌 모습으로)
    SetExitHoldPreview(false);

    // 술래가 여는 중이면 나가기를 거부함 - 잡히기 직전에 빠져나가는 걸 막는 진짜 방어선(서버 권위).
    // 클라이언트의 HandleExitInput/ServerSetExitHold 가드를 다 건너뛰고 여기로 직접 RPC를 보내는
    // 변조된 클라이언트가 있어도 여기서 최종적으로 막힘
    if (bIsBeingOpened) return;

    // 자발적으로 나가는 경우이므로, 숨기 직전 위치로 복귀시킴
    ReturnControlToRunner(true);
}

void ACabinetActor::ServerSetExitHold_Implementation(bool bHolding)
{
    // 홀드 시작(true) 요청은 술래가 여는 중이면 서버에서도 거부 (클라이언트 변조 대비).
    // 취소(false)는 항상 통과시켜 미리보기 상태가 꼬이지 않게 함
    if (bHolding && bIsBeingOpened) return;

    SetExitHoldPreview(bHolding);
}

float ACabinetActor::GetExitHoldPercent() const
{
    // ARunnerCharacter::GetInteractHoldPercent()/AChaserCharacter::GetInteractHoldPercent()와 완전히 동일한 패턴
    if (!GetWorldTimerManager().IsTimerActive(ExitHoldTimerHandle))
    {
        return 0.f;
    }

    const float Duration = GetWorldTimerManager().GetTimerRate(ExitHoldTimerHandle);
    if (Duration <= 0.f)
    {
        return 0.f;
    }

    const float Elapsed = GetWorldTimerManager().GetTimerElapsed(ExitHoldTimerHandle);
    return FMath::Clamp(Elapsed / Duration, 0.f, 1.f);
}

void ACabinetActor::SetMatchWidgetHoldGaugeActive(bool bActive, const FText& HoldText) const
{
    if (ADungeonPlayerController* PC = Cast<ADungeonPlayerController>(GetController()))
    {
        if (UMatchWidget* Widget = PC->GetMatchWidget())
        {
            Widget->SetHoldGaugeActive(bActive, HoldText);
        }
    }
}

void ACabinetActor::SetMatchWidgetPromptTextActive(bool bActive, const FText& PromptText) const
{
    if (ADungeonPlayerController* PC = Cast<ADungeonPlayerController>(GetController()))
    {
        if (UMatchWidget* Widget = PC->GetMatchWidget())
        {
            Widget->SetPromptTextActive(bActive, PromptText);
        }
    }
}

float ACabinetActor::GetFrontYawLocal() const
{
    const USceneComponent* FrontRef = PeekCamera ? static_cast<const USceneComponent*>(PeekCamera.Get()) : PeekArm.Get();
    if (!FrontRef)
    {
        return 0.f;
    }

    // 액터 로컬 공간에서의 PeekCamera 위치 (루트 스케일은 InverseTransform이 처리)
    const FVector Local = GetActorTransform().InverseTransformPosition(FrontRef->GetComponentLocation());
    if (FVector2D(Local.X, Local.Y).IsNearlyZero(1.f))
    {
        return 0.f; // 정중앙에 있으면 방향을 알 수 없으니 기존대로 +X를 앞면으로
    }

    const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
    // 벽과 나란하게 놓이도록 90° 단위로 스냅
    return FMath::GridSnap(Yaw, 90.f);
}
