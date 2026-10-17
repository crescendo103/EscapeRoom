#include "EscapeFadeWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Animation/WidgetAnimation.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "DungeonPlayerController.h"
#include "StartScreenController.h"

void UEscapeFadeWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (VignetteImage)
	{
		// Brush에 지정된 머티리얼로부터 동적 인스턴스를 만들어 Brush에 다시 꽂아줌
		// (Brush 리소스가 머티리얼이 아니면 nullptr - 그 경우 SetRadius가 이미지 투명도로 대체 동작)
		VignetteMID = VignetteImage->GetDynamicMaterial();
	}

	if (ReturnToLobbyButton)
	{
		ReturnToLobbyButton->OnClicked.RemoveDynamic(this, &UEscapeFadeWidget::HandleReturnToLobbyClicked);
		ReturnToLobbyButton->OnClicked.AddDynamic(this, &UEscapeFadeWidget::HandleReturnToLobbyClicked);
	}

	SetEscapeInfoVisible(false);
	SetLobbyButtonVisible(false);
	if (ResultText)
	{
		ResultText->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (ResultTitleText)
	{
		ResultTitleText->SetVisibility(ESlateVisibility::Collapsed);
	}
	SetRadius(1.f);
}

void UEscapeFadeWidget::StartEscapeFade()
{
	// 이미 게임 종료 화면이면 탈출 연출로 되돌리지 않음
	if (bMatchEndMode)
	{
		return;
	}

	Phase = EPhase::FadeOut;
	PhaseTime = 0.f;
	FadeOutStartRadius = 1.f;
	CachedTargetName.Reset();
	SetEscapeInfoVisible(false);
	SetLobbyButtonVisible(false);
	SetRadius(1.f);
}

void UEscapeFadeWidget::StartMatchEndFade(const FText& ResultMessage, const FText& Title, FLinearColor TitleColor, bool bInRevealResultRoom)
{
	bMatchEndMode = true;
	bRevealResultRoom = bInRevealResultRoom;
	PendingResultMessage = ResultMessage;
	PendingTitle = Title;
	PendingTitleColor = TitleColor;

	// 관전 관련 표시는 즉시 정리
	if (SpectateTargetText)
	{
		SpectateTargetText->SetVisibility(ESlateVisibility::Collapsed);
	}
	SetLobbyButtonVisible(false);

	if (Phase == EPhase::HoldBlack || CurrentRadius <= KINDA_SMALL_NUMBER)
	{
		// 이미 완전히 검은 상태 - 바로 다음 단계
		SetRadius(0.f);
		PhaseTime = 0.f;
		HandleMatchEndBlack();
		return;
	}

	// 지금 반경(관전 중이면 SpectateRadius, 탈출 암전 도중이면 그 중간값)에서부터 0까지 암전
	FadeOutStartRadius = CurrentRadius;
	Phase = EPhase::FadeOut;
	PhaseTime = 0.f;
}

void UEscapeFadeWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (Phase == EPhase::Idle || Phase == EPhase::MatchEnd || Phase == EPhase::FinalBlack)
	{
		return;
	}

	PhaseTime += InDeltaTime;

	switch (Phase)
	{
	case EPhase::FadeOut:
	{
		const float Alpha = FMath::Clamp(PhaseTime / FadeOutDuration, 0.f, 1.f);
		SetRadius(FadeOutStartRadius * (1.f - FMath::Pow(Alpha, EaseExponent)));
		if (Alpha >= 1.f)
		{
			SetRadius(0.f);
			PhaseTime = 0.f;
			if (bMatchEndMode)
			{
				// 게임 종료: 결과 방 연출이면 시점 전환 후 다시 열기, 아니면 검은 화면에서 결과 + 버튼
				HandleMatchEndBlack();
			}
			else
			{
				Phase = EPhase::HoldBlack;
				// 완전히 검어진 순간 - 컨트롤러가 이 뒤에서 관전 시점으로 전환
				OnFadeOutFinished.Broadcast();
			}
		}
		break;
	}
	case EPhase::HoldBlack:
		if (PhaseTime >= HoldBlackDuration)
		{
			Phase = EPhase::FadeIn;
			PhaseTime = 0.f;
			if (!bMatchEndMode)
			{
				SetEscapeInfoVisible(true);
			}
		}
		break;

	case EPhase::FadeIn:
	{
		const float Duration = bMatchEndMode ? MatchEndRevealDuration : FadeInDuration;
		const float TargetRadius = bMatchEndMode ? MatchEndRevealRadius : SpectateRadius;
		const float Alpha = FMath::Clamp(PhaseTime / Duration, 0.f, 1.f);
		// 열릴 때는 반대로 처음이 빠르고 끝이 느리게 (1 - (1-a)^n)
		const float Eased = 1.f - FMath::Pow(1.f - Alpha, EaseExponent);
		SetRadius(Eased * TargetRadius);
		if (bMatchEndMode)
		{
			if (Alpha >= 1.f)
			{
				// 결과 방이 다 드러남 -> 타이틀 + 결과 + 로비 버튼
				ShowMatchEndInfo();
			}
			break;
		}
		if (Alpha >= 1.f)
		{
			Phase = EPhase::Spectating;
			PhaseTime = 0.f;
			// 관전 화면이 다 열린 뒤 로비 버튼 표시
			SetLobbyButtonVisible(true);
		}
		UpdateSpectateTargetText();
		break;
	}
	case EPhase::Spectating:
		UpdateSpectateTargetText();
		break;

	default:
		break;
	}
}

void UEscapeFadeWidget::HandleMatchEndBlack()
{
	// [로비 엔딩] 암전 전용 모드: 검은 화면에서 멈추고 알림만 (컨트롤러가 이 순간 로비로 이동)
	if (bFadeToBlackOnly)
	{
		Phase = EPhase::FinalBlack;
		PhaseTime = 0.f;
		OnMatchEndBlack.Broadcast();
		return;
	}

	if (!bRevealResultRoom)
	{
		ShowMatchEndInfo();
		return;
	}

	// 검은 화면 뒤에서 컨트롤러가 결과 방 카메라로 전환 -> 잠깐 유지 후 열기
	OnMatchEndBlack.Broadcast();
	Phase = EPhase::HoldBlack;
	PhaseTime = 0.f;
}

void UEscapeFadeWidget::ShowMatchEndInfo()
{
	Phase = EPhase::MatchEnd;

	// ResultTitleText를 따로 안 만든 경우: 기존 ResultText에 승리/패배 문구, StatusText(상단)에 통계를 띄움
	if (!ResultTitleText && !PendingTitle.IsEmpty())
	{
		if (StatusText)
		{
			StatusText->SetText(PendingResultMessage);
			StatusText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		if (SpectateTargetText)
		{
			SpectateTargetText->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (ResultText)
		{
			ResultText->SetText(PendingTitle);
			ResultText->SetColorAndOpacity(FSlateColor(PendingTitleColor));
			ResultText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		if (ResultTitleAnim)
		{
			PlayAnimation(ResultTitleAnim);
		}
		SetLobbyButtonVisible(true);
		return;
	}

	if (ResultTitleText)
	{
		if (PendingTitle.IsEmpty())
		{
			ResultTitleText->SetVisibility(ESlateVisibility::Collapsed);
		}
		else
		{
			ResultTitleText->SetText(PendingTitle);
			ResultTitleText->SetColorAndOpacity(FSlateColor(PendingTitleColor));
			ResultTitleText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (ResultTitleAnim)
			{
				PlayAnimation(ResultTitleAnim);
			}
		}
	}

	if (StatusText)
	{
		StatusText->SetText(MatchEndStatusText);
		StatusText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (SpectateTargetText)
	{
		SpectateTargetText->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (ResultText)
	{
		ResultText->SetText(PendingResultMessage);
		ResultText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	SetLobbyButtonVisible(true);
}

void UEscapeFadeWidget::HandleReturnToLobbyClicked()
{
	if (ADungeonPlayerController* PC = Cast<ADungeonPlayerController>(GetOwningPlayer()))
	{
		PC->ReturnToLobby();
	}
	else if (AStartScreenController* LobbyPC = Cast<AStartScreenController>(GetOwningPlayer()))
	{
		// [로비 엔딩] 이미 로비에 있음 -> 결과 화면만 닫고 시작 화면으로
		LobbyPC->FinishMatchResult();
	}
}

void UEscapeFadeWidget::SnapToBlack()
{
	SetRadius(0.f);
}

void UEscapeFadeWidget::StartFadeToBlackOnly()
{
	if (bFadeToBlackOnly)
	{
		return; // 이미 진행 중
	}
	bMatchEndMode = true;       // 이후 탈출 연출(StartEscapeFade)이 덮어쓰지 않게
	bFadeToBlackOnly = true;
	bRevealResultRoom = false;

	// 화면에는 암전만 보이도록 문구/버튼 전부 숨김 (탈출 문구, 관전 대상, 결과 문구/타이틀, 로비 버튼)
	SetEscapeInfoVisible(false);
	SetLobbyButtonVisible(false);
	if (ResultText)
	{
		ResultText->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (ResultTitleText)
	{
		ResultTitleText->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (CurrentRadius <= KINDA_SMALL_NUMBER)
	{
		// 이미 검은 화면(탈출 암전 도중 등) - 바로 종료 단계
		SetRadius(0.f);
		HandleMatchEndBlack();
		return;
	}

	// 지금 반경(평소 1, 관전 중이면 SpectateRadius)에서 0까지 암전 - 완전히 검어지면 HandleMatchEndBlack
	FadeOutStartRadius = CurrentRadius;
	Phase = EPhase::FadeOut;
	PhaseTime = 0.f;
}

void UEscapeFadeWidget::SetRadius(float Radius)
{
	CurrentRadius = Radius;

	if (VignetteMID)
	{
		VignetteMID->SetScalarParameterValue(VignetteParamName, Radius);
	}
	else if (VignetteImage)
	{
		// 머티리얼을 안 넣었을 때의 대체 동작: 단색 이미지를 투명도로만 암전 (비네트 모양은 없음)
		FLinearColor Color = VignetteImage->GetColorAndOpacity();
		Color.A = 1.f - Radius;
		VignetteImage->SetColorAndOpacity(Color);
	}
}

void UEscapeFadeWidget::SetEscapeInfoVisible(bool bVisible)
{
	const ESlateVisibility Vis = bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	if (StatusText)
	{
		StatusText->SetText(EscapedStatusText);
		StatusText->SetVisibility(Vis);
	}
	if (SpectateTargetText)
	{
		SpectateTargetText->SetVisibility(Vis);
	}
}

void UEscapeFadeWidget::SetLobbyButtonVisible(bool bVisible)
{
	if (ReturnToLobbyButton)
	{
		ReturnToLobbyButton->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	// 버튼을 누를 수 있게 마우스 커서를 켜고 UI 입력도 받도록 전환 (게임 입력 - Space 관전 전환 - 도 계속 받음)
	if (bVisible)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			FInputModeGameAndUI InputMode;
			InputMode.SetHideCursorDuringCapture(false);
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			PC->SetInputMode(InputMode);
			PC->bShowMouseCursor = true;
		}
	}
}

void UEscapeFadeWidget::UpdateSpectateTargetText()
{
	if (!SpectateTargetText)
	{
		return;
	}

	// 지금 카메라가 붙어있는 폰 -> 그 폰의 PlayerState 이름 (캐비닛에 숨은 러너면 캐비닛 폰이지만
	// 러너가 빙의 중이라 PlayerState가 그 러너 것으로 나옴)
	FString TargetName;
	if (const APlayerController* PC = GetOwningPlayer())
	{
		if (const APawn* ViewPawn = Cast<APawn>(PC->GetViewTarget()))
		{
			if (ViewPawn != PC->GetPawn())
			{
				if (const APlayerState* PS = ViewPawn->GetPlayerState())
				{
					TargetName = PS->GetPlayerName();
				}
			}
		}
	}

	if (TargetName == CachedTargetName)
	{
		return;
	}
	CachedTargetName = TargetName;

	if (TargetName.IsEmpty())
	{
		SpectateTargetText->SetText(FText::GetEmpty());
	}
	else
	{
		SpectateTargetText->SetText(FText::Format(SpectateFormatText, FText::FromString(TargetName)));
	}
}
