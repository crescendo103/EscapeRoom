// ChaserCharacter.h
#pragma once

#include "CoreMinimal.h"
#include "TimerManager.h"
#include "escaperoomCharacter.h"
#include "ChaserCharacter.generated.h"

class UInputMappingContext;
class UInputAction;
struct FInputActionValue;
class ADoorActor;
class UAnimMontage;
class UMeshComponent;
class AWFCGridManager;
class ARunnerCharacter;
class UAudioComponent;

/**
 * 술래 캐릭터.
 * 이동/시야/점프는 부모(AescaperoomCharacter)에서 공통으로 처리하고,
 * 여기서는 술래 전용 입력(잡기, 상호작용, 부감)만 추가한다.
 *
 * 참고 (기획 메모):
 * - 이동속도: 도망자보다 약간 빠름 - 아직 값 미적용 (다음 단계)
 * - 시야: 도망자와 동일 - 별도 처리 없음, 부모 클래스 그대로 사용
 * - 조명 조작: 없음 - 해당 입력 자체를 두지 않음
 * - 문: 열기 불가, 부수기만 가능 - InteractAction으로 처리함(Grab이 아님). 카메라 정면
 *   라인 트레이스로 문을 찾으면 캐비닛과 동일한 홀드 파이프라인을 타서 KickHoldTime(기본 3초)만큼
 *   눌러야 하고, 그동안 MatchWidget에 프로그레스바가 뜸(중간에 키를 떼면 CancelHoldInteract()가 취소).
 *   홀드가 끝나면 KickDoor 몽타주 재생을 요청하고, 실제로 부술 수 있는지는 몽타주의 AnimNotify가
 *   트리거하는 서버의 DoorActor::Break() 내부에서 최종 검증(닫혀있어야 함, 즉 bIsOpen == false)
 * - 캐비닛: CabinetActor가 이미 ChaserTag를 보고 분기 처리함(OpenDuration만큼 직접 홀드해야 열림,
 *   홀드 중에는 bIsInteracting으로 이동도 정지됨).
 *   러너와 동일한 InteractAction 흐름을 그대로 재사용함 (문이 아닌 대상일 때만 이 경로를 탐)
 * - 잡기: 술래 고유의 새 입력 (GrabAction). 문 파괴와는 무관하며, 대상 유무와 상관없이 항상
 *   bIsGrab 펄스(잡기 애니메이션)를 재생하고, 서버가 술래 정면에 순간적인 박스 판정
 *   (GrabBoxForwardOffset/GrabBoxExtent)을 해서 안에 있는 러너 중 가장 가까운 한 명을 잡음
 *   (캐비닛에서 발각됐을 때 잡힘 처리 연동은 보류 중 - 나중에 붙일 예정)
 * - 부감(목 늘려서 위에서 내려다보기): 키를 누르면 OverheadViewDuration(5초) 동안 켜졌다가 서버가 자동으로 끄고,
 *   그때부터 OverheadViewCooldown(15초) 동안 다시 못 씀. 사용 중/쿨타임 중 입력은 무시.
 *   목 늘어나는 애니메이션은 서버 권위 + 전체 복제되는 bIsOverheadViewActive로 모든 클라(술래 본인 포함)에서 재생됨
 *   (BP_ChaserCharacter의 necklong 타임라인이 이 값을 보고 늘어나고/줄어듦)
 */
UCLASS()
class ESCAPEROOM_API AChaserCharacter : public AescaperoomCharacter
{
    GENERATED_BODY()

public:
    AChaserCharacter();

    // 부감 스킬 활성화 여부 (서버 권위 + 전체 클라이언트 복제).
    // AnimBP가 이 값을 참고해서 목 늘어나는 애니메이션으로 전환함 (술래 본인 포함 모든 클라에서 동일하게 재생됨)
    UPROPERTY(ReplicatedUsing = OnRep_IsOverheadViewActive, BlueprintReadOnly, Category = "State")
    bool bIsOverheadViewActive = false;

    // Grab 잡기 동작 재생 중인지 (서버 권위 + 전체 클라이언트 복제, bIsOverheadViewActive와 동일한 패턴).
    // 문 파괴/러너 포획 등 실제로 뭔가에 성공했을 때만이 아니라, Grab 입력이 들어오면 대상 유무와 상관없이
    // (허탕이어도) 항상 true가 됨 - 잡기 동작 자체는 매번 재생되어야 하기 때문. AnimBP가 이 값을 참고해서
    // 잡기 애니메이션을 재생하면 됨(술래 본인 포함 모든 클라에서 동일하게 재생됨). Grab은 홀드 없이 즉시
    // 처리되는 액션이라 true가 된 뒤 GrabPulseDuration만큼 지나면 서버가 자동으로 다시 false로 되돌림 -
    // AnimBP는 이 값의 false->true 엣지를 감지해서 짧은 잡기 애니메이션을 트리거하면 됨
    UPROPERTY(ReplicatedUsing = OnRep_IsGrab, BlueprintReadOnly, Category = "State")
    bool bIsGrab = false;

    // 부감 시 목이 늘어나는 현재 값. BP_ChaserCharacter의 Timeline(necklong)이 매 틱 이 값을 Set하고,
    // AnimBP는 As Chaser Character 레퍼런스로 Get만 해서 Transform Modify Bone에 그대로 꽂아 쓰면 됨
    UPROPERTY(BlueprintReadWrite, Category = "Animation")
    float NeckStretchAmount = 0.f;

    // kickdoor 애니메이션에 심어둔 AnimNotify(UAnimNotify_ChaserKickDoor)가 발로 문을 차는 프레임에
    // 도달했을 때 호출하는 함수. 실제 문 파괴 요청(ServerBreakDoor)이 여기서 나간다 - Interact() 시점이
    // 아니라 이 타이밍에 실행되므로 연출(발이 문에 닿는 순간)과 판정이 맞아떨어짐.
    // BlueprintCallable로도 열어둬서 네이티브 AnimNotify 대신 블루프린트 AnimNotify에서 직접
    // (Cast To ChaserCharacter -> Notify Kick Door Hit) 호출해도 동작함
    UFUNCTION(BlueprintCallable, Category = "Grab")
    void NotifyKickDoorHit();

    // 목조르기 몽타주(ChokeGrabMontage)에 심어둔 AnimNotify(UAnimNotify_ChaserChokeCapture)가 러너를
    // 놓아주는 프레임에 도달했을 때 호출. 몽타주가 끝날 때가 아니라 이 타이밍에 러너를 감옥으로 보냄.
    // 몽타주가 Multicast라 모든 머신에서 불리지만, 대상(PendingChokeRunner)과 Capture()가 서버 전용이라
    // 서버에서만 처리함(킥도어와 달리 Server RPC 불필요). BlueprintCallable이라 BP AnimNotify에서도 호출 가능
    UFUNCTION(BlueprintCallable, Category = "Grab")
    void NotifyChokeCapture();

    // [서버 전용] 러너 목조르기 연출 시작 - Grab(PerformGrabBoxCheck)과 캐비닛 열기(ACabinetActor) 공통 경로.
    // 러너를 ChokeGrabPoint로 스냅 + 양쪽 몽타주 재생(Multicast) + PendingChokeRunner/안전장치 타이머 세팅.
    // 실제 감옥 이동(Capture)은 Choke Capture 노티파이(없으면 몽타주 길이 타이머)가 처리.
    // 시작하지 못하면(이미 잡힘/목조르기 중인 러너, 이 술래가 이미 다른 러너를 조르는 중 등) false 반환 -
    // 호출부가 필요하면 Capture()로 폴백할 수 있도록 (캐비닛은 이미 러너를 꺼냈으므로 폴백 필수)
    bool StartChokeGrab(ARunnerCharacter* Runner);

    // StartChokeGrab()이 지금 시작 가능한지 (같은 조건). 캐비닛처럼 연출 전에 술래를 먼저 옮겨야 하는
    // 호출부가, 시작도 못 할 상황에서 술래만 괜히 순간이동시키지 않도록 미리 확인하는 용도
    bool CanStartChokeGrab(const ARunnerCharacter* Runner) const;

    // [서버 전용] 목조르기 연출 직전에 술래를 지정 위치/방향으로 옮김 (캐비닛에서 잡을 때 캐비닛 앞으로).
    // 반드시 StartChokeGrab()보다 먼저 호출 - 러너는 StartChokeGrab 호출 순간의 ChokeGrabPoint(술래 손
    // 소켓의 자식)로 스냅되므로, 술래만 옮겨두면 러너도 자동으로 옮겨진 손 앞에 놓임.
    // 1인칭이라 몸 방향이 컨트롤러 회전을 따라가므로 컨트롤러 회전도 같이 맞춤(서버 + 술래 본인 클라)
    void SnapForChokeGrab(const FVector& Location, const FRotator& Rotation);

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    // 조종 컨트롤러가 바뀔 때(서버 PossessedBy + 클라 OnRep_Controller 경유) 호출 - 공포 반경 사운드 on/off 재판단
    virtual void NotifyControllerChanged() override;

    // [리슨] 공통 IMC(부모) + 술래 전용 IMC 추가 (BeginPlay에서 옮겨옴 - 호스트에서 키가 안 먹던 문제)
    virtual void AddLocalInputMappings(UEnhancedInputLocalPlayerSubsystem* Subsystem) override;

    // ---- 공포 반경(Terror Radius) 사운드 ----
    // 술래 몸에 붙은 루프 BGM. 각 도망자 클라이언트에서 재생되고, Attenuation 반경 안에 들어온 사람에게만 들림.
    // 술래 본인 화면(IsLocallyControlled)과 데디서버에서는 재생하지 않음. 네트워크 복제/RPC 없음 - 각 머신이 로컬로 판단.
    // BP 디테일에서 Sound(루프 BGM)와 Attenuation Settings(반경)를 지정할 것.
    // 주의: 컴포넌트의 Allow Spatialization을 끄면 Attenuation 자체가 무시되어 거리와 상관없이 들림 ->
    // Allow Spatialization은 켜두고, 방향감을 없애려면 Attenuation 안의 Enable Spatialization만 끌 것
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chaser|Audio")
    TObjectPtr<UAudioComponent> TerrorRadiusAudio;

    // 지금 머신 기준으로 TerrorRadiusAudio를 재생할지 판단해서 켜거나 끔
    void RefreshTerrorRadiusAudio();

    // 배경음악 <-> 공포 반경 BGM 전환 기준 거리(cm). 0이면 TerrorRadiusAudio의 Attenuation 최대 거리를 그대로 사용
    UPROPERTY(EditDefaultsOnly, Category = "Chaser|Audio", meta = (ClampMin = "0.0"))
    float TerrorRadiusOverride = 0.f;

public:
    // 공포 반경(cm). DungeonPlayerController가 배경음악을 멈출지 판단할 때 사용 (외부 호출용이라 public)
    float GetTerrorRadius() const;

protected:
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // Grab 애니메이션 재생 중(bIsGrab) 또는 KickDoor 몽타주 재생 중에는 이동 입력을 막음.
    // bIsGrab은 서버 권위 + 전체 복제, KickDoor는 몽타주 자체 재생 상태(IsPlayingKickDoorMontage)를
    // 각 클라이언트가 직접 물어봐서 판단하므로 별도 bool/복제가 필요 없음
    virtual bool CanMove() const override;

    // 목조르기 몽타주(ChokeGrabMontage) 재생 중에는 이동뿐 아니라 시점 회전(Look)도 막음 -
    // 러너를 ChokeGrabPoint 기준으로 스냅시켜뒀으므로 술래가 돌아버리면 손-목 위치가 어긋나 보이기 때문
    virtual bool CanLook() const override;

    // ---- 잡기 판정: 술래 정면에 순간적으로 생기는 박스 ----
    // Grab 입력 시 서버가 한 번만 박스 겹침 검사를 하고 끝냄 (컴포넌트를 상시 붙여두지 않음).
    // 박스 중심 = 술래 위치 + 몸 정면(Yaw) * GrabBoxForwardOffset, 회전은 술래 Yaw만 따라감(시점 상하 무관)

    // 술래 위치에서 박스 중심까지 정면 거리 (cm)
    UPROPERTY(EditDefaultsOnly, Category = "Grab")
    float GrabBoxForwardOffset = 80.f;

    // 박스 반크기 (X=앞뒤, Y=좌우, Z=위아래, cm)
    UPROPERTY(EditDefaultsOnly, Category = "Grab")
    FVector GrabBoxExtent = FVector(40.f, 40.f, 80.f);

    // 입력 후 판정까지 지연(초). 잡기 애니메이션에서 손을 뻗는 타이밍에 맞추고 싶을 때 사용. 0 = 즉시
    UPROPERTY(EditDefaultsOnly, Category = "Grab", meta = (ClampMin = "0.0"))
    float GrabHitDelay = 0.f;

    // ChokeGrabPoint가 붙는 손 소켓 이름 (목조르기 연출 위치 기준)
    UPROPERTY(EditDefaultsOnly, Category = "Grab")
    FName GrabSocketName = TEXT("GrabSocket");

    // 러너를 잡았을 때(목조르기 연출) 러너가 스냅되어야 할 위치/각도. GrabSocketName 손
    // 소켓에 붙어 있고, 여기에 추가로 위치/각도 오프셋을 자유롭게 줄 수 있음 (연출 위치 튜닝용).
    // 에디터에서 ChokeGrabMontage/BeingChokedMontage 둘을 실제로 재생해보면서 손-목 위치가 맞도록
    // Relative Location/Rotation을 조정하면 됨 - AExitActor::ClimbStartPoint와 동일한 "위치 마커" 용도
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grab")
    TObjectPtr<USceneComponent> ChokeGrabPoint;

    // 러너를 잡는 데 성공했을 때 술래가 재생할 목조르기 몽타주 (제자리 재생, 루트모션 없음).
    // ARunnerCharacter::BeingChokedMontage와 짝을 이루는 애셋 - 서로 다른 길이면 안 됨
    UPROPERTY(EditDefaultsOnly, Category = "Animation")
    TObjectPtr<UAnimMontage> ChokeGrabMontage;

    // [미사용] 예전 문 탐지 라인 트레이스 사거리. 지금은 정면 박스(Interaction|Box 카테고리)로 판정
    UPROPERTY(EditDefaultsOnly, Category = "Grab")
    float KickDoorTraceRange = 200.f;

    // 술래 전용 Input Mapping Context (Grab/Interact/OverheadView 키가 여기 매핑됨)
    // 블루프린트에서 IMC_Chaser 애셋을 만들어서 지정해줘야 함
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputMappingContext* ChaserMappingContext;

    // 잡기: 근처 도망자를 즉시 탈락시키는 술래 고유 액션
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* GrabAction;

    // 상호작용: 캐비닛 등 IInteractable 대상 공용 처리 (러너의 InteractAction과 동일한 역할,
    // CabinetActor가 ActorHasTag(ChaserTag)로 이미 술래용 분기를 갖고 있음) + 문 부수기(KickDoor).
    // Interact() 안에서 대상이 ADoorActor면 캐비닛과 동일한 홀드 파이프라인을 타되, 홀드가 끝나면
    // 일반 상호작용 대신 KickDoor 몽타주 재생 분기를 탄다
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* InteractAction;

    // 부감: 목이 늘어나 위에서 미로를 내려다보는 스킬. 누를 때마다 On/Off 토글되는 방식 (홀드 아님)
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* OverheadViewAction;

    // ---- 부감 지속시간/쿨타임 ----
    // 한 번 쓰면 켜져있는 시간(초). 끝나면 서버가 자동으로 bIsOverheadViewActive를 끔
    UPROPERTY(EditDefaultsOnly, Category = "OverheadView", meta = (ClampMin = "0.1"))
    float OverheadViewDuration = 5.f;

    // 부감이 끝난 뒤 다시 쓸 수 있을 때까지의 시간(초)
    UPROPERTY(EditDefaultsOnly, Category = "OverheadView", meta = (ClampMin = "0.0"))
    float OverheadViewCooldown = 15.f;

    // 스킬이 끝난 뒤(목이 내려오는 동안)에도 이 시간(초)만큼 더 이동 불가. 0이면 끝나자마자 이동 가능
    // 기준 시각은 서버가 기록해 복제하는 OverheadActiveEndServerTime이라 별도 복제 변수/타이머 없음
    UPROPERTY(EditDefaultsOnly, Category = "OverheadView", meta = (ClampMin = "0.0"))
    float OverheadMoveLockAfterEnd = 5.f;

    // 스킬 사용 중이거나, 끝난 뒤 OverheadMoveLockAfterEnd가 아직 안 지났으면 true
    bool IsOverheadMoveLocked() const;

    // 사용 중에 MatchWidget 홀드 문구 자리에 띄울 문구. {0} 자리에 남은 초가 들어감 ({0}을 빼면 초 없이 표시)
    UPROPERTY(EditDefaultsOnly, Category = "OverheadView")
    FText OverheadActiveTextFormat = NSLOCTEXT("Chaser", "OverheadActive", "Overseeing {0}");

public:
    // UI(MatchWidget 쿨타임 아이콘)용 조회 함수 - 서버 시간 기준 복제값으로 계산하므로 클라에서도 정확함
    // 사용 중 남은 시간(초), 사용 중이 아니면 0
    float GetOverheadActiveRemaining() const;
    // 쿨타임 남은 시간(초), 쿨타임이 아니면 0
    float GetOverheadCooldownRemaining() const;
    // 쿨타임 진행률 (1 = 막 시작, 0 = 다시 사용 가능)
    float GetOverheadCooldownFraction() const;

protected:

    // 부감 중 이 이름의 본(NeckStretchAmount로 늘어남)이 지금 서있는 위치의 천장보다 높아지면
    // 맵 전체 천장을 투명화한다. 실제 스켈레톤 본 이름에 맞춰야 함 (현재 기준 "neck")
    UPROPERTY(EditDefaultsOnly, Category = "OverheadView")
    FName NeckBoneName = TEXT("neck");

    // 지금 맵 전체 천장이 페이드(투명) 상태인지. Tick에서 매 프레임 SetScalarParameterValue를
    // 다시 호출하지 않도록, 상태가 실제로 바뀔 때만 반영하기 위한 캐시용 플래그
    bool bCeilingsFaded = false;

    // 천장 FadeAmount가 0 <-> 1로 바뀌는 데 걸리는 시간(초). 0이면 즉시 전환
    UPROPERTY(EditAnywhere, Category = "Overhead", meta = (ClampMin = "0.0"))
    float CeilingFadeDuration = 0.5f;

    // 현재 실제로 적용 중인 FadeAmount 값과 목표값. Tick에서 Current를 Target 쪽으로 서서히 이동
    float CurrentCeilingFade = 0.f;
    float TargetCeilingFade = 0.f;

    // AllCeilings 전체에 FadeAmount 값을 적용 (MID가 없으면 생성)
    void ApplyCeilingFadeValue(float Value);

    // 매 틱 Current -> Target 보간 + 램프 메시 숨김 타이밍 처리
    void TickCeilingFade(float DeltaSeconds);

    // HideInOverhead 메시들의 표시 상태 갱신
    void SetOverheadMeshesHidden(bool bHideMeshes);

    // Ceiling 태그가 붙은 천장 메시 컴포넌트 전체 목록 (현재 방 하나가 아니라 맵에 존재하는 모든
    // 방/빅룸의 천장을 포함). AWFCGridManager::OnMapGenComplete 델리게이트에서 CacheAllCeilings()가
    // 호출될 때 딱 한 번 채워지고, 부감 스킬을 켜고 끄는 동작에서는 다시 채우지 않고 이 목록을
    // 그대로 재사용한다 (맵은 한 번 생성되면 바뀌지 않으므로 재수집이 불필요함)
    UPROPERTY()
    TArray<TWeakObjectPtr<UMeshComponent>> AllCeilings;

    // "HideInOverhead" 컴포넌트 태그가 붙은 메시 목록 (천장에 매달린 램프 등).
    // 천장처럼 머티리얼로 투명화하지 않고, 천장이 투명해지는 동안 SetVisibility(false)로 아예 숨김.
    // 라이트 컴포넌트는 대상이 아님 (메시만 숨기고 빛은 그대로 유지)
    UPROPERTY()
    TArray<TWeakObjectPtr<UMeshComponent>> OverheadHiddenMeshes;

    // 천장 높이 기준값 (맵이 평면 그리드라 모든 방 천장이 같은 Z에 있다는 전제 하에, CacheAllCeilings()에서
    // 찾은 천장 중 하나의 Z만 뽑아서 고정해둔 값). 매 틱 위치를 다시 찾을 필요 없이 이 값 하나로 비교한다
    float CachedCeilingZ = 0.f;

    // BeginPlay에서 찾아 캐싱해둔 맵 생성 매니저 참조 (레벨에 하나만 존재)
    TWeakObjectPtr<AWFCGridManager> CachedGridManager;

    // AWFCGridManager::OnMapGenComplete에 바인딩되는 콜백. 방/빅룸 스폰이 전부 끝난 뒤 한 번 호출되어
    // AllCeilings를 채운다.
    // [수정] 예전엔 GridManager->GetSpawnedRooms()에서 모았는데, 그 배열은 서버에서만 채워지고 복제되지
    // 않아서 클라이언트(술래가 원격 접속한 경우)에서는 항상 비어있었음 -> 천장이 안 사라지던 원인.
    // 지금은 월드에 실제로 존재하는(=복제되어 들어온) 액터들 중 "Ceiling" 컴포넌트 태그가 붙은 메시를 직접 모음
    void CacheAllCeilings();

    // 천장이 투명화된 동안 0.5초마다: 나중에 복제되어 들어온(멀리 있던) 방의 천장까지 다시 모아서 투명화 적용
    void RefreshFadedCeilings();
    FTimerHandle CeilingRefreshTimerHandle;

    // 부감 스킬(bIsOverheadViewActive)이 켜져있는 동안 Tick에서 호출: NeckBoneName 본이 지금
    // 서있는 위치의 천장보다 높아졌는지 확인해서, 맵 전체 천장의 FadeAmount를 한꺼번에 켜고 끈다
    void UpdateCeilingFade();

    // AllCeilings 전체에 FadeAmount 스칼라 파라미터를 세팅 (bFaded면 1.f, 아니면 0.f)
    void SetAllCeilingsFaded(bool bFaded);

    // Grab 상호작용 성공 후 bIsGrab을 true로 유지하는 시간(초). 몽타주 없이 상태값만으로 트리거하는
    // 방식이라, AnimBP에서 이 시간 동안만 잡기 애니메이션을 재생하도록 하면 됨
    UPROPERTY(EditDefaultsOnly, Category = "Animation")
    float GrabPulseDuration = 2.63f;//애니메이션 길이랑 같음 수정금지

    FTimerHandle GrabPulseTimerHandle;

    // 문 부수기(KickDoor) 몽타주. Grab 대상이 ADoorActor일 때 이 몽타주를 재생함 - 몽타주는 자체적으로
    // 재생 길이/진행 상태를 갖고 있으므로 bIsGrab처럼 별도의 bool+타이머로 펄스를 흉내낼 필요가 없다.
    // 몽타주 타임라인에 AnimNotify(UAnimNotify_ChaserKickDoor)를 심어서 발이 문에 닿는 프레임에
    // NotifyKickDoorHit()이 호출되도록 하면 됨
    UPROPERTY(EditDefaultsOnly, Category = "Animation")
    TObjectPtr<UAnimMontage> KickDoorMontage;

    // 문을 차기 위해 홀드(충전)하는 3초 동안 재생할 몽타주. DoorActor::KickHoldTime(기본 3초)과
    // 길이가 정확히 맞게 만들어진 애셋이라 재생 속도 보정 없이 1배속으로 그냥 재생하면 홀드가 끝나는
    // 시점과 몽타주가 끝나는 시점이 자연스럽게 맞아떨어짐. 홀드가 끝나면 KickDoorMontage가 이어서
    // 재생되면서 같은 슬롯 위에서 자동으로 블렌드 전환됨
    UPROPERTY(EditDefaultsOnly, Category = "Animation")
    TObjectPtr<UAnimMontage> KickChargeMontage;

    // 캐비넷 열기 홀드(OpenDuration, 기본 10초)를 시작할 때 한 번 재생하는 몽타주. 루프/속도 보정 없이
    // 1배속으로 한 번만 재생하므로 홀드 시간과 맞추려면 몽타주 에셋의 Rate Scale로 조절.
    // 홀드 취소 시 블렌드아웃으로 정지, 완료 시 목조르기/허탕 몽타주로 넘어가며 정지
    UPROPERTY(EditDefaultsOnly, Category = "Animation")
    TObjectPtr<UAnimMontage> CabinetOpenMontage;

    // 캐비넷을 끝까지 열었는데 안에 러너가 없었을 때(허탕) 재생하는 몽타주.
    // 재생되는 동안은 목조르기 몽타주와 동일하게 이동(CanMove)과 시점 회전(CanLook)이 막힘
    UPROPERTY(EditDefaultsOnly, Category = "Animation")
    TObjectPtr<UAnimMontage> CabinetEmptyMontage;

    // Grab()에서 문을 대상으로 KickDoor 몽타주를 재생시켰을 때, 실제로 부술 문을 기억해두는 로컬 전용
    // 포인터(복제 안 됨). NotifyKickDoorHit()가 호출되는 순간(몽타주를 재생 중인 로컬 컨트롤 클라이언트)에만
    // 유효하면 되고, 그 시점에 ServerBreakDoor로 서버에 전달하면 서버가 최종 검증 후 실제로 파괴함
    TWeakObjectPtr<ADoorActor> PendingKickDoorTarget;

    // KickDoorMontage가 현재 재생 중인지 (자기 자신의 AnimInstance 기준으로 즉시 판단, 복제 불필요).
    // Grab() 재입력 방지(CanMove 등)에 bIsGrab과 함께 사용됨
    bool IsPlayingKickDoorMontage() const;

    // ChokeGrabMontage가 현재 재생 중인지 - IsPlayingKickDoorMontage()와 완전히 동일한 패턴.
    // CanMove()가 참고해서 목조르는 연출 중에는 이동을 막음
    bool IsPlayingChokeGrabMontage() const;

    // CabinetEmptyMontage(허탕) 재생 중인지 - 이동/시점 회전 잠금 판단용 (IsPlayingChokeGrabMontage와 같은 패턴)
    bool IsPlayingCabinetEmptyMontage() const;

    // 캐비넷 여는 몽타주(CabinetOpenMontage) 재생 중인지 - 홀드가 끝난 뒤 남은 여는 모션 동안 시점 고정용
    bool IsPlayingCabinetOpenMontage() const;

    // Grab()으로 러너를 잡는 데 성공했을 때, 실제로 감옥으로 보낼(Capture) 대상을 기억해두는 로컬
    // 전용 포인터(복제 안 됨) - PendingKickDoorTarget과 동일한 성격. CompleteChokeGrab()이
    // ChokeGrabTimerHandle 타이머가 끝나는 시점에 이 값을 꺼내 씀
    TWeakObjectPtr<ARunnerCharacter> PendingChokeRunner;

    FTimerHandle ChokeGrabTimerHandle;

    // 실제로 PendingChokeRunner를 감옥으로 보냄(Capture). 두 경로에서 호출되며 먼저 불린 쪽만 처리됨:
    //  1) NotifyChokeCapture() - 몽타주의 Choke Capture 노티파이 타이밍 (기본 경로)
    //  2) ChokeGrabTimerHandle 완료 - 몽타주 길이만큼 걸어둔 안전장치 (노티파이 미배치/몽타주 중단 대비,
    //     안 그러면 러너가 목졸린 채 영구 고정됨)
    // 한 번 처리하면 타이머를 끄고 PendingChokeRunner를 비우므로 두 번 Capture되지 않음
    void CompleteChokeGrab();

    // 판정 박스의 중심/회전 계산 (술래 정면, Yaw만)
    void GetGrabBox(FVector& OutCenter, FQuat& OutRotation) const;

    // [서버] 정면 박스 안의 잡을 수 있는 러너 중 술래와 가장 가까운 한 명을 찾아 목조르기 시작
    void PerformGrabBoxCheck();

    FTimerHandle GrabHitDelayTimerHandle;

    // 몸 정면 박스 안의 ADoorActor 중 가장 가까운 것 (없으면 nullptr). 이름은 예전 그대로 둠
    // (AescaperoomCharacter::FindInteractableInFrontBox(ADoorActor) 호출)
    ADoorActor* FindDoorTargetByCameraTrace() const;

    // 잡기 입력 처리. 문 부수기는 Interact()에서 처리. 대상 유무와 상관없이 항상
    // ServerStartGrabPulse()를 보내고, 서버가 잡기 애니메이션 + 정면 박스 판정을 함께 처리함
    void Grab(const FInputActionValue& Value);

    // ---- 상호작용: 공통 흐름(Interact/홀드/완료/취소/서버 RPC)은 부모(AescaperoomCharacter)에 있음.
    // 술래 전용 부분만 아래 훅으로 override. 전체 동작 설명(원래 Interact 주석):
    // 상호작용 입력 처리. 먼저 카메라 정면 라인 트레이스(FindDoorTargetByCameraTrace)로
    // 문 부수기(KickDoor) 대상을 찾음. 문을 찾았으면 그 문을
    // Target으로 취급해서 캐비닛 등 다른 IInteractable 대상과 완전히 동일한 홀드 파이프라인을 탄다 -
    // IInteractable::GetInteractHoldTime()을 물어보면 DoorActor가 술래에게는 KickHoldTime(기본 3초)을
    // 반환하므로 그만큼 눌러야 하고, 그동안 MatchWidget에 프로그레스바가 뜬다(캐비닛과 동일한 UI/로직
    // 재사용). 홀드가 끝나면 CompleteHoldInteract()가 대상이 ADoorActor일 때 일반 OnInteract 대신
    // ServerPlayKickDoorMontage()를 호출해서 킥 몽타주를 재생하고, 실제 파괴는 몽타주의 AnimNotify
    // (NotifyKickDoorHit)가 트리거함(여기서 바로 ServerBreakDoor를 부르지 않음). 홀드 도중 키를 떼면
    // CancelHoldInteract()가 그대로 취소함(문 파괴 자체가 시작되지 않음).
    // 문이 아니면 캐비닛 등 기존과 동일한 공용 처리로 이어짐 - 홀드 중에는 bIsInteracting=true가 되어
    // CanMove()가 이동을 막는다

    // 잡기 중 / 킥 몽타주 재생 중이면 막음
    virtual bool CanStartInteract() const override;

    // 문 우선: 정면 박스 안의 문 -> 없으면 부모 기본(정면 박스 안의 IInteractable)
    virtual AActor* FindInteractableTarget() const override;

    // 모든 대상 CanInteract 체크 (+ 디버그 로그) - 이미 열린 문 등에 게이지/몽타주가 헛되이 시작되지 않게
    virtual bool ShouldStartInteract(AActor* Target) override;

    // 문이면 PendingKickDoorTarget 기억 (킥 AnimNotify가 나중에 꺼내 씀)
    virtual void OnInteractStarted(AActor* Target, float HoldTime) override;

    // 홀드 0초: 문이면 킥 몽타주, 아니면 부모 기본(ServerInteractWithTarget)
    virtual void HandleInstantInteract(AActor* Target) override;

    // 홀드 취소: 킥할 문 기억 해제 (문 파괴로 이어지면 안 됨)
    virtual void OnHoldInteractCanceled() override;

    // 홀드 완료: 문이면 킥 몽타주, 아니면 부모 기본(ServerInteractWithTarget)
    virtual void OnHoldInteractCompleted(AActor* Target) override;

    // [미사용] 예전 라인 트레이스 사거리. 지금은 정면 박스(InteractBoxForwardOffset/InteractBoxExtent)로 판정
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    float InteractRange = 200.f;

    // PendingHoldTarget / bIsInteracting / InteractHoldTimerHandle / ServerInteractWithTarget / ServerSetInteractHold는
    // 부모(AescaperoomCharacter)로 이동 (러너/술래 공통)

    // 부감 토글 입력 처리 (키를 누르는 순간에만 호출됨, 뗄 때는 아무 동작 없음)
    void ToggleOverheadView(const FInputActionValue& Value);

    UFUNCTION()
    void OnRep_IsOverheadViewActive();

    UFUNCTION()
    void OnRep_IsGrab();

    // 부감 사용 요청 (실제 권위는 서버가 가짐). 준비 상태일 때만 켜지고, 사용 중/쿨타임 중이면 무시
    UFUNCTION(Server, Reliable)
    void ServerRequestOverheadView();

    // 서버 전용: 지속시간이 끝나면 타이머가 호출 - 끄고 쿨타임 시작
    void EndOverheadView();
    FTimerHandle OverheadViewTimerHandle;

    // 사용 종료 / 쿨타임 종료 시각 (GameState 서버 시간 기준, 서버가 세팅하고 복제) - UI 계산용
    UPROPERTY(Replicated)
    float OverheadActiveEndServerTime = 0.f;

    UPROPERTY(Replicated)
    float OverheadCooldownEndServerTime = 0.f;

    // 서버/클라 공통 "서버 시간" (GameState::GetServerWorldTimeSeconds)
    float GetServerTimeSeconds() const;

    // 로컬(술래 본인): "목 늘어나는 중 (N)" 문구 갱신. 남은 초가 바뀔 때만 위젯에 다시 씀
    void UpdateOverheadPromptText();
    int32 LastShownOverheadSeconds = -1;
    void SetOverheadPromptText(bool bActive, const FText& Text = FText::GetEmpty());

    // 문 파괴를 서버에 요청. Interact()에서 카메라 라인 트레이스로 DoorActor를 찾으면 홀드/취소 없이 즉시 호출됨.
    // 서버는 클라이언트가 보낸 대상을 맹신하지 않고, 실제 파괴 가능 여부(닫혀있는지 등)는
    // DoorActor::Break() 내부에서 최종 검증함
    UFUNCTION(Server, Reliable)
    void ServerBreakDoor(ADoorActor* Door);

    // KickDoor 몽타주를 재생해달라고 서버에 알림. Interact() 대상이 ADoorActor일 때만 호출됨.
    // 실제 문 파괴는 여기서 하지 않음 - kickdoor 몽타주의 AnimNotify(NotifyKickDoorHit)가 대신 트리거함
    UFUNCTION(Server, Reliable)
    void ServerPlayKickDoorMontage();

    // 서버가 모든 클라이언트(술래 본인 포함)에 KickDoorMontage 재생을 지시함. PlayAnimMontage()는
    // 자동으로 복제되지 않으므로, ServerPlayKickDoorMontage()를 받은 서버가 이 Multicast RPC로
    // 각 클라이언트의 로컬 AnimInstance에서 몽타주를 재생시켜준다
    UFUNCTION(NetMulticast, Reliable)
    void MulticastPlayKickDoorMontage();

public:
    // ADoorActor::OnInteractHoldUpdateInternal(다른 클래스)이 술래의 홀드 시작(bHolding=true)
    // 신호를 받으면 호출해야 하므로 public이어야 함 - ARunnerCharacter::MulticastBeginLadderHold를
    // AExitActor가 외부에서 호출하는 것과 동일한 이유. Multicast를 써서 술래 본인을 포함한
    // 모든 클라이언트에서 동시에 충전 몽타주가 재생되게 함
    UFUNCTION(NetMulticast, Reliable)
    void MulticastBeginKickCharge();

    // 위 MulticastBeginKickCharge의 취소판. 홀드가 도중에 취소되면(OnInteractHoldUpdateInternal의
    // bHolding=false) 충전 몽타주를 멈춤. 홀드가 끝까지 채워져서 KickDoorMontage로 자연스럽게
    // 전환되는 정상 케이스에서는 호출되지 않음(같은 슬롯에 새 몽타주가 재생되며 자동 블렌드됨)
    UFUNCTION(NetMulticast, Reliable)
    void MulticastCancelKickCharge();

    // ACabinetActor가 서버에서 호출 (킥 충전 몽타주와 같은 방식 - Multicast라 본인 포함 모든 클라에 보임)
    // 열기 홀드 시작: CabinetOpenMontage 한 번 재생
    UFUNCTION(NetMulticast, Reliable)
    void MulticastBeginCabinetOpen();

    // 열기 홀드 취소 또는 완료: CabinetOpenMontage를 블렌드아웃으로 정지
    UFUNCTION(NetMulticast, Reliable)
    void MulticastStopCabinetOpen();

    // 끝까지 열었는데 비어있음(허탕): CabinetEmptyMontage 재생 (재생 중 이동/시점 회전 잠금)
    UFUNCTION(NetMulticast, Reliable)
    void MulticastPlayCabinetEmpty();

    // PerformGrabBoxCheck이 러너를 잡는 데 성공했을 때 호출. 술래 본인을 포함한 모든
    // 클라이언트에서 ChokeGrabMontage를 동시에 재생시킴 (KickDoorMontage와 동일한 이유로 Multicast 사용)
    UFUNCTION(NetMulticast, Reliable)
    void MulticastPlayChokeGrabMontage();

    // SnapForChokeGrab()이 호출: 러너의 MulticastBeginChoked 스냅과 같은 이유로 Multicast - 서버에서만
    // 옮기면 술래 본인 화면에서는 이동 보정이 올 때까지 한 박자 늦게 튀므로 모든 머신에서 동시에 옮김
    UFUNCTION(NetMulticast, Reliable)
    void MulticastSnapTransform(FVector Location, FRotator Rotation);

protected:
    // Grab 입력을 서버에 알림. 서버는 bIsGrab 펄스(잡기 애니메이션)를 시작하고,
    // 정면 박스 판정(PerformGrabBoxCheck, GrabHitDelay 후)으로 러너를 잡음.
    // 대상은 클라가 보내지 않고 서버가 직접 판정 (클라/서버 위치 차이로 인한 판정 어긋남 방지)
    UFUNCTION(Server, Reliable)
    void ServerStartGrabPulse();

    // bIsGrab을 true로 설정하고 서버 자신의 화면에도 즉시 반영한 뒤, GrabPulseDuration 후
    // 자동으로 다시 false로 되돌리는 타이머를 건다 (서버 권한 내부에서만 호출)
    void StartGrabPulse();

    // GrabPulseDuration 타이머 완료 시 호출: bIsGrab을 false로 되돌림
    void EndGrabPulse();
};
