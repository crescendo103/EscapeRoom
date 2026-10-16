// DiceActor.h
#pragma once

#include "CoreMinimal.h"
#include "MiscActor.h"
#include "DiceActor.generated.h"

class UStaticMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;

/**
 * 탈출 아이템: 주사위.
 *
 * 지금 단계에서는 "주웠는지" 상태를 게임플레이로 관리하지 않음 - 상호작용할 때마다
 * 비주얼(메시/콜리전/오라)만 켜고 끄는 토글로 동작. 5개 모으면 탈출구 개방 같은
 * 실제 수집 로직은 나중에 이 토글 지점(OnInteract_Implementation)에 이어붙이면 됨.
 *
 * - Root(Scene) 밑에 DiceMesh와 AuraComponent(Niagara)를 형제로 붙임. Root 자체는 흔들리지
 *   않게 고정해서 상호작용 라인 트레이스가 맞는 콜리전이나 오라 위치가 흔들림 때문에 같이
 *   출렁이지 않도록 함 - DiceMesh만 상대 위치를 오프셋시킴.
 * - 위아래 흔들림은 순수 연출이라 복제하지 않음: 서버/클라 각자 로컬에서 같은 사인파를 계산.
 * - CanInteract/OnInteract는 문/캐비넷과 동일하게 RunnerCharacter::Interact()의 라인 트레이스 ->
 *   ServerInteractWithTarget 서버 RPC를 거쳐서 도달하므로, OnInteract_Implementation은
 *   HasAuthority()로 스스로를 보호한다.
 * - 토글 상태(bIsVisible)를 서버에서 바꾸고, DoorActor::SetOpen과 동일하게 서버 자신의
 *   화면에도 즉시 반영되도록 OnRep과 같은 처리 함수를 직접 호출한다.
 */
UCLASS(Blueprintable)
class ESCAPEROOM_API ADiceActor : public AMiscActor
{
	GENERATED_BODY()

public:
	ADiceActor();

protected:
	// MiscActor 공통 구현이 이미 GetInteractHoldTime(0=즉시)/OnInteractHoldUpdate(빈 구현)를
	// 채워주므로, 상호작용 가능 여부(주사위는 러너 전용) + 실제 토글 로직만 override하면 됨
	virtual bool CanInteractInternal(const APawn* Interactor) const override { return !IsChaser(Interactor); }
	virtual void OnInteractInternal(APawn* Interactor) override;

	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 흔들림 애니메이션이 적용되는 실제 외형. 상호작용 라인 트레이스에 맞도록 콜리전 유지
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dice")
	TObjectPtr<UStaticMeshComponent> DiceMesh;

	// 주사위 주위 오라. 에디터에서 AuraEffect를 지정하면 켜져있는 동안 재생됨
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dice")
	TObjectPtr<UNiagaraComponent> AuraComponent;

	// 오라 나이아가라 시스템 에셋. 에디터에서 지정 필요 (비워두면 오라 없이 메시만 표시)
	UPROPERTY(EditDefaultsOnly, Category = "Dice|VFX")
	TObjectPtr<UNiagaraSystem> AuraEffect;

	// 위아래로 흔들리는 폭 (cm)
	UPROPERTY(EditAnywhere, Category = "Dice|Motion")
	float BobHeight = 15.f;

	// 흔들리는 속도
	UPROPERTY(EditAnywhere, Category = "Dice|Motion")
	float BobSpeed = 2.f;

	// 지금은 순수 테스트용 토글 상태 (주웠는지 여부가 아님). 상호작용할 때마다 뒤집힘.
	// 서버 권한으로만 바뀌고 복제됨
	UPROPERTY(ReplicatedUsing = OnRep_IsVisible, BlueprintReadOnly, Category = "Dice")
	bool bIsVisible = true;

	UFUNCTION()
	void OnRep_IsVisible();

	// bIsVisible에 따라 메시/콜리전/오라를 켜고 끄는 실제 처리. 서버(OnInteract_Implementation)와
	// 클라이언트(OnRep_IsVisible) 양쪽에서 호출되므로 로직을 이 함수 하나에만 둠
	void ApplyVisibility();

private:
	FVector InitialMeshRelativeLocation = FVector::ZeroVector;
	float RunningTime = 0.f;

	// ---- 술래 화면에서 주사위 숨기기 (로컬 전용, 복제 안 함) ----
	// 이 컴퓨터에서 화면을 보는 플레이어가 누구인지 판정이 끝났는지 (끝나면 더 이상 확인 안 함)
	bool bLocalViewerResolved = false;
	// 이 컴퓨터의 플레이어가 술래라서 주사위를 숨겨야 하는지
	bool bHiddenForLocalChaser = false;

	// 로컬 플레이어 폰을 찾아 Chaser 태그 여부로 판정. 폰이 아직 없으면 다음 Tick에 재시도
	void TryResolveLocalViewer();
};
