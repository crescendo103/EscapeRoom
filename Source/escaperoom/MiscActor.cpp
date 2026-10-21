// MiscActor.cpp
#include "MiscActor.h"
#include "InteractionTextComponent.h"
#include "Components/SphereComponent.h"
#include "Components/MeshComponent.h"
#include "DungeonPlayerController.h"
#include "MatchWidget.h"

AMiscActor::AMiscActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	// 실수로 AI/플레이어가 빙의하지 않도록 - Door/Dice/Exit/Cabinet 전부 Possess 대상이 아님
	// (Cabinet만 예외적으로 도망자가 숨을 때 서버 코드에서 직접 Possess시킴 - AutoPossess와는 별개)
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
	AutoPossessAI = EAutoPossessAI::Disabled;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	InteractionRange = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionRange"));
	InteractionRange->SetupAttachment(Root);
	InteractionRange->InitSphereRadius(150.f); // 에디터에서 컴포넌트별로 조정 가능
	InteractionRange->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionRange->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionRange->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	InteractionRange->SetGenerateOverlapEvents(true);

	// 순수 데이터 컴포넌트라 Attach 불필요 (월드에 배치되는 게 아니라 텍스트만 들고 있음)
	InteractionText = CreateDefaultSubobject<UInteractionTextComponent>(TEXT("InteractionText"));
}

void AMiscActor::BeginPlay()
{
	Super::BeginPlay();

	// 데디케이티드 서버는 렌더링도 없고 IsLocallyControlled()가 항상 false라 이 로직이 아무 의미가
	// 없음 - 완전히 클라이언트 개인 화면 전용이라 서버에서는 오버랩 바인딩 자체를 하지 않는다
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (InteractionRange)
	{
		InteractionRange->OnComponentBeginOverlap.AddDynamic(this, &AMiscActor::OnRangeBeginOverlap);
		InteractionRange->OnComponentEndOverlap.AddDynamic(this, &AMiscActor::OnRangeEndOverlap);
	}
}

bool AMiscActor::CanInteract_Implementation(APawn* Interactor)
{
	// [TEMP DEBUG] 이 로그가 안 찍히면 블루프린트 쪽에서 CanInteract 인터페이스 이벤트를
	// 오버라이드해서 C++ 구현 자체가 호출 안 되고 있다는 뜻 - 원인 찾으면 지워도 됨
	const bool bResult = CanInteractInternal(Interactor);
	UE_LOG(LogTemp, Warning, TEXT("[Interact][MiscActor] %s::CanInteract_Implementation called -> %d"), *GetNameSafe(this), bResult ? 1 : 0);
	return bResult;
}

void AMiscActor::OnInteract_Implementation(APawn* Interactor)
{
	// 실제 상태 변경은 서버 권한에서만. 호출부(캐릭터)는 서버 RPC를 거쳐서 여기 도달하지만,
	// Interactable.h 규약대로 스스로도 한 번 더 방어함
	if (!HasAuthority())
	{
		return;
	}

	if (!CanInteractInternal(Interactor))
	{
		return;
	}

	OnInteractInternal(Interactor);
}

float AMiscActor::GetInteractHoldTime_Implementation(APawn* Interactor)
{
	return InteractHoldTime;
}

void AMiscActor::OnInteractHoldUpdate_Implementation(APawn* Interactor, bool bHolding)
{
	// 서버 권한에서만 처리. 서브클래스(Door/Exit)의 OnInteractHoldUpdateInternal은 이 체크를 전제로 작성됨
	if (!HasAuthority())
	{
		return;
	}

	OnInteractHoldUpdateInternal(Interactor, bHolding);
}

FText AMiscActor::GetInteractionHoldText_Implementation(APawn* Interactor)
{
	if (!InteractionText)
	{
		return FText::GetEmpty();
	}

	return (Interactor && IsChaser(Interactor)) ? InteractionText->ChaserHoldText : InteractionText->RunnerHoldText;
}

void AMiscActor::OnRangeBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// 서버가 판정해서 복제해주는 게 아니라, "이 코드가 실행되는 화면"의 로컬 폰이 직접 겹쳤을
	// 때만 반응한다 - 다른(원격) 플레이어가 겹쳐도 IsLocallyControlled()가 false라 무시됨
	APawn* OtherPawn = Cast<APawn>(OtherActor);
	if (!OtherPawn || !OtherPawn->IsLocallyControlled() || !CanInteractInternal(OtherPawn))
	{
		return;
	}

	if (!bLocalPawnInRange)
	{
		bLocalPawnInRange = true;
		SetLocalMatchWidgetPromptTextActive(OtherPawn, true);
		SetHighlightActive(true);
	}
}

void AMiscActor::OnRangeEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	APawn* OtherPawn = Cast<APawn>(OtherActor);
	if (!OtherPawn || !OtherPawn->IsLocallyControlled())
	{
		return;
	}

	if (bLocalPawnInRange)
	{
		bLocalPawnInRange = false;
		SetLocalMatchWidgetPromptTextActive(OtherPawn, false);
		SetHighlightActive(false);
	}
}

void AMiscActor::SetLocalMatchWidgetPromptTextActive(const APawn* LocalPawn, bool bActive) const
{
	if (!LocalPawn)
	{
		return;
	}

	if (const ADungeonPlayerController* PC = Cast<ADungeonPlayerController>(LocalPawn->GetController()))
	{
		if (UMatchWidget* Widget = PC->GetMatchWidget())
		{
			FText PromptText = FText::GetEmpty();
			if (bActive && InteractionText)
			{
				PromptText = IsChaser(LocalPawn) ? InteractionText->ChaserPromptText : InteractionText->RunnerPromptText;
			}
			Widget->SetPromptTextActive(bActive, PromptText);
		}
	}
}

void AMiscActor::SetHighlightActive(bool bActive) const
{
	if (!HighlightMaterial || !Root)
	{
		return;
	}

	// AActor에 이미 Children(자식 액터 목록)이라는 멤버가 있어서 이름이 겹치면 C4458(섀도잉) 에러가
	// 나므로, 컴포넌트 목록에는 ChildComponents처럼 겹치지 않는 이름을 씀
	TArray<USceneComponent*> ChildComponents;
	Root->GetChildrenComponents(true, ChildComponents);

	for (USceneComponent* ChildComponent : ChildComponents)
	{
		if (UMeshComponent* Mesh = Cast<UMeshComponent>(ChildComponent))
		{
			Mesh->SetOverlayMaterial(bActive ? HighlightMaterial : nullptr);
		}
	}
}
