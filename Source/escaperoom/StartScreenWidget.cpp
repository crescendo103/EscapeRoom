#include "StartScreenWidget.h"
#include "StartScreenController.h"
#include "Components/EditableTextBox.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Kismet/KismetSystemLibrary.h"

void UStartScreenWidget::GoToCreateJoinRoom()
{
	if (AStartScreenController* PC = Cast<AStartScreenController>(GetOwningPlayer()))
	{
		if (PlayerNameTextBox)
		{
			FString Name = PlayerNameTextBox->GetText().ToString();
			if (Name.IsEmpty())
			{
				Name = TEXT("Player"); // 기본값 (RequestCreateRoom의 "New Room" 패턴과 동일)
			}
			PC->ServerChangeName(Name);
		}

		PC->ShowCreateJoinRoomWidget();
	}
}

void UStartScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (HowToButton)
	{
		HowToButton->OnClicked.AddDynamic(this, &UStartScreenWidget::OnHowToClicked);
	}
	if (GuideOverlayButton)
	{
		GuideOverlayButton->OnClicked.AddDynamic(this, &UStartScreenWidget::OnGuideOverlayClicked);
	}

	if (QuitButton)
	{
		QuitButton->OnClicked.AddDynamic(this, &UStartScreenWidget::OnQuitClicked);
	}

	// 시작 시 조작법 화면은 닫힌 상태
	ShowGuidePage(0);
}

void UStartScreenWidget::OnHowToClicked()
{
	ShowGuidePage(1); // 술래 조작법부터
}

void UStartScreenWidget::OnQuitClicked()
{
	// 패키징 빌드: 프로세스 종료 / 에디터 PIE: 플레이만 멈춤
	// 로비 서버 연결은 종료되면서 자동으로 끊김 (서버가 Logout 처리)
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UStartScreenWidget::OnGuideOverlayClicked()
{
	// 술래 조작법 -> 러너 조작법 -> 술래 목표 -> 러너 목표 -> 닫기
	ShowGuidePage(GuidePage + 1);
}

void UStartScreenWidget::ShowGuidePage(int32 Page)
{
	if (!GuideOverlayButton || !GuideImage)
	{
		return;
	}

	// 요청한 페이지부터 텍스처가 지정된 첫 페이지를 찾음 (미지정 페이지는 건너뜀)
	UTexture2D* Texture = nullptr;
	if (Page >= 1)
	{
		for (; Page <= GuidePageCount; ++Page)
		{
			Texture = GetGuideTexture(Page);
			if (Texture)
			{
				break;
			}
		}
	}

	// 닫기 또는 남은 페이지가 없으면 원래 화면으로
	if (!Texture)
	{
		GuidePage = 0;
		GuideOverlayButton->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	GuidePage = Page;
	GuideImage->SetBrushFromTexture(Texture, false); // false: 원본 크기 대신 영역에 맞춤
	GuideOverlayButton->SetVisibility(ESlateVisibility::Visible);
}

UTexture2D* UStartScreenWidget::GetGuideTexture(int32 Page) const
{
	switch (Page)
	{
	case 1: return TaggerGuideTexture;
	case 2: return RunnerGuideTexture;
	case 3: return TaggerGoalTexture;
	case 4: return RunnerGoalTexture;
	default: return nullptr;
	}
}
