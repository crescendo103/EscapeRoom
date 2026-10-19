// Copyright Epic Games, Inc. All Rights Reserved.

#include "escaperoomCharacter.h"
#include "escaperoomProjectile.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"
#include "DungeonPlayerController.h"
#include "MatchWidget.h"
#include "Components/AudioComponent.h"
#include "Interactable.h"
#include "Engine/OverlapResult.h"
#include "DrawDebugHelpers.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

//////////////////////////////////////////////////////////////////////////
// AescaperoomCharacter

AescaperoomCharacter::AescaperoomCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);
		
	// Create a CameraComponent	
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCameraComponent->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraComponent->SetRelativeLocation(FVector(-10.f, 0.f, 60.f)); // Position the camera
	FirstPersonCameraComponent->bUsePawnControlRotation = true;

	// Create a mesh component that will be used when being viewed from a '1st person' view (when controlling this pawn)
	Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh1P"));
	Mesh1P->SetOnlyOwnerSee(true);
	Mesh1P->SetupAttachment(FirstPersonCameraComponent);
	Mesh1P->bCastDynamicShadow = false;
	Mesh1P->CastShadow = false;
	//Mesh1P->SetRelativeRotation(FRotator(0.9f, -19.19f, 5.2f));
	Mesh1P->SetRelativeLocation(FVector(-30.f, 0.f, -150.f));

	// 이동 중 숨소리 - 자동 재생하지 않고 Tick의 UpdateBreathAudio()가 켜고 끔
	BreathAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("BreathAudio"));
	BreathAudio->SetupAttachment(GetCapsuleComponent());
	BreathAudio->SetRelativeLocation(FVector(0.f, 0.f, 60.f)); // 대략 머리 높이
	BreathAudio->bAutoActivate = false;
}

void AescaperoomCharacter::BeginPlay()
{
	// Call the base class  
	Super::BeginPlay();
}

void AescaperoomCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateBreathAudio(DeltaSeconds);
}

void AescaperoomCharacter::UpdateBreathAudio(float DeltaSeconds)
{
	if (!BreathAudio || !BreathAudio->Sound || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// 숨겨진 폰(탈출 후 대기 등)은 소리도 끔
	const bool bMoving = !IsHidden() && GetVelocity().Size2D() >= BreathMinSpeed;

	// 움직이기 시작하고 BreathStartDelay만큼 지나야 숨소리 시작
	BreathMovingTime = bMoving ? BreathMovingTime + DeltaSeconds : 0.f;
	const bool bShouldBreathe = bMoving && BreathMovingTime >= BreathStartDelay;

	if (bShouldBreathe && !bBreathOn)
	{
		bBreathOn = true;
		BreathAudio->FadeIn(BreathFadeInTime);
	}
	else if (!bMoving && bBreathOn)
	{
		bBreathOn = false;
		BreathAudio->FadeOut(BreathFadeOutTime, 0.f);
	}
}

//////////////////////////////////////////////////////////////////////////// Input

void AescaperoomCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	// 이 머신에서 직접 조종하는 캐릭터일 때만 (리슨 호스트 본인 / 클라 본인)
	APlayerController* PC = Cast<APlayerController>(Controller);
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		AddLocalInputMappings(Subsystem);
	}
}

void AescaperoomCharacter::AddLocalInputMappings(UEnhancedInputLocalPlayerSubsystem* Subsystem)
{
	// 같은 IMC를 다시 추가해도 중복되지 않음 (BP에서 이미 추가했어도 안전)
	if (UInputMappingContext* Common = CommonMappingContext.LoadSynchronous())
	{
		Subsystem->AddMappingContext(Common, 0);
	}
	else
	{
		UE_LOG(LogTemplateCharacter, Warning, TEXT("[Input] %s: CommonMappingContext를 불러오지 못함 (%s) - 캐릭터 BP에서 IMC_Default 지정 필요"),
			*GetNameSafe(this), *CommonMappingContext.ToString());
	}
}

void AescaperoomCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{	
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AescaperoomCharacter::Move);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AescaperoomCharacter::Look);
	}
	else
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("'%s' Failed to find an Enhanced Input Component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}


void AescaperoomCharacter::Move(const FInputActionValue& Value)
{
	// 상호작용/이모트 등으로 이동이 막혀있으면 입력을 무시 (스프린트 오작동 방지를 위해 최근 입력값도 초기화)
	if (!CanMove())
	{
		LastMoveInput = FVector2D::ZeroVector;
		return;
	}

	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// 이동 방향 판단(스프린트 제한 등)에 쓸 수 있도록 최근 입력값 저장
	LastMoveInput = MovementVector;

	if (Controller != nullptr)
	{
		// add movement 
		AddMovementInput(GetActorForwardVector(), MovementVector.Y);
		AddMovementInput(GetActorRightVector(), MovementVector.X);
	}
}

void AescaperoomCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// 연출 중(CanLook() == false)에는 컨트롤러 회전 입력을 막아 캐릭터/카메라 방향을 고정
	if (!CanLook())
	{
		return;
	}

	if (Controller != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
	}
}

void AescaperoomCharacter::SetMatchWidgetHoldGaugeActive(bool bActive, const FText& HoldText) const
{
	if (ADungeonPlayerController* PC = Cast<ADungeonPlayerController>(GetController()))
	{
		if (UMatchWidget* Widget = PC->GetMatchWidget())
		{
			Widget->SetHoldGaugeActive(bActive, HoldText);
		}
	}
}
//////////////////////////////////////////////////////////////////////////
// 상호작용 대상 판정 (정면 박스)

void AescaperoomCharacter::GetInteractBox(FVector& OutCenter, FQuat& OutRotation) const
{
	const FRotator YawOnly(0.f, GetActorRotation().Yaw, 0.f);
	OutRotation = YawOnly.Quaternion();
	OutCenter = GetActorLocation() + YawOnly.Vector() * InteractBoxForwardOffset;
}

AActor* AescaperoomCharacter::FindInteractableInFrontBox(TSubclassOf<AActor> RequiredClass) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FVector Center; FQuat Rot;
	GetInteractBox(Center, Rot);

	// 예전 라인 트레이스와 같은 채널(Visibility)로 검사 -> 트레이스에 맞던 메시(주사위/탈출구/캐비닛/문)가 그대로 걸리고,
	// Pawn 전용 Overlap 범위(MiscActor InteractionRange, 감옥 트리거 등)는 안 걸림
	FCollisionQueryParams Params(SCENE_QUERY_STAT(InteractBox), false, this);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByChannel(Overlaps, Center, Rot, ECC_Visibility, FCollisionShape::MakeBox(InteractBoxExtent), Params);

	const FVector MyLoc = GetActorLocation();
	const FVector EyeLoc = GetPawnViewLocation();

	AActor* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();

	for (const FOverlapResult& Result : Overlaps)
	{
		AActor* Actor = Result.GetActor();
		UPrimitiveComponent* Comp = Result.GetComponent();
		if (!Actor || !Comp || Actor == this)
		{
			continue;
		}
		if (!Actor->GetClass()->ImplementsInterface(UInteractable::StaticClass()))
		{
			continue;
		}
		if (RequiredClass && !Actor->IsA(RequiredClass))
		{
			continue;
		}

		// 대상 표면에서 내 쪽으로 가장 가까운 점 (복잡한 콜리전이라 실패하면 컴포넌트 중심)
		FVector Point;
		if (Comp->GetClosestPointOnCollision(EyeLoc, Point) < 0.f)
		{
			Point = Comp->Bounds.Origin;
		}

		const float DistSq = FVector::DistSquared(MyLoc, Point);
		if (DistSq >= BestDistSq)
		{
			continue;
		}

		// 벽 너머 방지: 눈 -> 대상 사이를 다른 물체가 막고 있으면 제외 (대상 자신에 맞거나 아무것도 안 맞으면 OK)
		FHitResult Hit;
		FCollisionQueryParams LosParams(SCENE_QUERY_STAT(InteractBoxLOS), false, this);
		if (World->LineTraceSingleByChannel(Hit, EyeLoc, Point, ECC_Visibility, LosParams)
			&& Hit.GetActor() != Actor)
		{
			continue;
		}

		BestDistSq = DistSq;
		Best = Actor;
	}

	if (bDebugDrawInteractBox)
	{
		DrawDebugBox(World, Center, InteractBoxExtent, Rot, Best ? FColor::Green : FColor::Red, false, 2.f, 0, 1.5f);
	}

	return Best;
}

//////////////////////////////////////////////////////////////////////////
// 상호작용 공통 파이프라인 (러너/술래 공통)

AActor* AescaperoomCharacter::FindInteractableTarget() const
{
	return FindInteractableInFrontBox();
}

bool AescaperoomCharacter::ShouldStartInteract(AActor* Target)
{
	return IInteractable::Execute_CanInteract(Target, this);
}

void AescaperoomCharacter::Interact(const FInputActionValue& Value)
{
	if (!CanStartInteract())
	{
		return;
	}

	AActor* Target = FindInteractableTarget();
	if (!Target)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Interact][%s] No target found"), *GetNameSafe(this));
		return;
	}

	// 서버가 최종 검증하긴 하지만, 상호작용 불가능한 대상이면 여기서 걸러서
	// 헛되이 홀드 타이머/게이지/몽타주가 시작되지 않게 함
	if (!ShouldStartInteract(Target))
	{
		return;
	}

	const float HoldTime = IInteractable::Execute_GetInteractHoldTime(Target, this);
	const FText InteractHoldText = IInteractable::Execute_GetInteractionHoldText(Target, this);
	UE_LOG(LogTemp, Warning, TEXT("[Interact][%s] Target=%s HoldTime=%.2f"), *GetNameSafe(this), *GetNameSafe(Target), HoldTime);

	// 즉시/홀드 공통 시작 처리 (러너: 회전 잠금 + 몽타주 요청, 술래: 킥할 문 기억)
	OnInteractStarted(Target, HoldTime);

	if (HoldTime <= 0.f)
	{
		HandleInstantInteract(Target);
		return;
	}

	// 홀드가 필요한 상호작용: 로컬 타이머로 HoldTime을 직접 재고, 다 차면 CompleteHoldInteract()가 완료 처리.
	// 그동안 CanMove()는 bIsInteracting을 보고 이동을 막음
	PendingHoldTarget = Target;
	bIsInteracting = true;
	GetWorldTimerManager().SetTimer(InteractHoldTimerHandle, this, &AescaperoomCharacter::CompleteHoldInteract, HoldTime, false);

	// 홀드가 시작됐음을 서버에 알려서 미리보기 연출("여는 중"/"부수는 중" 등)을 시작시킴
	ServerSetInteractHold(Target, true);

	// 홀드 게이지 바 + "캐비닛 열기" 같은 텍스트 표시 (키를 떼거나 홀드가 끝나면 다시 끔)
	SetMatchWidgetHoldGaugeActive(true, InteractHoldText);
}

void AescaperoomCharacter::CancelHoldInteract(const FInputActionValue& Value)
{
	CancelHoldInteractInternal();
}

void AescaperoomCharacter::CancelHoldInteractInternal()
{
	if (!GetWorldTimerManager().IsTimerActive(InteractHoldTimerHandle))
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(InteractHoldTimerHandle);

	// 홀드가 취소됐음을 서버에 알려서 미리보기 연출을 원상복구시킴
	if (AActor* Target = PendingHoldTarget.Get())
	{
		ServerSetInteractHold(Target, false);
	}

	PendingHoldTarget = nullptr;
	bIsInteracting = false;
	SetMatchWidgetHoldGaugeActive(false);

	OnHoldInteractCanceled();
}

void AescaperoomCharacter::CompleteHoldInteract()
{
	// 게이지/상태를 먼저 정리한 다음 서버에 통지 (헤더 주석 참고 - 리슨 서버 Possess 교체 대비)
	AActor* Target = PendingHoldTarget.Get();

	PendingHoldTarget = nullptr;
	bIsInteracting = false;
	SetMatchWidgetHoldGaugeActive(false);

	if (Target)
	{
		OnHoldInteractCompleted(Target);
	}
}

void AescaperoomCharacter::HandleInstantInteract(AActor* Target)
{
	ServerInteractWithTarget(Target);
}

void AescaperoomCharacter::OnHoldInteractCompleted(AActor* Target)
{
	ServerInteractWithTarget(Target);
}

float AescaperoomCharacter::GetInteractHoldPercent() const
{
	// Interact()가 건 로컬 타이머(서버 응답을 기다리지 않는 클라이언트 예측)를 그대로 진행률 소스로 사용
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

void AescaperoomCharacter::ServerInteractWithTarget_Implementation(AActor* Target)
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

	IInteractable::Execute_OnInteract(Target, this);
}

void AescaperoomCharacter::ServerSetInteractHold_Implementation(AActor* Target, bool bHolding)
{
	if (!Target || !Target->GetClass()->ImplementsInterface(UInteractable::StaticClass()))
	{
		return;
	}

	// 홀드 "시작"할 때만 재검증. 취소(false)는 항상 통과시켜서 미리보기 연출이 확실히 원상복구되도록 함
	if (bHolding && !IInteractable::Execute_CanInteract(Target, this))
	{
		return;
	}

	IInteractable::Execute_OnInteractHoldUpdate(Target, this, bHolding);
}
