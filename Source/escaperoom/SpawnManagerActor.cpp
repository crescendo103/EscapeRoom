// Fill out your copyright notice in the Description page of Project Settings.
#include "SpawnManagerActor.h"
#include "EscapeRoomSessionSubsystem.h"

void ASpawnManagerActor::BeginPlay()
{
    Super::BeginPlay();

    ShowLoadingAndGenerate();
}

void ASpawnManagerActor::ShowLoadingAndGenerate()
{
    // [데디서버] 화면이 없는 서버에서는 로딩 위젯을 만들지 않음.
    // 예전엔 null 체크 없이 AddToViewport를 불러서 데디서버에서 크래시 위험이 있었음.
    // (아래 Handle* 함수들은 전부 LoadingWidget null 체크가 있어서 위젯 없이도 안전)
    if (!IsNetMode(NM_DedicatedServer) && LoadingWidgetClass)
    {
        LoadingWidget = CreateWidget<ULoadingScreenWidget>(GetWorld(), LoadingWidgetClass);
        if (LoadingWidget)
        {
            LoadingWidget->AddToViewport(100);
            LoadingWidget->SetProgress(0, 100);
        }
    }

    if (MapGenerator)
    {
        MapGenerator->OnMapGenProgress.AddUObject(this, &ASpawnManagerActor::HandleMapProgress);
        MapGenerator->OnMapGenComplete.AddUObject(this, &ASpawnManagerActor::HandleMapComplete);
    }

    if (PropSpawner)
    {
        PropSpawner->OnPropSpawnProgress.AddUObject(this, &ASpawnManagerActor::HandlePropProgress);
        PropSpawner->OnPropSpawnComplete.AddUObject(this, &ASpawnManagerActor::HandlePropComplete);
    }

    // 한 프레임 뒤에 실제 생성 시작 (위젯이 먼저 화면에 붙게)
    GetWorldTimerManager().SetTimerForNextTick(this, &ASpawnManagerActor::RunMapGeneration);
}

void ASpawnManagerActor::RunMapGeneration()
{
    if (MapGenerator)
    {
        // 시드는 서버 GameMode가 GameState->MapSeed로 정하고, GenerateMap()이 거기서 직접 읽는다
        MapGenerator->TryGenerateMap();     // 실제 생성 호출 (비동기 - Tick으로 이어서 진행됨)
    }

    // NOTE: 위젯 제거는 여기서 하지 않는다. 구조 생성이 비동기(Tick 기반)라 이 시점엔
    // 아직 하나도 스폰 안 됐을 수 있음 - 실제 제거는 구조+소품 생성이 전부 끝나는
    // HandlePropComplete (PropSpawner 없으면 HandleMapComplete)에서 처리한다.
}

// 진행바 0~90% 구간: 구조(방/빅룸/문) 생성
void ASpawnManagerActor::HandleMapProgress(int32 Current, int32 Total)
{
    if (!LoadingWidget) return;

    const int32 Percent = (Total > 0) ? FMath::Clamp(Current * 90 / Total, 0, 90) : 0;
    LoadingWidget->SetProgress(Percent, 100);
}

// 구조 생성이 끝나는 시점.
// NOTE: PropSpawner는 이제 WFCGridManager::OnMapGenComplete에 스스로 바인딩해서
// 자기가 알아서 PopulateProps()를 시작한다 (자기완결적). 여기서 직접 호출하면 중복 실행되므로
// 호출하지 않는다 - PropSpawner가 아예 없는 경우에만 로딩을 바로 끝낸다.
void ASpawnManagerActor::HandleMapComplete()
{
    if (!PropSpawner)
    {
        HandlePropComplete();
    }
}

// 진행바 90~100% 구간: 소품(캐비닛/다이스/Exit) 배치
// NOTE: 90/100 분기점은 정확한 시간/개수 비율 계산이 아니라 임의로 고정한 값.
// 구조 Total과 소품 Total을 합산해서 실제 비율로 계산하는 방법도 있지만, 소품 Total은
// 구조 생성이 끝나야만 알 수 있어서 그 시점에 분모가 갑자기 늘어나며 퍼센트가 순간적으로
// 뒤로 튀는 부작용이 있음 - 지금은 그 문제를 피하려고 단순 고정 분할(90/10)을 사용.
void ASpawnManagerActor::HandlePropProgress(int32 Current, int32 Total)
{
    if (!LoadingWidget) return;

    const int32 Percent = 90 + ((Total > 0) ? FMath::Clamp(Current * 10 / Total, 0, 10) : 10);
    LoadingWidget->SetProgress(Percent, 100);
}

// 구조+소품 생성이 전부 끝나는 시점 - 로딩 위젯 제거
void ASpawnManagerActor::HandlePropComplete()
{
    if (LoadingWidget)
    {
        LoadingWidget->SetProgress(100, 100);
        LoadingWidget->RemoveFromParent();
        LoadingWidget = nullptr;
    }
}
