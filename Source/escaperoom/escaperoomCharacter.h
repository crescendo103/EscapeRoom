// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "escaperoomCharacter.generated.h"

class UInputComponent;
class USkeletalMeshComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UAudioComponent;
class UEnhancedInputLocalPlayerSubsystem;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

UCLASS(config=Game)
class AescaperoomCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Pawn mesh: 1st person view (arms; seen only by self) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Mesh, meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* Mesh1P;

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	UInputAction* MoveAction;
	
public:
	AescaperoomCharacter();

protected:
	virtual void BeginPlay();

	// ---- 입력 매핑 (리슨서버 대응) ----
	// [리슨] 예전엔 BeginPlay에서 GetController()로 IMC를 추가했는데, 리슨서버 호스트(술래)는
	// 서버에서 스폰 -> BeginPlay(이때 컨트롤러 없음) -> 빙의 순서라 IMC가 안 들어가서 키가 전혀 안 먹었음.
	// 빙의가 확정되는 NotifyControllerChanged(서버 PossessedBy + 클라 OnRep_Controller 양쪽에서 호출)에서 추가함
	virtual void NotifyControllerChanged() override;

	// 로컬 플레이어에게 이 캐릭터용 IMC들을 추가. 자식(술래/러너)은 Super 호출 후 자기 IMC를 추가로 쌓음
	virtual void AddLocalInputMappings(UEnhancedInputLocalPlayerSubsystem* Subsystem);

	// 공통(이동/시점/점프) Input Mapping Context - 우선순위 0으로 추가.
	// 기본값은 FirstPerson 템플릿의 IMC_Default. 경로가 다르면 캐릭터 BP 디테일에서 지정할 것
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputMappingContext> CommonMappingContext =
		TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(TEXT("/Game/FirstPerson/Input/IMC_Default.IMC_Default")));

public:
	virtual void Tick(float DeltaSeconds) override;

	// ---- 이동 중 숨소리 (러너/술래 공통) ----
	// 루프 숨소리. 이동 속도가 BreathMinSpeed 이상이면 페이드인, 멈추면 페이드아웃.
	// 각 머신이 복제된 Velocity를 보고 로컬로 판단 (RPC/복제 없음), 데디서버에서는 재생 안 함.
	// BP 디테일에서 Sound(루프 체크된 숨소리)와 Attenuation Settings(다른 사람에게 들릴 거리)를 지정할 것
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio|Breath")
	TObjectPtr<UAudioComponent> BreathAudio;

	// 이 속도(cm/s, 수평) 이상으로 움직일 때 숨소리 재생
	UPROPERTY(EditDefaultsOnly, Category = "Audio|Breath", meta = (ClampMin = "0.0"))
	float BreathMinSpeed = 50.f;

	// 계속 움직인 지 이 시간(초)이 지나야 숨소리 시작. 중간에 멈추면 다시 0부터 셈
	UPROPERTY(EditDefaultsOnly, Category = "Audio|Breath", meta = (ClampMin = "0.0"))
	float BreathStartDelay = 1.f;

	UPROPERTY(EditDefaultsOnly, Category = "Audio|Breath", meta = (ClampMin = "0.0"))
	float BreathFadeInTime = 0.3f;

	UPROPERTY(EditDefaultsOnly, Category = "Audio|Breath", meta = (ClampMin = "0.0"))
	float BreathFadeOutTime = 0.6f;

protected:
	// 매 프레임 이동 여부를 보고 BreathAudio를 켜고 끔
	void UpdateBreathAudio(float DeltaSeconds);

	// 연속으로 움직인 시간(초). 멈추면 0으로 리셋
	float BreathMovingTime = 0.f;

	// FadeOut 중에도 IsPlaying()이 true라서, 켜/끔 "의도" 상태를 따로 기억
	bool bBreathOn = false;

public:
		
	/** Look Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* LookAction;

protected:
	/** Called for movement input. 자식 클래스(예: 이동 시 이모트를 취소해야 하는 러너)에서 오버라이드할 수 있도록 virtual */
	virtual void Move(const FInputActionValue& Value);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

	/** 이동 입력을 실제로 적용해도 되는지 여부. 상호작용/이모트 중에는 자식 클래스에서 false를 반환하도록 오버라이드해서 이동을 막을 수 있음 */
	virtual bool CanMove() const { return true; }

	/** 시점 회전(마우스 Look) 입력을 적용해도 되는지 여부. CanMove()와 같은 패턴 - 연출 중 캐릭터/카메라 방향을
	 * 고정해야 할 때 자식 클래스에서 false를 반환하도록 오버라이드 (예: 술래 목조르기 몽타주 재생 중) */
	virtual bool CanLook() const { return true; }

	/** 홀드 상호작용 시작/취소/완료 시점에 자식 클래스(러너/술래)가 호출: 그동안만 MatchWidget의 홀드 게이지 Tick을 켜고 끔.
	 * HoldText를 같이 넘기면 게이지 옆에 "캐비닛 열기"처럼 무엇을 하는 중인지 텍스트로도 보여줌(비워두면 텍스트 없이 게이지만).
	 * (Controller가 ADungeonPlayerController가 아니거나 MatchWidget을 아직 못 찾았으면 무시) */
	void SetMatchWidgetHoldGaugeActive(bool bActive, const FText& HoldText = FText::GetEmpty()) const;

	/** Move()에서 매 프레임 저장되는 최근 이동 입력값 (X = 좌우, Y = 전후). 자식 클래스에서 이동 방향 판단용으로 사용 */
	UPROPERTY(BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	FVector2D LastMoveInput = FVector2D::ZeroVector;

protected:
	// APawn interface
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;
	// End of APawn interface

public:
	/** Returns Mesh1P subobject **/
	USkeletalMeshComponent* GetMesh1P() const { return Mesh1P; }
	/** Returns FirstPersonCameraComponent subobject **/
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }

	/** 홀드가 필요한 상호작용 진행률(0~1). 홀드 중이 아니면 0을 반환. 자식 클래스(러너/술래)가 각자의 로컬 홀드 타이머(InteractHoldTimerHandle)
	 * 기준으로 오버라이드함 - MatchWidget(다른 클래스)이 매 프레임 이 값을 읽어서 홀드 게이지 바를 갱신하므로 public이어야 함 */
	UFUNCTION(BlueprintPure, Category = "Interaction")
	virtual float GetInteractHoldPercent() const { return 0.f; }

	// ---- 상호작용 대상 판정: 몸 정면 박스 (러너/술래 공통) ----
	// 예전엔 카메라 정면 라인 트레이스였음. 지금은 술래 잡기(PerformGrabBoxCheck)처럼 몸 정면에 순간 박스를 만들어서
	// 겹친 IInteractable 중 가장 가까운 것을 고름. 박스 회전은 몸 Yaw만 따라감(시점 상하 무관)
protected:
	// 캐릭터 위치(캡슐 중심)에서 박스 중심까지 정면 거리 (cm)
	UPROPERTY(EditDefaultsOnly, Category = "Interaction|Box")
	float InteractBoxForwardOffset = 100.f;

	// 박스 반크기 (X=앞뒤, Y=좌우, Z=위아래, cm). 기본값 = 몸 앞 40~160cm, 바닥~머리 높이
	UPROPERTY(EditDefaultsOnly, Category = "Interaction|Box")
	FVector InteractBoxExtent = FVector(60.f, 50.f, 96.f);

	// 켜면 상호작용 키를 누를 때 박스를 화면에 2초간 그림 (초록 = 대상 찾음, 빨강 = 없음). 크기 맞출 때 사용
	UPROPERTY(EditDefaultsOnly, Category = "Interaction|Box")
	bool bDebugDrawInteractBox = false;

	// 박스 안의 IInteractable 액터 중 캐릭터와 가장 가까운 것 (없으면 nullptr).
	// RequiredClass를 주면 그 클래스(자식 포함)만 대상으로 함 (예: 술래의 문 우선 탐색)
	// 벽 너머 대상은 눈 위치에서 대상까지 가려지는지 한 번 더 확인해서 제외함
	AActor* FindInteractableInFrontBox(TSubclassOf<AActor> RequiredClass = nullptr) const;

	void GetInteractBox(FVector& OutCenter, FQuat& OutRotation) const;
};

