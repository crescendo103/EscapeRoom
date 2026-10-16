// PropSpawnManagerActor.cpp
#include "PropSpawnManagerActor.h"
#include "WFCGridManager.h"
#include "RoomActor.h"
#include "CabinetActor.h"
#include "DiceActor.h"
#include "ExitActor.h"
#include "EngineUtils.h"
#include "EscapeRoomGameState.h"
#include <Net/UnrealNetwork.h>

APropSpawnManagerActor::APropSpawnManagerActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false; // PopulateProps가 결정 단계까지 끝낸 뒤에만 켠다
	bReplicates = true; // PropProgressDone/Total/bPropReplicatedComplete를 클라로 복제하기 위해 필요
}

void APropSpawnManagerActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APropSpawnManagerActor, PropProgressDone);
	DOREPLIFETIME(APropSpawnManagerActor, PropProgressTotal);
	DOREPLIFETIME(APropSpawnManagerActor, bPropReplicatedComplete);
}

// 진행률 갱신 + 서버 자신의 화면에도 즉시 반영 (OnRep은 원래 클라에서만 자동 호출되므로,
// 서버 자신은 값 대입 후 수동으로 OnRep을 불러줘야 함 - CabinetActor/DoorActor와 동일한 패턴)
void APropSpawnManagerActor::UpdatePropProgress(int32 Done, int32 Total)
{
	PropProgressDone = Done;
	PropProgressTotal = Total;
	OnRep_PropProgress();
}

void APropSpawnManagerActor::MarkPropComplete()
{
	bPropReplicatedComplete = true;
	OnRep_PropComplete();

	// 구조 + 소품 생성이 전부 끝난 순간 = 매치 시작. 공용 게임 시계를 여기서 시작 (서버 전용, 중복 호출 무시)
	if (HasAuthority())
	{
		if (AEscapeRoomGameState* GS = GetWorld()->GetGameState<AEscapeRoomGameState>())
		{
			GS->StartMatchClock();
		}
	}
}

void APropSpawnManagerActor::OnRep_PropProgress()
{
	OnPropSpawnProgress.Broadcast(PropProgressDone, PropProgressTotal);
}

void APropSpawnManagerActor::OnRep_PropComplete()
{
	if (bPropReplicatedComplete)
	{
		OnPropSpawnComplete.Broadcast();
	}
}

void APropSpawnManagerActor::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return; // 스폰은 WFCGridManager와 동일하게 서버 권한에서만
	}

	if (!GridManager)
	{
		for (TActorIterator<AWFCGridManager> It(GetWorld()); It; ++It)
		{
			GridManager = *It;
			break;
		}
	}

	if (!GridManager)
	{
		UE_LOG(LogTemp, Error, TEXT("[PropSpawn] No AWFCGridManager found in level!"));
		return;
	}

	// WFCGridManager와 동일하게 자기완결적으로 동작한다 - 외부(ASpawnManagerActor,
	// ADungeonPlayerController 등)가 PopulateProps()를 직접 호출해주지 않아도,
	// 구조 생성이 끝나는 즉시 스스로 소품 배치를 시작한다. 외부 쪽은 진행률만 구독하면 됨.
	GridManager->OnMapGenComplete.AddUObject(this, &APropSpawnManagerActor::PopulateProps);

	// 다이스 단계가 클리어될 때마다(매치당 여러 번) 탈출구/다음 다이스를 마저 스폰하기 위해 구독
	if (AEscapeRoomGameState* GS = GetWorld()->GetGameState<AEscapeRoomGameState>())
	{
		GS->OnDiceStageCleared.AddUObject(this, &APropSpawnManagerActor::HandleDiceStageCleared);
	}
}

void APropSpawnManagerActor::CollectSpawnPoints(TMap<EPropSlotType, TArray<UPropSpawnPoint*>>& OutPointsByType) const
{
	OutPointsByType.Reset();
	if (!GridManager) return;

	auto ScanActor = [&OutPointsByType](AActor* Actor)
	{
		if (!Actor) return;

		TArray<UPropSpawnPoint*> Found;
		Actor->GetComponents<UPropSpawnPoint>(Found);

		for (UPropSpawnPoint* Point : Found)
		{
			if (Point)
			{
				OutPointsByType.FindOrAdd(Point->SlotType).Add(Point);
			}
		}
	};

	// 일반 룸(ARoomActor)과 빅룸(AActor) 둘 다 순회 - GetComponents<>()는 AActor 기준이라 타입 구분 불필요
	for (const TObjectPtr<ARoomActor>& Room : GridManager->GetSpawnedRooms())
	{
		ScanActor(Room);
	}
	for (const TObjectPtr<AActor>& BigRoom : GridManager->GetSpawnedBigRoomActors())
	{
		ScanActor(BigRoom);
	}
}

TArray<UPropSpawnPoint*> APropSpawnManagerActor::ApplyExclusiveGroups(const TArray<UPropSpawnPoint*>& Points)
{
	TArray<UPropSpawnPoint*> Result;
	TMap<FName, TArray<UPropSpawnPoint*>> Groups;

	for (UPropSpawnPoint* Point : Points)
	{
		if (!Point) continue;

		if (Point->ExclusiveGroup == NAME_None)
		{
			Result.Add(Point); // 그룹 없음 - 그대로 후보에 포함
		}
		else
		{
			Groups.FindOrAdd(Point->ExclusiveGroup).Add(Point);
		}
	}

	for (auto& Pair : Groups)
	{
		TArray<UPropSpawnPoint*>& GroupPoints = Pair.Value;
		if (GroupPoints.Num() == 0) continue;

		const int32 Pick = Rng.RandRange(0, GroupPoints.Num() - 1);
		Result.Add(GroupPoints[Pick]); // 그룹당 하나만 후보로 승격, 나머지는 버림
	}

	return Result;
}

void APropSpawnManagerActor::ShuffleArray(TArray<UPropSpawnPoint*>& Array)
{
	for (int32 i = Array.Num() - 1; i > 0; --i)
	{
		const int32 j = Rng.RandRange(0, i);
		Array.Swap(i, j);
	}
}

void APropSpawnManagerActor::PopPendingPointsInto(TArray<UPropSpawnPoint*>& Pool, int32 Count, TSubclassOf<AActor> ActorClass)
{
	if (!ActorClass) return;

	for (int32 i = 0; i < Count && Pool.Num() > 0; ++i)
	{
		UPropSpawnPoint* Point = Pool.Pop();
		if (Point)
		{
			ToSpawn.Add({ Point, ActorClass });
		}
	}
}

void APropSpawnManagerActor::PopulateProps()
{
	if (!HasAuthority()) return;

	// 맵 시드에서 파생한 시드 사용 (예전엔 FMath::Rand()라 같은 시드로도 소품 배치는 재현 불가였음).
	// GridManager->RandomSeed는 GenerateMap에서 GameState->MapSeed로 이미 맞춰진 값.
	// 구조 생성과 똑같은 난수열을 쓰지 않도록 고정 상수('PROP')를 섞는다
	const int32 BaseSeed = GridManager ? GridManager->RandomSeed : 0;
	const int32 PropSeed = static_cast<int32>(HashCombine(GetTypeHash(BaseSeed), GetTypeHash(0x50524F50)));
	Rng = FRandomStream(PropSeed);
	UE_LOG(LogTemp, Log, TEXT("[PropSpawn] MapSeed=%d -> PropSeed=%d"), BaseSeed, PropSeed);
	ToSpawn.Reset();
	Cursor = 0;
	PendingItemPoints.Reset();
	PendingExitPoints.Reset();

	// 1) 방/빅룸 전체에서 마커 수집
	TMap<EPropSlotType, TArray<UPropSpawnPoint*>> ByType;
	CollectSpawnPoints(ByType);

	// 2) 캐비닛은 기존과 동일하게 확률로 즉시 스폰 목록에 편입
	const TArray<UPropSpawnPoint*> CabinetCandidates = ApplyExclusiveGroups(ByType.FindOrAdd(EPropSlotType::Cabinet));
	for (UPropSpawnPoint* Point : CabinetCandidates)
	{
		if (!Point || !CabinetClass) continue;
		const float Chance = (Point->SpawnChanceOverride >= 0.f) ? Point->SpawnChanceOverride : DefaultCabinetChance;
		if (Rng.FRand() <= Chance)
		{
			ToSpawn.Add({ Point, CabinetClass });
		}
	}

	// 3) [단계형 시스템] 다이스(Item)/탈출구(Exit)는 확률로 뿌리지 않고 전부 보류 풀에 담아둔다.
	//    실제로 몇 개씩 꺼내 쓸지는 GameState->DiceStages(도망자 수에 맞춰 GameMode가 미리 채워둔 표)가 결정.
	PendingItemPoints = ApplyExclusiveGroups(ByType.FindOrAdd(EPropSlotType::Item));
	PendingExitPoints = ApplyExclusiveGroups(ByType.FindOrAdd(EPropSlotType::Exit));
	ShuffleArray(PendingItemPoints);
	ShuffleArray(PendingExitPoints);

	// 4) 1단계 몫의 다이스만 지금 바로 스폰 목록에 편입 (탈출구는 1단계 클리어 전까지는 아직 없음)
	if (AEscapeRoomGameState* GS = GetWorld() ? GetWorld()->GetGameState<AEscapeRoomGameState>() : nullptr)
	{
		if (GS->DiceStages.IsValidIndex(0))
		{
			PopPendingPointsInto(PendingItemPoints, GS->DiceStages[0].DiceRequired, DiceClass);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[PropSpawn] GameState->DiceStages가 비어있음 - GameMode의 DiceStageTable/RunnerCount 설정을 확인할 것"));
		}
	}

	// 5) 이제 Total(ToSpawn.Num())이 확정됐으니 여기서부터 진행률을 보고하기 시작한다.
	//    (Broadcast 직접 호출 대신 Replicated 프로퍼티를 갱신 - 클라에도 전달되어야 하므로)
	bIsSpawning = true;
	SetActorTickEnabled(true);
	UpdatePropProgress(0, ToSpawn.Num());

	if (ToSpawn.Num() == 0)
	{
		// 스폰할 게 아예 없으면 Tick을 기다릴 필요 없이 바로 완료 처리
		bIsSpawning = false;
		SetActorTickEnabled(false);
		MarkPropComplete();
	}
}

void APropSpawnManagerActor::HandleDiceStageCleared(int32 ExitsToSpawn)
{
	if (!HasAuthority()) return;

	// 이번 단계 클리어로 열리는 탈출구를 보류 풀에서 꺼내 스폰 목록에 편입
	PopPendingPointsInto(PendingExitPoints, ExitsToSpawn, ExitClass);

	// 다음 단계가 남아있으면 그 몫의 다이스도 마저 꺼내 스폰 목록에 편입
	if (AEscapeRoomGameState* GS = GetWorld() ? GetWorld()->GetGameState<AEscapeRoomGameState>() : nullptr)
	{
		if (GS->DiceStages.IsValidIndex(GS->CurrentDiceStageIndex))
		{
			PopPendingPointsInto(PendingItemPoints, GS->DiceStages[GS->CurrentDiceStageIndex].DiceRequired, DiceClass);
		}
	}

	// 이미 스폰이 다 끝나 Tick이 꺼져있었을 수 있으니 다시 켜고, 늘어난 Total을 반영해서 진행률 갱신
	bIsSpawning = true;
	SetActorTickEnabled(true);
	UpdatePropProgress(Cursor, ToSpawn.Num());
}

void APropSpawnManagerActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bIsSpawning) return;

	// 결정은 이미 끝났으니 여기서는 순수하게 스폰만 페이싱해서 처리
	int32 SpawnedThisTick = 0;
	while (Cursor < ToSpawn.Num() && SpawnedThisTick < PropsPerTick)
	{
		const FResolvedPropSpawn& Entry = ToSpawn[Cursor];

		if (UPropSpawnPoint* Point = Entry.Point.Get())
		{
			// 캐비닛(APawn 기반) 등은 클래스 기본 콜리전 정책이 "겹치면 스폰 안 함"으로 돼있어서,
			// 마커가 벽/바닥에 살짝 걸쳐만 있어도 SpawnActor가 조용히 실패할 수 있다
			// ("SpawnActor failed because of collision at the spawn location" 로그).
			// 가능하면 안 겹치는 위치로 살짝 밀어서라도 반드시 스폰되게 강제한다.
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

			AActor* Spawned = GetWorld()->SpawnActor<AActor>(Entry.ActorClass, Point->GetComponentTransform(), SpawnParams);

			// 캐비넷은 PeekCamera가 있는 쪽을 앞면으로 보고, 그쪽이 마커 화살표(+X)를 향하게 액터를 통째로 돌린다.
			// (스폰과 같은 프레임에서 돌리므로 클라에는 최종 회전값으로 리플리케이트됨)
			if (ACabinetActor* Cabinet = Cast<ACabinetActor>(Spawned))
			{
				const float FrontYaw = Cabinet->GetFrontYawLocal();
				if (!FMath::IsNearlyZero(FrontYaw))
				{
					const FQuat Corrected = Point->GetComponentQuat() * FQuat(FRotator(0.f, -FrontYaw, 0.f));
					Cabinet->SetActorRotation(Corrected);
				}
			}
		}

		Cursor++;
		SpawnedThisTick++;
	}

	UpdatePropProgress(Cursor, ToSpawn.Num());

	if (Cursor >= ToSpawn.Num())
	{
		bIsSpawning = false;
		SetActorTickEnabled(false);
		MarkPropComplete();
	}
}
