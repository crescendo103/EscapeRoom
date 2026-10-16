// ExitActor.h
#pragma once

#include "CoreMinimal.h"
#include "MiscActor.h"
#include "ExitActor.generated.h"

class UStaticMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UActorSequenceComponent;
class USphereComponent;
class ARunnerCharacter;

/**
 * 탈출구.
 *
 * [탈출 처리] 러너가 InteractHoldTime(사다리 몽타주 길이)만큼 홀드를 채우면 서버의 OnInteractInternal이
 * 불리고, 그 즉시 탈출이 확정된다:
 *   1) ARunnerCharacter::SetEscaped() - 러너 숨김/조작 차단, 남은 러너 수 -1, 본인 화면 비네트 페이드 후 관전
 *   2) 탈출구 Destroy() (1인 1탈출구 - 서버에서 파괴하면 모든 클라에서 사라짐)
 * 남은 탈출구 수는 GameState::ActiveExitCount로 관리 (서버 BeginPlay +1, EndPlay -1) - HUD 표시용
 * 한 번에 한 명만 오를 수 있도록, 홀드를 시작한 러너를 CurrentClimber로 잠가둔다(복제됨 - 다른 러너의
 * 클라이언트도 "누가 쓰는 중"임을 알아야 홀드 시작 자체를 로컬에서 막을 수 있음).
 *
 * - Root(Scene) 밑에 ExitMesh(외형), AuraComponent(나이아가라 - 열렸을 때 표시용 오라)를 형제로 붙임.
 * - CanInteract/OnInteract는 DiceActor/CabinetActor/DoorActor와 동일하게 RunnerCharacter::Interact()의
 *   라인 트레이스 -> ServerInteractWithTarget 서버 RPC를 거쳐서 도달하므로 HasAuthority()로 스스로 보호.
 * - 토글 상태(bIsOpen)를 서버에서 바꾸고, 서버 자신의 화면에도 즉시 반영되도록 OnRep과 같은
 *   처리 함수(ApplyOpenVisuals)를 직접 호출한다.
 *
 * [진입/이탈 연출 시퀀스] 상호작용(문 열기/닫기)과는 완전히 별개로, 근처에 아무 폰이나 들어오고
 * 나갈 때 재생되는 순수 연출용 Actor Sequence를 하나 더 둔다. DiceActor의 흔들림 애니메이션과
 * 같은 이유로 서버 권위 체크(HasAuthority) 없이 서버/클라 각자 로컬에서 자기 화면의 오버랩만
 * 보고 재생한다 - 다른 플레이어 위치는 이미 리플리케이트돼서 내 화면에도 보이므로, 각자 독립적으로
 * 재생해도 결과적으로 모두에게 똑같이 보인다. 시퀀스는 하나만 두고 들어올 때 정방향(Play),
 * 나갈 때 역방향(PlayReverse)으로만 재생해서 문이 열리고 닫히는 것 같은 연출을 낸다.
 */
class USoundBase;
class USoundAttenuation;

UCLASS(Blueprintable)
class ESCAPEROOM_API AExitActor : public AMiscActor
{
	GENERATED_BODY()

public:
	AExitActor();

	UFUNCTION(BlueprintPure, Category = "Exit")
	bool IsOpen() const { return bIsOpen; }

	// ---- 문 열림/닫힘 사운드 ----
	// 문 위치에서 3D로 재생. 시퀀스를 재생/역재생하는 곳(각 머신 로컬)에서 같이 재생하므로 복제/RPC 없음
	UPROPERTY(EditDefaultsOnly, Category = "Exit|Sound")
	TObjectPtr<USoundBase> OpenSound;

	UPROPERTY(EditDefaultsOnly, Category = "Exit|Sound")
	TObjectPtr<USoundBase> CloseSound;

	// 들리는 거리. 비워두면 맵 전체에서 들리므로 지정할 것
	UPROPERTY(EditDefaultsOnly, Category = "Exit|Sound")
	TObjectPtr<USoundAttenuation> DoorSoundAttenuation;

	UFUNCTION(BlueprintCallable, Category = "Exit|Sound")
	void PlayOpenSound();

	UFUNCTION(BlueprintCallable, Category = "Exit|Sound")
	void PlayCloseSound();

	// 지금 이 탈출구를 홀드 중인(사다리를 오르는 중인) 러너가 있는지
	UFUNCTION(BlueprintPure, Category = "Exit")
	bool IsOccupied() const;

protected:
	// MiscActor 공통 구현이 이미 GetInteractHoldTime(0=즉시)/OnInteractHoldUpdate(빈 구현)를
	// 채워주므로, 상호작용 가능 여부(탈출구는 러너 전용) + 실제 토글 로직만 override하면 됨
	// 러너만, 아직 안 쓰인(bIsOpen) 탈출구만, 다른 러너가 오르는 중이 아닐 때만 가능
	virtual bool CanInteractInternal(const APawn* Interactor) const override;
	virtual void OnInteractInternal(APawn* Interactor) override;

	// 홀드(사다리 오르기 예고) 시작/취소 신호. ServerSetInteractHold를 거쳐 서버 권한으로 호출됨.
	// 러너면 ClimbMontage 재생/정지를 ARunnerCharacter의 Multicast 함수에 위임한다 - 여기서 직접
	// 몽타주를 다루지 않는 이유는 ExitActor는 위치 정보(ClimbStartPoint)만 제공할 뿐, 실제 몽타주
	// 애셋은 캐릭터 쪽(ARunnerCharacter::ClimbMontage)이 들고 있기 때문
	virtual void OnInteractHoldUpdateInternal(APawn* Interactor, bool bHolding) override;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_IsOpen();

	// bIsOpen에 따른 실제 시각 처리 (오라 켜기/끄기 등). 서버(OnInteractInternal)와
	// 클라이언트(OnRep_IsOpen) 양쪽에서 호출되므로 로직을 이 함수 하나에만 둠
	void ApplyOpenVisuals();

	// 근처 연출 시퀀스 열기/닫기 + 사운드. 이미 열려있거나(열리는 중) 이미 닫혀있으면(닫히는 중) 소리를 다시 내지 않음
	// (러너 여러 명이 범위에 드나들 때 소리가 중복되지 않도록)
	void PlayRangeSequenceOpen();
	void PlayRangeSequenceClose();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Exit")
	TObjectPtr<UStaticMeshComponent> ExitMesh;

	// 열렸을 때 표시용 오라. 에디터에서 AuraEffect 지정 시 열린 동안 재생
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Exit")
	TObjectPtr<UNiagaraComponent> AuraComponent;

	UPROPERTY(EditDefaultsOnly, Category = "Exit|VFX")
	TObjectPtr<UNiagaraSystem> AuraEffect;

	// 아직 탈출에 쓸 수 있는지. 스폰 시 true, 누군가 탈출에 성공하면 false가 되고 다시 켜지지 않음.
	// 서버 권한으로만 바뀌고 복제됨 (false면 오라가 꺼지고 근처 연출 시퀀스도 재생 안 함)
	UPROPERTY(ReplicatedUsing = OnRep_IsOpen, BlueprintReadOnly, Category = "Exit")
	bool bIsOpen = true;

	// 진입/이탈 연출용 시퀀스. C++에서 CreateDefaultSubobject로 직접 만들면 블루프린트에서
	// Sequencer 탭이 열리지 않는 문제가 있어서, 블루프린트(BP_Exit)에 ActorSequenceComponent를
	// 직접 추가하도록 하고 BeginPlay에서 FindComponentByClass로 찾아 캐싱한다.
	// 실제 키프레임 내용은 블루프린트에서 그 컴포넌트를 선택해 Sequencer 패널을 열어 채우면 됨 -
	// C++은 재생/역재생 트리거만 담당
	UPROPERTY(Transient)
	TObjectPtr<UActorSequenceComponent> RangeSequence;

	// 지금 이 탈출구를 홀드 중인 러너 (없으면 nullptr). 서버가 홀드 시작/취소/완료 시점에 세팅하고 복제됨.
	// 오르던 러너가 잡히거나(IsCaught/IsBeingCaught) 사라지면 IsOccupied()가 자동으로 무시함
	UPROPERTY(Replicated)
	TObjectPtr<APawn> CurrentClimber;

	// 서버에서 GameState::ActiveExitCount에 +1 했는지 (EndPlay에서 짝 맞춰 -1 하기 위함)
	bool bCountedAsActiveExit = false;

	// 진입/이탈 판정용 트리거 볼륨. MiscActor의 InteractionRange(로컬 폰 전용 프롬프트용)와는
	// 목적이 달라서 별도로 둠 - 이건 근처의 아무 폰이나 대상으로 함. 반지름은 에디터에서 조정
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Exit|Sequence")
	TObjectPtr<USphereComponent> SequenceTriggerRange;

	// [사다리 오르기 연출] 정밀한 손/발 IK 대신, 상호작용 순간 캐릭터를 이 위치/각도로 스냅시킨 뒤
	// 루트모션 클라이밍 몽타주를 재생하는 방식으로 처리한다. 블루프린트에서 이 컴포넌트를 선택해
	// 사다리 발판 맨 아래(캐릭터가 처음 손/발을 짚어야 하는 자리)에 맞춰 위치/회전을 잡아주면 됨
	// 실제 재생할 몽타주는 이 사다리가 아니라 러너 캐릭터(ARunnerCharacter::ClimbMontage)가 들고 있음 -
	// 어떤 캐릭터가 와도 같은 몽타주를 재생해야 하므로 캐릭터 쪽 데이터가 맞음. 이 액터는 "어디서
	// 재생할지" 위치 정보만 제공
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Exit|Climb")
	TObjectPtr<USceneComponent> ClimbStartPoint;

private:
	UFUNCTION()
	void OnSequenceRangeBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnSequenceRangeEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);
};
