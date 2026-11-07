#include "MatchWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "EscapeRoomGameState.h"
#include "escaperoomCharacter.h"
#include "CabinetActor.h"
#include "TimerManager.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "MatchPlayerEntryWidget.h"
#include "MatchPlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "ChaserCharacter.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Animation/WidgetAnimation.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

void UMatchWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 탈출구 섹션은 문이 열리기 전까지 기본 비활성화(안 보이게)
	SetExitSectionVisible(false);

	// 부감 쿨타임 아이콘: 머티리얼이면 동적 인스턴스 확보, 술래인지 확인되기 전까지는 숨김
	if (OverheadSkillIcon)
	{
		OverheadSkillIconMID = OverheadSkillIcon->GetDynamicMaterial();
		OverheadSkillIcon->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (OverheadCooldownText)
	{
		OverheadCooldownText->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 홀드 게이지도 기본은 숨김 - SetHoldGaugeActive(true)가 호출될 때만 켜짐
	if (HoldProgressBar)
	{
		HoldProgressBar->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (HoldActionText)
	{
		HoldActionText->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 실제 값은 TryBindToGameState -> HandleDiceProgressChanged에서 바인딩 직후 1회 채워짐
	if (DiceCountText)
	{
		DiceCountText->SetText(FText::FromString(TEXT("0 : 0")));
	}
	if (ExitCountText)
	{
		ExitCountText->SetText(FText::FromString(TEXT("0")));
	}

	// 알림(배경 패널 포함)은 기본 숨김 - 큐에 알림이 들어오면 ShowNextNotification에서 켜짐
	if (UWidget* NotifyRoot = GetNotificationRoot())
	{
		NotifyRoot->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (NotificationAnim)
	{
		FWidgetAnimationDynamicEvent AnimFinishedEvent;
		AnimFinishedEvent.BindDynamic(this, &UMatchWidget::OnNotificationAnimFinished);
		BindToAnimationFinished(NotificationAnim, AnimFinishedEvent);
	}

	TryBindToGameState();

	// 러너 목록은 로비 멤버 목록과 같은 0.5초 폴링 (이름/감옥 여부/입퇴장을 한 곳에서 처리)
	RefreshPlayerList();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(PlayerListRefreshTimerHandle, this, &UMatchWidget::RefreshPlayerList, 0.5f, true);
	}
}

void UMatchWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BindRetryTimerHandle);
		World->GetTimerManager().ClearTimer(PlayerListRefreshTimerHandle);
		World->GetTimerManager().ClearTimer(NotificationHoldTimerHandle);
		World->GetTimerManager().ClearAllTimersForObject(this); // SetTimerForNextTick으로 예약한 ShowNextNotification 포함
	}

	if (AEscapeRoomGameState* GS = BoundGameState.Get())
	{
		GS->OnDiceProgressChanged.RemoveAll(this);
		GS->OnActiveExitCountChanged.RemoveAll(this);
		GS->OnMatchNotification.RemoveAll(this);
	}

	if (NotificationAnim)
	{
		UnbindAllFromAnimationFinished(NotificationAnim);
	}
	PendingNotifications.Empty();
	bIsShowingNotification = false;

	Super::NativeDestruct();
}

void UMatchWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 부감 쿨타임 아이콘은 홀드 여부와 무관하게 매 틱 갱신 (술래가 아니면 내부에서 바로 숨기고 끝)
	UpdateOverheadSkillIcon();

	// 남은 시간 표시 (홀드 게이지 조기 return보다 먼저)
	UpdateMatchTimer();

	// bHoldGaugeActive가 false면(홀드 중이 아니면) 여기서 바로 리턴 - 캐스팅/타이머 조회 등 실제 작업은
	// 전혀 하지 않음. UUserWidget 자체의 매 프레임 Tick 디스패치는 막을 수 없지만(TickFrequency가 private),
	// 이 조기 return으로 사실상 컴포넌트의 SetComponentTickEnabled(false)와 동일한 효과를 냄
	if (!bHoldGaugeActive || !HoldProgressBar)
	{
		return;
	}

	// 이 위젯을 소유한 로컬 플레이어의 폰을 직접 물어봄 - 러너/술래 어느 쪽이 빙의 중이든
	// 공통 부모(AescaperoomCharacter)의 GetInteractHoldPercent()만 부르면 됨
	// 평소엔 러너/술래(AescaperoomCharacter)가 조종되지만, 캐비넷에 숨어있는 동안은 캐비넷 자체(ACabinetActor)가
	// 조종되는 폰으로 바뀌므로(나가기 홀드가 여기서 처리됨) 둘 다 시도해봄
	float HoldPercent = 0.f;
	if (const AescaperoomCharacter* MyCharacter = Cast<AescaperoomCharacter>(GetOwningPlayerPawn()))
	{
		HoldPercent = MyCharacter->GetInteractHoldPercent();
	}
	else if (const ACabinetActor* MyCabinet = Cast<ACabinetActor>(GetOwningPlayerPawn()))
	{
		HoldPercent = MyCabinet->GetExitHoldPercent();
	}

	if (HoldProgressBar->GetVisibility() != ESlateVisibility::Visible)
	{
		HoldProgressBar->SetVisibility(ESlateVisibility::Visible);
	}
	HoldProgressBar->SetPercent(HoldPercent);
}

void UMatchWidget::UpdateOverheadSkillIcon()
{
	if (!OverheadSkillIcon)
	{
		return;
	}

	// 술래 본인 화면에서만 표시 (러너, 탈출 후 관전자 등은 숨김)
	const AChaserCharacter* Chaser = Cast<AChaserCharacter>(GetOwningPlayerPawn());
	if (!Chaser)
	{
		if (OverheadSkillIcon->GetVisibility() != ESlateVisibility::Collapsed)
		{
			OverheadSkillIcon->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (OverheadCooldownText && OverheadCooldownText->GetVisibility() != ESlateVisibility::Collapsed)
		{
			OverheadCooldownText->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	if (OverheadSkillIcon->GetVisibility() != ESlateVisibility::SelfHitTestInvisible)
	{
		OverheadSkillIcon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	const bool bActive = Chaser->bIsOverheadViewActive;
	const float CooldownRemaining = Chaser->GetOverheadCooldownRemaining();
	const bool bOnCooldown = !bActive && CooldownRemaining > 0.f;

	// 1 = 전부 어두움(쿨타임 막 시작) -> 0 = 사용 가능. 사용 중/준비됨은 0 (어두운 부분 없음)
	const float Progress = bOnCooldown ? Chaser->GetOverheadCooldownFraction() : 0.f;
	const FLinearColor Tint = bActive ? SkillActiveTint : (bOnCooldown ? SkillCooldownTint : SkillReadyTint);

	if (OverheadSkillIconMID)
	{
		OverheadSkillIconMID->SetScalarParameterValue(CooldownProgressParamName, Progress);
		OverheadSkillIconMID->SetVectorParameterValue(TEXT("Tint"), Tint);
	}
	else
	{
		// 머티리얼이 없으면 색만으로 구분
		OverheadSkillIcon->SetColorAndOpacity(Tint);
	}

	// 쿨타임 남은 초 (올림 - 15,14,...,1), 바뀔 때만 갱신
	if (OverheadCooldownText)
	{
		const int32 Seconds = bOnCooldown ? FMath::CeilToInt(CooldownRemaining) : 0;
		if (Seconds != LastShownCooldownSeconds)
		{
			LastShownCooldownSeconds = Seconds;
			if (Seconds > 0)
			{
				OverheadCooldownText->SetText(FText::AsNumber(Seconds));
				OverheadCooldownText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			}
			else
			{
				OverheadCooldownText->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}
}

void UMatchWidget::SetHoldGaugeActive(bool bActive, const FText& HoldText)
{
	// bHoldGaugeActive만 켜고 끔 - NativeTick 맨 위에서 이 플래그를 검사해 조기 return하므로,
	// 꺼진 동안은 캐스팅/타이머 조회 같은 실제 작업이 전혀 일어나지 않는다.
	// 꺼질 때(홀드 취소/완료)는 다음 홀드까지 값이 남아있지 않도록 즉시 숨겨둠
	bHoldGaugeActive = bActive;
	CurrentHoldText = HoldText;

	if (!bActive && HoldProgressBar)
	{
		HoldProgressBar->SetVisibility(ESlateVisibility::Collapsed);
	}

	RefreshHoldActionText();
}

void UMatchWidget::SetPromptTextActive(bool bActive, const FText& PromptText)
{
	// 홀드 게이지와 별개의 상태 - 게이지 바는 전혀 안 건드리고 HoldActionText만 갱신함
	bPromptTextActive = bActive;
	CurrentPromptText = PromptText;

	RefreshHoldActionText();
}

void UMatchWidget::RefreshHoldActionText()
{
	if (!HoldActionText)
	{
		return;
	}

	// 우선순위: 홀드 중이면 홀드 문구, 아니면 프롬프트 문구, 둘 다 없으면 숨김
	if (bHoldGaugeActive && !CurrentHoldText.IsEmpty())
	{
		HoldActionText->SetText(CurrentHoldText);
		HoldActionText->SetVisibility(ESlateVisibility::Visible);
	}
	else if (bPromptTextActive && !CurrentPromptText.IsEmpty())
	{
		HoldActionText->SetText(CurrentPromptText);
		HoldActionText->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		HoldActionText->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UMatchWidget::TryBindToGameState()
{
	auto* GS = GetWorld() ? GetWorld()->GetGameState<AEscapeRoomGameState>() : nullptr;
	if (!GS)
	{
		// 아직 GameState가 리플리케이트되지 않음(클라 기준) - 짧은 주기로 재시도
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(BindRetryTimerHandle, this, &UMatchWidget::TryBindToGameState, 0.2f, false);
		}
		return;
	}

	BoundGameState = GS;
	GS->OnDiceProgressChanged.AddUObject(this, &UMatchWidget::HandleDiceProgressChanged);
	GS->OnActiveExitCountChanged.AddUObject(this, &UMatchWidget::HandleActiveExitCountChanged);
	GS->OnMatchNotification.AddUObject(this, &UMatchWidget::HandleMatchNotification);

	// 바인딩된 시점의 현재 값으로 1회 초기화 (그 이후로는 델리게이트로만 갱신, 폴링 없음)
	HandleDiceProgressChanged(GS->TotalDiceCollected, GS->GetCurrentStageDiceRequirement());
	HandleActiveExitCountChanged(GS->ActiveExitCount);
}

void UMatchWidget::HandleDiceProgressChanged(int32 Collected, int32 Required)
{
	if (DiceCountText)
	{
		DiceCountText->SetText(FText::FromString(FString::Printf(TEXT("%d : %d"), Collected, Required)));
	}
}

void UMatchWidget::HandleActiveExitCountChanged(int32 Count)
{
	// 남은 탈출구가 있으면 섹션을 켜고 개수 표시, 0이면(아직 안 열렸거나 전부 쓰임) 숨김
	SetExitSectionVisible(Count > 0);

	if (ExitCountText)
	{
		ExitCountText->SetText(FText::AsNumber(Count));
	}
}

// =========================== 매치 알림 ===========================

void UMatchWidget::HandleMatchNotification(const FText& Message, EMatchNotifySound SoundType)
{
	PendingNotifications.Enqueue(FPendingNotification{ Message, SoundType });

	// 표시 중이면 큐에서 기다렸다가, 지금 알림의 역재생이 끝날 때 ShowNextNotification이 꺼내감
	if (!bIsShowingNotification)
	{
		ShowNextNotification();
	}
}

void UMatchWidget::ShowNextNotification()
{
	FPendingNotification NextNotify;
	if (!NotificationText || !PendingNotifications.Dequeue(NextNotify))
	{
		// 큐가 비었음(또는 WBP에 텍스트가 없음) - 다음 알림이 오면 바로 표시되도록 대기 상태로
		bIsShowingNotification = false;
		return;
	}

	bIsShowingNotification = true;
	bIsNotificationReversing = false;

	NotificationText->SetText(NextNotify.Message);

	// 알림음 - 큐에서 꺼내 실제로 보여주는 이 순간에 재생해서 화면 표시와 소리가 항상 맞음.
	// 감옥문 열림이면 JailDoorOpenedSound(비어있으면 공통), 나머지는 공통 알림음. 2D라 맵 어디서든 들림
	USoundBase* SoundToPlay = NotificationSound.Get();
	if (NextNotify.SoundType == EMatchNotifySound::JailDoorOpened && JailDoorOpenedSound)
	{
		SoundToPlay = JailDoorOpenedSound.Get();
	}
	if (SoundToPlay)
	{
		UGameplayStatics::PlaySound2D(this, SoundToPlay);
	}
	// 패널이 있으면 패널을 켬 (텍스트는 WBP 기본값 그대로 두면 부모를 따라 보임).
	// HitTestInvisible = 보이기만 하고 클릭은 막지 않음
	if (NotificationPanel)
	{
		NotificationPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		NotificationText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	if (NotificationAnim)
	{
		PlayAnimationForward(NotificationAnim);
	}
	else
	{
		OnNotificationAnimFinished(); // 애니메이션이 없으면 등장 연출을 건너뛰고 바로 유지 단계로
	}
}

void UMatchWidget::OnNotificationAnimFinished()
{
	if (!bIsShowingNotification)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!bIsNotificationReversing)
	{
		// 등장 끝 -> 유지. (SetTimer에 0 이하를 넣으면 타이머가 아예 안 돌아서 최소값을 둠)
		World->GetTimerManager().SetTimer(NotificationHoldTimerHandle, this, &UMatchWidget::OnNotificationHoldFinished,
			FMath::Max(NotificationHoldSeconds, 0.01f), false);
		return;
	}

	// 퇴장 끝 -> 숨기고 다음 알림.
	// 같은 애니메이션의 종료 콜백 안에서 곧바로 그 애니메이션을 다시 재생하면 엔진이 끝난 플레이어를
	// 정리하는 과정과 겹칠 수 있어서, 다음 틱으로 한 번 미뤄서 꺼냄 (그 사이 bIsShowingNotification은
	// true로 유지되므로 새로 온 알림은 큐에서 기다림)
	if (UWidget* NotifyRoot = GetNotificationRoot())
	{
		NotifyRoot->SetVisibility(ESlateVisibility::Collapsed);
	}
	World->GetTimerManager().SetTimerForNextTick(this, &UMatchWidget::ShowNextNotification);
}

UWidget* UMatchWidget::GetNotificationRoot() const
{
	return NotificationPanel ? NotificationPanel : static_cast<UWidget*>(NotificationText);
}

void UMatchWidget::OnNotificationHoldFinished()
{
	bIsNotificationReversing = true;

	if (NotificationAnim)
	{
		PlayAnimationReverse(NotificationAnim);
	}
	else
	{
		OnNotificationAnimFinished(); // 애니메이션이 없으면 바로 숨기고 다음으로
	}
}

void UMatchWidget::SetExitSectionVisible(bool bVisible)
{
	const ESlateVisibility NewVisibility = bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;

	if (ExitIcon)
	{
		ExitIcon->SetVisibility(NewVisibility);
	}
	if (ExitCountText)
	{
		ExitCountText->SetVisibility(NewVisibility);
	}
}

void UMatchWidget::RefreshPlayerList()
{
	if (!PlayerListBox || !PlayerEntryWidgetClass)
	{
		return;
	}

	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!GS)
	{
		return; // 클라 기준 아직 GameState 리플리케이트 전 - 다음 폴링 때 다시 시도
	}

	int32 VisibleCount = 0;

	for (APlayerState* PS : GS->PlayerArray)
	{
		const AMatchPlayerState* MPS = Cast<AMatchPlayerState>(PS);

		// nullptr, 술래, 아직 역할이 안 정해진(접속 직후) 플레이어는 표시하지 않음 -> 러너만
		if (!MPS || !MPS->IsRunner())
		{
			continue;
		}

		// 엔트리가 모자라면 하나 새로 만들어서 PlayerListBox에 추가
		if (!PlayerEntries.IsValidIndex(VisibleCount))
		{
			UMatchPlayerEntryWidget* NewEntry = CreateWidget<UMatchPlayerEntryWidget>(GetOwningPlayer(), PlayerEntryWidgetClass);
			if (!NewEntry)
			{
				break;
			}

			if (UVerticalBoxSlot* EntrySlot = Cast<UVerticalBoxSlot>(PlayerListBox->AddChild(NewEntry)))
			{
				EntrySlot->SetHorizontalAlignment(HAlign_Fill);
				EntrySlot->SetPadding(FMargin(0.f, 4.f));
			}
			PlayerEntries.Add(NewEntry);
		}

		UMatchPlayerEntryWidget* Entry = PlayerEntries[VisibleCount];
		Entry->SetPlayerInfo(MPS->GetPlayerName(), MPS->IsInJail(), MPS->HasEscaped());
		Entry->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		++VisibleCount;
	}

	// 나간 러너 몫으로 남은 엔트리는 숨김 (재사용 대기)
	for (int32 i = VisibleCount; i < PlayerEntries.Num(); ++i)
	{
		if (PlayerEntries[i])
		{
			PlayerEntries[i]->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UMatchWidget::UpdateMatchTimer()
{
	if (!MatchTimerText)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const AEscapeRoomGameState* GS = World ? World->GetGameState<AEscapeRoomGameState>() : nullptr;
	if (!GS)
	{
		return;
	}

	const float Remaining = GS->GetMatchRemainingSeconds();
	const bool bTimeOut = GS->HasMatchClockStarted() && GS->MatchTimeLimitSeconds > 0.f && Remaining <= 0.f;
	const int32 ShownSeconds = bTimeOut ? -1 : FMath::CeilToInt(Remaining);

	if (ShownSeconds == LastShownTimerSeconds)
	{
		return;
	}
	LastShownTimerSeconds = ShownSeconds;

	MatchTimerText->SetText(GS->GetMatchRemainingTimeText());
	const bool bWarning = bTimeOut || (GS->HasMatchClockStarted() && Remaining <= TimerWarningSeconds);
	MatchTimerText->SetColorAndOpacity(FSlateColor(bWarning ? TimerWarningColor : TimerNormalColor));
}
