// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LoadingScreenWidget.h"
#include "WFCGridManager.h"
#include "PropSpawnManagerActor.h"

#include "SpawnManagerActor.generated.h"


// SpawnManagerActor.h
UCLASS()
class ASpawnManagerActor : public AActor
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly)
    TSubclassOf<ULoadingScreenWidget> LoadingWidgetClass;

    UPROPERTY(EditInstanceOnly)
    AWFCGridManager* MapGenerator;

    // 구조(방/빅룸/문) 생성이 끝난 뒤 캐비닛/다이스/Exit를 배치하는 매니저.
    // 비워두면 소품 배치 단계 없이 구조 생성만으로 로딩을 끝낸다.
    UPROPERTY(EditInstanceOnly)
    APropSpawnManagerActor* PropSpawner;

    virtual void BeginPlay() override;

private:
    UPROPERTY()
    ULoadingScreenWidget* LoadingWidget;

    void ShowLoadingAndGenerate();
    void RunMapGeneration();

    // 진행바 0~90% 구간: 구조(방/빅룸/문) 생성
    void HandleMapProgress(int32 Current, int32 Total);
    // 구조 생성이 끝나는 시점 - 여기서 소품 배치를 시작시킨다
    void HandleMapComplete();
    // 진행바 90~100% 구간: 소품(캐비닛/다이스/Exit) 배치
    // NOTE: 90/100 분기점은 정확한 비율 계산이 아니라 임의로 정한 값.
    // 실제로는 구조 Total과 소품 Total의 비율이 매판 다르게 나올 수 있음 - 필요해지면
    // "구조 Total + 소품 Total" 합산 방식으로 바꿀 것 (그 경우 소품 Total이 늦게 확정되면서
    // 퍼센트가 한 번 튀는 부작용이 있어 지금은 단순한 고정 분할을 택함.
    void HandlePropProgress(int32 Current, int32 Total);
    // 구조+소품 생성이 전부 끝나는 시점 - 여기서 로딩 위젯을 제거한다
    void HandlePropComplete();
};