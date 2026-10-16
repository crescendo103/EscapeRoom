// MiscActor.h
//
// [설계 메모] Door/Cabinet/Dice/Exit처럼 "상호작용 가능한 배치 오브젝트"의 공통 부모.
// Cabinet은 도망자를 Possess해서 숨기는 기능 때문에 반드시 APawn이어야 해서(순수 AActor는 Possess
// 대상이 될 수 없음), 이 공통 부모도 AActor가 아니라 APawn을 상속한다. Door/Dice/Exit는 Possess될
// 일이 없지만 Pawn이어도 동작엔 아무 문제 없다(AutoPossess를 꺼두므로 AI/플레이어가 실수로 빙의하지도 않음).
//
// 공통으로 제공하는 것:
// 1. Root(SceneComponent) - 서브클래스는 각자 새로 만들지 말고 이 Root 밑에 메시 등을 붙이면 됨
// 2. IInteractable 기본 구현 - CanInteract/OnInteract/GetInteractHoldTime/OnInteractHoldUpdate가
//    전부 아래 *Internal() 가상 함수로 위임되도록 기본 구현을 제공. 서브클래스는:
//      - 단순한 경우(Door/Dice/Exit): CanInteractInternal/OnInteractInternal만 override하면 끝
//      - 복잡한 경우(Cabinet처럼 Interactor 타입별로 완전히 다른 홀드시간/분기가 필요하면):
//        _Implementation 함수 자체를 다시 override해서 기본 구현을 통째로 대체해도 됨(평범한 가상함수
//        오버라이드라 몇 단계를 내려가도 자유롭게 재정의 가능)
// 3. 상호작용 범위 판정(InteractionRange 구체) - "이 화면을 보고 있는 사람 자신의 폰"이 범위 안에
//    들어와 있고 CanInteractInternal()이 true일 때만 MatchWidget 프롬프트 문구 + 하이라이트를
//    켠다. (월드 스페이스 "Press E" 위젯은 제거됨) 서버가 판정해서 복제해주는 게 아니라, 각 클라이언트가 자기 로컬 폰(IsLocallyControlled())
//    기준으로만 스스로 판단하는 완전히 클라이언트 개인 화면 전용 로직이라, 다른(원격) 플레이어가
//    근처에 있어도 내 화면에는 영향이 없다. 데디케이티드 서버에는 애초에 로컬 폰이라는 개념 자체가
//    없어(항상 false) 아무 의미가 없으므로 BeginPlay에서 오버랩 바인딩 자체를 하지 않는다.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Interactable.h"
#include "MiscActor.generated.h"

class UInteractionTextComponent;
class USphereComponent;
class UPrimitiveComponent;
class ADungeonPlayerController;
class UMaterialInterface;
struct FHitResult;

UCLASS(Abstract, Blueprintable)
class ESCAPEROOM_API AMiscActor : public APawn, public IInteractable
{
	GENERATED_BODY()

public:
	AMiscActor();

	//~ Begin IInteractable
	virtual bool CanInteract_Implementation(APawn* Interactor) override;
	virtual void OnInteract_Implementation(APawn* Interactor) override;
	virtual float GetInteractHoldTime_Implementation(APawn* Interactor) override;
	virtual void OnInteractHoldUpdate_Implementation(APawn* Interactor, bool bHolding) override;
	virtual FText GetInteractionHoldText_Implementation(APawn* Interactor) override;
	//~ End IInteractable

protected:
	virtual void BeginPlay() override;

	// 서브클래스가 새로 Root를 만들지 말고 이 밑에 메시 등을 붙이면 됨 (생성자에서 이미 만들어져 있음)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MiscActor")
	TObjectPtr<USceneComponent> Root;

	// Chaser(술래)인지 판별할 때 쓰는 태그. CabinetActor 등에서 술래/러너 분기에 재사용
	UPROPERTY(EditAnywhere, Category = "Interaction")
	FName ChaserTag = TEXT("Chaser");

	// 오버랩 시 하얗게 켤 아웃라인 하이라이트 머티리얼(인스턴스). MT_ItemHighlight 계열 MI를 지정하면 됨.
	// 비워두면 하이라이트 없이 기존(프롬프트/텍스트)만 동작. Root 밑에 붙은 메시란 메시는 전부(자식까지
	// 재귀적으로) 이 머티리얼을 오버레이하므로, 서브클래스마다 따로 "어떤 메시냐"를 지정할 필요 없음
	UPROPERTY(EditAnywhere, Category = "Interaction")
	TObjectPtr<UMaterialInterface> HighlightMaterial;

	bool IsChaser(const APawn* Interactor) const { return Interactor && Interactor->ActorHasTag(ChaserTag); }

	// 홀드 없이 즉시 발동하려면 0(기본값)으로 둠. Interactor별로 다른 홀드시간이 필요하면(캐비닛처럼)
	// GetInteractHoldTime_Implementation 자체를 override하면 됨
	UPROPERTY(EditAnywhere, Category = "Interaction")
	float InteractHoldTime = 0.f;

	// 이 Interactor가 지금 상호작용 가능한지 - 서브클래스에서 override (문=파괴 안 됐으면,
	// 캐비닛/주사위/탈출구는 각자 상태에 맞게). 기본값은 항상 true
	virtual bool CanInteractInternal(const APawn* Interactor) const { return true; }

	// 실제 상호작용 처리 - 서브클래스에서 override (문 열기, 주사위 토글 등). 호출 시점엔 이미
	// HasAuthority() + CanInteractInternal() 확인이 끝난 뒤임
	virtual void OnInteractInternal(APawn* Interactor) {}

	// 홀드 진행 중 미리보기 연출이 필요하면 서브클래스에서 override (기본은 아무 것도 안 함)
	virtual void OnInteractHoldUpdateInternal(APawn* Interactor, bool bHolding) {}

	// 상호작용 관련 표시 문구만 들고 있는 순수 데이터 컴포넌트(렌더링 없음). 서브클래스 블루프린트의
	// 컴포넌트 계층에서 이 컴포넌트를 선택해 HoldText를 직접 입력하면 됨(예: "캐비닛 열기", "문 부수기").
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<UInteractionTextComponent> InteractionText;

	// "내 화면"에서 로컬 폰이 이 범위 안에 있으면 프롬프트 문구/하이라이트를 자동으로 켬(자세한 설명은 위 참고).
	// 반지름은 에디터에서 이 컴포넌트를 선택해 Shape > Sphere Radius로 직접 조정하면 됨
	// (기본값은 생성자에서 150cm로 세팅)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<USphereComponent> InteractionRange;

private:
	UFUNCTION()
	void OnRangeBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnRangeEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	// 로컬 폰(범위에 들어온/나간 그 폰)을 조종 중인 화면의 MatchWidget에 InteractionText의 역할별 PromptText를
	// 보이거나 숨김 (화면 HUD의 HoldActionText 자리) - OnRangeBeginOverlap/EndOverlap에서 호출
	void SetLocalMatchWidgetPromptTextActive(const APawn* LocalPawn, bool bActive) const;

	// Root 밑의 메시 컴포넌트를 전부(자식까지 재귀) 찾아서 HighlightMaterial을 오버레이 머티리얼로
	// 켜거나(bActive=true) 끔(false, nullptr로 해제). SetOverlayMaterial은 순수 로컬 렌더링 호출이라
	// 네트워크 복제가 필요 없음 - OnRangeBeginOverlap/EndOverlap을 부르는 클라이언트 화면에만 반영됨
	void SetHighlightActive(bool bActive) const;

	// 지금 이 화면의 로컬 폰이 범위 안에 있는지 (로컬 폰은 한 화면에 하나뿐이라 집합이 아니라 bool로 충분)
	bool bLocalPawnInRange = false;
};
