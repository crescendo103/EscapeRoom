#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FogDriftComponent.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;

// 안개 뭉치 하나의 "처음 상태".
// 서버가 맵 생성 과정(WFCGridManager 4단계)에서 한 번 정해서 복제하고,
// 각 클라이언트는 이 값 + 서버 시간만으로 현재 위치를 직접 계산한다 (매 프레임 위치 전송 없음).
USTRUCT()
struct FFogPatchData
{
	GENERATED_BODY()

	// 처음 생성 위치 (XY, 월드 좌표). 실제 방이 있는 칸 위에서 시작한다
	UPROPERTY()
	FVector2D StartPos = FVector2D::ZeroVector;

	// Niagara User Parameter(BoxSizeParamName)로 넘길 스폰 박스 크기
	UPROPERTY()
	FVector Size = FVector(2000.f, 2000.f, 40.f);

	// 바람 방향으로의 이동 속도 (cm/s). 뭉치마다 달라서 시간이 지나면 자연스럽게 흩어짐
	UPROPERTY()
	float Speed = 30.f;
};

/**
 * 맵 전체에 흘러가는 바닥 안개.
 *
 * - 서버: WFCGridManager가 GenerateMap()에서 BuildPendingPatches()로 안개 뭉치 목록을 정하고,
 *         Tick 4단계(방 -> 큰방 -> 문 -> 안개)에서 CommitPendingPatch()로 몇 개씩 확정한다.
 *         이 확정 개수가 맵 생성 진행률(로딩바)에 포함된다.
 * - 클라: 복제된 Patches를 받으면 뭉치마다 Niagara 컴포넌트를 "로컬로" 만든다 (복제 안 함).
 *         데디서버는 그리지 않는다. 리슨서버 호스트는 그린다.
 * - 이동: 위치 = 시작위치 + 속도 * (서버시간 - FogStartServerTime).
 *         바람 축 위에서 [AlongMin, AlongMax] 구간(맵 끝 + 여유)을 넘으면 반대편 바깥으로 되돌린다.
 *         옆 방향(바람과 수직) 위치는 그대로 유지한다.
 *         모든 클라가 같은 서버 시간으로 같은 식을 계산하므로 안개 위치가 일치한다.
 *
 * NS_GroundFog 요구사항: Vector User Parameter(기본 이름 FogBoxSize)를 Shape Location의 Box Size에 연결,
 * Emitter Properties의 Local Space 켜기 (컴포넌트를 옮기면 안개 전체가 같이 이동해야 함).
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class ESCAPEROOM_API UFogDriftComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFogDriftComponent();

	// ---- 설정값 (WFC 매니저 BP 디테일 > FogDrift 컴포넌트) ----

	// 사용할 안개 Niagara 시스템 (NS_GroundFog). 비어 있으면 데이터만 만들고 그리지 않음
	UPROPERTY(EditAnywhere, Category = "Fog")
	TObjectPtr<UNiagaraSystem> FogSystem;

	// 맵에 깔 안개 뭉치 개수
	UPROPERTY(EditAnywhere, Category = "Fog", meta = (ClampMin = "0"))
	int32 NumPatches = 12;

	// 뭉치 하나의 가로/세로 크기 범위 (각 축 따로 랜덤)
	UPROPERTY(EditAnywhere, Category = "Fog", meta = (ClampMin = "100.0"))
	float PatchSizeMin = 1500.f;

	UPROPERTY(EditAnywhere, Category = "Fog", meta = (ClampMin = "100.0"))
	float PatchSizeMax = 3000.f;

	// 뭉치 스폰 박스 높이 (바닥에서 얼마나 두껍게 깔릴지)
	UPROPERTY(EditAnywhere, Category = "Fog", meta = (ClampMin = "1.0"))
	float PatchHeight = 40.f;

	// 이동 속도 범위 (cm/s)
	UPROPERTY(EditAnywhere, Category = "Fog", meta = (ClampMin = "0.0"))
	float SpeedMin = 20.f;

	UPROPERTY(EditAnywhere, Category = "Fog", meta = (ClampMin = "0.0"))
	float SpeedMax = 60.f;

	// 바람 방향 (XY). 길이는 상관없음 - 내부에서 정규화함
	UPROPERTY(EditAnywhere, Category = "Fog")
	FVector2D WindDirection = FVector2D(1.f, 0.f);

	// 맵 끝에서 이만큼 더 나가야 반대편 바깥으로 되돌림.
	// 뭉치가 완전히 맵 밖으로 나간 뒤에 옮기도록, 최대 크기의 절반보다 작으면 자동으로 늘림
	UPROPERTY(EditAnywhere, Category = "Fog", meta = (ClampMin = "0.0"))
	float WrapMargin = 1000.f;

	// WFC 매니저 Z(= 방 바닥 기준) + 이 값 = 안개 박스 중심 높이
	UPROPERTY(EditAnywhere, Category = "Fog")
	float HeightOffset = 20.f;

	// NS_GroundFog 안에 만든 Vector User Parameter 이름
	UPROPERTY(EditAnywhere, Category = "Fog")
	FName BoxSizeParamName = TEXT("FogBoxSize");

	// ---- 서버 전용 (WFCGridManager가 생성 과정에서 호출) ----

	// 방이 있는 칸들의 중심(XY)을 받아 안개 뭉치 목록과 되돌리기 구간을 정한다. 아직 복제하지 않음
	void BuildPendingPatches(const FRandomStream& Rng, const TArray<FVector2D>& RoomCenters, float RoomSize, float BaseZ);

	int32 GetPendingPatchCount() const { return PendingPatches.Num(); }

	// Index번째 뭉치를 확정해서 복제 배열에 넣는다 (진행률 1칸)
	void CommitPendingPatch(int32 Index);

	// 맵을 다시 만들 때 등 - 데이터와 로컬 Niagara 컴포넌트를 전부 지움
	void ClearPatches();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// 확정된 안개 뭉치들 (서버 -> 모든 클라)
	UPROPERTY(ReplicatedUsing = OnRep_Patches)
	TArray<FFogPatchData> Patches;

	// ---- 이동 규칙 (서버가 BuildPendingPatches에서 한 번 정해서 복제) ----
	UPROPERTY(Replicated)
	FVector2D ReplicatedWindDir = FVector2D(1.f, 0.f);

	// 바람 축(ReplicatedWindDir) 위의 되돌리기 구간 (맵 끝 + 여유 포함)
	UPROPERTY(Replicated)
	double AlongMin = 0.0;

	UPROPERTY(Replicated)
	double AlongMax = 0.0;

	UPROPERTY(Replicated)
	float FogBaseZ = 0.f;

	// 이동 계산의 기준 시각 (서버 시간)
	UPROPERTY(Replicated)
	double FogStartServerTime = 0.0;

	// 서버 전용: 아직 확정 안 된 뭉치들
	TArray<FFogPatchData> PendingPatches;

	// 이 머신에서 로컬로 만든 Niagara 컴포넌트들 (Patches와 인덱스 대응, 복제 안 함)
	UPROPERTY(Transient)
	TArray<TObjectPtr<UNiagaraComponent>> FogFx;

	UFUNCTION()
	void OnRep_Patches();

	bool ShouldRender() const;
	void SyncFxComponents();
	void DestroyFxComponents();
	double GetElapsedSeconds() const;
	FVector ComputePatchLocation(const FFogPatchData& Patch, double Elapsed) const;
};
