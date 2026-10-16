// PropSpawnPoint.h
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "PropSpawnPoint.generated.h"

class UArrowComponent;

// 이 지점에 어떤 종류의 소품이 스폰될 수 있는지 구분하는 태그.
// 방/빅룸 블루프린트에서 컴포넌트를 추가할 때 이 값을 지정해서 "여긴 캐비닛 자리",
// "여긴 아이템(다이스) 자리"처럼 표시해두면, PropSpawnManagerActor가 타입별로 걸러서 사용한다.
UENUM(BlueprintType)
enum class EPropSlotType : uint8
{
	Cabinet UMETA(DisplayName = "Cabinet"),
	Item    UMETA(DisplayName = "Item (Dice)"),
	Exit    UMETA(DisplayName = "Exit"),
	Generic UMETA(DisplayName = "Generic")
};

/**
 * 방/빅룸 블루프린트 안에 미리 배치해두는 "소품이 스폰될 수 있는 자리" 마커.
 *
 * - 디자이너가 룸 블루프린트를 만들 때, 캐비닛/아이템/Exit가 들어가도 자연스러운 위치
 *   (벽에 안 파묻히고 문 앞을 막지 않는 자리)에 이 컴포넌트를 자식으로 붙여서 배치해둔다.
 *   실제 게임플레이 액터(ACabinetActor 등)는 여기 직접 놓지 않는다 - 위치/방향 정보만 가진 빈 마커.
 * - 맵 생성이 끝나면 APropSpawnManagerActor가 스폰된 모든 방/빅룸 액터를 돌면서
 *   GetComponents<UPropSpawnPoint>()로 이 마커들을 찾아 SlotType별로 모으고,
 *   확률(SpawnChanceOverride 또는 매니저의 타입별 기본 확률)에 따라 실제 액터를 스폰한다.
 * - ARoomActor에 종속되지 않고 순수 AActor::GetComponents<>()로 찾으므로, 일반 룸이든
 *   빅룸(BigRoomClassesBySize에 등록된 아무 AActor 블루프린트)이든 동일하게 동작한다.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class ESCAPEROOM_API UPropSpawnPoint : public USceneComponent
{
	GENERATED_BODY()

public:
	UPropSpawnPoint();

#if WITH_EDITOR
	// 에디터(레벨/블루프린트 뷰포트)에서만 방향 화살표를 만들어 붙인다. 게임 월드에서는 생성 안 함.
	virtual void OnRegister() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
#endif

	// 이 자리에 어떤 종류의 소품이 스폰될 수 있는지
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PropSpawn")
	EPropSlotType SlotType = EPropSlotType::Generic;

	// 이 자리 전용 스폰 확률(0~1). 음수(기본값 -1)면 매니저의 타입별 기본 확률을 그대로 사용.
	// 특정 자리만 확률을 따로 조정하고 싶을 때(예: "이 캐비닛 자리는 항상 나오게" = 1.0)만 덮어쓰면 됨.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PropSpawn", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float SpawnChanceOverride = -1.f;

	// 같은 그룹 태그를 가진 자리들 중 최대 하나만 스폰 후보로 남기고 싶을 때 사용(선택 사항).
	// 예: 방 하나에 후보 자리가 3개 있는데 그 중 하나에만 캐비닛이 나오길 원하면 셋 다 같은 태그를 준다.
	// 비워두면(NAME_None) 그룹 제한 없이 각 자리가 독립적으로 확률 판정된다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PropSpawn")
	FName ExclusiveGroup = NAME_None;

private:
#if WITH_EDITORONLY_DATA
	// 스폰될 액터의 정면(+X)을 보여주는 에디터 전용 화살표. 저장/쿡 안 됨(Transient).
	// 캐비닛은 +X가 정면이므로, 화살표가 방 안쪽을 향하게 마커를 돌려두면 된다.
	UPROPERTY(Transient)
	TObjectPtr<UArrowComponent> DirectionArrow;
#endif
};
