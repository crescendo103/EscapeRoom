#include "DoorActor.h"
#include "Net/UnrealNetwork.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "Components/StaticMeshComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "ChaserCharacter.h"

ADoorActor::ADoorActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicatingMovement(true);

	MeshRoot = CreateDefaultSubobject<USceneComponent>(TEXT("MeshRoot"));
	MeshRoot->SetupAttachment(Root);

	BrokenMesh = CreateDefaultSubobject<UGeometryCollectionComponent>(TEXT("BrokenMesh"));
	BrokenMesh->SetupAttachment(MeshRoot);
	BrokenMesh->SetVisibility(false);
	BrokenMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BrokenMesh->SetSimulatePhysics(false);
	// 부서진 파편이 플레이어(술래/러너 모두)와 물리적으로 충돌/밀림이 생기지 않도록 Pawn 채널은 무시.
	// 환경(Static/Dynamic 등) 및 다른 파편끼리의 충돌/파괴 시뮬레이션에는 영향 없음
	BrokenMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

	ImpactPoint = CreateDefaultSubobject<USceneComponent>(TEXT("ImpactPoint"));
	ImpactPoint->SetupAttachment(MeshRoot);
	ImpactPoint->SetRelativeLocation(FVector(0.f, 0.f, 100.f)); // 기본값, 블루프린트에서 문 메시 중심에 맞게 조정

	// 술래 킥 시작 위치 - 문이 열리거나 부서져도 따라 움직이지 않게 MeshRoot가 아니라 Root에 붙임.
	// 기본값은 문 정면(X축) 80cm 앞에서 문을 바라보는 방향, 실제 위치/방향은 블루프린트에서 조정
	ChaserInteractPoint = CreateDefaultSubobject<USceneComponent>(TEXT("ChaserInteractPoint"));
	ChaserInteractPoint->SetupAttachment(Root);
	ChaserInteractPoint->SetRelativeLocation(FVector(80.f, 0.f, 0.f));
	ChaserInteractPoint->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
}

void ADoorActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ADoorActor, bIsOpen);
	DOREPLIFETIME(ADoorActor, bIsBroken);
}

float ADoorActor::GetInteractHoldTime_Implementation(APawn* Interactor)
{
	if (Interactor && IsChaser(Interactor))
	{
		return KickHoldTime;
	}

	return InteractHoldTime;
}

void ADoorActor::OnInteractHoldUpdateInternal(APawn* Interactor, bool bHolding)
{
	// 호출 시점엔 이미 MiscActor::OnInteractHoldUpdate_Implementation에서 HasAuthority() 확인이
	// 끝난 뒤임 (bHolding=true일 때는 CanInteractInternal 재검증까지 끝난 뒤) -
	// AExitActor::OnInteractHoldUpdateInternal과 완전히 동일한 패턴
	if (AChaserCharacter* Chaser = Cast<AChaserCharacter>(Interactor))
	{
		if (bHolding)
		{
			// 홀드 시작 순간 한 번: 술래를 ChaserInteractPoint로 옮기고 문을 바라보게 함
			// (높이는 술래 현재 Z 유지, 회전은 Yaw만). 홀드 중엔 bIsInteracting으로 이동이 막혀 그대로 유지됨
			if (ChaserInteractPoint)
			{
				FVector SnapLocation = ChaserInteractPoint->GetComponentLocation();
				SnapLocation.Z = Chaser->GetActorLocation().Z;
				const FRotator SnapRotation(0.f, ChaserInteractPoint->GetComponentRotation().Yaw, 0.f);
				Chaser->SnapForChokeGrab(SnapLocation, SnapRotation);
			}

			Chaser->MulticastBeginKickCharge();
		}
		else
		{
			Chaser->MulticastCancelKickCharge();
		}
	}
}

void ADoorActor::SetOpen(bool bNewOpen)
{
	// ������ �ƴϸ� ���¸� �ٲ��� ���� (������ �׻� ����)
	if (!HasAuthority()) return;
	// �μ��� ���� �� �̻� ���� �ݴ� ��ȣ�ۿ��� �Ұ���
	if (bIsBroken) return;
	if (bIsOpen == bNewOpen) return;

	bIsOpen = bNewOpen;
	// ���� �ڽ��� ȭ�鿡�� ��� �ݿ� (OnRep�� Ŭ���̾�Ʈ������ ȣ��ǹǷ� ������ ���� ȣ��)
	OnDoorOpenChanged(bIsOpen);
}

void ADoorActor::Break()
{
	if (!HasAuthority()) return;
	// �̹� �μ��� ���� �ٽ� �μ� �ʿ� ���� (�� �� �μ����� ���� ����)
	if (bIsBroken) return;
	// ���� ���������� �μ� �� ���� - �������� ���� �ı� ����
	if (bIsOpen) return;

	bIsBroken = true;
	// �μ��� ������ ���� ���ΰ� �ǹ� �������Ƿ� "���� ��"���� �����ؼ�
	// Ŭ���̾�Ʈ�� ���� ���� ����(�ݸ��� ��)�� ������ �ʰ� ����
	bIsOpen = true;

	ActivateBrokenMesh();
	OnDoorBroken();
}

void ADoorActor::ShatterBrokenMesh()
{
	if (!BrokenMesh) return;

	const int32 RootIndex = BrokenMesh->GetRootIndex();
	const FVector Location = ImpactPoint ? ImpactPoint->GetComponentLocation() : GetActorLocation();

	// SimulatePhysics(true) 직후엔 Chaos 솔버가 Sleep 상태로 시작해서 Strain/Impulse가 씹힐 수 있으므로 깨워둠
	BrokenMesh->WakeRigidBody();

	UE_LOG(LogTemp, Warning, TEXT("[DoorActor] ShatterBrokenMesh: IsSimulatingPhysics=%d, RootIndex=%d"),
		BrokenMesh->IsSimulatingPhysics() ? 1 : 0, RootIndex);

	// 1) 먼저 클러스터 결합을 끊는다 - External Strain이 Damage Threshold를 초과해야
	// 조각들이 하나의 클러스터에서 분리되어 개별 리지드바디로 동작하기 시작함.
	// 이게 없으면 AddRadialImpulse를 줘도 문 전체가 통짜로 밀리기만 하고 파편처럼 흩어지지 않음.
	BrokenMesh->ApplyExternalStrain(
		RootIndex,
		Location,
		500.f,        // Radius - 이 반경 안의 클러스터에 Strain 적용
		100,          // PropagationDepth - 하위 계층까지 전파할 깊이
		1.f,          // PropagationFactor
		100000000.f   // Strain - Damage Threshold를 확실히 초과하도록 크게
	);

	// 2) 방금 클러스터에서 분리된 조각들에 사방으로 퍼지는 힘을 준다(사용하는 임펄스는 이거 하나로 통일)
	BrokenMesh->AddRadialImpulse(Location, 400.f, 40.f, ERadialImpulseFalloff::RIF_Linear, true);
}

void ADoorActor::OnRep_IsOpen()
{
	// �μ��� �ڿ� ���� ���� ������ �ڴʰ� �͵� �ı� ������ ����� �ʵ��� ���
	if (bIsBroken) return;
	OnDoorOpenChanged(bIsOpen);
}

void ADoorActor::ActivateBrokenMesh()
{
	if (!BrokenMesh) return;

	// ������ �⺻��(�� ���� + �ݸ��� ���� + ���� �ùķ��̼� ����)�� �μ����� ���� ��������.
	// �̰� �� ���ָ� �Ʒ� ShatterBrokenMesh()�� ApplyExternalStrain�� ���������� Strain�� �൵
	// ������ Kinematic ���� �״�ζ� ���������� �������� ����(�Ѻ��⿣ �ƹ� ��ȭ ����)
	BrokenMesh->SetVisibility(true, true);
	BrokenMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BrokenMesh->SetSimulatePhysics(true);

	// ��������Ʈ���� MeshRoot �Ʒ� �����ص� "���� ��" static mesh�� ���� ã�Ƽ� ����� �浹�� ��
	// (���� OnDoorBroken ��������Ʈ �̺�Ʈ���� Door Mesh�� ������� ���� �ϴ� ó���� C++�� �ű�).
	// ������Ʈ �̸�/������ ���� �ǵ��� MeshRoot�� �ڽ� �� UStaticMeshComponent�� ���� ã�� ó����
	if (MeshRoot)
	{
		TArray<USceneComponent*> MeshRootChildren;
		MeshRoot->GetChildrenComponents(true, MeshRootChildren);
		for (USceneComponent* Child : MeshRootChildren)
		{
			if (UStaticMeshComponent* IntactMesh = Cast<UStaticMeshComponent>(Child))
			{
				IntactMesh->SetVisibility(false, true);
				IntactMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}
	}

	// 딜레이 없이 바로 Shatter 처리 - 지금 Damage Threshold(1.0)가 워낙 낮아서
	// 등록 타이밍 문제로 씹힐 걱정 없이 바로 호출해도 확실히 끊어짐
	// SetSimulatePhysics(true)와 같은 프레임에 Strain을 주면 Chaos 물리 프록시가 아직 준비 안 돼서
	// 가끔 씹힘 -> GC가 안 쪼개지고 문 통짜로 넘어짐. 다음 틱에 Shatter해서 프록시 등록 이후에 들어가게 함
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &ADoorActor::ShatterBrokenMesh));
	SpawnKickImpactEffect();
}

void ADoorActor::SpawnKickImpactEffect()
{
	if (!KickImpactNiagaraEffect || !ImpactPoint)
	{
		return;
	}

	UNiagaraComponent* NiagaraComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(), KickImpactNiagaraEffect, ImpactPoint->GetComponentLocation(), ImpactPoint->GetComponentRotation());

	if (NiagaraComp)
	{
		FTimerHandle NiagaraStopTimerHandle;
		GetWorldTimerManager().SetTimer(NiagaraStopTimerHandle, FTimerDelegate::CreateWeakLambda(NiagaraComp, [NiagaraComp]()
		{
			NiagaraComp->Deactivate();
		}), 2.0f, false);
	}
}

void ADoorActor::OnRep_IsBroken()
{
	if (!bIsBroken) return;
	ActivateBrokenMesh();
	// �ݸ���/��ȣ�ۿ� ���� ��Ȱ��ȭ �� �߰� ������ ������ ��������Ʈ���� ����
	OnDoorBroken();
}