#include "FogDriftComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UFogDriftComponent::UFogDriftComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // 그릴 안개가 생겼을 때만 켬
	SetIsReplicatedByDefault(true);
}

void UFogDriftComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UFogDriftComponent, Patches);
	DOREPLIFETIME(UFogDriftComponent, ReplicatedWindDir);
	DOREPLIFETIME(UFogDriftComponent, AlongMin);
	DOREPLIFETIME(UFogDriftComponent, AlongMax);
	DOREPLIFETIME(UFogDriftComponent, FogBaseZ);
	DOREPLIFETIME(UFogDriftComponent, FogStartServerTime);
}

void UFogDriftComponent::BeginPlay()
{
	Super::BeginPlay();

	// 늦게 접속해서 이미 데이터가 들어와 있는 경우 대비 (보통은 OnRep_Patches에서 처리됨)
	if (Patches.Num() > 0)
	{
		SyncFxComponents();
	}
}

void UFogDriftComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyFxComponents();
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------
// 서버: 생성
// ---------------------------------------------------------------------------

void UFogDriftComponent::BuildPendingPatches(const FRandomStream& Rng, const TArray<FVector2D>& RoomCenters, float RoomSize, float BaseZ)
{
	PendingPatches.Reset();

	if (RoomCenters.Num() == 0 || NumPatches <= 0)
	{
		return;
	}

	// 1) 바람 방향 (정규화)
	FVector2D Dir = WindDirection.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		Dir = FVector2D(1.f, 0.f);
	}

	// 2) 실제 방이 있는 범위의 네 모서리를 바람 축에 투영해서 되돌리기 구간을 구함
	const float HalfRoom = RoomSize * 0.5f;
	FBox2D MapBounds(ForceInit);
	for (const FVector2D& Center : RoomCenters)
	{
		MapBounds += Center - FVector2D(HalfRoom, HalfRoom);
		MapBounds += Center + FVector2D(HalfRoom, HalfRoom);
	}

	const FVector2D Corners[4] =
	{
		MapBounds.Min,
		FVector2D(MapBounds.Max.X, MapBounds.Min.Y),
		FVector2D(MapBounds.Min.X, MapBounds.Max.Y),
		MapBounds.Max
	};

	double MinAlong = TNumericLimits<double>::Max();
	double MaxAlong = TNumericLimits<double>::Lowest();
	for (const FVector2D& Corner : Corners)
	{
		const double A = FVector2D::DotProduct(Corner, Dir);
		MinAlong = FMath::Min(MinAlong, A);
		MaxAlong = FMath::Max(MaxAlong, A);
	}

	const float SizeLo = FMath::Min(PatchSizeMin, PatchSizeMax);
	const float SizeHi = FMath::Max(PatchSizeMin, PatchSizeMax);
	const float SpeedLo = FMath::Min(SpeedMin, SpeedMax);
	const float SpeedHi = FMath::Max(SpeedMin, SpeedMax);

	// 뭉치(축 정렬 박스)가 바람 축 방향으로 차지하는 최대 반길이 + 약간의 여유.
	// 여유 거리가 이보다 작으면 박스 일부가 맵 안에 걸친 채로 순간이동하는 게 보일 수 있음
	const double PatchHalfAlong = 0.5 * (FMath::Abs(Dir.X) + FMath::Abs(Dir.Y)) * SizeHi;
	const double Margin = FMath::Max(static_cast<double>(WrapMargin), PatchHalfAlong + 100.0);

	AlongMin = MinAlong - Margin;
	AlongMax = MaxAlong + Margin;
	ReplicatedWindDir = Dir;
	FogBaseZ = BaseZ + HeightOffset;

	if (const UWorld* World = GetWorld())
	{
		const AGameStateBase* GS = World->GetGameState();
		FogStartServerTime = GS ? GS->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
	}

	// 3) 뭉치 생성 - 방이 있는 칸 중 랜덤 위치에서 시작 (같은 칸이 여러 번 뽑히면 자연스럽게 뭉침)
	PendingPatches.Reserve(NumPatches);
	for (int32 i = 0; i < NumPatches; ++i)
	{
		const FVector2D& Center = RoomCenters[Rng.RandRange(0, RoomCenters.Num() - 1)];

		FFogPatchData Patch;
		Patch.StartPos = Center + FVector2D(Rng.FRandRange(-HalfRoom, HalfRoom), Rng.FRandRange(-HalfRoom, HalfRoom));
		Patch.Size = FVector(Rng.FRandRange(SizeLo, SizeHi), Rng.FRandRange(SizeLo, SizeHi), PatchHeight);
		Patch.Speed = Rng.FRandRange(SpeedLo, SpeedHi);
		PendingPatches.Add(Patch);
	}

	UE_LOG(LogTemp, Log, TEXT("[Fog] Built %d patches. Along range [%.0f, %.0f], wind (%.2f, %.2f)"),
		PendingPatches.Num(), AlongMin, AlongMax, Dir.X, Dir.Y);
}

void UFogDriftComponent::CommitPendingPatch(int32 Index)
{
	if (!PendingPatches.IsValidIndex(Index))
	{
		return;
	}

	Patches.Add(PendingPatches[Index]);
	OnRep_Patches(); // 서버 자신(리슨서버 호스트)도 그리기 - 기존 OnRep 수동 호출 패턴과 동일
}

void UFogDriftComponent::ClearPatches()
{
	PendingPatches.Reset();
	Patches.Reset();
	DestroyFxComponents();
}

// ---------------------------------------------------------------------------
// 클라(+리슨서버): 그리기
// ---------------------------------------------------------------------------

void UFogDriftComponent::OnRep_Patches()
{
	SyncFxComponents();
}

bool UFogDriftComponent::ShouldRender() const
{
	const AActor* Owner = GetOwner();
	return Owner && FogSystem && Owner->GetNetMode() != NM_DedicatedServer;
}

double UFogDriftComponent::GetElapsedSeconds() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}

	const AGameStateBase* GS = World->GetGameState();
	const double Now = GS ? GS->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
	return FMath::Max(0.0, Now - FogStartServerTime);
}

FVector UFogDriftComponent::ComputePatchLocation(const FFogPatchData& Patch, double Elapsed) const
{
	const FVector2D Dir = ReplicatedWindDir;
	const FVector2D Perp(-Dir.Y, Dir.X);

	// 바람 축 성분(Along)만 움직이고, 수직 성분(Side)은 처음 값 그대로 유지 (같은 줄로 반복)
	const double Along0 = FVector2D::DotProduct(Patch.StartPos, Dir);
	const double Side = FVector2D::DotProduct(Patch.StartPos, Perp);

	double Along = Along0 + static_cast<double>(Patch.Speed) * Elapsed;

	// 구간 끝(맵 끝 + 여유)을 넘으면 구간 시작(반대편 맵 바깥)으로 되돌림.
	// 처음 위치는 맵 안이라 첫 바퀴는 방 위에서 출발하고, 이후로는 항상 바깥에서 다시 들어옴
	const double Span = AlongMax - AlongMin;
	if (Span > 1.0)
	{
		Along = AlongMin + FMath::Fmod(Along - AlongMin, Span);
		if (Along < AlongMin)
		{
			Along += Span;
		}
	}

	const FVector2D XY = Dir * Along + Perp * Side;
	return FVector(XY.X, XY.Y, FogBaseZ);
}

void UFogDriftComponent::SyncFxComponents()
{
	if (!ShouldRender())
	{
		return;
	}

	// 배열이 줄어든 경우(맵 재생성 등)는 전부 다시 만듦. 보통은 뒤에 추가만 됨
	if (FogFx.Num() > Patches.Num())
	{
		DestroyFxComponents();
	}

	AActor* Owner = GetOwner();
	const double Elapsed = GetElapsedSeconds();

	for (int32 i = FogFx.Num(); i < Patches.Num(); ++i)
	{
		UNiagaraComponent* Fx = NewObject<UNiagaraComponent>(Owner);
		Fx->SetAsset(FogSystem);
		Fx->SetAutoActivate(false);
		Fx->SetUsingAbsoluteLocation(true);
		Fx->SetUsingAbsoluteRotation(true);
		Fx->SetUsingAbsoluteScale(true);
		Fx->SetWorldLocation(ComputePatchLocation(Patches[i], Elapsed));
		Fx->RegisterComponent();
		Fx->SetVariableVec3(BoxSizeParamName, Patches[i].Size);
		Fx->Activate(true);
		FogFx.Add(Fx);
	}

	SetComponentTickEnabled(FogFx.Num() > 0);
}

void UFogDriftComponent::DestroyFxComponents()
{
	for (UNiagaraComponent* Fx : FogFx)
	{
		if (IsValid(Fx))
		{
			Fx->DestroyComponent();
		}
	}
	FogFx.Reset();
	SetComponentTickEnabled(false);
}

void UFogDriftComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const double Elapsed = GetElapsedSeconds();
	const int32 Count = FMath::Min(FogFx.Num(), Patches.Num());
	for (int32 i = 0; i < Count; ++i)
	{
		if (UNiagaraComponent* Fx = FogFx[i])
		{
			Fx->SetWorldLocation(ComputePatchLocation(Patches[i], Elapsed));
		}
	}
}
