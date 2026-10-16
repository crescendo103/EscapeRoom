// Fill out your copyright notice in the Description page of Project Settings.
#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "LoadingScreenWidget.generated.h"

UCLASS()
class ULoadingScreenWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(meta = (BindWidget))
    UProgressBar* ProgressBar;

    UPROPERTY(meta = (BindWidget))
    UTextBlock* ProgressText;

    // ---- 로딩 팁 ----
    // 위젯 BP 디자이너에서 이 이름으로 Image / TextBlock을 배치하면 자동 바인딩됨 (없어도 컴파일은 됨)
    UPROPERTY(meta = (BindWidgetOptional))
    UImage* TipImage;

    UPROPERTY(meta = (BindWidgetOptional))
    UTextBlock* TipText;

    // 팁 이미지/문구. 같은 인덱스끼리 한 쌍 (TipImages[0] <-> TipTexts[0] ...).
    // 위젯 BP Class Defaults에서 순서대로 채워 넣으면 됨. 개수가 다르면 짧은 쪽 개수까지만 사용
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loading|Tips")
    TArray<TObjectPtr<UTexture2D>> TipImages;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loading|Tips")
    TArray<FText> TipTexts;

    // 위젯이 생성될 때마다 팁 하나를 랜덤으로 골라 표시 (직전과 같은 팁은 피함)
    UFUNCTION(BlueprintCallable, Category = "Loading|Tips")
    void ShowRandomTip();

protected:
    virtual void NativeConstruct() override;

public:
    void SetProgress(int32 Current, int32 Total)
    {
        float Percent = Total > 0 ? (float)Current / (float)Total : 0.f;
        if (ProgressBar)  ProgressBar->SetPercent(Percent);
        if (ProgressText) ProgressText->SetText(FText::FromString(FString::Printf(TEXT("%d : %d"), Current, Total)));
    }
};