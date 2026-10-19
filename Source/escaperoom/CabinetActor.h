// CabinetActor.h
#pragma once

#include "CoreMinimal.h"
#include "MiscActor.h"
#include "TimerManager.h"
#include "CabinetActor.generated.h"

class UStaticMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;
class ARunnerCharacter;
class APlayerController;
class UActorSequenceComponent;
class USoundBase;
class USoundAttenuation;

/**
 * 도망자가 숨을 수 있는 장롱(캐비닛). 데드바이데이라이트 캐비닛 참고.
 *
 * - 도망자가 상호작용키를 RequiredHoldTime만큼 누르고 있으면 숨음.
 *   숨는 순간 도망자 캐릭터 -> 이 캐비넷(APawn)으로 Possess를 전환하고,
 *   도망자 폰은 캐비넷 위치에 겹쳐서 대기(콜리전/이동 비활성화).
 * - 캐비넷은 자체 Input(IMC_Cabinet)을 갖고 있어서, 숨어있는 동안
 *   좌우/상하로 훔쳐보는 Look과, 들어올 때와 동일하게 RequiredHoldTime만큼 눌러야 나가지는 Exit를 처리한다.
 * - "술래"(ChaserTag가 붙은 폰)는 상호작용키를 OpenDuration(기본 10초)만큼 직접 홀드해야 열린다.
 *   시간 자체는 도망자 홀드와 동일하게 술래 클라이언트(AChaserCharacter)가 로컬 타이머로 재고,
 *   홀드 시작/취소 신호(ServerSetInteractHold)는 OnInteractHoldUpdate_Implementation에서
 *   bIsBeingOpened를 토글해 "여는 중" 연출만 담당한다. 홀드가 끝까지 채워지면 클라이언트가
 *   OnInteract(ServerInteractWithTarget)를 보내고, 그 시점에 OnInteract_Implementation이
 *   즉시 판정을 확정한다 - 안에 도망자가 있었다면 잡힘 처리(조종권을 도망자에게 되돌린 뒤
 *   OnCaughtInCabinet 호출). 서버가 홀드 시간을 직접 검증하지는 않으므로(러너의 숨기와 동일한
 *   신뢰 모델) 변조된 클라이언트가 홀드를 건너뛸 가능성은 남아있다 - 필요해지면 서버가 홀드
 *   시작 시각을 기록해두고 완료 요청 시각과 비교하는 방식으로 보강할 수 있음.
 *
 * 상태 변경은 전부 서버 권한(HasAuthority)에서만 일어나고, Replicated 변수를 바꾼 뒤
 * 서버 자신의 화면에도 즉시 반영되도록 OnRep 함수를 직접 호출해준다
 * (OnRep은 원래 클라이언트에서만 자동 호출되므로 - DoorActor의 SetOpen과 동일한 패턴).
 */
UCLASS(Blueprintable)
class ESCAPEROOM_API ACabinetActor : public AMiscActor
{
    GENERATED_BODY()

public:
    ACabinetActor();

    //~ Begin IInteractable (CanInteract는 MiscActor 기본 구현이 CanInteractInternal()로
    //~ 위임해주므로 여기선 override 안 함 - 아래 protected 섹션에서 CanInteractInternal만 override)
    virtual void OnInteract_Implementation(APawn* Interactor) override;
    virtual float GetInteractHoldTime_Implementation(APawn* Interactor) override;
    virtual void OnInteractHoldUpdate_Implementation(APawn* Interactor, bool bHolding) override;
    //~ End IInteractable

    virtual void PossessedBy(AController* NewController) override;
    virtual void UnPossessed() override;

    // [FIX] 조종 컨트롤러가 바뀔 때 서버 + 해당 클라이언트 양쪽에서 호출됨(클라는 OnRep_Controller 경유).
    // UnPossessed()는 서버에서만 불려서, 원격 클라이언트(리슨서버의 두 번째 창 등)에서는 IMC_Cabinet이
    // 안 지워지고 남아 캐비넷 Look(우선순위 1)이 러너 Look 입력을 계속 가로채 회전이 안 되던 문제가 있었음.
    // 여기서 로컬 클라이언트 쪽 IMC/나가기 문구를 정리함
    virtual void NotifyControllerChanged() override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

    // 빙의될 때마다(첫 빙의 포함) 조종하는 로컬 클라이언트에서 호출됨. SetupPlayerInputComponent는 클라이언트에서
    // 입력 컴포넌트가 처음 만들어질 때 한 번만 불리므로, 매번 다시 해야 하는 IMC 추가/나가기 문구는 여기서 처리
    virtual void PawnClientRestart() override;

    // PC가 이 캐비넷에 로컬 입력(IMC_Cabinet/나가기 문구)을 등록해둔 상태면 정리. 다른 폰으로 빙의가 끝난 순간
    // DungeonPlayerController::AcknowledgePossession이 호출 - NotifyControllerChanged 복제 타이밍에 의존하지 않는 안전장치
    void ReleaseLocalInputFor(APlayerController* PC);

protected:
    virtual void BeginPlay() override;

    // 범위 안 상호작용 프롬프트 표시 여부에도 그대로 쓰이므로, 실제 판정 로직은 여기 하나에만 둠
    // (술래: 이미 여는 중이 아니면 언제든 가능 / 도망자: 비어있고 술래가 여는 중이 아닐 때만 가능)
    virtual bool CanInteractInternal(const APawn* Interactor) const override;

public:

    // 도망자가 숨기 위해 상호작용키를 눌러야 하는 시간(초)
    UPROPERTY(EditAnywhere, Category = "Cabinet")
    float RequiredHoldTime = 1.0f;

    // 술래가 이 장롱을 여는 데 필요한 채널링 시간(초)
    UPROPERTY(EditAnywhere, Category = "Cabinet")
    float OpenDuration = 10.f;

    // PeekCamera(없으면 PeekArm)가 액터 기준 어느 방향에 있는지를 "앞면"으로 보고,
    // 그 방향의 Yaw(도, 90° 단위로 스냅)를 반환. PropSpawnManager가 스폰 직후 이 값만큼 액터를 돌려서
    // PeekCamera 쪽이 스폰 마커 화살표(+X)를 향하게 한다.
    float GetFrontYawLocal() const;

    // 캐비넷을 조종 중이고 나갈 수 있는 동안(나가기 홀드 시작 전) MatchWidget에 상시 표시할 문구
    // (예: "나가기"). InteractionText 컴포넌트의 PromptText/HoldText는 "들어오는" 쪽 문구라 겹치므로,
    // 나가기는 IInteractable을 안 타는 자기 자신의 액션답게 전용 필드를 따로 둠
    UPROPERTY(EditAnywhere, Category = "Cabinet|Exit")
    FText ExitPromptText;

    // 나가기 홀드 중일 때 MatchWidget에 표시할 문구 (예: "나가는 중...")
    UPROPERTY(EditAnywhere, Category = "Cabinet|Exit")
    FText ExitHoldText;

    // 술래 판별용 ChaserTag는 AMiscActor에서 상속받아 그대로 재사용 (숨는 대신 장롱을 여는(수색하는)
    // 상호작용을 하게 됨)

    // 좌우로 훔쳐볼 수 있는 최대 각도
    UPROPERTY(EditAnywhere, Category = "Cabinet|Peek")
    float PeekYawLimit = 35.f;

    // 상하로 훔쳐볼 수 있는 최대 각도
    UPROPERTY(EditAnywhere, Category = "Cabinet|Peek")
    float PeekPitchLimit = 15.f;

    // 마우스 입력 -> 훔쳐보기 각도 변환 감도
    UPROPERTY(EditAnywhere, Category = "Cabinet|Peek")
    float PeekLookSensitivity = 1.0f;

    UFUNCTION(BlueprintPure, Category = "Cabinet")
    bool IsOccupied() const { return OccupyingRunner != nullptr; }

    UFUNCTION(BlueprintPure, Category = "Cabinet")
    bool IsBeingOpened() const { return bIsBeingOpened; }

    // [리슨] 숨어있던 도망자가 게임을 나갈 때 서버에서 호출 (ADungeonPlayerController::PawnLeavingGame).
    // 엔진은 나가는 플레이어가 "조종 중인 폰"을 지우는데, 숨는 동안엔 그게 이 캐비넷이라 캐비넷이 통째로 사라지고
    // 투명한 도망자 몸만 남았음 -> 먼저 조종권을 도망자에게 돌려줘서, 지워지는 대상이 도망자 캐릭터가 되게 함
    void ReleaseOccupantForLeavingPlayer(APlayerController* LeavingPC);

    // 나가기 홀드 진행률(0~1). ExitHoldTimerHandle이 활성 상태일 때만 0보다 큼 - ARunnerCharacter::
    // GetInteractHoldPercent()와 완전히 동일한 패턴. 숨어있는 동안은 이 캐비넷 자체가 조종되는 폰이라
    // MatchWidget이 이 값을 읽어서 나가기 게이지를 갱신함
    UFUNCTION(BlueprintPure, Category = "Cabinet")
    float GetExitHoldPercent() const;

    // 술래가 상호작용을 도중에 취소했을 때(키를 뗐을 때 등) 호출해서 진행 중이던 오픈 타이머를 취소함
    UFUNCTION(BlueprintCallable, Category = "Cabinet")
    void CancelOpening();

    // 도망자를 숨을 때 이 위치로 겹쳐서 옮김. 에디터에서 캐비넷 내부/앞쪽으로 위치 조정
    USceneComponent* GetParkPoint() const { return ParkPoint; }

protected:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // 액터의 실제 루트는 AMiscActor의 Root를 그대로 사용 (CabinetMesh/ParkPoint/PeekArm이 전부
    // 이 밑에 형제 관계로 붙어있어서, CabinetMesh(외형)만 따로 옮기거나 교체해도 ParkPoint/PeekArm
    // 위치가 같이 끌려가지 않음)

    // 장롱 본체 메시. 라인 트레이스(상호작용)에 맞도록 콜리전이 있어야 함
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cabinet")
    TObjectPtr<UStaticMeshComponent> CabinetMesh;

    // 도망자가 숨었을 때 겹쳐서 이동할 위치. 에디터에서 캐비넷 안쪽으로 배치
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cabinet")
    TObjectPtr<USceneComponent> ParkPoint;

    // 술래가 열어서 안의 도망자를 잡았을 때, 목조르기 연출 직전에 술래가 옮겨질 위치/방향.
    // 에디터(캐비넷 블루프린트 Components)에서 캐비넷 앞쪽에 배치하고, X축(빨간 화살표)이 캐비넷을
    // 바라보도록 회전시킬 것. 러너는 술래 손(ChokeGrabPoint) 앞에 스냅되므로, 러너가 캐비넷 메시에
    // 파묻혀 보이면 이 점을 캐비넷에서 더 멀리 빼면 됨.
    // 위치는 X/Y만 사용하고 높이(Z)는 술래의 현재 높이를 유지(점을 바닥에 둬도 술래가 박히지 않도록),
    // 회전은 Yaw만 사용
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cabinet")
    TObjectPtr<USceneComponent> ChaserCatchPoint;

    // 술래가 캐비넷 열기 홀드를 시작하는 순간 한 번 옮겨질 위치/방향 마커 (잡을 때 쓰는 ChaserCatchPoint와 별개).
    // 블루프린트에서 캐비넷 앞에 배치하고 X축(빨간 화살표)이 캐비넷을 바라보도록 회전시킬 것.
    // 위치는 X/Y만 사용(높이는 술래 현재 Z 유지), 회전은 Yaw만 사용
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cabinet")
    TObjectPtr<USceneComponent> ChaserInteractPoint;

    // 문틈으로 훔쳐보는 시점을 만들기 위한 아주 짧은 스프링암 (회전시키면 고정된 지점을 중심으로 도는 효과)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cabinet|Peek")
    TObjectPtr<USpringArmComponent> PeekArm;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cabinet|Peek")
    TObjectPtr<UCameraComponent> PeekCamera;

    // 캐비넷 전용 Input Mapping Context. Look/Exit 액션이 여기 매핑됨. 에디터에서 IMC_Cabinet 지정 필요
    UPROPERTY(EditDefaultsOnly, Category = "Cabinet|Input")
    TObjectPtr<UInputMappingContext> CabinetMappingContext;

    // 훔쳐보기 Look (Axis2D)
    UPROPERTY(EditDefaultsOnly, Category = "Cabinet|Input")
    TObjectPtr<UInputAction> CabinetLookAction;

    // 나가기 - 들어올 때(RequiredHoldTime)와 동일하게 홀드해야 실제로 나가짐 (즉시 발동 아님).
    // HandleExitInput/HandleExitInputCanceled/CompleteExitHold가 이 홀드 타이머(ExitHoldTimerHandle)를 관리함
    UPROPERTY(EditDefaultsOnly, Category = "Cabinet|Input")
    TObjectPtr<UInputAction> CabinetExitAction;

    // ---- 문 열림/닫힘 사운드 ----
    // 술래 열기(C++ 시퀀스), 러너 숨기/나가기 홀드(OnRep_IsHoldPreview/OnRep_IsExitHoldPreview) 모두 C++에서 자동 재생
    // 문 위치에서 3D로 재생. 시퀀스를 재생/역재생하는 곳(각 머신 로컬)에서 같이 재생하므로 복제/RPC 없음
    UPROPERTY(EditDefaultsOnly, Category = "Cabinet|Sound")
    TObjectPtr<USoundBase> OpenSound;

    UPROPERTY(EditDefaultsOnly, Category = "Cabinet|Sound")
    TObjectPtr<USoundBase> CloseSound;

    // 들리는 거리. 비워두면 맵 전체에서 들리므로 지정할 것
    UPROPERTY(EditDefaultsOnly, Category = "Cabinet|Sound")
    TObjectPtr<USoundAttenuation> DoorSoundAttenuation;

    UFUNCTION(BlueprintCallable, Category = "Cabinet|Sound")
    void PlayOpenSound();

    UFUNCTION(BlueprintCallable, Category = "Cabinet|Sound")
    void PlayCloseSound();

    // 현재 숨어있는 도망자. 서버에서만 채워지고 복제됨
    UPROPERTY(ReplicatedUsing = OnRep_OccupyingRunner, BlueprintReadOnly, Category = "Cabinet")
    TObjectPtr<ARunnerCharacter> OccupyingRunner;

    // 술래가 지금 이 장롱을 여는 중인지
    UPROPERTY(ReplicatedUsing = OnRep_IsBeingOpened, BlueprintReadOnly, Category = "Cabinet")
    bool bIsBeingOpened = false;

    // 술래의 열기가 "완료"로 끝났는지(true) / 취소로 끝났는지(false). bIsBeingOpened가 false가 되는 건
    // 취소와 완료 둘 다라 클라이언트에서 구분이 안 되므로 같이 복제함. 같은 업데이트로 도착하는 프로퍼티는
    // 값이 전부 적용된 뒤에 RepNotify가 불리므로, OnRep_IsBeingOpened 안에서 이 값은 이미 최신 상태
    UPROPERTY(Replicated)
    bool bOpenCompleted = false;

    // 블루프린트에 추가된 ActorSequence(뚜껑 열림 시퀀스). BeginPlay에서 FindComponentByClass로 찾아 캐싱 -
    // AExitActor::RangeSequence와 같은 방식. 술래의 열기(OpenDuration 홀드)에 맞춰 C++이 직접 재생/역재생함
    // (러너 숨기 미리보기 OnInteractHoldChanged 쪽은 기존대로 블루프린트가 담당)
    UPROPERTY(Transient)
    TObjectPtr<UActorSequenceComponent> OpeningSequence;

    // 술래 열기 시퀀스를 시작하기까지 기다리는 타이머 (OpenDuration - 시퀀스 길이). 각 머신 로컬
    FTimerHandle OpeningSequenceTimerHandle;

    // 이번 열기 시도에서 시퀀스가 실제로 재생을 시작했는지 (취소 시 역재생할지 판단용). 각 머신 로컬
    bool bOpeningSequenceStarted = false;

    // bIsBeingOpened 변화에 맞춰 시퀀스 처리 (OnRep_IsBeingOpened에서 모든 머신이 각자 호출):
    //  - 열기 시작: 시퀀스의 마지막 프레임이 홀드 끝(OpenDuration)과 맞도록 늦게 시작
    //  - 취소: 이미 재생 중이었으면 그 자리에서 역재생(닫힘)
    //  - 완료: 열린 직후 바로 역재생해서 닫는 연출
    void HandleOpeningSequence(bool bNowOpening);

    // OpeningSequenceTimerHandle 완료 시 호출 - 시퀀스를 처음부터 재생
    void StartOpeningSequence();

    // 완료 신호가 (네트워크 지연 등으로) 정방향 재생이 다 끝나기 전에 도착했으면, 뚜껑이 끝까지 열린 걸
    // 보여준 뒤에 닫도록 정방향 재생 종료(OnFinished)를 기다렸다가 역재생하기 위한 플래그. 각 머신 로컬
    bool bCloseWhenOpenFinished = false;

    // 시퀀스 플레이어 OnFinished에 바인딩 (다이나믹 델리게이트라 UFUNCTION 필요)
    UFUNCTION()
    void OnOpeningSequenceFinished();

    // 다음 틱에 역재생 실행 (OnFinished 콜백 안에서 바로 PlayReverse하면, 시퀀스 플레이어가 콜백 이후에
    // 처리하는 "정지" 처리에 덮여서 역재생이 무시됨 → 콜백을 빠져나간 다음 틱에 실행)
    void PlayOpeningSequenceReverse();

    // 도망자가 지금 이 캐비넷에 숨기 위해 홀드 상호작용 중인지 (뚜껑 열리는 미리보기 연출 등에 사용)
    UPROPERTY(ReplicatedUsing = OnRep_IsHoldPreview, BlueprintReadOnly, Category = "Cabinet")
    bool bIsHoldPreview = false;

    // 도망자가 지금 이 캐비넷에서 나가기 위해 홀드 상호작용 중인지 (뚜껑이 반대로 열리는 미리보기 연출 등에 사용)
    UPROPERTY(ReplicatedUsing = OnRep_IsExitHoldPreview, BlueprintReadOnly, Category = "Cabinet")
    bool bIsExitHoldPreview = false;

    // 숨기 전 도망자를 조종하던 PlayerController. 나갈 때 다시 이쪽으로 Possess. 서버 전용, 복제 안 함
    UPROPERTY()
    TObjectPtr<APlayerController> OccupyingController;

    // 지금 이 캐비넷을 여는 중인 술래. 서버 전용, 복제 안 함(bIsBeingOpened만 복제되면 충분 -
    // 이 값은 CanInteractInternal이 "본인의 완료 요청"과 "다른 술래의 새 시도"를 구분하는 데만 쓰임).
    // 홀드 완료 시점에 CanInteract가 bIsBeingOpened==true 때문에 자기 자신을 막아버리는 교착을
    // 피하려고 존재함 (StartOpening에서 세팅, CancelOpening/OnInteract_Implementation 완료 시 비움)
    UPROPERTY()
    TObjectPtr<APawn> CurrentOpener;

    // 지금 이 캐비넷에 숨으려고 홀드를 시작한 도망자. 서버 전용, 복제 안 함. OccupyingRunner는
    // 홀드가 "완료"돼야 채워지므로, 홀드 "도중"에 다른 도망자가 같은 캐비넷에 동시에 홀드를
    // 시작하는 걸 막으려면 이 값이 따로 필요함(CurrentOpener와 동일한 목적).
    // OnInteractHoldUpdate_Implementation의 도망자 분기에서 홀드 시작 시 세팅, 취소/완료 시 비움
    UPROPERTY()
    TObjectPtr<APawn> PendingHider;

    float PeekYaw = 0.f;
    float PeekPitch = 0.f;

    // BeginPlay 시점에 PeekArm의 에디터 지정 회전값(캐비넷 문틈이 실제로 바라보는 방향)을 캐싱해둠.
    // Look 입력은 이 값에 대한 상대 오프셋으로 적용됨 (그래야 에디터에서 돌려둔 방향이 안 날아감)
    FRotator PeekHomeRotation;

    void StartHiding(ARunnerCharacter* Runner);

    // "여는 중" 상태(bIsBeingOpened)와 CurrentOpener만 세팅함 - 타이머는 안 걸림(시간은 술래
    // 클라이언트의 로컬 홀드 타이머가 잼). 실제 완료 판정은 OnInteract_Implementation에서 처리
    void StartOpening(APawn* Chaser);

    // 조종권을 원래 도망자에게 되돌리고 숨김 상태를 해제 (나가기/잡힘 공통 처리).
    // bRestoreOriginalPosition=true면 도망자를 숨기 직전 위치로 복귀시킴 (자발적으로 나갈 때만 사용,
    // 술래에게 잡혔을 때는 일단 기본값 false로 캐비넷 위치에 그대로 둠 - 나중에 다시 검토)
    void ReturnControlToRunner(bool bRestoreOriginalPosition = false);

    // bIsHoldPreview를 갱신하고, 서버 자신의 화면에도 즉시 반영 (OnRep 수동 호출 패턴)
    void SetHoldPreview(bool bNewHoldPreview);

    // bIsExitHoldPreview를 갱신하고, 서버 자신의 화면에도 즉시 반영 (OnRep 수동 호출 패턴)
    void SetExitHoldPreview(bool bNewExitHoldPreview);

    void HandleLookInput(const FInputActionValue& Value);

    // Exit 키를 누른 순간: 들어올 때와 동일하게 즉시 나가지 않고 홀드 타이머를 시작함
    void HandleExitInput(const FInputActionValue& Value);

    // Exit 키를 뗀 순간: 홀드 타이머가 아직 진행 중이었다면 취소 (나가기 실패, 미리보기 원상복구)
    void HandleExitInputCanceled(const FInputActionValue& Value);

    // Exit 홀드 타이머 완료 시 실제로 서버에 나가기를 요청
    void CompleteExitHold();

    UFUNCTION(Server, Reliable)
    void ServerRequestExit();

    // Exit 홀드 시작/취소를 서버에 알려서, 서버 권한으로 bIsExitHoldPreview를 갱신시킴 (뚜껑 역재생 미리보기용)
    UFUNCTION(Server, Reliable)
    void ServerSetExitHold(bool bHolding);

    // AescaperoomCharacter::SetMatchWidgetHoldGaugeActive()와 동일한 목적/구현 - 나가기 홀드 시작/취소/완료
    // 시점에 MatchWidget의 홀드 게이지를 켜고 끔 (Controller가 ADungeonPlayerController가 아니거나
    // MatchWidget을 아직 못 찾았으면 조용히 무시)
    void SetMatchWidgetHoldGaugeActive(bool bActive, const FText& HoldText = FText::GetEmpty()) const;

    // 캐비넷을 조종하게 된 순간부터(SetupPlayerInputComponent) 나가기 전까지(UnPossessed) MatchWidget에
    // ExitPromptText를 상시 표시/숨김. 위와 동일한 이유로 조용히 무시될 수 있음
    void SetMatchWidgetPromptTextActive(bool bActive, const FText& PromptText = FText::GetEmpty()) const;

    UFUNCTION()
    void OnRep_OccupyingRunner();

    UFUNCTION()
    void OnRep_IsBeingOpened();

    UFUNCTION()
    void OnRep_IsHoldPreview();

    UFUNCTION()
    void OnRep_IsExitHoldPreview();

    // 숨음/나옴 상태가 바뀔 때 연출(문 애니메이션, 사운드 등)을 위해 블루프린트에서 구현
    UFUNCTION(BlueprintImplementableEvent, Category = "Cabinet")
    void OnOccupancyChanged(bool bNowOccupied);

    // 술래가 열기 시작/취소/완료됐을 때 연출을 위해 블루프린트에서 구현
    UFUNCTION(BlueprintImplementableEvent, Category = "Cabinet")
    void OnOpeningStateChanged(bool bNowOpening);

    // 도망자의 홀드 상호작용 진행 상태가 바뀔 때(true=시작, false=취소/완료) 미리보기 연출
    // (뚜껑이 서서히 열리는 애니메이션 등)을 위해 블루프린트에서 구현
    UFUNCTION(BlueprintImplementableEvent, Category = "Cabinet")
    void OnInteractHoldChanged(bool bNowHolding);

    // 도망자의 나가기 홀드 진행 상태가 바뀔 때(true=시작, false=취소/완료) 미리보기 연출
    // (뚜껑이 반대로 열리는 애니메이션 등)을 위해 블루프린트에서 구현
    UFUNCTION(BlueprintImplementableEvent, Category = "Cabinet")
    void OnExitHoldChanged(bool bNowHolding);

    // 열었더니 안에 도망자가 있어서 잡았을 때 연출을 위해 블루프린트에서 구현
    UFUNCTION(BlueprintImplementableEvent, Category = "Cabinet")
    void OnOccupantCaught(ARunnerCharacter* CaughtRunner);

private:
    FTimerHandle ExitHoldTimerHandle;

    // 이 캐비넷을 조종하면서 IMC_Cabinet을 추가한 "로컬" PlayerController (SetupPlayerInputComponent에서
    // 세팅, 로컬 전용/복제 안 함). 조종이 끝났을 때 원격 클라이언트에서도 이 컨트롤러 기준으로 정리하기 위함
    TWeakObjectPtr<APlayerController> LocalInputController;

    // PC의 로컬 플레이어에서 IMC_Cabinet 제거 + MatchWidget의 나가기 문구/게이지 끄기
    void CleanupLocalCabinetInput(APlayerController* PC);

    // CleanupLocalCabinetInput의 반대: IMC_Cabinet 추가 + 나가기 문구 표시 (PawnClientRestart에서 매 빙의마다 호출)
    void SetupLocalCabinetInput();
};
