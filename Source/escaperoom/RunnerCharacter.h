// RunnerCharacter.h
#pragma once

#include "CoreMinimal.h"
#include "TimerManager.h"
#include "escaperoomCharacter.h"
#include "RunnerCharacter.generated.h"

class UInputMappingContext;
class UInputAction;
class UAnimMontage;
class UCurveFloat;
struct FInputActionValue;
class ACabinetActor;

/**
 * 도망자(러너) 캐릭터.
 * 이동/시야/점프는 부모(AescaperoomCharacter)에서 공통으로 처리하고,
 * 여기서는 러너 전용 입력(달리기, 웅크리기, 상호작용, 이모티콘)만 추가한다.
 */
UCLASS()
class ESCAPEROOM_API ARunnerCharacter : public AescaperoomCharacter
{
    GENERATED_BODY()

public:
    ARunnerCharacter();

    // AnimBP(ABP_BaseCharacter)에서 State Machine 분기 조건으로 참고하는 상태 플래그들.
    // Crouch는 ACharacter 내장 bIsCrouched/Crouch()가 CharacterMovement의 "Can Crouch" 설정에 막혀 잘 안 먹혀서
    // 별도 커스텀 bool(bIsCrouching)로 직접 관리함.
    // 본인 클라이언트가 입력 즉시 로컬로 바꾸고(반응성), 서버 RPC로 알려서 서버 + 다른 클라이언트에 복제.
    // 본인은 이미 최신값을 들고 있으므로 COND_SkipOwner (늦게 도착한 옛 값이 본인 입력을 덮어쓰지 않도록)
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "State")
    bool bIsCrouching = false;

    // 웅크린 상태인지 (램프 등 외부에서 판정용)
    bool IsCrouching() const { return bIsCrouching; }

    // 달리기 여부. MaxWalkSpeed는 반드시 이 값을 통해서만(SetSprintingLocal) 바꿀 것 - 서버와 클라이언트의
    // 속도가 다르면 CharacterMovement가 매 프레임 위치 보정을 해서 몸이 떨림
    UPROPERTY(ReplicatedUsing = OnRep_IsSprinting, BlueprintReadOnly, Category = "State")
    bool bIsSprinting = false;

    UPROPERTY(BlueprintReadOnly, Category = "State")
    bool bIsInteracting = false;

    UPROPERTY(BlueprintReadOnly, Category = "State")
    bool bIsEmoting = false;

    // 현재 재생 중인 이모트 번호 (0 = 없음, 1 = EmoteAction, 2 = EmoteAction2). 두 이모트가 동시에
    // 겹쳐 재생되는 것을 막고, 이동으로 취소될 때 "누가 재생 중이었는지" 판단하는 데 사용.
    // 서버 권한으로만 바뀌며 Replicated - AnimBP 스테이트 머신이 이 값을 보고 순수 애니메이션(몽타주 아님)
    // 재생/전환을 직접 판단함(bIsCrouching/bIsSprinting과 같은 성격). 일반 Replicated 프로퍼티라
    // 몽타주 리플리케이션과 달리 COND_SkipOwner가 아니고, 값이 바뀌면 본인 포함 전 클라이언트에 뿌려짐
    UPROPERTY(ReplicatedUsing = OnRep_ActiveEmoteIndex, BlueprintReadOnly, Category = "State")
    int32 ActiveEmoteIndex = 0;

    // 서버 권한으로만 바뀌는 "장롱에 숨어있는지" 상태 (숨어있는 동안은 캐비넷 액터를 Possess 중이라
    // 이 캐릭터 자신은 입력을 받지 않음). 클라이언트는 OnRep_IsHiding으로 연출을 갱신함
    UPROPERTY(ReplicatedUsing = OnRep_IsHiding, BlueprintReadOnly, Category = "State")
    bool bIsHiding = false;

    // 서버 권한으로만 바뀌는 "잡혔는지" 상태. 감옥 구역 Box(UJailGateComponent::JailAreas)에 들어가면
    // true, 나가면 false - 반드시 SetCaught()를 통해서만 바꿀 것(남은 러너 수 갱신이 같이 묶여 있음).
    // Capture()는 감옥으로 순간이동만 시키고, 그 순간 일어나는 Box 오버랩이 true로 만든다.
    // 이동 자체는 막지 않으므로(기획상 감옥 안에서는 자유롭게 움직일 수 있어야 함) CanMove()는 이 값을 참고하지 않는다
    UPROPERTY(ReplicatedUsing = OnRep_IsCaught, BlueprintReadOnly, Category = "State")
    bool bIsCaught = false;

    // 서버 권한으로만 바뀌는 "탈출에 성공했는지" 상태 - 반드시 SetEscaped()를 통해서만 바꿀 것(남은 러너 수/
    // PlayerState 갱신이 같이 묶여 있음). true가 되면 되돌아가지 않는다. true인 동안 이 폰은 숨겨지고
    // 콜리전/이동/입력이 전부 막힌 채로 남아있고(파괴하지 않음 - 다른 코드가 들고 있는 참조 보호),
    // 플레이어는 컨트롤러(ADungeonPlayerController)의 관전 모드로 다른 폰의 시점을 본다
    UPROPERTY(ReplicatedUsing = OnRep_IsEscaped, BlueprintReadOnly, Category = "State")
    bool bIsEscaped = false;

    // 술래에게 잡혀서 목조르기 연출이 재생되는 동안 true (AChaserCharacter::PerformGrabBoxCheck이
    // MulticastBeginChoked로 켜고, 실제로 감옥으로 보내지는 Capture()에서 다시 꺼짐). bClimbFlightModeActive와
    // 동일한 성격 - Multicast로 본인 포함 전 클라이언트에서 직접 값을 세팅하므로 Replicated 아님.
    // CanMove()가 이 값을 보고 이동을 막아서 연출 중 제자리에 고정되게 함
    bool bIsBeingCaught = false;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

    // 러너 전용 Input Mapping Context (Sprint/Crouch/Interact/Emote 키가 여기 매핑됨)
    // 블루프린트에서 IMC_Runner 같은 애셋을 만들어서 지정해줘야 함
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputMappingContext* RunnerMappingContext;

    // 달리기 (Shift 등)
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* SprintAction;

    // 기본 걷기 속도 (스프린트 종료 시 복구되는 값)
    UPROPERTY(EditDefaultsOnly, Category = "Movement")
    float WalkSpeed = 400.f;

    // 스프린트(달리기) 속도. 전방으로 움직일 때만 적용됨. 에디터에서 250~600 사이로만 조정 가능
    UPROPERTY(EditDefaultsOnly, Category = "Movement", meta = (ClampMin = "250.0", ClampMax = "600.0", UIMin = "250.0", UIMax = "600.0"))
    float SprintSpeed = 600.f;

    // 웅크리기 이동 속도. 서버와 본인 클라 양쪽에서 같은 값으로 적용됨 (ApplyMoveSpeed)
    UPROPERTY(EditDefaultsOnly, Category = "Movement", meta = (ClampMin = "0.0"))
    float CrouchSpeed = 150.f;

    // 웅크리기
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* CrouchAction;

    // 물체와 상호작용 (E)
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* InteractAction;

    // 이모티콘 (티배깅 등)
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* EmoteAction;

    // 이모티콘 2번째 슬롯. EmoteAction과 동일한 방식으로 동작하며, 서로 동시에 재생되지는 않음
    // (하나가 재생 중일 때 다른 쪽 키를 눌러도 무시됨 - ActiveEmoteIndex 참고)
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* EmoteAction2;

    // 상호작용 애니메이션 몽타주 (예: MT_Interaction). 지정하면 이걸 재생하고, 재생이 끝나는 시점에 맞춰
    // bIsInteracting이 자동으로 false가 됨. 비워두면 아래 InteractPulseDuration 타이머로 대체 동작
    // (홀드가 필요한 상호작용 - 예: 캐비넷 숨기 - 에는 사용되지 않음)
    UPROPERTY(EditDefaultsOnly, Category = "Animation")
    UAnimMontage* InteractMontage;

    // 상호작용 트리거(클릭) 후 bIsInteracting을 true로 유지하는 시간. InteractMontage가 없을 때만 사용됨
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    float InteractPulseDuration = 0.15f;

    // [미사용] 예전 라인 트레이스 사거리. 지금은 정면 박스(InteractBoxForwardOffset/InteractBoxExtent)로 판정
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    float InteractRange = 200.f;

    // 사다리(탈출구) 오르기 몽타주(루트모션). AExitActor::OnInteractInternal이
    // MulticastBeginLadderClimb를 호출할 때 재생됨 - 어떤 탈출구든 캐릭터는 같은 애니메이션을
    // 재생하므로 탈출구가 아니라 캐릭터 쪽 데이터로 둔다 (InteractMontage와 같은 성격)
    UPROPERTY(EditDefaultsOnly, Category = "Animation")
    UAnimMontage* ClimbMontage;

    // 상호작용(홀드 포함) 시작 시점의 회전값. Tick()에서 상호작용 중엔 매 프레임 이 값으로 되돌려서
    // 카메라/이동 등 어떤 경로로도 캐릭터가 회전하지 않게 고정한다
    FRotator InteractLockedRotation;

    FTimerHandle InteractPulseTimerHandle;

    void StartSprint(const FInputActionValue& Value);
    void StopSprint(const FInputActionValue& Value);
    void StartCrouch(const FInputActionValue& Value);
    void StopCrouch(const FInputActionValue& Value);

    // 상호작용키를 누른 순간: 대상 탐색 후, 홀드가 필요없으면 즉시 처리 / 필요하면 홀드 타이머 시작
    void Interact(const FInputActionValue& Value);

    // 캐비넷에서 나와 다시 빙의된 직후, 나가기에 쓰던 키(E)를 아직 누르고 있으면 Interact가 새로 눌린 것으로
    // 들어와 바로 앞 캐비넷에 다시 숨기 홀드가 걸림 -> 빙의 직후 이 시간(초) 동안은 Interact 입력 무시
    UPROPERTY(EditDefaultsOnly, Category = "Interaction", meta = (ClampMin = "0.0"))
    float InteractBlockAfterPossess = 0.3f;

    // 이 월드 시간 전까지는 Interact 무시 (본인 클라 로컬)
    float InteractBlockedUntil = 0.f;

    virtual void PawnClientRestart() override;

    // 상호작용키를 뗀 순간: 홀드 타이머가 아직 진행 중이었다면 취소
    void CancelHoldInteract(const FInputActionValue& Value);

    // CancelHoldInteract의 실제 취소 로직. Move()에서도(이동 입력이 들어오면 홀드를 취소하기 위해)
    // 같이 써야 해서 FInputActionValue 없이 부를 수 있도록 분리함
    void CancelHoldInteractInternal();

    // ClimbMontage 재생 중(홀드 미리보기 + 실제 클라이밍) true. CharacterMovementComponent가
    // Walking 모드일 때 매 틱 바닥 높이로 캡슐을 되돌려버려서 AnimNotify_ClimbStep의
    // AddActorWorldOffset이 무효화되는 것을 막기 위해, 이 동안만 Flying 모드로 바꿔둔다.
    // 홀드가 취소되면(사다리를 실제로 타지 않게 되면) 다시 Walking으로 복구해야 하므로 플래그로 추적
    bool bClimbFlightModeActive = false;

    // 홀드 타이머 완료 시 실제로 서버에 상호작용을 요청
    void CompleteHoldInteract();

    // 몸 정면 박스 안의 IInteractable 중 가장 가까운 액터 (없으면 nullptr). 박스 크기는 Interaction|Box 카테고리
    AActor* FindInteractableTarget() const;

    void EndInteractPulse();
    void OnInteractMontageEnded(UAnimMontage* Montage, bool bInterrupted);

    // MulticastBeginLadderHold가 재생한 ClimbMontage가 끝났을 때(정상 종료든 중단이든) 호출되도록
    // 바인딩해둠. 몽타주가 끝난 뒤에도 Flying 모드가 계속 켜져 있으면 중력이 안 먹혀서 캐릭터가
    // 허공에 뜬 채로 멈춰있고, 이동하면 Walking 모드의 바닥 마찰이 없어서 스케이트 타듯 미끄러진다 -
    // 그래서 몽타주가 끝나는 시점에 확실히 Walking으로 복구해줘야 함
    void OnClimbMontageEnded(UAnimMontage* Montage, bool bInterrupted);
    // 키를 누르는 순간 시작만 함. 키를 떼도(Completed) 취소되지 않음 - 애니메이션은 끝까지 재생되거나
    // 이동으로 취소되는 두 가지 경우로만 끝남 (아래 Move() 오버라이드, 그리고 블루프린트 상태머신의
    // Time Remaining 조건 참고)
    void PlayEmote(const FInputActionValue& Value);
    void PlayEmote2(const FInputActionValue& Value);

    // PlayEmote/PlayEmote2가 공유하는 실제 시작 처리부. EmoteIndex는 1 또는 2
    void StartEmote(int32 EmoteIndex);

    // 이모트를 실제로 종료 처리. 지금은 이동으로 취소될 때만 호출됨(Move() 참고). 애니메이션이 자연스럽게
    // 끝나는 시점은 블루프린트 쪽(Time Remaining)에서만 판단하고 여기로 알려주지 않으므로, 그 경우엔
    // ActiveEmoteIndex/bIsEmoting이 실제 화면과 어긋난 채로 남아있다가 다음 이동 입력에 정리됨 - 완벽하게
    // 하려면 나중에 AnimNotify_ChaserKickDoor 같은 방식으로 애니메이션 끝에서 이 함수를 호출하도록 연결하면 됨
    void EndEmote(int32 EmoteIndex);

    // 이동 입력이 들어오는 순간 재생 중인 이모트가 있으면 즉시 취소하고, 이동은 그대로 진행시킴
    // (이모트 중이라고 이동 자체를 막지는 않음 - CanMove()에는 더 이상 bIsEmoting을 반영하지 않음)
    virtual void Move(const FInputActionValue& Value) override;

    // 상호작용/장롱 은신 중에는 이동 입력을 막기 위한 오버라이드 (이모트는 더 이상 이동을 막지 않고,
    // 대신 Move()에서 이동이 시작되는 순간 이모트 쪽을 취소함)
    virtual bool CanMove() const override;

    // 목조르기 연출 중(bIsBeingCaught)에는 시점 회전(Look)도 막음 - 1인칭이라 몸 Yaw가 컨트롤러 회전을
    // 따라가므로, 마우스를 움직이면 술래 손에 잡힌 채로 몸이 돌아가 버리기 때문 (술래 쪽 CanLook과 동일)
    virtual bool CanLook() const override;

public:
    // ACabinetActor가 서버에서 호출: 이 캐릭터를 숨김 상태로 전환하고 캐비넷 위치로 겹쳐서 이동
    // (실제 조종권 Possess 전환은 ACabinetActor 쪽에서 처리)
    void EnterCabinet(ACabinetActor* Cabinet);

    // ACabinetActor가 서버에서 호출: 숨김 상태 해제 (콜리전/이동 복구).
    // bRestoreOriginalPosition=true면 숨기 직전 위치/회전으로 복귀시킴 (자발적으로 나갈 때).
    // 기본값 false면 위치는 그대로 둠 (술래에게 잡혔을 때 등 - 나중에 다시 검토)
    void ExitCabinet(bool bRestoreOriginalPosition = false);

    UFUNCTION(BlueprintPure, Category = "State")
    bool IsHidingInCabinet() const { return bIsHiding; }

    UFUNCTION(BlueprintPure, Category = "State")
    bool IsCaught() const { return bIsCaught; }

    UFUNCTION(BlueprintPure, Category = "State")
    bool IsEscaped() const { return bIsEscaped; }

    // 탈출 확정 처리 (서버 전용). AExitActor::OnInteractInternal이 홀드 완료 시 호출.
    // - bIsEscaped = true (+ 숨김/콜리전 끔/이동 끔/홀드 상태 정리 - OnRep_IsEscaped)
    // - AMatchPlayerState::SetEscaped(true) (플레이어 목록 탈출 이미지용)
    // - AEscapeRoomGameState::NotifyRunnerResolved() (남은 러너 수 -1)
    // - 조종 중인 ADungeonPlayerController::ClientBeginEscape() (본인 화면 비네트 페이드 -> 관전)
    // 이미 탈출했거나 잡힌 상태면 아무것도 하지 않음
    void SetEscaped();

    // AChaserCharacter::PerformGrabBoxCheck이 이미 목조르기 연출 중인 러너를 다시
    // 잡으려는 중복 트리거를 막는 데 사용 (public이어야 다른 클래스에서 재검증 가능)
    UFUNCTION(BlueprintPure, Category = "State")
    bool IsBeingCaught() const { return bIsBeingCaught; }

    // 홀드 상호작용 진행률(0~1). InteractHoldTimerHandle이 활성 상태일 때만 0보다 큼 (MatchWidget이 참고).
    // 부모(AescaperoomCharacter)에서 이미 UFUNCTION(BlueprintPure)로 선언되어 있어서, 오버라이드에는
    // UFUNCTION()을 다시 붙이면 안 됨 (UHT 에러: "cannot have a UFUNCTION() declaration above it")
    virtual float GetInteractHoldPercent() const override;

    // ACabinetActor::OnInteract_Implementation()이 서버에서 호출: 장롱 안에서 술래에게 발각되어
    // 잡혔을 때(목조르기 연출 시작 직후). public이어야 CabinetActor(다른 클래스)에서 호출 가능.
    // 실제 탈락 처리는 Capture()가 담당하고, 이건 순수 연출용 훅(애니메이션/사운드 등은 블루프린트에서 구현).
    // BlueprintImplementableEvent라 블루프린트에서 구현 안 했으면 호출돼도 아무 일도 안 일어남
    UFUNCTION(BlueprintImplementableEvent, Category = "State")
    void OnCaughtInCabinet();

    // 잡힌 처리 진입점. 평소엔 AChaserCharacter::PerformGrabBoxCheck()가 서버 권한에서 호출함
    // (캐비닛에서 발각됐을 때 연동은 보류 - 나중에 ACabinetActor::FinishOpening()에서도 이 함수를
    // 호출하도록 붙이면 됨). 맵의 WFC 빅룸들 중 "Jail" 컴포넌트 태그가 붙은 위치를 찾아(FindJailTransform)
    // 그 자리로 순간이동시키고, AEscapeRoomGameState::NotifyRunnerResolved()로 남은 러너 수를 갱신한다.
    // 이미 잡힌 상태면 아무 것도 하지 않음(중복 호출 방지).
    // BlueprintCallable은 디버그 키 등으로 블루프린트에서 직접 테스트해볼 수 있도록 열어둔 것 -
    // 내부에서 HasAuthority() 체크를 하므로 서버(또는 스탠드얼론/리슨 서버 호스트)에서 호출해야 실제로 동작함
    UFUNCTION(BlueprintCallable, Category = "State")
    void Capture();

    // bIsCaught를 바꾸는 유일한 진입점 (서버 전용). UJailGateComponent의 감옥 구역 Box가
    // BeginOverlap -> true, EndOverlap -> false로 호출함. 값이 이미 같으면 아무 것도 하지 않으므로
    // 중복 호출돼도 남은 러너 수가 두 번 바뀌지 않음.
    // true: AEscapeRoomGameState::NotifyRunnerResolved() (남은 인원 -1)
    // false: AEscapeRoomGameState::NotifyRunnerRestored() (남은 인원 +1)
    UFUNCTION(BlueprintCallable, Category = "State")
    void SetCaught(bool bNewCaught);

    // 술래에게 잡혔을 때 재생할 목졸림 몽타주 (제자리 재생, 루트모션 없음).
    // AChaserCharacter::ChokeGrabMontage보다 짧아도 됨 - [에디터 필수 설정] 이 몽타주 에셋의
    // "Enable Auto Blend Out" 체크를 꺼둬야 함. 그러면 다 재생된 뒤 마지막 프레임 포즈에서 그대로 멈춰
    // 있다가, Capture()가 부르는 MulticastStopBeingChoked()의 Montage_Stop에서야 풀림.
    // (예전엔 길이만큼 타이머를 걸고 Montage_Pause했는데, Auto Blend Out이 끝나기 BlendOut 시간 전에
    //  이미 블렌드아웃을 시작해버려서 Pause할 활성 인스턴스가 없어 포즈가 풀리는 문제가 있었음)
    // 들어올리기는 이 몽타주에 UAnimNotifyState_ChokeLift를 배치해서 처리
    UPROPERTY(EditDefaultsOnly, Category = "Animation")
    TObjectPtr<UAnimMontage> BeingChokedMontage;

    // AChaserCharacter::PerformGrabBoxCheck이 서버에서 호출: 이 캐릭터를 지정된
    // 위치/각도(술래의 ChokeGrabPoint)로 스냅시키고 BeingChokedMontage를 재생한다. 이동 모드를
    // MOVE_None으로 바꿔서, 이후 ChokeLift Notify가 캡슐을 들어올려도 CMC가 중력/바닥 스냅으로 되돌리지 않게 함.
    // MulticastBeginLadderClimb와 완전히 동일한 이유로 Multicast 사용 (본인 화면에도 보이도록)
    UFUNCTION(NetMulticast, Reliable, Category = "State")
    void MulticastBeginChoked(FVector TargetLocation, FRotator TargetRotation);

    // UAnimNotifyState_ChokeLift(BeingChokedMontage에 배치)가 호출. 몽타주가 Multicast로 서버+모든
    // 클라이언트에서 재생되므로 각 머신에서 로컬로 호출됨 - NotifyClimbStep과 같은 철학(HasAuthority 불필요).
    // 스냅 위치(ChokeLiftBaseLocation)를 기준으로 캡슐(액터)을 월드 Z로 천천히 들어올림 → 본인 1인칭 카메라도 같이 올라감
    // InLiftCurve가 있으면 그 커브(X: 구간 진행률 0~1, Y: LiftHeight 비율)대로, 없으면 EaseExponent 이징으로 올라감
    void BeginChokeLift(float InLiftHeight, float InDuration, float InEaseExponent, UCurveFloat* InLiftCurve = nullptr);
    void UpdateChokeLift(float DeltaTime);
    void EndChokeLift();

protected:
    // 목졸림 들어올리기 상태 (NotifyState UObject는 몽타주 애셋에 하나라 모든 메시가 공유하므로
    // 진행 상태는 캐릭터 쪽에 보관). 높이는 누적 이동이 아니라 기준 위치 + 오프셋으로 매번 절대 계산 → 오차 누적 없음
    FVector ChokeLiftBaseLocation = FVector::ZeroVector; // MulticastBeginChoked에서 스냅한 위치
    float ChokeLiftOffset = 0.f;          // 현재 들어올려진 높이 (cm)
    bool bChokeLiftActive = false;
    float ChokeLiftStartOffset = 0.f;
    float ChokeLiftTargetOffset = 0.f;
    float ChokeLiftHeight = 0.f;
    float ChokeLiftElapsed = 0.f;
    float ChokeLiftDuration = 0.f;
    float ChokeLiftEaseExponent = 2.f;

    // 현재 구간에서 쓰는 커브 (NotifyState의 LiftCurve). 커브 에셋은 몽타주가 참조하고 있어 살아있지만,
    // GC 안전을 위해 UPROPERTY로 보관 (Transient - 저장/복제 안 함)
    UPROPERTY(Transient)
    TObjectPtr<UCurveFloat> ChokeLiftCurve;

    // 구간 진행률(0~1) → LiftHeight 대비 비율. 커브가 있으면 커브 값, 없으면 EaseExponent 이징
    float EvaluateChokeLiftFraction(float Alpha) const;

    // 목조르기 중 이동 모드를 MOVE_None으로 바꿔뒀는지 (MulticastStopBeingChoked에서 Walking 복구용)
    bool bChokeMovementDisabled = false;

    // Capture()가 서버에서 호출(성공/실패 양쪽): BeingChokedMontage를 Stop시켜 마지막 프레임 포즈를 풀고,
    // 들어올리기/이동 모드/bIsBeingCaught를 원래대로 복구. Montage/이동모드/bIsBeingCaught 모두 각 머신의
    // 로컬 상태라 Multicast로 본인 포함 모든 클라에서 실행해야 함
    UFUNCTION(NetMulticast, Reliable, Category = "State")
    void MulticastStopBeingChoked();

public:

    // AExitActor::OnInteractInternal이 서버에서 호출: 지정된 위치/각도로 이 캐릭터를 스냅시키고
    // ClimbMontage(루트모션)를 재생한다. Multicast라서 서버 자신 + 모든 클라이언트(상호작용한
    // 본인 화면 포함)에서 동일하게 재생됨 - ACharacter의 기본 몽타주 리플리케이션(COND_SkipOwner)은
    // 상호작용한 본인 화면에는 안 보이는 문제가 있어서 이렇게 명시적으로 Multicast를 쓴다
    UFUNCTION(NetMulticast, Reliable, Category = "Exit")
    void MulticastBeginLadderClimb(FVector TargetLocation, FRotator TargetRotation);

    // AExitActor::OnInteractHoldUpdateInternal이 서버에서 홀드 시작(bHolding=true) 신호를 받으면 호출.
    // ClimbMontage 재생 + 루프 설정 + Flying 전환을 서버 자신을 포함한 모든 클라이언트에서 동시에
    // 실행시킨다. 예전엔 Interact()가 로컬로만 재생했는데, 그러면 이 몽타주의 AnimNotify(NotifyClimbStep)가
    // 만드는 위치 변화를 서버가 전혀 모르게 되고, CharacterMovementComponent의 클라-서버 교차검증에서
    // "서버 기준 위치"로 계속 되돌려버리는 문제가 있었음. Multicast로 서버 자신도 같이 재생하게 하면
    // 서버의 권위있는 위치 자체가 같이 올라가므로 더 이상 되돌아가지 않음
    UFUNCTION(NetMulticast, Reliable, Category = "Exit")
    void MulticastBeginLadderHold();

    // 위 MulticastBeginLadderHold의 취소판. OnInteractHoldUpdateInternal이 bHolding=false를 받으면 호출 -
    // 모든 클라이언트에서 몽타주를 멈추고 Flying으로 바꿔뒀던 걸 다시 Walking으로 복구한다
    UFUNCTION(NetMulticast, Reliable, Category = "Exit")
    void MulticastCancelLadderHold();

    // UAnimNotify_ClimbStep이 ClimbMontage 재생 중 호출: 그 시점(발/손이 발판을 딛는 프레임)에 맞춰
    // 캐릭터를 Z축으로 HeightDelta(cm)만큼 실제로 이동시킨다. 몽타주 자체는 제자리 루프라 이렇게
    // 노티파이 타이밍으로 실제 상승을 만들어냄
    UFUNCTION(BlueprintCallable, Category = "Exit")
    void NotifyClimbStep(float HeightDelta);

protected:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UFUNCTION()
    void OnRep_IsHiding();

    UFUNCTION()
    void OnRep_IsCaught();

    UFUNCTION()
    void OnRep_IsEscaped();

    // ActiveEmoteIndex가 리플리케이트될 때(원격 클라이언트) 또는 서버 자신이 값을 바꾼 직후 수동으로도
    // 호출됨(리슨 서버 자신의 화면 갱신용 - bIsCaught 등과 동일한 이유). bIsEmoting을 값에 맞춰 동기화만
    // 하고, 실제 애니메이션 재생/전환 자체는 AnimBP 스테이트 머신이 ActiveEmoteIndex를 직접 보고 처리함
    UFUNCTION()
    void OnRep_ActiveEmoteIndex();

    // ---- 달리기/웅크리기 동기화 ----
    // bIsSprinting + MaxWalkSpeed를 함께 적용 (로컬 입력, 서버 RPC, OnRep 모두 이 함수 하나만 사용)
    void SetSprintingLocal(bool bNewSprinting);

    // 현재 상태(웅크림 > 달리기 > 걷기 우선순위)에 맞는 MaxWalkSpeed 적용.
    // 서버/클라 속도가 다르면 위치 보정으로 몸이 떨리므로 속도 변경은 반드시 이 함수 하나로만 할 것
    void ApplyMoveSpeed();

    UFUNCTION()
    void OnRep_IsSprinting();

    UFUNCTION(Server, Reliable)
    void ServerSetSprinting(bool bNewSprinting);

    UFUNCTION(Server, Reliable)
    void ServerSetCrouching(bool bNewCrouching);

    // WFC로 스폰된 빅룸들(AWFCGridManager::GetSpawnedBigRoomActors()) 전체에서 "Jail" 컴포넌트
    // 태그가 붙은 씬 컴포넌트를 찾아 그 트랜스폼을 반환. AChaserCharacter::CacheAllCeilings()가
    // "Ceiling" 태그를 찾는 것과 동일한 방식. 못 찾으면 false
    bool FindJailTransform(FTransform& OutTransform) const;

    // Interact()의 라인 트레이스로 찾은 상호작용 대상을 서버에 알려서, 서버 권한으로 실제 처리(IInteractable::OnInteract)를 수행시킴
    UFUNCTION(Server, Reliable)
    void ServerInteractWithTarget(AActor* Target);

    // InteractMontage 재생을 서버에 요청 (예전엔 Interact()에서 로컬로만 Montage_Play해서 다른
    // 클라이언트에는 안 보였음). 술래의 KickDoor 몽타주와 동일한 Server->Multicast 패턴으로 바꿔서
    // 본인을 포함한 모든 클라이언트에서 동일하게 재생되게 함
    UFUNCTION(Server, Reliable)
    void ServerPlayInteractMontage();

    // 위 ServerPlayInteractMontage가 서버에서 호출: 실제 Montage_Play + 종료 델리게이트 바인딩을
    // 모든 클라이언트(서버 자신 포함)에서 동시에 실행
    UFUNCTION(NetMulticast, Reliable)
    void MulticastPlayInteractMontage();

    // 홀드 상호작용 시작/취소를 서버에 알려서, 서버 권한으로 IInteractable::OnInteractHoldUpdate를 호출시킴
    // (뚜껑 열리는 미리보기 애니메이션처럼 홀드 완료 전에도 연출이 필요한 경우에 사용. 홀드 완료 시에는
    // ServerInteractWithTarget -> OnInteract 쪽에서 상태를 마무리하므로 별도로 false를 보내지 않아도 됨)
    UFUNCTION(Server, Reliable)
    void ServerSetInteractHold(AActor* Target, bool bHolding);

    // 이모트 시작을 서버에 요청. 서버가 재검증(ActiveEmoteIndex != 0이면 거부) 후 ActiveEmoteIndex를
    // 직접 바꿔주면, Replicated라서 본인 포함 전 클라이언트에 자동으로 뿌려짐 (Multicast 불필요)
    UFUNCTION(Server, Reliable)
    void ServerPlayEmote(int32 EmoteIndex);

    // 이동 등으로 이모트가 취소됐음을 서버에 알림. 서버가 ActiveEmoteIndex를 0으로 되돌리면 마찬가지로
    // 리플리케이션으로 전파됨 (취소는 항상 통과시킴 - ServerSetInteractHold의 bHolding=false 케이스와 동일한 이유)
    UFUNCTION(Server, Reliable)
    void ServerStopEmote();

private:
    // 현재 숨어있는 장롱 (숨어있지 않으면 nullptr). 서버에서만 채워짐
    UPROPERTY()
    TObjectPtr<ACabinetActor> CurrentCabinet;

    // 홀드 상호작용 진행 중인 대상 (라인 트레이스로 찾은 즉시가 아니라, 홀드가 끝나야 서버로 전송됨)
    TWeakObjectPtr<AActor> PendingHoldTarget;

    FTimerHandle InteractHoldTimerHandle;

    // 숨기 직전 위치/회전. 나갈 때 이 자리로 복귀시키기 위해 EnterCabinet에서 캐싱함 (서버 전용, 복제 안 함)
    FTransform PreHideTransform;
};
