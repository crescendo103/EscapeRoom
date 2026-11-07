// MatchIntroComponent.cpp

#include "MatchIntroComponent.h"
#include "escaperoomCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Components/TextBlock.h"
#include "Blueprint/UserWidget.h"
#include "Animation/WidgetAnimation.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

UMatchIntroComponent::UMatchIntroComponent()
{
	// 연출 중에만 Tick (Play에서 켜고 Finish에서 끔)
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics; // 캐릭터 위치가 확정된 뒤 카메라를 놓음
}

void UMatchIntroComponent::Play(FSimpleDelegate OnFinished)
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	APlayerController* PC = OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;

	// 연출을 못 하는 상황이면 바로 완료 처리 (호출 쪽은 콜백만 기다리면 됨)
	if (bPlaying || !bEnableIntro || !OwnerPawn || !PC || !PC->IsLocalController())
	{
		OnFinished.ExecuteIfBound();
		return;
	}

	OnFinishedDelegate = MoveTemp(OnFinished);
	bPlaying = true;
	bReturnStarted = false;
	Elapsed = 0.f;
	OrbitBaseYaw = OwnerPawn->GetActorRotation().Yaw;

	BeginIntroCamera();
	ApplyIntroMeshVisibility();
	ShowIntroWidget(PC);

	if (IntroSound)
	{
		IntroAudio = UGameplayStatics::SpawnSound2D(this, IntroSound);
	}

	// 첫 프레임부터 올바른 위치에서 보이도록 바로 한 번 배치
	if (IntroCamera)
	{
		IntroCamera->SetWorldTransform(ComputeOrbitTransform(0.f));
	}

	SetComponentTickEnabled(true);
	UE_LOG(LogTemp, Log, TEXT("[Intro] Play on %s (%.1fs)"), *GetNameSafe(OwnerPawn), Duration);
}

void UMatchIntroComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bPlaying || !IntroCamera)
	{
		return;
	}

	Elapsed += DeltaTime;

	const float SafeReturn = FMath::Clamp(ReturnDuration, 0.f, Duration);
	const float OrbitTime = FMath::Max(Duration - SafeReturn, KINDA_SMALL_NUMBER);

	if (Elapsed < OrbitTime)
	{
		// 1) 회전 구간: 천천히 출발해서 천천히 멈추는 곡선
		const float OrbitAlpha = FMath::InterpEaseInOut(0.f, 1.f, Elapsed / OrbitTime, 2.f);
		IntroCamera->SetWorldTransform(ComputeOrbitTransform(OrbitAlpha));
		return;
	}

	// 2) 1인칭 복귀 구간: 지금 카메라 위치에서 1인칭 카메라 위치/방향으로 보간
	if (!bReturnStarted)
	{
		bReturnStarted = true;
		ReturnStartTransform = IntroCamera->GetComponentTransform();

		// 문구도 같이 사라지게
		if (IntroWidget && IntroFadeAnimation)
		{
			IntroWidget->PlayAnimationReverse(IntroFadeAnimation);
		}
	}

	const float ReturnAlpha = SafeReturn > KINDA_SMALL_NUMBER
		? FMath::Clamp((Elapsed - OrbitTime) / SafeReturn, 0.f, 1.f)
		: 1.f;
	const float Smooth = FMath::InterpEaseInOut(0.f, 1.f, ReturnAlpha, 2.f);

	const FTransform Target = ComputeFirstPersonTransform();
	const FVector Location = FMath::Lerp(ReturnStartTransform.GetLocation(), Target.GetLocation(), Smooth);
	const FQuat Rotation = FQuat::Slerp(ReturnStartTransform.GetRotation(), Target.GetRotation(), Smooth);
	IntroCamera->SetWorldLocationAndRotation(Location, Rotation);

	if (ReturnAlpha >= 1.f)
	{
		Finish();
	}
}

FTransform UMatchIntroComponent::ComputeOrbitTransform(float OrbitAlpha) const
{
	const FVector Pivot = GetComponentLocation();

	// 카메라가 바라보는 방향(Pitch, Yaw). 카메라는 그 반대쪽으로 ArmLength만큼 떨어져 있음
	const FRotator ViewRotation(Pitch, OrbitBaseYaw + StartYawOffset + OrbitDegrees * OrbitAlpha, 0.f);
	const FVector Desired = Pivot - ViewRotation.Vector() * ArmLength;

	// 벽 충돌 검사: 중심점 -> 원하는 위치로 구체를 쓸어서 막히면 벽 앞까지만 (스프링암과 같은 원리)
	FVector CameraLocation = Desired;
	if (const UWorld* World = GetWorld())
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MatchIntroCamera), false, GetOwner());
		FHitResult Hit;
		if (World->SweepSingleByChannel(Hit, Pivot, Desired, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(ProbeRadius), Params))
		{
			CameraLocation = Hit.Location;
		}
	}

	// 항상 중심점을 바라보게 (너무 가까워져 방향을 못 구하면 원래 방향 유지)
	const FVector ToPivot = Pivot - CameraLocation;
	const FRotator LookRotation = ToPivot.SizeSquared() > 1.f ? ToPivot.Rotation() : ViewRotation;
	return FTransform(LookRotation, CameraLocation);
}

FTransform UMatchIntroComponent::ComputeFirstPersonTransform() const
{
	UCameraComponent* FirstPersonCamera = nullptr;
	if (const AescaperoomCharacter* Character = Cast<AescaperoomCharacter>(GetOwner()))
	{
		FirstPersonCamera = Character->GetFirstPersonCameraComponent();
	}
	if (!FirstPersonCamera && DisabledCameras.Num() > 0)
	{
		FirstPersonCamera = DisabledCameras[0].Get();
	}

	if (!FirstPersonCamera)
	{
		return IntroCamera ? IntroCamera->GetComponentTransform() : GetComponentTransform();
	}

	// 1인칭 카메라는 꺼져 있는 동안 컨트롤 회전이 반영되지 않으므로, 회전은 컨트롤러 기준으로 직접 구함
	FRotator TargetRotation = FirstPersonCamera->GetComponentRotation();
	if (FirstPersonCamera->bUsePawnControlRotation)
	{
		if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()))
		{
			TargetRotation = OwnerPawn->GetControlRotation();
		}
	}
	return FTransform(TargetRotation, FirstPersonCamera->GetComponentLocation());
}

void UMatchIntroComponent::BeginIntroCamera()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// 지금 켜져 있는 카메라(1인칭 등)를 모두 끔 -> 엔진은 뷰 타깃의 "켜진 첫 카메라"로 화면을 그림
	DisabledCameras.Reset();
	float FieldOfView = 90.f;
	TInlineComponentArray<UCameraComponent*> Cameras(Owner);
	for (UCameraComponent* Camera : Cameras)
	{
		if (Camera && Camera->IsActive())
		{
			if (DisabledCameras.Num() == 0)
			{
				FieldOfView = Camera->FieldOfView;
			}
			DisabledCameras.Add(Camera);
			Camera->Deactivate();
		}
	}

	// 연출용 카메라: 부모를 따라 움직이지 않게 월드 좌표로 직접 배치
	IntroCamera = NewObject<UCameraComponent>(Owner, UCameraComponent::StaticClass(),
		MakeUniqueObjectName(Owner, UCameraComponent::StaticClass(), TEXT("MatchIntroCamera")), RF_Transient);
	IntroCamera->SetupAttachment(this);
	IntroCamera->SetUsingAbsoluteLocation(true);
	IntroCamera->SetUsingAbsoluteRotation(true);
	IntroCamera->SetUsingAbsoluteScale(true);
	IntroCamera->bUsePawnControlRotation = false;
	IntroCamera->SetFieldOfView(FieldOfView);
	IntroCamera->RegisterComponent();
	IntroCamera->Activate(true);
}

void UMatchIntroComponent::EndIntroCamera()
{
	if (IntroCamera)
	{
		IntroCamera->Deactivate();
		IntroCamera->DestroyComponent();
		IntroCamera = nullptr;
	}

	for (const TWeakObjectPtr<UCameraComponent>& Camera : DisabledCameras)
	{
		if (Camera.IsValid())
		{
			Camera->Activate(true);
		}
	}
	DisabledCameras.Reset();
}

void UMatchIntroComponent::ApplyIntroMeshVisibility()
{
	SavedMeshStates.Reset();

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	TInlineComponentArray<USkeletalMeshComponent*> Meshes(Owner);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (!Mesh)
		{
			continue;
		}

		FSavedMeshState& State = SavedMeshStates.AddDefaulted_GetRef();
		State.Mesh = Mesh;
		State.bOwnerNoSee = Mesh->bOwnerNoSee;
		State.bVisible = Mesh->IsVisible();

		if (Mesh->bOnlyOwnerSee)
		{
			// 1인칭 전용 팔 메시 -> 3인칭으로 보는 동안 숨김
			Mesh->SetVisibility(false, true);
		}
		else if (Mesh->bOwnerNoSee)
		{
			// 평소 내 화면에선 안 보이던 전신 메시 -> 연출 동안 보이게
			Mesh->SetOwnerNoSee(false);
		}
	}
}

void UMatchIntroComponent::RestoreMeshVisibility()
{
	for (const FSavedMeshState& State : SavedMeshStates)
	{
		if (USkeletalMeshComponent* Mesh = State.Mesh.Get())
		{
			Mesh->SetOwnerNoSee(State.bOwnerNoSee);
			// 인트로에서 숨긴 1인칭 팔 메시만 복구. 전신 메시까지 자식 전파로 복구하면
			// 소켓에 붙은 SpotLight(러너 전용 조명)도 술래 화면에서 다시 켜짐
			if (Mesh->bOnlyOwnerSee)
			{
				Mesh->SetVisibility(State.bVisible, true);
			}
		}
	}
	SavedMeshStates.Reset();
}

void UMatchIntroComponent::ShowIntroWidget(APlayerController* PC)
{
	if (!IntroWidgetClass || !PC)
	{
		return;
	}

	if (!PC->GetLocalPlayer())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Intro] %s: LocalPlayer 없음 - 인트로 위젯 생성 건너뜀"), *PC->GetName());
		return;
	}

	IntroWidget = CreateWidget<UUserWidget>(PC, IntroWidgetClass);
	if (!IntroWidget)
	{
		return;
	}
	IntroWidget->AddToViewport(50);

	if (!IntroText.IsEmpty())
	{
		if (UTextBlock* TextBlock = Cast<UTextBlock>(IntroWidget->GetWidgetFromName(TextWidgetName)))
		{
			TextBlock->SetText(IntroText);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[Intro] %s has no TextBlock named '%s'"), *GetNameSafe(IntroWidgetClass), *TextWidgetName.ToString());
		}
	}

	// 위젯 블루프린트의 애니메이션은 같은 이름의 UWidgetAnimation 변수로 노출되므로 이름으로 찾아서 재생
	IntroFadeAnimation = nullptr;
	if (!FadeAnimationName.IsNone())
	{
		if (const FObjectProperty* AnimProperty = FindFProperty<FObjectProperty>(IntroWidget->GetClass(), FadeAnimationName))
		{
			IntroFadeAnimation = Cast<UWidgetAnimation>(AnimProperty->GetObjectPropertyValue_InContainer(IntroWidget));
		}
	}
	if (IntroFadeAnimation)
	{
		IntroWidget->PlayAnimation(IntroFadeAnimation);
	}
}

void UMatchIntroComponent::RemoveIntroWidget()
{
	if (IntroWidget)
	{
		IntroWidget->RemoveFromParent();
		IntroWidget = nullptr;
	}
	IntroFadeAnimation = nullptr;
}

void UMatchIntroComponent::Finish()
{
	if (!bPlaying)
	{
		return;
	}
	bPlaying = false;
	SetComponentTickEnabled(false);

	EndIntroCamera();
	RestoreMeshVisibility();
	RemoveIntroWidget();
	IntroAudio = nullptr; // 사운드는 끝까지 재생되게 둠 (2D 사운드는 재생이 끝나면 알아서 정리됨)

	UE_LOG(LogTemp, Log, TEXT("[Intro] Finished on %s"), *GetNameSafe(GetOwner()));

	// 콜백 안에서 이 컴포넌트를 다시 건드려도 안전하도록 꺼낸 뒤 호출
	FSimpleDelegate Callback = MoveTemp(OnFinishedDelegate);
	OnFinishedDelegate.Unbind();
	Callback.ExecuteIfBound();
}

void UMatchIntroComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 연출 도중 캐릭터가 사라짐 -> 사운드 끊고 원상복구, 컨트롤러가 기다리지 않게 완료 콜백은 그대로 호출
	if (bPlaying)
	{
		if (IntroAudio)
		{
			IntroAudio->Stop();
		}
		Finish();
	}

	Super::EndPlay(EndPlayReason);
}
