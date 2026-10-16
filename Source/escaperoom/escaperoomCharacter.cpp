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
