#include "RoomMemberEntryWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

void URoomMemberEntryWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// SetMemberInfo가 불리기 전까지는 "준비 안 됨" 모양으로 시작
	if (!bHasCachedState)
	{
		ApplyReadyVisual(false);
	}
}

void URoomMemberEntryWidget::SetMemberInfo(const FString& PlayerName, bool bIsReady)
{
	// 값이 그대로면 아무것도 안 함 (0.5초 폴링마다 불리므로)
	if (bHasCachedState && CachedName == PlayerName && bCachedReady == bIsReady)
	{
		return;
	}

	if (PlayerNameText && (!bHasCachedState || CachedName != PlayerName))
	{
		PlayerNameText->SetText(FText::FromString(PlayerName));
	}

	if (!bHasCachedState || bCachedReady != bIsReady)
	{
		ApplyReadyVisual(bIsReady);
	}

	CachedName = PlayerName;
	bCachedReady = bIsReady;
	bHasCachedState = true;
}

void URoomMemberEntryWidget::ApplyReadyVisual(bool bIsReady)
{
	if (!ReadyImage)
	{
		return;
	}

	UTexture2D* Texture = bIsReady ? ReadyTexture.Get() : NotReadyTexture.Get();
	if (Texture)
	{
		// bMatchSize=false: WBP에서 잡아둔 이미지 크기(Image Size) 유지
		ReadyImage->SetBrushFromTexture(Texture, false);
		ReadyImage->SetColorAndOpacity(FLinearColor::White);
	}
	else
	{
		// 텍스처 미지정 시 색으로만 구분
		ReadyImage->SetColorAndOpacity(bIsReady ? ReadyTint : NotReadyTint);
	}
}
