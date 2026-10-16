#pragma once

#include "CoreMinimal.h"
#include "WFCTypes.h"
#include "WFCBigRoomTypes.generated.h"

// 큰방 액터 컨셉 후보 목록을 감싸는 래퍼.
// TMap의 Value로 TArray를 직접 못 넣기 때문에(언리얼 리플렉션 제약) 구조체로 한 번 감싼다.
USTRUCT()
struct FWFCBigRoomVariantSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	TArray<TSubclassOf<AActor>> Classes;
};

// 큰방(NxN 정사각형) 하나의 배치 정보.
// WFC 붕괴 루프를 돌기 전에 AWFCGridManager::PlaceBigRooms()가 먼저 채워 넣는다.
USTRUCT()
struct FWFCBigRoomPlacement
{
	GENERATED_BODY()

	// 좌하단(로컬 0,0) 기준 절대 격자 좌표
	UPROPERTY()
	FIntPoint Origin = FIntPoint::ZeroValue;

	// 한 변의 칸 수 (정사각형만 지원)
	UPROPERTY()
	int32 Size = 2;

	// 이 큰방이 차지하는 모든 절대 격자 좌표 (Size*Size개)
	UPROPERTY()
	TArray<FIntPoint> OccupiedCells;

	// 실제로 스폰된 큰방 액터 (SpawnBigRooms에서 채움)
	UPROPERTY()
	TObjectPtr<AActor> SpawnedActor = nullptr;
};