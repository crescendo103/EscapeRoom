#include "RoomActor.h"
#include "LampActor.h"
#include "Components/ChildActorComponent.h"
#include "Net/UnrealNetwork.h"

ARoomActor::ARoomActor()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	MeshRoot = CreateDefaultSubobject<USceneComponent>(TEXT("MeshRoot"));
	MeshRoot->SetupAttachment(Root);
}

void ARoomActor::SetLampSeed(int32 InSeed)
{
	LampSeed = InSeed;
	bHasLampSeed = true;
}

void ARoomActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARoomActor, LampSeed);
	DOREPLIFETIME(ARoomActor, bHasLampSeed);
}

void ARoomActor::BeginPlay()
{
	Super::BeginPlay();
	ApplyLampState();
}

void ARoomActor::OnRep_LampSeed()
{
	// 보통은 BeginPlay 전에 값이 와서 BeginPlay에서 처리되지만, 늦게 온 경우 대비
	if (HasActorBegunPlay())
	{
		ApplyLampState();
	}
}

void ARoomActor::ApplyLampState()
{
	if (bLampStateApplied || !bHasLampSeed) return;
	bLampStateApplied = true;

	TArray<UChildActorComponent*> ChildComps;
	GetComponents<UChildActorComponent>(ChildComps);

	// 모든 머신에서 같은 순서로 굴리도록 컴포넌트 이름(BP에서 정해진 값)으로 정렬
	ChildComps.Sort([](const UChildActorComponent& A, const UChildActorComponent& B)
	{
		return A.GetFName().LexicalLess(B.GetFName());
	});

	FRandomStream LampRng(LampSeed);
	for (UChildActorComponent* Comp : ChildComps)
	{
		ALampActor* Lamp = Comp ? Cast<ALampActor>(Comp->GetChildActor()) : nullptr;
		if (!Lamp) continue;

		// LampOffChance 확률로 감응형 대신 "항상 켜진 일반 조명"으로 전환 (예전엔 램프를 통째로 숨겼음).
		// 시드가 같아서 서버/모든 클라에서 같은 램프가 선택됨
		if (LampRng.FRand() < LampOffChance)
		{
			Lamp->SetConstantLight();
		}
	}
}
