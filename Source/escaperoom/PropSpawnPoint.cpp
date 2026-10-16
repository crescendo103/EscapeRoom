// PropSpawnPoint.cpp
#include "PropSpawnPoint.h"

#if WITH_EDITOR
#include "Components/ArrowComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#endif

UPropSpawnPoint::UPropSpawnPoint()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Static으로 두면 부모(Room의 Root 등)가 Static이 아닐 때 부착 자체가 거부됨
	// ("AttachTo: ... is not static, cannot attach ... which is static to it" 에러의 원인).
	// 이 컴포넌트는 위치/방향만 읽어가는 마커라 굳이 Static일 필요가 없으므로 기본 Movable로 둔다.

#if WITH_EDITORONLY_DATA
	// 에디터 뷰포트에서 위치를 아이콘으로 표시해서 룸 블루프린트에 배치하기 쉽게 함
	bVisualizeComponent = true;
#endif
}

#if WITH_EDITOR
void UPropSpawnPoint::OnRegister()
{
	Super::OnRegister();

	AActor* MyOwner = GetOwner();
	UWorld* World = GetWorld();
	// 게임 월드(PIE/패키징)나 커맨드렛(쿡 등)에선 만들지 않음 - 순수 에디터 시각화용
	if (!MyOwner || !World || World->IsGameWorld() || IsRunningCommandlet())
	{
		return;
	}

	// UCameraComponent의 에디터 프록시 메시와 같은 패턴: 소유 액터에 임시 컴포넌트를 만들어 이 마커에 붙임
	if (!DirectionArrow)
	{
		DirectionArrow = NewObject<UArrowComponent>(MyOwner, NAME_None, RF_Transactional | RF_Transient | RF_TextExportTransient);
		DirectionArrow->SetupAttachment(this);
		DirectionArrow->SetIsVisualizationComponent(true);
		DirectionArrow->CreationMethod = CreationMethod;
		DirectionArrow->bHiddenInGame = true;
		DirectionArrow->bIsScreenSizeScaled = false;
		DirectionArrow->ArrowSize = 1.5f;
		DirectionArrow->ArrowLength = 100.f;
		DirectionArrow->SetCanEverAffectNavigation(false);
		DirectionArrow->RegisterComponentWithWorld(World);
	}

	// 슬롯 타입별로 색을 다르게 해서 한눈에 구분되게 함
	FLinearColor Color = FLinearColor::White;
	switch (SlotType)
	{
	case EPropSlotType::Cabinet: Color = FLinearColor(1.f, 0.45f, 0.f); break; // 주황
	case EPropSlotType::Item:    Color = FLinearColor::Yellow;          break;
	case EPropSlotType::Exit:    Color = FLinearColor::Green;           break;
	default: break;
	}
	DirectionArrow->SetArrowColor(Color);
}

void UPropSpawnPoint::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	if (DirectionArrow)
	{
		DirectionArrow->DestroyComponent();
		DirectionArrow = nullptr;
	}

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}
#endif
