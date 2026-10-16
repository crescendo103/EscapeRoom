#include "WFCGridManager.h"
#include "escaperoomGameMode.h"
#include "EscapeRoomSessionSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "WFCTileDefinition.h"
#include "RoomActor.h"
#include "FogDriftComponent.h"
#include "Containers/Queue.h"
#include "DrawDebugHelpers.h"
#include <Net/UnrealNetwork.h>
#include "EscapeRoomGameState.h"

namespace
{
	// Å«¹æ ¾È¿¡¼­ Æ¯Á¤ º¯(Side)¿¡ ¼ÓÇÑ ·ÎÄÃ ÁÂÇ¥µéÀ» ¼ø¼­´ë·Î ¹ÝÈ¯.
	// EDoorDir::North = +Y ¹æÇâÀÌ¹Ç·Î, "ºÏÂÊ º¯"Àº ·ÎÄÃ Y°¡ ÃÖ´ñ°ªÀÎ ÁÙÀÌ´Ù.
	TArray<FIntPoint> GetBigRoomSideLocalCells(EDoorDir Side, int32 Size)
	{
		TArray<FIntPoint> LocalCells;
		LocalCells.Reserve(Size);
		switch (Side)
		{
		case EDoorDir::North:
			for (int32 X = 0; X < Size; ++X) LocalCells.Add(FIntPoint(X, Size - 1));
			break;
		case EDoorDir::South:
			for (int32 X = 0; X < Size; ++X) LocalCells.Add(FIntPoint(X, 0));
			break;
		case EDoorDir::East:
			for (int32 Y = 0; Y < Size; ++Y) LocalCells.Add(FIntPoint(Size - 1, Y));
			break;
		case EDoorDir::West:
			for (int32 Y = 0; Y < Size; ++Y) LocalCells.Add(FIntPoint(0, Y));
			break;
		}
		return LocalCells;
	}

	// ÀÌ º¯ÀÌ ÀüÃ¼ °ÝÀÚÀÇ ¹Ù±ù °æ°è¿Í ¸Â´ê¾Æ ÀÖ¾î¼­ ¹®À» ³¾ ¼ö ¾ø´ÂÁö
	bool IsBigRoomSideOnGridBoundary(EDoorDir Side, const FIntPoint& Origin, int32 Size, int32 GridSize)
	{
		switch (Side)
		{
		case EDoorDir::North: return Origin.Y + Size >= GridSize;
		case EDoorDir::South: return Origin.Y <= 0;
		case EDoorDir::East:  return Origin.X + Size >= GridSize;
		case EDoorDir::West:  return Origin.X <= 0;
		default: return true;
		}
	}

	// °°Àº Å«¹æ ¾ÈÀÇ ³»ºÎ Á¢ÇÕ¸é(Ä­°ú Ä­ »çÀÌ, º® ¾øÀÌ ¶Õ¸° ÇÑ ¹æ)¿¡ ¾²´Â ¿¹¾à°ª.
	// AllTiles¿¡¼­ ½ÇÁ¦·Î ¾²´Â ¼ÒÄÏ °ª°ú Àý´ë °ãÄ¡Áö ¾Êµµ·Ï int32 ÃÖ´ñ°ªÀ» »ç¿ëÇÑ´Ù.
	constexpr int32 BigRoomInternalSocketValue = TNumericLimits<int32>::Max();
}

AWFCGridManager::AWFCGridManager()
{
	PrimaryActorTick.bCanEverTick = true;        // false ¡æ true
	PrimaryActorTick.bStartWithTickEnabled = true; // Æò¼Ò¿£ ²¨µÒ
	bReplicates = true;

	// »ý¼º ÁøÇà·ü/¿Ï·á/¾È°³ µ¥ÀÌÅÍ°¡ ÇÃ·¹ÀÌ¾î À§Ä¡¿Í »ó°ü¾øÀÌ ¸ðµç Å¬¶ó¿¡ °¡¾ß ÇÏ¹Ç·Î Ç×»ó relevant
	bAlwaysRelevant = true;

	FogDrift = CreateDefaultSubobject<UFogDriftComponent>(TEXT("FogDrift"));
}

void AWFCGridManager::BeginPlay()
{	
	Super::BeginPlay();

	// [HANG DIAG / FIX] Do not trust whatever the Blueprint class-defaults ("Details" panel,
	// under the "Actor Tick" category) may have serialized for tick settings - a corrupted or
	// accidentally-toggled BP-level override (e.g. "Can Ever Tick" unchecked) would silently
	// stop Tick() from EVER firing, with GenerateMap() itself still running perfectly fine
	// (it does not depend on Tick). This would explain why a full clean C++ rebuild changed
	// nothing: a bad bool baked into the .uasset is not touched by recompiling C++ at all.
	// Force it here in code, unconditionally, ignoring the BP Details panel entirely.
	UE_LOG(LogTemp, Warning, TEXT("[WFC TickDiag] BEFORE force: bCanEverTick=%d bStartWithTickEnabled=%d IsTickFunctionRegistered=%d IsActorTickEnabled=%d"),
		PrimaryActorTick.bCanEverTick, PrimaryActorTick.bStartWithTickEnabled, PrimaryActorTick.IsTickFunctionRegistered(), IsActorTickEnabled());

	if (!PrimaryActorTick.bCanEverTick)
	{
		UE_LOG(LogTemp, Error, TEXT("[WFC TickDiag] bCanEverTick was FALSE! (BP Details override, not code) Forcing true now."));
	}
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	if (!PrimaryActorTick.IsTickFunctionRegistered())
	{
		UE_LOG(LogTemp, Error, TEXT("[WFC TickDiag] Tick function was NOT registered! Registering now."));
		PrimaryActorTick.RegisterTickFunction(GetLevel());
	}
	//SetActorTickEnabled(true);

	UE_LOG(LogTemp, Warning, TEXT("[WFC TickDiag] AFTER force: bCanEverTick=%d bStartWithTickEnabled=%d IsTickFunctionRegistered=%d IsActorTickEnabled=%d"),
		PrimaryActorTick.bCanEverTick, PrimaryActorTick.bStartWithTickEnabled, PrimaryActorTick.IsTickFunctionRegistered(), IsActorTickEnabled());

	// [HANG DIAG / FIX] Same story for the other diagnostic/behavior bools that live in the
	// BP Details panel and could hold a stale/corrupted override from earlier testing.
	// Log what the BP is actually handing us, then force the values this investigation needs,
	// purely in code, regardless of what the panel shows.
	UE_LOG(LogTemp, Warning, TEXT("[WFC TickDiag] BP bool values as-loaded: bGenerateMapOnBeginPlay=%d bDebugSkipActorSpawn=%d bHasGenerated=%d"),
		bGenerateMapOnBeginPlay, bDebugSkipActorSpawn, bHasGenerated);

	bGenerateMapOnBeginPlay = true;
	bDebugSkipActorSpawn = false;//µð¹ö±×¿ë ½ÇÁ¦»ý¼º¾ÈÇÏ°í ·Î±×·Î¸¸
	bHasGenerated = false;

	// ¼­¹ö(Host)¿¡¼­¸¸ ¸ÊÀ» »ý¼º
	if (HasAuthority())
	{
		// [Match server] Launched by the lobby with -ExpectedPlayers=N: do NOT auto-generate here.
		// AescaperoomGameMode::PostLogin calls TryGenerateMap() once every player has joined,
		// so clients actually see the loading bar fill up. PIE/editor keeps the old auto-generate path.
		// [Listen] Host-left result mode (GameLV reopened standalone with ?HostLeftResult): the runner only
		// watches the existing result-room ending, so never generate the map here.
		if (UEscapeRoomSessionSubsystem::IsHostLeftResultWorld(GetWorld()))
		{
			UE_LOG(LogTemp, Warning, TEXT("[WFC] Host-left result mode: skipping map generation"));
			return;
		}

		// [Listen] The chaser's PC hosts GameLV as a listen server with ?ExpectedPlayers=N in the map URL.
		// The GameMode parses it in InitGame (runs before actor BeginPlay) and triggers TryGenerateMap()
		// itself once everyone has joined - so skip auto-generation here while it is waiting.
		const AescaperoomGameMode* MatchGM = GetWorld() ? GetWorld()->GetAuthGameMode<AescaperoomGameMode>() : nullptr;
		if (MatchGM && MatchGM->IsWaitingForPlayers())
		{
			UE_LOG(LogTemp, Warning, TEXT("[WFC] Match host: waiting for all players before generating (GameMode triggers it)"));
			return;
		}

		UE_LOG(LogTemp, Warning, TEXT("[WFC] Server BeginPlay: scheduling TryGenerateMap() in %.2fs"), InitialGenerateDelay);

		if (InitialGenerateDelay > 0.f)
		{
			GetWorldTimerManager().SetTimer(
				TimerHandle_DelayedGenerate,
				this,
				&AWFCGridManager::DelayedTryGenerateMap,
				InitialGenerateDelay,
				false);
		}
		else
		{
			DelayedTryGenerateMap();
		}
	}
}

void AWFCGridManager::DelayedTryGenerateMap()
{
	UE_LOG(LogTemp, Warning, TEXT("[WFC] Delayed start: Starting TryGenerateMap()"));
	TryGenerateMap();
}

bool AWFCGridManager::CoordOf(int32 Index, int32& OutX, int32& OutY) const
{
	if (Index < 0 || Index >= Cells.Num()) return false;
	OutX = Index % GridSize;
	OutY = Index / GridSize;
	return true;
}

bool AWFCGridManager::IsValidCoord(int32 X, int32 Y) const
{
	return X >= 0 && X < GridSize && Y >= 0 && Y < GridSize;
}

void AWFCGridManager::InitCells()
{
	Cells.Reset();
	Cells.SetNum(GridSize * GridSize);

	static const EDoorDir AllDirs[4] = { EDoorDir::North, EDoorDir::East, EDoorDir::South, EDoorDir::West };

	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		FWFCCell& Cell = Cells[i];
		Cell.CollapsedTile = nullptr;
		Cell.Candidates = AllTiles; // ½ÃÀÛÀº ¸ðµç Ä­ÀÌ ¸ðµç Å¸ÀÏÀ» ÈÄº¸·Î °¡Áü (ÃÖ´ë ¿£Æ®·ÎÇÇ)

		int32 X, Y;
		CoordOf(i, X, Y);

		// °ÝÀÚ °¡ÀåÀÚ¸®¶ó¸é, °ÝÀÚ ¹ÛÀ¸·Î ³ª°¡´Â ¹æÇâÀº ¹Ýµå½Ã ¸·ÇôÀÖ¾î¾ß(¼ÒÄÏ 0) ÇÏ¹Ç·Î
		// ±× ¹æÇâÀÌ ¶Õ·ÁÀÖ´Â Å¸ÀÏÀº ¾ÖÃÊ¿¡ ÈÄº¸¿¡¼­ Á¦¿ÜÇÑ´Ù (ºØ±«/ÀüÆÄ ÀÌÀü¿¡ ¹Ì¸® Á¦¾à).
		for (EDoorDir Dir : AllDirs)
		{
			const FIntPoint Offset = FWFCSocketSet::DirToOffset(Dir);
			const bool bFacesOutside = !IsValidCoord(X + Offset.X, Y + Offset.Y);
			if (!bFacesOutside) continue;

			Cell.Candidates.RemoveAll([&](const TObjectPtr<UWFCTileDefinition>& Tile)
				{
					return !Tile || Tile->Sockets.Get(Dir) != 0;
				});
		}
	}
}

int32 AWFCGridManager::PickLowestEntropyCell() const
{
	int32 BestEntropy = TNumericLimits<int32>::Max();
	TArray<int32> TiedIndices;

	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		const FWFCCell& Cell = Cells[i];
		if (Cell.IsCollapsed()) continue;

		const int32 E = Cell.Entropy();
		if (E == 0) continue; // ¸ð¼ø »óÅÂ(ÈÄº¸ ¾øÀ½)´Â ´ë»ó¿¡¼­ Á¦¿Ü

		if (E < BestEntropy)
		{
			BestEntropy = E;
			TiedIndices.Reset();
			TiedIndices.Add(i);
		}
		else if (E == BestEntropy)
		{
			TiedIndices.Add(i);
		}
	}

	if (TiedIndices.Num() == 0) return -1;

	const int32 Pick = FMath::Min(FMath::FloorToInt(Rng.FRand() * TiedIndices.Num()), TiedIndices.Num() - 1);
	return TiedIndices[Pick];
}

void AWFCGridManager::CollapseCell(int32 Index)
{
	FWFCCell& Cell = Cells[Index];
	if (Cell.Candidates.Num() == 0) return;

	static const EDoorDir AllDirs[4] = { EDoorDir::North, EDoorDir::East, EDoorDir::South, EDoorDir::West };

	// ¿­¸° ¼ÒÄÏ °³¼ö°¡ ¸¹Àº Å¸ÀÏÀÏ¼ö·Ï »ÌÈú È®·üÀ» Áö¼öÀûÀ¸·Î ³ô°Ô ÁØ´Ù
	TArray<float> Weights;
	Weights.Reserve(Cell.Candidates.Num());
	float TotalWeight = 0.f;

	for (const TObjectPtr<UWFCTileDefinition>& Tile : Cell.Candidates)
	{
		int32 OpenCount = 0;
		for (EDoorDir Dir : AllDirs)
		{
			if (Tile->Sockets.Get(Dir) != 0) OpenCount++;
		}
		const float W = FMath::Pow(2.f, (float)OpenCount);
		Weights.Add(W);
		TotalWeight += W;
	}

	float RandVal = Rng.FRand() * TotalWeight;
	int32 Pick = 0;
	for (int32 i = 0; i < Weights.Num(); ++i)
	{
		RandVal -= Weights[i];
		if (RandVal <= 0.f) { Pick = i; break; }
		Pick = i;
	}

	UWFCTileDefinition* Chosen = Cell.Candidates[Pick];

	Cell.CollapsedTile = Chosen;
	Cell.Candidates.Reset();
	Cell.Candidates.Add(Chosen);
}

bool AWFCGridManager::SocketsMatch(const UWFCTileDefinition* A, EDoorDir DirFromA, const UWFCTileDefinition* B) const
{
	if (!A || !B) return false;
	const EDoorDir Opp = FWFCSocketSet::Opposite(DirFromA);
	return A->Sockets.Get(DirFromA) == B->Sockets.Get(Opp);
}

void AWFCGridManager::PropagateFrom(int32 StartIndex)
{
	TQueue<int32> BfsQueue;
	BfsQueue.Enqueue(StartIndex);

	static const EDoorDir AllDirs[4] = { EDoorDir::North, EDoorDir::East, EDoorDir::South, EDoorDir::West };

	while (!BfsQueue.IsEmpty())
	{
		int32 CurIndex;
		BfsQueue.Dequeue(CurIndex);

		int32 CX, CY;
		if (!CoordOf(CurIndex, CX, CY)) continue;

		const FWFCCell& CurCell = Cells[CurIndex];

		for (EDoorDir Dir : AllDirs)
		{
			const FIntPoint Offset = FWFCSocketSet::DirToOffset(Dir);
			const int32 NX = CX + Offset.X;
			const int32 NY = CY + Offset.Y;
			if (!IsValidCoord(NX, NY)) continue;

			const int32 NeighborIndex = IndexOf(NX, NY);
			FWFCCell& NeighborCell = Cells[NeighborIndex];
			if (NeighborCell.IsCollapsed()) continue;

			const int32 BeforeNum = NeighborCell.Candidates.Num();

			// ÀÌ¿ô ÈÄº¸ Áß, ÇöÀç Ä­ÀÇ ³²Àº ÈÄº¸ ¾î´À °Í°úµµ È£È¯ ¾È µÇ´Â °ÍµéÀ» Á¦°Å
			NeighborCell.Candidates.RemoveAll([&](const TObjectPtr<UWFCTileDefinition>& NeighborTile)
				{
					for (const TObjectPtr<UWFCTileDefinition>& CurTile : CurCell.Candidates)
					{
						if (SocketsMatch(CurTile, Dir, NeighborTile))
						{
							return false; // ÁöÁöÇØÁÖ´Â ÈÄº¸°¡ ÀÖÀ¸´Ï À¯Áö
						}
					}
					return true;
				});

			if (NeighborCell.Candidates.Num() != BeforeNum)
			{
				if (NeighborCell.Candidates.Num() == 0)
				{
					UE_LOG(LogTemp, Warning, TEXT("WFC: cell (%d,%d) has no valid candidates left (contradiction)"), NX, NY);
				}
				BfsQueue.Enqueue(NeighborIndex); // ¹Ù²ï Ä­µµ °è¼Ó ÀüÆÄ¸¦ ÀÌ¾î°¡¾ß ÇÏ´Ï Å¥¿¡ Ãß°¡
			}
		}
	}
}

// ---- Å«¹æ(BigRoom) ----

void AWFCGridManager::PlaceBigRooms()
{
	BigRoomPlacements.Reset();

	TSet<FIntPoint> Occupied;

	for (int32 i = 0; i < NumBigRooms; ++i)
	{
		const int32 Size = Rng.RandRange(BigRoomMinSize, BigRoomMaxSize);
		if (Size <= 0 || Size > GridSize) continue;

		FIntPoint Origin;
		bool bFound = false;

		// 4¸é ¸ðµÎ ¹®À» ³¾ °Å¶ó, Å«¹æÀÌ °ÝÀÚ °¡ÀåÀÚ¸®¿¡ ¾Æ¿¹ ¾È ºÙµµ·Ï »ç¹æ¿¡ 1Ä­ ¿©¹éÀ» µÐ´Ù.
		// (¿©¹éÀÌ ¾øÀ¸¸é ±× º¯Àº ¹ÛÀÌ ¾ø¾î¼­ ¹®À» ³¾ ¼ö ¾ø¾î º®À¸·Î Ã³¸®µÊ)
		const int32 MinOrigin = 1;
		const int32 MaxOrigin = GridSize - Size - 1;
		if (MaxOrigin < MinOrigin)
		{
			UE_LOG(LogTemp, Warning, TEXT("WFC: grid too small to place a %d-cell big room with all sides free (margin needed)"), Size);
			continue;
		}

		constexpr int32 MaxAttempts = 30;
		for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
		{
			const int32 OriginX = Rng.RandRange(MinOrigin, MaxOrigin);
			const int32 OriginY = Rng.RandRange(MinOrigin, MaxOrigin);
			const FIntPoint Candidate(OriginX, OriginY);

			bool bOverlap = false;
			for (int32 X = 0; X < Size && !bOverlap; ++X)
			{
				for (int32 Y = 0; Y < Size && !bOverlap; ++Y)
				{
					if (Occupied.Contains(Candidate + FIntPoint(X, Y)))
					{
						bOverlap = true;
					}
				}
			}

			if (!bOverlap)
			{
				Origin = Candidate;
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			UE_LOG(LogTemp, Warning, TEXT("WFC: could not find space for a %d-cell big room, skipped"), Size);
			continue;
		}

		FWFCBigRoomPlacement Placement;
		Placement.Origin = Origin;
		Placement.Size = Size;

		for (int32 X = 0; X < Size; ++X)
		{
			for (int32 Y = 0; Y < Size; ++Y)
			{
				const FIntPoint Cell = Origin + FIntPoint(X, Y);
				Placement.OccupiedCells.Add(Cell);
				Occupied.Add(Cell);
			}
		}

		BigRoomPlacements.Add(MoveTemp(Placement));
	}
}

void AWFCGridManager::SeedBigRoomCells()
{
	static const EDoorDir AllDirs[4] = { EDoorDir::North, EDoorDir::East, EDoorDir::South, EDoorDir::West };

	for (FWFCBigRoomPlacement& Placement : BigRoomPlacements)
	{
		// 1) º¯(Side)¸¶´Ù ¹® ³¾ Ä­À» ¸ÕÀú °áÁ¤ (·ÎÄÃ ÁÂÇ¥ -> ¹æÇâ -> ¹® ¿©ºÎ)
		TMap<FIntPoint, TMap<EDoorDir, bool>> DoorFlags;

		for (EDoorDir Side : AllDirs)
		{
			const TArray<FIntPoint> LocalCells = GetBigRoomSideLocalCells(Side, Placement.Size);
			const bool bOnGridBoundary = IsBigRoomSideOnGridBoundary(Side, Placement.Origin, Placement.Size, GridSize);

			if (bOnGridBoundary)
			{
				// °ÝÀÚ ¹Ù±ù°ú ¸Â´êÀº º¯Àº ¹®À» ³¾ ¼ö ¾øÀ¸´Ï ÀüºÎ º®À¸·Î °íÁ¤
				for (const FIntPoint& Local : LocalCells)
				{
					DoorFlags.FindOrAdd(Local).Add(Side, false);
				}
				continue;
			}

			// ¹® ÈÄº¸ Ä­ °áÁ¤.
			// - º¯ ±æÀÌ°¡ È¦¼ö(5 µî)¸é Á¤Áß¾Ó Ä­ ÇÑ °÷¸¸ ÈÄº¸·Î °íÁ¤ÇÑ´Ù.
			//   Å«¹æ ¾×ÅÍ¸¦ Á÷Á¢ ÇÏ³ª¸¸ ¸¸µé¾î¼­ ¾µ °Å¶ó, ¹® À§Ä¡°¡ ¸Å¹ø ´Þ¶óÁö¸é ¸Þ½Ã¶û ¾È ¸ÂÀ¸´Ï
			//   "°¢ º¯ Á¤Áß¾Ó¿¡ ¹®"À¸·Î °íÁ¤ÇØ¼­ ¾×ÅÍ ÇÏ³ª·Î °è¼Ó Àç»ç¿ëÇÒ ¼ö ÀÖ°Ô ÇÏ±â À§ÇÔ.
			// - º¯ ±æÀÌ°¡ Â¦¼ö¸é Á¤Áß¾ÓÀÌ ¾øÀ¸´Ï ¸ð¼­¸®¸¦ »« ³ª¸ÓÁö Ä­µéÀ» ÈÄº¸·Î µÐ´Ù (ÂªÀ¸¸é ÀüºÎ Çã¿ë).
			TArray<int32> CandidateIdx;
			if (Placement.Size % 2 == 1)
			{
				CandidateIdx.Add(LocalCells.Num() / 2);
			}
			else if (Placement.Size >= 3)
			{
				for (int32 Idx = 1; Idx < LocalCells.Num() - 1; ++Idx) CandidateIdx.Add(Idx);
			}
			else
			{
				for (int32 Idx = 0; Idx < LocalCells.Num(); ++Idx) CandidateIdx.Add(Idx);
			}

			const int32 NumDoors = FMath::Clamp(Rng.RandRange(MinDoorsPerSide, MaxDoorsPerSide), 0, CandidateIdx.Num());

			TSet<int32> DoorIdx;
			while (DoorIdx.Num() < NumDoors && DoorIdx.Num() < CandidateIdx.Num())
			{
				DoorIdx.Add(CandidateIdx[Rng.RandRange(0, CandidateIdx.Num() - 1)]);
			}

			for (int32 Idx = 0; Idx < LocalCells.Num(); ++Idx)
			{
				DoorFlags.FindOrAdd(LocalCells[Idx]).Add(Side, DoorIdx.Contains(Idx));
			}
		}

		// 2) Ä­¸¶´Ù ¼ÒÄÏ°ªÀ» °è»êÇØ¼­ ·±Å¸ÀÓ Àü¿ë Å¸ÀÏÀ» ¸¸µé°í ±× ÀÚ¸®¿¡¼­ ¹Ù·Î È®Á¤
		for (int32 X = 0; X < Placement.Size; ++X)
		{
			for (int32 Y = 0; Y < Placement.Size; ++Y)
			{
				const FIntPoint Local(X, Y);
				const FIntPoint World = Placement.Origin + Local;

				FWFCSocketSet Sockets;
				for (EDoorDir Dir : AllDirs)
				{
					const FIntPoint Offset = FWFCSocketSet::DirToOffset(Dir);
					const FIntPoint NeighborLocal = Local + Offset;
					const bool bInsideFootprint =
						NeighborLocal.X >= 0 && NeighborLocal.X < Placement.Size &&
						NeighborLocal.Y >= 0 && NeighborLocal.Y < Placement.Size;

					int32 Value = 0; // ±âº»Àº º®(¸·Èû)

					if (bInsideFootprint)
					{
						// Å«¹æ ³»ºÎ³¢¸® ¸Â´ê´Â ¸é - º® ¾øÀÌ ¶Õ¸° ÇÑ ¹æÀÌ¹Ç·Î Ç×»ó "ÀÌ¾îÁü" Ãë±Þ
						Value = BigRoomInternalSocketValue;
					}
					else if (const TMap<EDoorDir, bool>* Flags = DoorFlags.Find(Local))
					{
						if (const bool* bDoor = Flags->Find(Dir))
						{
							Value = *bDoor ? BigRoomDoorSocketValue : 0;
						}
					}

					switch (Dir)
					{
					case EDoorDir::North: Sockets.North = Value; break;
					case EDoorDir::East:  Sockets.East = Value; break;
					case EDoorDir::South: Sockets.South = Value; break;
					case EDoorDir::West:  Sockets.West = Value; break;
					}
				}

				UWFCTileDefinition* Tile = NewObject<UWFCTileDefinition>(this);
				Tile->Sockets = Sockets;
				Tile->RoomClass = nullptr; // Ä­ ÇÏ³ªÇÏ³ª´Â ½ºÆù ¾È ÇÔ - SpawnBigRooms°¡ Å«¹æ ¾×ÅÍ ÇÏ³ª¸¸ ½ºÆù
				Tile->TileName = FName(TEXT("BigRoomCell"));
				TransientBigRoomTiles.Add(Tile);

				const int32 Index = IndexOf(World.X, World.Y);
				Cells[Index].CollapsedTile = Tile;
				Cells[Index].Candidates.Reset();
				Cells[Index].Candidates.Add(Tile);
			}
		}

		// 3) ¸ðµç Ä­À» È®Á¤ÇÑ µÚ¿¡ ¹Ù±ùÀ¸·Î ÀüÆÄ (¹®/º® Á¶°Ç¿¡ ¸Â°Ô ¹Ù±ù ÀÌ¿ô ÈÄº¸¸¦ Á¦ÇÑ)
		for (const FIntPoint& World : Placement.OccupiedCells)
		{
			PropagateFrom(IndexOf(World.X, World.Y));
		}
	}
}

void AWFCGridManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);


	// ½ºÆù ÁøÇà ÁßÀÏ ¶§¸¸ µ¿ÀÛ
	if (bIsSpawningRooms || bIsSpawningBigRooms || bIsSpawningDoors || bIsSpawningFog)
	{
		int32 SpawnedThisFrame = 0;

		// 1. ÀÏ¹Ý ¹æ ½ºÆù
		if (bIsSpawningRooms)
		{
			UE_LOG(LogTemp, Warning, TEXT("[WFC] SpawnLoop tick: cursor=%d, SpawnsPerTick=%d"), SpawnCursor, SpawnsPerTick);

			while (SpawnCursor < Cells.Num() && SpawnedThisFrame < SpawnsPerTick)
			{
				SpawnOneRoomCell(SpawnCursor);
				SpawnCursor++;
				SpawnedThisFrame++;
			}

			if (SpawnCursor >= Cells.Num())
			{
				bIsSpawningRooms = false;
				bIsSpawningBigRooms = true;
			}
		}
		// 2. Å« ¹æ ½ºÆù
		else if (bIsSpawningBigRooms)
		{
			while (BigRoomSpawnCursor < BigRoomPlacements.Num() && SpawnedThisFrame < BigRoomsPerTick)
			{
				SpawnOneBigRoom(BigRoomSpawnCursor);
				BigRoomSpawnCursor++;
				SpawnedThisFrame++;
			}

			if (BigRoomSpawnCursor >= BigRoomPlacements.Num())
			{
				bIsSpawningBigRooms = false;
				bIsSpawningDoors = true;
			}
		}
		// 3. ¹® ½ºÆù
		else if (bIsSpawningDoors)
		{
			while (DoorSpawnCursor < PendingDoorTransforms.Num() && SpawnedThisFrame < DoorsPerTick)
			{
				SpawnOneDoor(DoorSpawnCursor);
				DoorSpawnCursor++;
				SpawnedThisFrame++;
			}

			if (DoorSpawnCursor >= PendingDoorTransforms.Num())
			{
				bIsSpawningDoors = false;
				bIsSpawningFog = true; // ´ÙÀ½Àº 4´Ü°è: ¾È°³
			}
		}
		// 4. ¾È°³ ¹¶Ä¡ È®Á¤ (FogDrift°¡ º¹Á¦ ¹è¿­¿¡ ³Ö°í, °¢ Å¬¶ó°¡ ·ÎÄÃ·Î Niagara »ý¼º)
		else if (bIsSpawningFog)
		{
			const int32 FogTotal = GetFogWorkTotal();
			const int32 FogPerTick = FMath::Max(1, FogPatchesPerTick);

			while (FogSpawnCursor < FogTotal && SpawnedThisFrame < FogPerTick)
			{
				FogDrift->CommitPendingPatch(FogSpawnCursor);
				FogSpawnCursor++;
				SpawnedThisFrame++;
			}

			if (FogSpawnCursor >= FogTotal)
			{
				bIsSpawningFog = false;
				SetActorTickEnabled(false); // ¿Ï°ø ÈÄ Tick ºñÈ°¼ºÈ­

				if (HasAuthority())
				{
					bReplicatedGenComplete = true;
					OnRep_GenComplete(); // ¼­¹ö¿¡¼­µµ ¿Ï·á ÀÌº¥Æ® ½ÇÇà
				}
			}
		}

		// ÁøÇà·ü °»½Å
		const int32 TotalWork = Cells.Num() + BigRoomPlacements.Num() + PendingDoorTransforms.Num() + GetFogWorkTotal();
		const int32 DoneWork = SpawnCursor + BigRoomSpawnCursor + DoorSpawnCursor + FogSpawnCursor;

		if (GenProgressDone != DoneWork || GenProgressTotal != TotalWork)
		{
			GenProgressDone = DoneWork;
			GenProgressTotal = TotalWork;

			// ¼­¹ö(Listen Server / Standalone)ÀÏ °æ¿ì Á÷Á¢ Broadcast
			if (HasAuthority())
			{
				OnMapGenProgress.Broadcast(GenProgressDone, GenProgressTotal);
			}
		}
	}
}

void AWFCGridManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearMap();
	Super::EndPlay(EndPlayReason);
}
void AWFCGridManager::SpawnOneRoomCell(int32 i)
{
	if (!Cells.IsValidIndex(i)) return;

	const FWFCCell& Cell = Cells[i];

	// 1. ºØ±«µÇÁö ¾Ê¾Ò°Å³ª Å¸ÀÏÀÌ ¾ø´Â °æ¿ì (Prune Ã³¸®µÈ Å¸ÀÏ Æ÷ÇÔ)
	if (!Cell.IsCollapsed() || !Cell.CollapsedTile)
	{
		return;
	}

	// 2. TileDefinition¿¡ RoomClass(¾×ÅÍ ºí·çÇÁ¸°Æ®)°¡ ¾È ÇÒ´çµÈ °æ¿ì!!
	if (!Cell.CollapsedTile->RoomClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[WFC Error] Tile '%s' has NO RoomClass assigned!"), *Cell.CollapsedTile->TileName.ToString());
		return;
	}

	int32 X, Y;
	CoordOf(i, X, Y);

	const FVector Location = GetActorLocation() + FVector(X * RoomSize, Y * RoomSize, 0.f);
	const FTransform SpawnTransform(FRotator::ZeroRotator, Location);

	if (bDebugSkipActorSpawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("[WFC Debug] SkipActorSpawn: would spawn Room at cell (%d, %d)"), X, Y);
		return;
	}

	// ·¥ÇÁ on/off¸¦ Á¤ÇÒ ¹æº° ½Ãµå¸¦ BeginPlay Àü¿¡ ³Ö¾îÁÖ·Á°í Deferred ½ºÆù »ç¿ë
	ARoomActor* Room = GetWorld()->SpawnActorDeferred<ARoomActor>(Cell.CollapsedTile->RoomClass, SpawnTransform);
	if (Room)
	{
		Room->SetLampSeed(MakeRoomLampSeed(i));
		Room->FinishSpawning(SpawnTransform);
		SpawnedRooms[i] = Room;
		AssignFolder(Room);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[WFC Error] Failed to SpawnActor for Cell (%d, %d)"), X, Y);
	}
}

int32 AWFCGridManager::MakeRoomLampSeed(int32 Index) const
{
	// °°Àº RandomSeed + °°Àº ÀÎµ¦½º¸é Ç×»ó °°Àº °ª (¸Ê »ý¼º¿ë Rng ½ÃÄö½º´Â °Çµå¸®Áö ¾ÊÀ½)
	return static_cast<int32>(HashCombine(GetTypeHash(RandomSeed), GetTypeHash(Index)));
}

void AWFCGridManager::SpawnOneBigRoom(int32 i)
{
	if (!BigRoomPlacements.IsValidIndex(i)) return;
	FWFCBigRoomPlacement& Placement = BigRoomPlacements[i];

	const FWFCBigRoomVariantSet* FoundVariants = BigRoomClassesBySize.Find(Placement.Size);
	if (!FoundVariants || FoundVariants->Classes.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[WFC Error] BigRoom Size %d HAS NO REGISTERED CLASSES!"), Placement.Size);
		return;
	}

	const TSubclassOf<AActor> ChosenClass = FoundVariants->Classes[i % FoundVariants->Classes.Num()];
	if (!ChosenClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("WFC: big room variant %d for size %d is None"), i % FoundVariants->Classes.Num(), Placement.Size);
		return;
	}

	const float CenterX = (Placement.Origin.X + (Placement.Size - 1) * 0.5f) * RoomSize;
	const float CenterY = (Placement.Origin.Y + (Placement.Size - 1) * 0.5f) * RoomSize;
	const FVector Location = GetActorLocation() + FVector(CenterX, CenterY, 0.f);
	const FTransform SpawnTransform(FRotator::ZeroRotator, Location);

	if (bDebugSkipActorSpawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("[WFC Debug] SkipActorSpawn: would spawn BigRoom %d"), i);
		return;
	}

	// ÀÏ¹Ý ¹æ°ú °°Àº ÀÌÀ¯·Î Deferred ½ºÆù (ºò·ë BPµµ ARoomActor ÀÚ½ÄÀÌ¸é ·¥ÇÁ ½Ãµå Àü´Þ)
	AActor* BigRoomActor = GetWorld()->SpawnActorDeferred<AActor>(ChosenClass, SpawnTransform);
	if (BigRoomActor)
	{
		if (ARoomActor* BigRoom = Cast<ARoomActor>(BigRoomActor))
		{
			// ÀÏ¹Ý ¼¿ ÀÎµ¦½º(0 ~ GridSize^2-1)¿Í ¾È °ãÄ¡°Ô À½¼ö ÂÊ ÀÎµ¦½º »ç¿ë
			BigRoom->SetLampSeed(MakeRoomLampSeed(-1 - i));
		}
		BigRoomActor->FinishSpawning(SpawnTransform);
	}
	Placement.SpawnedActor = BigRoomActor;
	SpawnedBigRoomActors.Add(BigRoomActor);
	AssignFolder(BigRoomActor);
}

void AWFCGridManager::BuildPendingDoors()
{
	PendingDoorTransforms.Reset();

	static const EDoorDir CheckDirs[2] = { EDoorDir::North, EDoorDir::East };

	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		const FWFCCell& Cell = Cells[i];
		if (!Cell.IsCollapsed() || !Cell.CollapsedTile) continue;

		int32 X, Y;
		CoordOf(i, X, Y);

		for (EDoorDir Dir : CheckDirs)
		{
			const int32 SocketValue = Cell.CollapsedTile->Sockets.Get(Dir);
			if (SocketValue == 0) continue;
			if (SocketValue == BigRoomInternalSocketValue) continue;

			const FIntPoint Offset = FWFCSocketSet::DirToOffset(Dir);
			const int32 NX = X + Offset.X;
			const int32 NY = Y + Offset.Y;
			if (!IsValidCoord(NX, NY)) continue;

			const FWFCCell& NeighborCell = Cells[IndexOf(NX, NY)];
			if (!NeighborCell.IsCollapsed() || !NeighborCell.CollapsedTile) continue;
			if (!SocketsMatch(Cell.CollapsedTile, Dir, NeighborCell.CollapsedTile)) continue;

			if (Rng.FRand() > DoorSpawnChance) continue; // ÆÇÁ¤Àº ¿©±â¼­ ¹Ì¸® È®Á¤

			const FVector CellLoc = GetActorLocation() + FVector(X * RoomSize, Y * RoomSize, 0.f);
			const FVector NeighborLoc = CellLoc + FVector(Offset.X, Offset.Y, 0.f) * RoomSize;
			const FVector DoorLoc = (CellLoc + NeighborLoc) * 0.5f;

			FRotator DoorRot = FRotator::ZeroRotator;
			if (Dir == EDoorDir::East)
			{
				DoorRot.Yaw = 90.f;
			}

			PendingDoorTransforms.Add(FTransform(DoorRot, DoorLoc));
		}
	}
}

void AWFCGridManager::SpawnOneDoor(int32 i)
{
	if (!DoorActorClass) return;

	if (bDebugSkipActorSpawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("[WFC Debug] SkipActorSpawn: would spawn Door %d"), i);
		return;
	}

	// ADoorActorëŠ” APawn ê³„ì—´ì´ë¼ ê¸°ë³¸ê°’ì´ "ê²¹ì¹˜ë©´ ìŠ¤í° ì·¨ì†Œ"ìž„.
	// ë¬¸ì€ ë²½ êµ¬ë©(ë¬¸í‹€)ê³¼ í•­ìƒ ê²¹ì¹˜ë¯€ë¡œ AlwaysSpawnìœ¼ë¡œ ê°•ì œí•´ì•¼ í•œë‹¤ (ì•ˆ ê·¸ëŸ¬ë©´ ëŒ€ë¶€ë¶„ ìŠ¤í° ì‹¤íŒ¨)
	FActorSpawnParameters DoorSpawnParams;
	DoorSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Door = GetWorld()->SpawnActor<AActor>(DoorActorClass, PendingDoorTransforms[i], DoorSpawnParams);
	SpawnedDoorActors.Add(Door);
	AssignFolder(Door);
}

void AWFCGridManager::GenerateMap()
{
	UE_LOG(LogTemp, Warning, TEXT("[WFC Debug] 1. GenerateMap Started!"));

	ClearMap();

	// [MapSeed] In a match, the server-decided seed in GameState wins (see AescaperoomGameMode::InitGameState).
	// Without an EscapeRoomGameState (editor preview / other levels) the RandomSeed property is used as-is.
	if (const AEscapeRoomGameState* GS = GetWorld() ? GetWorld()->GetGameState<AEscapeRoomGameState>() : nullptr)
	{
		RandomSeed = GS->MapSeed;
	}
	UE_LOG(LogTemp, Warning, TEXT("[WFC] GenerateMap with seed %d"), RandomSeed);
	Rng = FRandomStream(RandomSeed);

	if (AllTiles.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[WFC Debug] ERROR: AllTiles array is EMPTY!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[WFC Debug] 2. AllTiles Count: %d"), AllTiles.Num());

	InitCells();
	UE_LOG(LogTemp, Warning, TEXT("[WFC Debug] 3. Cells Initialized. Count: %d"), Cells.Num());

	PlaceBigRooms();
	SeedBigRoomCells();

	const int32 TotalCells = GridSize * GridSize;
	int32 CollapsedCount = 0;

	for (int32 Step = 0; Step < TotalCells; ++Step)
	{
		const int32 Target = PickLowestEntropyCell();
		if (Target == -1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[WFC Debug] PickLowestEntropyCell returned -1 at step %d"), Step);
			break;
		}

		CollapseCell(Target);
		PropagateFrom(Target);
		CollapsedCount++;
	}

	UE_LOG(LogTemp, Warning, TEXT("[WFC Debug] 4. Cell Collapse Complete! Collapsed Cells: %d / %d"), CollapsedCount, TotalCells);

	PruneDisconnectedRooms();
	CapDanglingDoors();
	BuildPendingDoors();
	BuildFogPatches(); // ¹®±îÁö Rng »ç¿ëÀÌ ³¡³­ µÚ¿¡ »ÌÀ¸¹Ç·Î ±âÁ¸ ¹æ/¹® ¹èÄ¡ °á°ú´Â ¹Ù²îÁö ¾ÊÀ½

	SpawnedRooms.SetNum(Cells.Num());
	SpawnCursor = 0;
	BigRoomSpawnCursor = 0;
	DoorSpawnCursor = 0;
	FogSpawnCursor = 0;

	bIsSpawningRooms = true;
	bIsSpawningBigRooms = false;
	bIsSpawningDoors = false;
	bIsSpawningFog = false;

	// [¼öÁ¤] SetActorTickEnabled ´ë½Å Tick ³»ºÎ º¯¼ö·Î¸¸ Á¦¾îÇÏ°Å³ª, °­Á¦ ÄÑ±â
	SetActorTickEnabled(true);

	// [Ãß°¡] Ã¹ ÁøÇàµµ Áï½Ã °è»ê ¹× ºê·ÎµåÄ³½ºÆ® (Tick Ã¹ ÇÁ·¹ÀÓ Àü 0/Total ¹æÁö)
	GenProgressDone = 0;
	GenProgressTotal = Cells.Num() + BigRoomPlacements.Num() + PendingDoorTransforms.Num() + GetFogWorkTotal();

	// ¼­¹ö ÀÚ½Åµµ UI ¾÷µ¥ÀÌÆ®¸¦ À§ÇØ È£Ãâ
	OnMapGenProgress.Broadcast(GenProgressDone, GenProgressTotal);

	UE_LOG(LogTemp, Warning, TEXT("[WFC Debug] 5. Tick Enabled! Starting Actor Spawning... Total Work: %d"), GenProgressTotal);
}

void AWFCGridManager::ClearMap()
{
	for (ARoomActor* Room : SpawnedRooms)
	{
		if (Room) Room->Destroy();
	}
	SpawnedRooms.Reset();

	for (AActor* BigRoom : SpawnedBigRoomActors)
	{
		if (BigRoom) BigRoom->Destroy();
	}
	SpawnedBigRoomActors.Reset();

	for (AActor* Door : SpawnedDoorActors)
	{
		if (Door) Door->Destroy();
	}
	SpawnedDoorActors.Reset();

	BigRoomPlacements.Reset();
	TransientBigRoomTiles.Reset();

	if (FogDrift)
	{
		FogDrift->ClearPatches();
	}

	Cells.Reset();

	if (bShowDebugLabels)
	{
		FlushPersistentDebugLines(GetWorld());
		FlushDebugStrings(GetWorld());
	}
}

void AWFCGridManager::TryGenerateMap()
{

	if (bHasGenerated) return;

	if (bGenerateMapOnBeginPlay && HasAuthority())
	{
		bHasGenerated = true; // Áßº¹ »ý¼º ¹æÁö¿ë ÇÃ·¡±×¸¸ ¼³Á¤
		GenerateMap();        // TickÀ» ÅëÇØ ºñµ¿±â ½ºÆù ½ÃÀÛ
	}
		
}

void AWFCGridManager::PruneDisconnectedRooms()
{
	static const EDoorDir AllDirs[4] = { EDoorDir::North, EDoorDir::East, EDoorDir::South, EDoorDir::West };

	// ºØ±«µÈ Ä­µé Áß ¾ÆÁ÷ ¹æ¹® ¾È ÇÑ Ã¹ Ä­À» Ã£¾Æ BFS. ÀÌ°É ¹Ýº¹ÇØ¼­ "¿¬°áµÈ ¹æ ±×·ì"µéÀ» ÀüºÎ Ã£´Â´Ù.
	TArray<bool> Visited;
	Visited.SetNumZeroed(Cells.Num());

	TArray<TArray<int32>> Groups; // ±×·ìº°·Î ¼ÓÇÑ Ä­ ÀÎµ¦½ºµé

	for (int32 Start = 0; Start < Cells.Num(); ++Start)
	{
		if (!Cells[Start].IsCollapsed() || Visited[Start]) continue;

		TArray<int32> Group;
		TQueue<int32> BfsQueue;
		BfsQueue.Enqueue(Start);
		Visited[Start] = true;

		while (!BfsQueue.IsEmpty())
		{
			int32 CurIndex;
			BfsQueue.Dequeue(CurIndex);
			Group.Add(CurIndex);

			int32 CX, CY;
			CoordOf(CurIndex, CX, CY);
			const UWFCTileDefinition* CurTile = Cells[CurIndex].CollapsedTile;

			for (EDoorDir Dir : AllDirs)
			{
				if (CurTile->Sockets.Get(Dir) == 0) continue; // ÀÌ ¹æÇâÀ¸·Î ¹®ÀÌ ¾øÀ¸¸é ÀÌ¾îÁú ¼ö ¾øÀ½

				const FIntPoint Offset = FWFCSocketSet::DirToOffset(Dir);
				const int32 NX = CX + Offset.X;
				const int32 NY = CY + Offset.Y;
				if (!IsValidCoord(NX, NY)) continue;

				const int32 NeighborIndex = IndexOf(NX, NY);
				if (Visited[NeighborIndex] || !Cells[NeighborIndex].IsCollapsed()) continue;

				// ½ÇÁ¦·Î ¹®³¢¸® ¼­·Î ¸Â¹°¸®´Â °æ¿ì¸¸ "ÀÌ¾îÁü"À¸·Î ÀÎÁ¤
				if (!SocketsMatch(CurTile, Dir, Cells[NeighborIndex].CollapsedTile)) continue;

				Visited[NeighborIndex] = true;
				BfsQueue.Enqueue(NeighborIndex);
			}
		}

		Groups.Add(MoveTemp(Group));
	}

	if (Groups.Num() <= 1) return; // ±×·ìÀÌ ÇÏ³ª»ÓÀÌ¸é(=ÀüºÎ ¿¬°áµÊ) Á¤¸®ÇÒ °Ô ¾øÀ½

	// °¡Àå Ä­ ¼ö°¡ ¸¹Àº ±×·ìÀ» ¸ÞÀÎ ´øÀüÀ¸·Î ³²±â°í, ³ª¸ÓÁö ±×·ìÀº ÀüºÎ ºñÈ°¼ºÈ­(½ºÆù Á¦¿Ü)
	int32 BiggestGroupIdx = 0;
	for (int32 i = 1; i < Groups.Num(); ++i)
	{
		if (Groups[i].Num() > Groups[BiggestGroupIdx].Num())
		{
			BiggestGroupIdx = i;
		}
	}

	for (int32 g = 0; g < Groups.Num(); ++g)
	{
		if (g == BiggestGroupIdx) continue;

		for (int32 CellIndex : Groups[g])
		{
			UE_LOG(LogTemp, Warning, TEXT("WFC: cell %d is disconnected from the main dungeon, pruned"), CellIndex);
			Cells[CellIndex].CollapsedTile = nullptr; // ½ºÆù ´ë»ó¿¡¼­ Á¦¿Ü (SpawnResult°¡ IsCollapsed()==false·Î °Ç³Ê¶Ü)
		}
	}
}

UWFCTileDefinition* AWFCGridManager::FindTileWithClosedDir(const UWFCTileDefinition* Source, EDoorDir DirToClose) const
{
	static const EDoorDir AllDirs[4] = { EDoorDir::North, EDoorDir::East, EDoorDir::South, EDoorDir::West };

	for (const TObjectPtr<UWFCTileDefinition>& Tile : AllTiles)
	{
		if (!Tile || Tile == Source) continue;
		if (Tile->Sockets.Get(DirToClose) != 0) continue; // ´Ý¾Æ¾ß ÇÒ ¹æÇâÀÌ ¿­·ÁÀÖÀ¸¸é ÈÄº¸ ¾Æ´Ô

		bool bOthersMatch = true;
		for (EDoorDir Dir : AllDirs)
		{
			if (Dir == DirToClose) continue;
			if (Tile->Sockets.Get(Dir) != Source->Sockets.Get(Dir))
			{
				bOthersMatch = false;
				break;
			}
		}

		if (bOthersMatch) return Tile; // ³ª¸ÓÁö ¹æÇâÀº ÀüºÎ µ¿ÀÏÇÏ°í, ¹®Á¦ ¹æÇâ¸¸ ´ÝÈù Å¸ÀÏ ¹ß°ß
	}
	return nullptr;
}

void AWFCGridManager::CapDanglingDoors()
{
	static const EDoorDir AllDirs[4] = { EDoorDir::North, EDoorDir::East, EDoorDir::South, EDoorDir::West };

	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		FWFCCell& Cell = Cells[i];
		if (!Cell.IsCollapsed() || !Cell.CollapsedTile) continue;

		int32 X, Y;
		CoordOf(i, X, Y);

		for (EDoorDir Dir : AllDirs)
		{
			if (Cell.CollapsedTile->Sockets.Get(Dir) == 0) continue; // ÀÌ¹Ì ´ÝÇôÀÖÀ¸¸é ÆÐ½º

			const FIntPoint Offset = FWFCSocketSet::DirToOffset(Dir);
			const int32 NX = X + Offset.X;
			const int32 NY = Y + Offset.Y;

			bool bNeighborSpawned = false;
			if (IsValidCoord(NX, NY))
			{
				const FWFCCell& NeighborCell = Cells[IndexOf(NX, NY)];
				bNeighborSpawned = NeighborCell.IsCollapsed() && NeighborCell.CollapsedTile != nullptr;
			}

			if (bNeighborSpawned) continue; // Á¤»óÀûÀ¸·Î ÀÌ¿ôÀÌ ÀÖÀ¸¸é ¹® À¯Áö

			// ÀÌ¿ôÀÌ ¾ø´Âµ¥ ¹®ÀÌ ¿­·ÁÀÖ´Â »óÅÂ -> ÀÌ ¹æÇâ¸¸ ´ÝÈù ´ëÃ¼ Å¸ÀÏ·Î ±³Ã¼ ½Ãµµ
			// (ÁÖÀÇ: Å«¹æ Ä­ÀÇ CollapsedTileÀº AllTiles¿¡ ¾ø´Â ·±Å¸ÀÓ Àü¿ë Å¸ÀÏÀÌ¶ó FindTileWithClosedDir·Î ´ëÃ¼µÇÁö ¾Ê´Â´Ù.
			//  Å«¹æ Ä­Àº SeedBigRoomCells¿¡¼­ ÀÌ¹Ì ÀÌ¿ô Á¸Àç ¿©ºÎ¸¦ °¨¾ÈÇØ ¼ÒÄÏÀ» Á¤ÇßÀ¸¹Ç·Î Á¤»óÀûÀ¸·Î´Â ¿©±â ¾È °É¸°´Ù.)
			if (UWFCTileDefinition* Replacement = FindTileWithClosedDir(Cell.CollapsedTile, Dir))
			{
				UE_LOG(LogTemp, Warning, TEXT("WFC: cell (%d,%d) capped dir %d -> tile %s"), X, Y, (int32)Dir, *Replacement->TileName.ToString());
				Cell.CollapsedTile = Replacement;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("WFC: cell (%d,%d) dangling door on %d direction, no capped tile found"), X, Y, (int32)Dir);
			}
		}
	}
}

void AWFCGridManager::BuildFogPatches()
{
	if (!FogDrift)
	{
		return;
	}

	// ½ÇÁ¦·Î ¹æÀÌ È®Á¤µÈ Ä­(PruneµÈ Ä­ Á¦¿Ü, Å«¹æ Ä­ Æ÷ÇÔ)ÀÇ Áß½É¸¸ ÈÄº¸·Î »ç¿ë
	TArray<FVector2D> RoomCenters;
	const FVector Origin = GetActorLocation();

	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		const FWFCCell& Cell = Cells[i];
		if (!Cell.IsCollapsed() || !Cell.CollapsedTile) continue;

		int32 X, Y;
		CoordOf(i, X, Y);
		RoomCenters.Add(FVector2D(Origin.X + X * RoomSize, Origin.Y + Y * RoomSize));
	}

	FogDrift->BuildPendingPatches(Rng, RoomCenters, RoomSize, Origin.Z);
}

int32 AWFCGridManager::GetFogWorkTotal() const
{
	return FogDrift ? FogDrift->GetPendingPatchCount() : 0;
}

void AWFCGridManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWFCGridManager, GenProgressDone);
	DOREPLIFETIME(AWFCGridManager, GenProgressTotal);
	DOREPLIFETIME(AWFCGridManager, bReplicatedGenComplete);
}

// Done, Total µÑ Áß ¹¹°¡ ¹Ù²î¾îµµ ÀÌ ÇÔ¼ö°¡ ºÒ¸®´Âµ¥,
// È£Ãâ ½ÃÁ¡¿£ µÎ ÇÁ·ÎÆÛÆ¼ ¸ðµÎ ÀÌ¹Ì ÃÖ½Å°ªÀ¸·Î ¹Ý¿µµÈ µÚ¶ó ¾ÈÀüÇÏ°Ô °°ÀÌ ÀÐ¾îµµ µÊ
void AWFCGridManager::OnRep_GenProgress()
{
	OnMapGenProgress.Broadcast(GenProgressDone, GenProgressTotal);
}

void AWFCGridManager::OnRep_GenComplete()
{
	if (bReplicatedGenComplete)
	{
		OnMapGenComplete.Broadcast();
	}
}

void AWFCGridManager::AssignFolder(AActor* Actor)
{
#if WITH_EDITOR
	// [HANG FIX TEST] SetFolderPath on a World Partition (OFPA) level touches per-level
	// actor-folder bookkeeping. During PIE multiplayer testing, the "dedicated server"
	// instance is still an editor process (WITH_EDITOR=1), so this used to fire ~500+
	// times back-to-back while GameLV's WP cells may still be streaming in for joining
	// clients ??a likely cause of the post-generation freeze. Skip it on any
	// server (listen or dedicated); it is purely a cosmetic editor-authoring convenience.
	if (Actor && !IsRunningDedicatedServer() && GetNetMode() != NM_DedicatedServer && GetNetMode() != NM_ListenServer)
	{
		Actor->SetFolderPath(TEXT("WFCDungeon"));
	}
#endif
}