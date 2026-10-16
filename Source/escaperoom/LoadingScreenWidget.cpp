// Fill out your copyright notice in the Description page of Project Settings.


#include "LoadingScreenWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

// 마지막으로 보여준 팁 번호 (게임을 다시 들어와도 같은 팁이 연속으로 나오지 않게 static으로 유지)
static int32 GLastLoadingTipIndex = INDEX_NONE;

void ULoadingScreenWidget::NativeConstruct()
{
    Super::NativeConstruct();
    ShowRandomTip();
}

void ULoadingScreenWidget::ShowRandomTip()
{
    const int32 Count = FMath::Min(TipImages.Num(), TipTexts.Num());
    if (Count <= 0)
    {
        return;
    }

    int32 Index = FMath::RandRange(0, Count - 1);
    if (Count > 1 && Index == GLastLoadingTipIndex)
    {
        // 직전 팁과 같으면 나머지 중에서 하나로 밀어줌 (분포는 균등 유지)
        Index = (Index + FMath::RandRange(1, Count - 1)) % Count;
    }
    GLastLoadingTipIndex = Index;

    if (TipImage && TipImages[Index])
    {
        TipImage->SetBrushFromTexture(TipImages[Index]);
    }
    if (TipText)
    {
        TipText->SetText(TipTexts[Index]);
    }
}
