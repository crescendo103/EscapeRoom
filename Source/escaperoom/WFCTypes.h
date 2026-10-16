#pragma once

#include "CoreMinimal.h"
#include "WFCTypes.generated.h"

// 방의 문 방향
UENUM(BlueprintType)
enum class EDoorDir : uint8
{
	North UMETA(DisplayName = "North"),
	East  UMETA(DisplayName = "East"),
	South UMETA(DisplayName = "South"),
	West  UMETA(DisplayName = "West")
};

// 상하좌우 소켓 값. 0 = 문 없음(막힘). 0이 아닌 값은 서로 "같은 값"끼리만 연결 가능하다고 규칙을 둠.
// (모든 문을 그냥 통로로 취급할 거면 전부 1로 둬도 됨 - 문 있음/없음만 구분)
USTRUCT(BlueprintType)
struct FWFCSocketSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 North = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 East = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 South = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 West = 0;

	int32 Get(EDoorDir Dir) const
	{
		switch (Dir)
		{
		case EDoorDir::North: return North;
		case EDoorDir::East:  return East;
		case EDoorDir::South: return South;
		case EDoorDir::West:  return West;
		default: return 0;
		}
	}

	static EDoorDir Opposite(EDoorDir Dir)
	{
		switch (Dir)
		{
		case EDoorDir::North: return EDoorDir::South;
		case EDoorDir::East:  return EDoorDir::West;
		case EDoorDir::South: return EDoorDir::North;
		case EDoorDir::West:  return EDoorDir::East;
		default: return EDoorDir::North;
		}
	}

	static FIntPoint DirToOffset(EDoorDir Dir)
	{
		switch (Dir)
		{
		case EDoorDir::North: return FIntPoint(0, 1);
		case EDoorDir::East:  return FIntPoint(1, 0);
		case EDoorDir::South: return FIntPoint(0, -1);
		case EDoorDir::West:  return FIntPoint(-1, 0);
		default: return FIntPoint(0, 0);
		}
	}
};
