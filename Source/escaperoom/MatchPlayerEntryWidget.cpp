#include "MatchPlayerEntryWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

void UMatchPlayerEntryWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// SetPlayerInfo가 불리기 전까지는 "자유" 모양으로 시작
	if (!bHasCachedState)
	{
		ApplyStateVisual(EMatchPlayerEntryState::Free);
	}
}

void UMatchPlayerEntryWidget::SetPlayerInfo(const FString& PlayerName, bool bIsInJail, bool bHasEscaped)
{
	// 탈출 > 감옥 > 자유 순으로 우선 (탈출한 러너는 되돌아오지 않으므로 항상 탈출 이미지)
	const EMatchPlayerEntryState NewState =
		bHasEscaped ? EMatchPlayerEntryState::Escaped :
		bIsInJail ? EMatchPlayerEntryState::Jailed :
		EMatchPlayerEntryState::Free;

	// 값이 그대로면 아무것도 안 함 (0.5초 폴링마다 불리므로)
	if (bHasCachedState && CachedName == PlayerName && CachedState == NewState)
	{
		return;
	}

	if (PlayerNameText && (!bHasCachedState || CachedName != PlayerName))
	{
		PlayerNameText->SetText(FText::FromString(PlayerName));
	}

	if (!bHasCachedState || CachedState != NewState)
	{
		ApplyStateVisual(NewState);
	}

	CachedName = PlayerName;
	CachedState = NewState;
	bHasCachedState = true;
}

void UMatchPlayerEntryWidget::ApplyStateVisual(EMatchPlayerEntryState State)
{
	if (!JailStateImage)
	{
		return;
	}

	UTexture2D* Texture = nullptr;
	FLinearColor Tint = FreeTint;
	switch (State)
	{
	case EMatchPlayerEntryState::Escaped:
		Texture = EscapedTexture.Get();
		Tint = EscapedTint;
		break;
	case EMatchPlayerEntryState::Jailed:
		Texture = JailedTexture.Get();
		Tint = JailedTint;
		break;
	default:
		Texture = FreeTexture.Get();
		Tint = FreeTint;
		break;
	}

	if (Texture)
	{
		// bMatchSize=false: WBP에서 잡아둔 이미지 크기 유지
		JailStateImage->SetBrushFromTexture(Texture, false);
		JailStateImage->SetColorAndOpacity(FLinearColor::White);
	}
	else
	{
		JailStateImage->SetColorAndOpacity(Tint);
	}
}
