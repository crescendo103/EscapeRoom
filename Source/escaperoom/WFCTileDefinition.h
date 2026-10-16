#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WFCTypes.h"
#include "WFCTileDefinition.generated.h"

class ARoomActor;

// 방 "종류" 하나를 정의하는 데이터 애셋.
// 유니티 프리팹처럼, 콘텐츠 브라우저에서 이 애셋을 여러 개 만들어 타일 세트를 구성한다.
UCLASS(BlueprintType)
class UWFCTileDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	// 스폰할 실제 Room 블루프린트 클래스
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC")
	TSubclassOf<ARoomActor> RoomClass;

	// 상하좌우 소켓 값
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC")
	FWFCSocketSet Sockets;

	// 에디터 식별용 이름표
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC")
	FName TileName;
};
