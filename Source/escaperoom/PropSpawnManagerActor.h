// PropSpawnManagerActor.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PropSpawnPoint.h"
#include "PropSpawnManagerActor.generated.h"

class AWFCGridManager;
class ACabinetActor;
class ADiceActor;
class AExitActor;

// 진행률(현재/전체) 보고용. WFCGridManager::OnMapGenProgress와 같은 모양이지만
// 완전히 별개의 델리게이트 - 소품 배치 단계 전용.
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPropSpawnProgress, int32, int32);

// 소품 배치(확률 판정 + 스폰)가 전부 끝났을 때 1회 브로드캐스트.
DECLARE_MULTICAST_DELEGATE(FOnPropSpawnComplete);

/**
 * WFCGridManager가 방/빅룸/문 스폰까지 다 끝낸 뒤, 각 방 블루프린트 안에 미리 배치해둔
 * UPropSpawnPoint 마커들을 모아 캐비닛/다이스/Exit를 배치하는 매니저.
 *
 * [단계형 다이스/탈출구 시스템] 캐비닛은 기존처럼 확률(DefaultCabinetChance)로 즉시 스폰하지만,
 * 다이스(Item)와 탈출구(Exit)는 더 이상 확률로 한 번에 뿌리지 않는다. 대신:
 *   1) PopulateProps()에서 Item/Exit 마커 후보 전부를 섞어서 "보류 풀"(PendingItemPoints/
 *      PendingExitPoints)에 담아두고, GameState->DiceStages의 1단계 요구량만큼만 다이스를
 *      즉시 ToSpawn에 편입시킨다.
 *   2) 러너가 다이스를 다 먹어서 GameState::AddDice()가 한 단계를 클리어시키면
 *      OnDiceStageCleared(ExitsToSpawn) 델리게이트가 발동되고, HandleDiceStageCleared가
 *      그 개수만큼 보류 풀에서 탈출구를 꺼내 스폰하고, 다음 단계 몫의 다이스도 마저 꺼내 스폰한다.
 *   3) 이 과정이 반복되며 각 단계가 순서대로 진행된다 (표는 DiceStageDataAsset 참고).
 *
 * - 방/빅룸을 ARoomActor로 한정하지 않고 AActor 기준 GetComponents<UPropSpawnPoint>()를 쓰므로
 *   일반 룸이든 빅룸 블루프린트든 동일하게 동작한다.
 * - 스폰은 WFCGridManager와 동일하게 서버 권한(HasAuthority)에서만 수행한다.
 */
UCLASS()
class ESCAPEROOM_API APropSpawnManagerActor : public AActor
{
	GENERATED_BODY()

public:
	APropSpawnManagerActor();

	// 맵을 생성하는 WFCGridManager 참조. 비워두면 BeginPlay에서 레벨 내 첫 번째
	// AWFCGridManager를 자동으로 찾아서 쓴다 (직접 지정해도 됨).
	UPROPERTY(EditInstanceOnly, Category = "PropSpawn")
	TObjectPtr<AWFCGridManager> GridManager;

	UPROPERTY(EditDefaultsOnly, Category = "PropSpawn|Classes")
	TSubclassOf<ACabinetActor> CabinetClass;

	UPROPERTY(EditDefaultsOnly, Category = "PropSpawn|Classes")
	TSubclassOf<ADiceActor> DiceClass;

	UPROPERTY(EditDefaultsOnly, Category = "PropSpawn|Classes")
	TSubclassOf<AExitActor> ExitClass;

	// 캐비닛 기본 스폰 확률(0~1). 개별 마커의 SpawnChanceOverride가 음수면 이 값을 사용.
	// 다이스/탈출구는 이제 확률이 아니라 GameState->DiceStages 단계별 개수로 스폰되므로
	// 여기 확률은 캐비닛에만 적용된다.
	UPROPERTY(EditAnywhere, Category = "PropSpawn|Chance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultCabinetChance = 0.5f;

	// 한 틱에 실제로 SpawnActor를 몇 개까지 처리할지 (WFCGridManager::SpawnsPerTick과 동일한 목적)
	UPROPERTY(EditDefaultsOnly, Category = "PropSpawn|Pacing")
	int32 PropsPerTick = 5;

	// 로컬(서버 자신 또는 리슨서버 호스트)에서 구독하는 델리게이트.
	// 서버에서만 직접 브로드캐스트되므로, 원격 클라이언트는 대신 아래 Replicated 프로퍼티의
	// OnRep 콜백을 통해 같은 델리게이트를 로컬로 브로드캐스트받는다 (WFCGridManager와 동일한 패턴).
	FOnPropSpawnProgress OnPropSpawnProgress;
	FOnPropSpawnComplete OnPropSpawnComplete;

	// 소품 배치가 이미 끝났는지 (클라에서는 복제된 값). 구독하기 전에 완료 신호가 먼저 지나간 경우
	// (늦게 접속/재접속한 클라 등) 구독만 걸어두면 완료 이벤트를 영영 못 받으므로, 구독 직후 이걸로 직접 확인한다
	bool IsPropSpawnComplete() const { return bPropReplicatedComplete; }

	virtual void Tick(float DeltaTime) override;

	// 확률 계산부터 시작해서 최종적으로 소품들을 배치한다.
	// GridManager::OnMapGenComplete에 자기 자신을 바인딩해서 스스로 호출한다 (자기완결적).
	UFUNCTION(BlueprintCallable, Category = "PropSpawn")
	void PopulateProps();

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// ---- 진행률 복제 (WFCGridManager::GenProgressDone/Total/bReplicatedGenComplete와 동일한 목적) ----
	// PopulateProps()/Tick()은 HasAuthority()에서만 실행되므로, OnPropSpawnProgress/Complete를
	// 서버에서 직접 브로드캐스트만 해서는 클라이언트에 절대 전달되지 않는다.
	// 그래서 진행 상황 자체를 Replicated 프로퍼티로 만들어 값만 클라로 복제하고,
	// 클라에서 자동 호출되는 OnRep에서 로컬로 델리게이트를 브로드캐스트한다.
	UPROPERTY(ReplicatedUsing = OnRep_PropProgress)
	int32 PropProgressDone = 0;

	UPROPERTY(ReplicatedUsing = OnRep_PropProgress)
	int32 PropProgressTotal = 0;

	UPROPERTY(ReplicatedUsing = OnRep_PropComplete)
	bool bPropReplicatedComplete = false;

	UFUNCTION() void OnRep_PropProgress();
	UFUNCTION() void OnRep_PropComplete();

	// 진행률 갱신 + 서버 자신의 화면에도 즉시 반영 (OnRep은 클라에서만 자동 호출되므로 서버는 수동 호출)
	void UpdatePropProgress(int32 Done, int32 Total);
	void MarkPropComplete();


	// 확률 판정까지 끝나고 "실제로 스폰될 것"만 담기는 최종 리스트
	struct FResolvedPropSpawn
	{
		TWeakObjectPtr<UPropSpawnPoint> Point;
		TSubclassOf<AActor> ActorClass;
	};

	FRandomStream Rng;
	TArray<FResolvedPropSpawn> ToSpawn;
	int32 Cursor = 0;
	bool bIsSpawning = false;

	// 아직 안 쓴 Item/Exit 마커 풀. PopulateProps에서 한 번 섞어서 채워두고,
	// 단계가 넘어갈 때마다(HandleDiceStageCleared) 필요한 만큼씩 꺼내 씀
	TArray<UPropSpawnPoint*> PendingItemPoints;
	TArray<UPropSpawnPoint*> PendingExitPoints;

	// 스폰된 방/빅룸 액터 전체에서 UPropSpawnPoint를 모아 SlotType별로 분류
	void CollectSpawnPoints(TMap<EPropSlotType, TArray<UPropSpawnPoint*>>& OutPointsByType) const;

	// 그룹 제약(ExclusiveGroup)을 적용 - 같은 그룹이면 그 중 하나만 후보로 남기고 나머지는 제외
	TArray<UPropSpawnPoint*> ApplyExclusiveGroups(const TArray<UPropSpawnPoint*>& Points);

	// Rng를 이용한 Fisher-Yates 셔플 (Pending 풀에서 "무작위 위치"가 뽑히도록)
	void ShuffleArray(TArray<UPropSpawnPoint*>& Array);

	// Pool에서 최대 Count개를 꺼내 ActorClass로 ToSpawn에 편입
	void PopPendingPointsInto(TArray<UPropSpawnPoint*>& Pool, int32 Count, TSubclassOf<AActor> ActorClass);

	// GameState::OnDiceStageCleared 구독 콜백 - 이번 단계 클리어로 열려야 할 탈출구를 스폰하고,
	// 다음 단계가 남아있으면 그 몫의 다이스도 마저 스폰
	void HandleDiceStageCleared(int32 ExitsToSpawn);
};
