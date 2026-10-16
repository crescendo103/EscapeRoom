#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoomActor.generated.h"

// 정사각형 방 액터.
// 어느 변이 뚫려있는지는 WFC가 고른 UWFCTileDefinition의 Sockets 값으로 이미 정해지고,
// 뚫린 변이 있다면 그 변의 정중앙이 뚫려있다고 간주한다 (문 위치가 고정이라 별도 마커/복도 액터 불필요).
// 블루프린트 자식 클래스에서 MeshRoot 아래에 그 소켓 조합에 맞는 StaticMesh를 조립하면 된다.
UCLASS(Blueprintable)
class ARoomActor : public AActor
{
	GENERATED_BODY()

public:
	ARoomActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC")
	TObjectPtr<USceneComponent> Root;

	// 이 아래에 StaticMeshComponent들을 자식으로 붙여서 방을 조립 (블루프린트 에디터에서 작업)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC")
	TObjectPtr<USceneComponent> MeshRoot;

	// [서버] 생성기가 스폰 직후(FinishSpawning 전) 호출. 이 시드로 방 안 차일드 램프 on/off가 결정됨
	void SetLampSeed(int32 InSeed);

	// 방 안의 ChildActor 램프 하나하나가 꺼질 확률 (0~1)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lamp", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LampOffChance = 0.7f;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 서버가 정한 램프 시드. 클라엔 첫 리플리케이션으로 BeginPlay 전에 도착함
	UPROPERTY(ReplicatedUsing = OnRep_LampSeed)
	int32 LampSeed = 0;

	// 생성기를 거쳐 시드를 받은 방인지 (레벨에 직접 놓은 방은 false -> 램프 그대로 둠)
	UPROPERTY(Replicated)
	bool bHasLampSeed = false;

	UFUNCTION()
	void OnRep_LampSeed();

	// LampSeed로 차일드 램프들을 굴려서 끌 것들을 끔 (한 번만 실행)
	void ApplyLampState();

private:
	bool bLampStateApplied = false;
};
