#pragma once
#include "CoreMinimal.h"
#include "MiscActor.h"
#include "TimerManager.h"
#include "DoorActor.generated.h"

class UStaticMeshComponent;
class UGeometryCollectionComponent;
class UNiagaraSystem;

UCLASS(Blueprintable)
class ADoorActor : public AMiscActor
{
	GENERATED_BODY()
public:
	ADoorActor();

protected:
	// MiscActor 공통 구현이 이미 OnInteractHoldUpdate(빈 구현)를 채워주므로, 상호작용 가능 여부 +
	// 실제 열고닫기 토글 + 역할별 홀드 시간만 override하면 됨.
	// 술래도 이제 이 IInteractable 파이프라인을 그대로 타지만(캐비닛과 동일한 홀드 게이지/프로그레스바
	// 재사용 목적), 실제 파괴는 OnInteractInternal이 아니라 킥 몽타주의 AnimNotify가 처리하므로
	// 여기서는 아무것도 안 하고 리턴함 - 러너만 부서지지 않은 문을 즉시 여닫을 수 있음.
	// 술래는 러너와 달리 "부술 수 있을 때"(CanBreak() - 안 부서졌고 안 열려있을 때)만 상호작용
	// 가능하게 해서, 이미 열려있는 문에는 프롬프트/홀드 게이지 자체가 안 뜨게 함
	// (InteractionRange 오버랩 판정도 이 함수를 그대로 쓰므로 자동으로 같이 적용됨)
	virtual bool CanInteractInternal(const APawn* Interactor) const override
	{
		// [TEMP DEBUG] 이 로그가 안 찍히면 CanInteract_Implementation은 호출됐지만 여기까지
		// 가상함수 디스패치가 안 내려온다는 뜻(비정상) - 원인 찾으면 지워도 됨
		const bool bChaser = IsChaser(Interactor);
		const bool bResult = bChaser ? CanBreak() : IsUsable();
		UE_LOG(LogTemp, Warning, TEXT("[Interact][Door] %s::CanInteractInternal IsChaser=%d IsOpen=%d IsBroken=%d -> %d"),
			*GetNameSafe(this), bChaser ? 1 : 0, bIsOpen ? 1 : 0, bIsBroken ? 1 : 0, bResult ? 1 : 0);
		return bResult;
	}
	virtual void OnInteractInternal(APawn* Interactor) override
	{
		if (IsChaser(Interactor))
		{
			return;
		}
		SetOpen(!bIsOpen);
	}

	// 술래는 KickHoldTime만큼, 러너는 기존 InteractHoldTime(기본 0=즉시)만큼 홀드해야 함 -
	// ACabinetActor::GetInteractHoldTime_Implementation과 완전히 동일한 패턴
	virtual float GetInteractHoldTime_Implementation(APawn* Interactor) override;

	// 술래가 문 부수기 홀드를 시작/취소할 때 호출됨 (AExitActor::OnInteractHoldUpdateInternal이
	// 러너의 사다리 홀드 몽타주를 재생시키는 것과 동일한 패턴). AChaserCharacter의 전체 타입이
	// 필요해서(Cast 후 멤버 함수 호출) 헤더에서는 선언만 하고 구현은 DoorActor.cpp에 둔다
	// (ChaserCharacter.h를 여기서 include하면 순환 include가 생기므로 회피)
	virtual void OnInteractHoldUpdateInternal(APawn* Interactor, bool bHolding) override;

public:

	// ���������� ȣ��: ���� ���� ����. �̹� �μ��� ���� ���õ�(�� �̻� ��ȣ�ۿ� �Ұ�)
	UFUNCTION(BlueprintCallable, Category = "WFC|Door")
	void SetOpen(bool bNewOpen);

	// ���������� ȣ��: ���� �μ�. �� �� �μ����� �ǵ��� �� ����
	UFUNCTION(BlueprintCallable, Category = "WFC|Door")
	void Break();

	// BrokenMesh�� ������ �ı� ���(Strain)�� �༭ ������ �������� ��
	UFUNCTION(BlueprintCallable, Category = "WFC|Door")
	void ShatterBrokenMesh();

	UFUNCTION(BlueprintPure, Category = "WFC|Door")
	bool IsOpen() const { return bIsOpen; }

	UFUNCTION(BlueprintPure, Category = "WFC|Door")
	bool IsBroken() const { return bIsBroken; }

	// ���� �μ����� �ʾƼ� ���� �ݴ� ��ȣ�ۿ��� ���� ��������
	// [FIX] 예전 이름 CanInteract()가 IInteractable 인터페이스의 CanInteract(APawn*)와
	// 이름이 겹쳐서 UFunction 리플렉션 테이블에서 충돌 - Execute_CanInteract가 엉뚱하게
	// 동작하던 원인이었음. 이름을 IsUsable()로 바꿔서 충돌 제거
	UFUNCTION(BlueprintPure, Category = "WFC|Door")
	bool IsUsable() const { return !bIsBroken; }

	// ���� �μ����� �ʾҰ� �������� ���� �μ� �� ���� (�����ִ� ���� �μ� �ʿ䰡 �����Ƿ� ����)
	UFUNCTION(BlueprintPure, Category = "WFC|Door")
	bool CanBreak() const { return !bIsBroken && !bIsOpen; }

	// 술래가 문을 차서 부수기 위해 홀드해야 하는 시간(초). ChaserCharacter::Interact()가
	// IInteractable::GetInteractHoldTime()으로 이 값을 받아 홀드 타이머+프로그레스바를 돌리고,
	// 다 채워지면 KickDoor 몽타주를 재생(ServerPlayKickDoorMontage)함 - 실제 파괴는 몽타주의
	// AnimNotify(NotifyKickDoorHit)가 트리거함
	UPROPERTY(EditAnywhere, Category = "WFC|Door")
	float KickHoldTime = 3.f;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// �� ���°� �ٲ�� Ŭ���̾�Ʈ���� ȣ��Ǿ� �ð�/�浹 ����
	UFUNCTION()
	void OnRep_IsOpen();

	UFUNCTION()
	void OnRep_IsBroken();

	// BrokenMesh�� ������ �ı� ������ ����(���̱� On + �ݸ��� On + ���� �ùķ��̼� On)�� ��ȯ�ϰ�
	// ShatterBrokenMesh()�� ȣ���ؼ� ������ ������. �����ڿ��� BrokenMesh�� �� ���̰�/�ݸ��� ����/
	// �ùķ��̼� �� ���·� �����ϹǷ�, �μ����� ���� �� �Լ��� �� ���¸� ���������� ������
	// ApplyExternalStrain���� ���� Strain�� �൵ ������ Kinematic ���� �״�ζ� �����δ� �������� ����
	// (�Ѻ��⿣ Break()�� ����ŵ� �ƹ� �ϵ� �� �Ͼ ��ó�� ���̴� ����). Break()(����)��
	// OnRep_IsBroken()(����Ʈ Ŭ��) ���ʿ��� �� ������ ȣ��Ǿ� �� �ӽſ��� ���÷� �ı� ������ �����
	void ActivateBrokenMesh();

	// KickImpactNiagaraEffect를 ImpactPoint 위치/회전에 스폰. ActivateBrokenMesh()에서 호출되므로
	// 서버(Break() 경로) + 모든 클라(OnRep_IsBroken() 경로)에서 각자 한 번씩 실행되어 전원에게 보임
	void SpawnKickImpactEffect();

	// ���� �� ���� �ݿ� (�޽� �ִϸ��̼�, �ݸ��� ��� ���� ��������Ʈ���� �����ϱ� ���� �̺�Ʈ�� �и�)
	UFUNCTION(BlueprintImplementableEvent, Category = "WFC|Door")
	void OnDoorOpenChanged(bool bOpen);

	// ī���� ��Ʈ���� �ı� ���� + ���� �ݸ���/��ȣ�ۿ� ��Ȱ��ȭ�� ��������Ʈ���� ����
	UFUNCTION(BlueprintImplementableEvent, Category = "WFC|Door")
	void OnDoorBroken();

public:
	// �� �޽ô� ��������Ʈ���� Root(AMiscActor ���) �Ʒ��� ����
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC")
	TObjectPtr<USceneComponent> MeshRoot;

	// �μ��� �� ������ ������ �޽� (Fracture ���� �̸� ���� GeometryCollection �ּ��� Rest Collection�� ����)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC")
	TObjectPtr<UGeometryCollectionComponent> BrokenMesh;

	// 파편에 힘/Strain을 줄 기준점(힘점). 문 메시의 시각적 중심 근처로 블루프린트에서 위치를 옮겨서 맞춰줌
	// (GetActorLocation()은 액터 피벗이라 보통 문 바닥에 있어서, 그걸 기준으로 RadialImpulse를 주면
	// 파편이 전부 위쪽으로만 솟구치는 문제가 있었음 - 이 컴포넌트를 대신 기준점으로 사용)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC")
	TObjectPtr<USceneComponent> ImpactPoint;

	// 술래가 문 부수기 홀드를 시작하는 순간 한 번 옮겨질 위치/방향 마커 (앞/뒤 2개).
	// 문은 양쪽에서 찰 수 있으므로 술래와 더 가까운 쪽으로 옮김.
	// 블루프린트에서 각각 문 앞/뒤에 배치하고 X축(빨간 화살표)이 문을 바라보도록 회전시킬 것.
	// 위치는 X/Y만 사용(높이는 술래 현재 Z 유지), 회전은 Yaw만 사용
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC|Door")
	TObjectPtr<USceneComponent> ChaserInteractPoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC|Door")
	TObjectPtr<USceneComponent> ChaserInteractPointBack;

	// KickDoor 몽타주 타격 시 재생할 나이아가라 이펙트. 예전에는 ChaserCharacter가 들고 있었으나,
	// 리플리케이션 경로(Break()/OnRep_IsBroken()) 위에서 확실히 모든 클라에 재생되도록 Door로 옮겨옴
	UPROPERTY(EditDefaultsOnly, Category = "WFC|Door")
	TObjectPtr<UNiagaraSystem> KickImpactNiagaraEffect;

protected:
	// ������ ������ ������ ���°� - �ٲ� ������ �����ϴ� OnRep�� Ŭ���̾�Ʈ���� �ڵ� ȣ���
	UPROPERTY(ReplicatedUsing = OnRep_IsOpen, BlueprintReadOnly, Category = "WFC", VisibleAnywhere)
	bool bIsOpen = true; // 시작 시 열린 상태. BP_Door2에서 RotationPivot Yaw 90 + BeginPlay에서 DoorOpen Set New Time(끝)으로 모양을 맞춰줌

	// �� �� true�� �Ǹ� ���ķδ� ���� false�� �ǵ��ư��� ���� (���� �ı��� ���� ����)
	UPROPERTY(ReplicatedUsing = OnRep_IsBroken, BlueprintReadOnly, Category = "WFC", VisibleAnywhere)
	bool bIsBroken = false;
};