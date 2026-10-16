// DiceActor.cpp
#include "DiceActor.h"
#include "Components/StaticMeshComponent.h"
#include "NiagaraComponent.h"
#include "Net/UnrealNetwork.h"
#include "EscapeRoomGameState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

ADiceActor::ADiceActor()
{
	PrimaryActorTick.bCanEverTick = true; // 위아래 흔들림 애니메이션에 필요
	bReplicates = true;

	DiceMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DiceMesh"));
	DiceMesh->SetupAttachment(Root);
	// 상호작용 라인 트레이스(RunnerCharacter::FindInteractableTarget)가 맞을 수 있도록
	// 콜리전은 기본값(Mesh 임포트 설정)을 그대로 사용

	AuraComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("AuraComponent"));
	AuraComponent->SetupAttachment(Root);
	AuraComponent->bAutoActivate = false; // ApplyVisibility에서 상태에 맞춰 재생/정지 제어

	// 주사위가 플레이어 이동을 막지 않도록 Pawn 충돌은 항상 무시.
	// 상호작용은 Pawn 채널이 아닌 라인 트레이스로 판정하므로 영향 없음.
	// (술래 화면에서만 무시하면 서버 쪽에서는 여전히 막혀서 술래 위치가 보정되며 덜덜 떨릴 수 있으므로 전원 공통으로 처리)
	DiceMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
}

void ADiceActor::BeginPlay()
{
	Super::BeginPlay();

	InitialMeshRelativeLocation = DiceMesh->GetRelativeLocation();

	if (AuraEffect)
	{
		AuraComponent->SetAsset(AuraEffect);
	}

	// 시작 상태(기본 표시) 반영
	ApplyVisibility();
}

void ADiceActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bLocalViewerResolved)
	{
		TryResolveLocalViewer();
	}

	if (!bIsVisible || bHiddenForLocalChaser)
	{
		return; // 숨긴 상태면 흔들림 계산도 생략
	}

	// 순수 연출용 흔들림 - 복제하지 않고 서버/클라 각자 로컬에서 동일한 사인파를 계산
	RunningTime += DeltaTime;
	const float ZOffset = FMath::Sin(RunningTime * BobSpeed) * BobHeight;
	DiceMesh->SetRelativeLocation(InitialMeshRelativeLocation + FVector(0.f, 0.f, ZOffset));
}

void ADiceActor::OnInteractInternal(APawn* Interactor)
{
	// 호출 시점엔 이미 MiscActor::OnInteract_Implementation에서 HasAuthority() 확인이 끝난 뒤임

	// 이제 토글이 아니라 "먹으면 끝" - GameState에 1개 보고하고(단계 클리어 판정은 GameState가 처리)
	// 액터 자체를 제거해서 다시 못 먹게 함
	if (AEscapeRoomGameState* GS = GetWorld()->GetGameState<AEscapeRoomGameState>())
	{
		GS->AddDice(1);
	}
	Destroy();
}

void ADiceActor::OnRep_IsVisible()
{
	ApplyVisibility();
}

void ADiceActor::TryResolveLocalViewer()
{
	// 각 컴퓨터에서 "그 컴퓨터의" 플레이어 컨트롤러를 돌려줌 -> 같은 주사위라도 머신마다 판정이 다름
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->IsLocalController())
	{
		return; // 데디케이티드 서버처럼 화면이 없는 경우는 판정 불필요
	}

	APawn* LocalPawn = PC->GetPawn();
	if (!LocalPawn)
	{
		return; // 아직 폰이 연결(Possess) 안 됨 -> 다음 Tick에 다시 시도
	}

	bLocalViewerResolved = true;
	bHiddenForLocalChaser = IsChaser(LocalPawn); // MiscActor의 Chaser 태그 판정 재사용

	if (bHiddenForLocalChaser)
	{
		ApplyVisibility(); // 숨김 상태로 다시 반영
	}
}

void ADiceActor::ApplyVisibility()
{
	// 실제로 보여줄지 = 서버 상태(bIsVisible) AND 이 화면의 주인이 술래가 아님
	const bool bShow = bIsVisible && !bHiddenForLocalChaser;

	SetActorEnableCollision(bIsVisible); // 상호작용 판정용 콜리전은 서버 상태 기준 유지
	DiceMesh->SetVisibility(bShow, true);

	if (bShow && AuraEffect)
	{
		AuraComponent->Activate(true);
	}
	else
	{
		AuraComponent->Deactivate();
	}
}

void ADiceActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ADiceActor, bIsVisible);
}
