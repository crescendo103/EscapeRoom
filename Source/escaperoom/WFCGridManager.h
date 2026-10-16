#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WFCTypes.h"
#include "WFCBigRoomTypes.h"
#include "WFCGridManager.generated.h"

class UWFCTileDefinition;
class ARoomActor;
class UFogDriftComponent;

// ���� �� ĭ�� ����. �ĺ� ����Ʈ(��Ʈ����) + Ȯ���� Ÿ��
USTRUCT()
struct FWFCCell
{
	GENERATED_BODY()
	UPROPERTY()
	TArray<TObjectPtr<UWFCTileDefinition>> Candidates;
	UPROPERTY()
	TObjectPtr<UWFCTileDefinition> CollapsedTile = nullptr;
	bool IsCollapsed() const { return CollapsedTile != nullptr; }
	int32 Entropy() const { return Candidates.Num(); }
};



UCLASS()
class AWFCGridManager : public AActor
{
	GENERATED_BODY()
public:
	AWFCGridManager();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;


	// �� ���� �Ϸ� ���θ� ��ȯ�ϴ� Getter �Լ� �߰�
	UFUNCTION(BlueprintCallable, Category = "WFC")
	bool IsMapGenerated() const { return bHasGenerated; }

	// PropSpawnManagerActor 등 외부에서 스폰된 방/빅룸 액터 목록을 읽을 수 있게 열어주는 getter.
	// (Cells와 인덱스 대응되는 SpawnedRooms는 nullptr/prune된 셀을 포함할 수 있음 - 호출부에서 null 체크 필요)
	const TArray<TObjectPtr<ARoomActor>>& GetSpawnedRooms() const { return SpawnedRooms; }

	// RandomSeed + 방 인덱스로 방별 램프 시드 생성 (같은 시드면 항상 같은 결과)
	int32 MakeRoomLampSeed(int32 Index) const;
	const TArray<TObjectPtr<AActor>>& GetSpawnedBigRoomActors() const { return SpawnedBigRoomActors; }

	// ���� ���� �� �������� �ڵ����� ���� �������� ����
	UPROPERTY(EditAnywhere, Category = "WFC")
	bool bGenerateMapOnBeginPlay = true;

	// ���簢�� ���� �� n*n ����. 10*10 = �� 100��
	UPROPERTY(EditAnywhere, Category = "WFC")
	int32 GridSize = 10;

	// �� �ϳ�(���簢��)�� ���� ���� �� �� ����. ���� ����� �� �������� ��ġ�Ǿ� ���� �´�´�.
	UPROPERTY(EditAnywhere, Category = "WFC")
	float RoomSize = 1000.f;

	// ����� Ÿ��(�� ����) ���. ������ ���������� ���� UWFCTileDefinition �ּµ��� ���
	UPROPERTY(EditAnywhere, Category = "WFC")
	TArray<TObjectPtr<UWFCTileDefinition>> AllTiles;

	// ���� �õ尪. ���� ���̸� �׻� ���� ���� ����
	UPROPERTY(EditAnywhere, Category = "WFC")
	int32 RandomSeed = 0;

	// �ѵθ� �� �� ���� ������ǥ/Ÿ���̸�/���ϰ��� ���� �ؽ�Ʈ�� ǥ�� (������)
	UPROPERTY(EditAnywhere, Category = "WFC|Debug")
	bool bShowDebugLabels = true;

	// ---- ū��(BigRoom) ���� ----

	// ��ġ�� �õ��� ū�� ���� (�ڸ��� �����ϸ� �׺��� ���� ��ġ�� �� ����).
	// BigRoomClassesBySize�� ����� ���� ����(��: 3��)�� ����θ� ��� ������ �� ���� ������.
	UPROPERTY(EditAnywhere, Category = "WFC|BigRoom")
	int32 NumBigRooms = 3;

	// ū�� �� ���� ĭ �� ���� (���簢���� ����).
	// �⺻���� 5x5�� �����ص� - RoomSize(���� �� �� ĭ ũ��) �������� ���͸� 5�� ũ��� ���� ������ �����̶�.
	UPROPERTY(EditAnywhere, Category = "WFC|BigRoom")
	int32 BigRoomMinSize = 5;

	UPROPERTY(EditAnywhere, Category = "WFC|BigRoom")
	int32 BigRoomMaxSize = 5;

	// ū����, ���� �ٱ��� �� �´��� �� ���� �� �� ���� ����
	UPROPERTY(EditAnywhere, Category = "WFC|BigRoom")
	int32 MinDoorsPerSide = 1;

	UPROPERTY(EditAnywhere, Category = "WFC|BigRoom")
	int32 MaxDoorsPerSide = 1;

	UPROPERTY(EditDefaultsOnly, Category = "WFC|Spawn Pacing")
	int32 SpawnsPerTick = 3;      // �� ���� ����/������

	UPROPERTY(EditDefaultsOnly, Category = "WFC|Spawn Pacing")
	int32 BigRoomsPerTick = 3;    // ū�� ���� ����/������

	UPROPERTY(EditDefaultsOnly, Category = "WFC|Spawn Pacing")
	int32 DoorsPerTick = 3;       // �� ���� ����/������

	UPROPERTY(EditDefaultsOnly, Category = "WFC|Spawn Pacing", meta = (ClampMin = "1"))
	int32 FogPatchesPerTick = 2;  // 안개 뭉치 확정 개수/프레임 (4단계)

	// ---- 안개(Fog) ----
	// 맵 전체에 흘러가는 바닥 안개. 설정값(Niagara 에셋, 개수, 크기, 속도, 바람 방향 등)은 이 컴포넌트 디테일에서 조절.
	// 생성 4단계(방 -> 큰방 -> 문 -> 안개)로 진행되며 진행률(로딩바)에 포함된다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC|Fog")
	TObjectPtr<UFogDriftComponent> FogDrift;

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMapGenProgress, int32, int32);
	FOnMapGenProgress OnMapGenProgress;

	DECLARE_MULTICAST_DELEGATE(FOnMapGenComplete);
	FOnMapGenComplete OnMapGenComplete;

	// ū�� ���� ����� ���� ��. AllTiles�� �ִ� �Ϲ� ����� "�� ����" ���ϰ��� �ݵ�� ���ƾ�
	// ���� �̾����ٰ� �νĵ� (�ٸ��� ���պο��� ����� ���� �� ĭ�� ������ �� �ر��� �� ����)
	UPROPERTY(EditAnywhere, Category = "WFC|BigRoom")
	int32 BigRoomDoorSocketValue = 1;

	// ũ��(�� �� ĭ ��)���� ������ ū�� ���� �ĺ� ���.
	// ��ġ �ϳ��� Ȯ���� ������ �� ����� ������� ���ư��� �����Ѵ� (������ ���� ���� �� ���).
	// ��: 5 -> Classes = [BP_BigRoom_Farm, BP_BigRoom_Ruins, BP_BigRoom_Camp]
	UPROPERTY(EditAnywhere, Category = "WFC|BigRoom")
	TMap<int32, FWFCBigRoomVariantSet> BigRoomClassesBySize;

	// ---- ��(Door) ���� ----
	// �� ���� ��ü���� �� �޽ø� ���� �ʰ�, �� ���� ������ �̾��� ��踶��
	// �Ŵ����� �� ���͸� Ȯ�������� �����Ѵ� (�ߺ� ���� + ���/�� ���� ��ġ).

	UPROPERTY(EditAnywhere, Category = "WFC|Door")
	TSubclassOf<AActor> DoorActorClass;

	// �̾��� ��� �ϳ��� ������ �� ���͸� ������ Ȯ�� (0~1). �� ������ �׳� �ո� ��η� ����.
	UPROPERTY(EditAnywhere, Category = "WFC|Door", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DoorSpawnChance = 0.5f;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "WFC")
	void GenerateMap();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "WFC")
	void ClearMap();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "WFC")
	void TryGenerateMap();

	// Delay (seconds) after BeginPlay before generation actually starts.
	// Mitigates a timing overlap between WorldPartition streaming/client join right after level travel and the burst of dynamic spawning.
	UPROPERTY(EditAnywhere, Category = "WFC")
	float InitialGenerateDelay = 1.0f;

	// [HANG DIAG] When true, the Tick spawn loop advances its cursors and logs progress
	// but never calls GetWorld()->SpawnActor(...). Used to bisect whether the freeze
	// lives inside actual actor spawn/construction (meshes, components, GC) or elsewhere.
	UPROPERTY(EditAnywhere, Category = "WFC|Debug")
	bool bDebugSkipActorSpawn = false;

private:
	UPROPERTY()
	TArray<FWFCCell> Cells;

	UPROPERTY()
	TArray<TObjectPtr<ARoomActor>> SpawnedRooms; // Cells�� ���� �ε���

	UPROPERTY()
	TArray<FWFCBigRoomPlacement> BigRoomPlacements;

	// ū�� ĭ�� ���� ��Ÿ�� ���� Ÿ�� ���� (GC ������ ���� ���⼭ ���� ����)
	UPROPERTY()
	TArray<TObjectPtr<UWFCTileDefinition>> TransientBigRoomTiles;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> SpawnedBigRoomActors;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> SpawnedDoorActors;

	FRandomStream Rng;

	int32 IndexOf(int32 X, int32 Y) const { return Y * GridSize + X; }
	bool CoordOf(int32 Index, int32& OutX, int32& OutY) const;
	bool IsValidCoord(int32 X, int32 Y) const;

	void InitCells();

	// ���� �ر� �� �� ĭ���� ���� �Ⱦ ��Ʈ����(�ĺ� ��)�� ���� ���� ĭ�� ã�´�.
	// �����̸� ���� �������� �ϳ�. ���� ĭ�� ������ -1.
	int32 PickLowestEntropyCell() const;

	// �ĺ��� �� ���� �������� �ϳ� �̾� Ȯ��
	void CollapseCell(int32 Index);

	// index ĭ�� �� Ȯ��/���ŵ��� �� �̿� �ĺ��� ���̰�, ���������� ��� ���� (BFS)
	void PropagateFrom(int32 StartIndex);

	// A Ÿ���� Dir ���� ������ B Ÿ�ϰ� �¹��� �� �ִ���
	bool SocketsMatch(const UWFCTileDefinition* A, EDoorDir DirFromA, const UWFCTileDefinition* B) const;

	// Ȯ���� ��� ��, ������ ������ �¹��� ���� �̾��� ��鳢�� BFS�� �׷��� ������
	// ���� ū �׷�(���� ����)�� ������ ���� ������ ���� CollapsedTile�� ����� ���� ��󿡼� �����Ѵ�.
	void PruneDisconnectedRooms();	

	void CapDanglingDoors();
	UWFCTileDefinition* FindTileWithClosedDir(const UWFCTileDefinition* Source, EDoorDir DirToClose) const;

	// ---- ū�� ���� ----

	// WFC �ر� ���� ���� ����, ū����� ��ġ�� �������� ���Ѵ� (��ħ ����).
	void PlaceBigRooms();

	// PlaceBigRooms�� ������ �� ū�濡 ����, ������ ��/���� ���ϰ�
	// �� ĭ���� �̹� Ȯ����(CollapsedTile ����) ���·� ���� �� �ٱ� �̿��� �����Ѵ�.
	void SeedBigRoomCells();	

	int32 SpawnCursor = 0;
	int32 BigRoomSpawnCursor = 0;
	int32 DoorSpawnCursor = 0;

	bool bIsSpawningRooms = false;
	bool bIsSpawningBigRooms = false;
	bool bIsSpawningDoors = false;
	bool bIsSpawningFog = false;
	int32 FogSpawnCursor = 0;

	// 방이 있는 칸들의 중심을 모아 FogDrift에 넘겨 안개 뭉치 목록을 만든다 (서버, GenerateMap 안에서)
	void BuildFogPatches();

	// 진행률 계산용 - 안개 단계 전체 작업 수
	int32 GetFogWorkTotal() const;

	// Timer handle + callback for the InitialGenerateDelay delay.
	FTimerHandle TimerHandle_DelayedGenerate;
	void DelayedTryGenerateMap();

	TArray<FTransform> PendingDoorTransforms; // �� ������ �̸� ������, ������ �����Ӻ��� ������ ��

	void SpawnOneRoomCell(int32 i);
	void SpawnOneBigRoom(int32 i);
	void SpawnOneDoor(int32 i);
	void BuildPendingDoors(); // ���� SpawnDoors()�� ���� ������ ��� ����Ʈ�� �׾Ƶ�

	protected://���൵ ���� �Լ�
		UPROPERTY(ReplicatedUsing = OnRep_GenProgress)
		int32 GenProgressDone = 0;

		UPROPERTY(ReplicatedUsing = OnRep_GenProgress)
		int32 GenProgressTotal = 0;

		UPROPERTY(ReplicatedUsing = OnRep_GenComplete)
		bool bReplicatedGenComplete = false;

		UFUNCTION() void OnRep_GenProgress();
		UFUNCTION() void OnRep_GenComplete();

		virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

		void AssignFolder(AActor* Actor);

		// �� ������ �Ϸ�Ǿ����� ����ϴ� �÷���
		UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WFC")
		bool bHasGenerated = false;
};