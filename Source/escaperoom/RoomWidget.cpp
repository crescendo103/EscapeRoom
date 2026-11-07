#include "RoomWidget.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Engine/Texture2D.h"
#include "RoomMemberEntryWidget.h"
#include "StartScreenController.h"
#include "EscapeRoomGameState.h"
#include "EscapeRoomPlayerState.h"
#include "TimerManager.h"

void URoomWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 디자이너에서 Collapsed로 안 해뒀어도 처음엔 숨김
	UpdateLoadingState(false);

	RefreshRoomInfo();

	if (UWorld* World = GetWorld())
	{
		// Poll the replicated room list periodically to keep name/member-count up to date.
		World->GetTimerManager().SetTimer(RefreshTimerHandle, this, &URoomWidget::RefreshRoomInfo, 0.5f, true);
	}
}

void URoomWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimerHandle);
		World->GetTimerManager().ClearTimer(LoadingImageTimerHandle);
	}
	Super::NativeDestruct();
}

void URoomWidget::RefreshRoomInfo()
{
	APlayerController* PC = GetOwningPlayer();
	auto* EP = PC ? PC->GetPlayerState<AEscapeRoomPlayerState>() : nullptr;
	auto* GS = GetWorld() ? GetWorld()->GetGameState<AEscapeRoomGameState>() : nullptr;
	if (!EP || !GS || EP->CurrentRoomId < 0) return;

	if (FRoomInfo* Room = GS->FindRoom(EP->CurrentRoomId))
	{
		if (RoomNameText)
		{
			RoomNameText->SetText(FText::FromString(Room->RoomName));
		}
		// 매치 시작 후에는 인원수/멤버 목록을 마지막 모습 그대로 고정.
		// 술래가 먼저 맵을 옮기면 로비 서버 Logout -> Members에서 빠지는데, 이게 복제되면 남은 사람 화면에서 줄이 사라졌음.
		// (시작 실패로 서버가 bMatchStarted를 false로 되돌리면 다시 갱신됨)
		if (!Room->bMatchStarted)
		{
			if (MemberCountText)
			{
				MemberCountText->SetText(FText::FromString(FString::Printf(TEXT("%d : %d"), Room->Members.Num(), Room->MaxMembers)));
			}

			RefreshMemberList(Room->Members);
		}

		// 서버가 매치 서버를 띄우기 시작하면 true (실패하면 서버가 다시 false로 돌려서 패널도 꺼짐)
		UpdateLoadingState(Room->bMatchStarted);

		// UpdateLoadingState가 로딩 기준으로 켠 Ready 버튼을 인원 기준으로 한 번 더 판정
		if (ReadyButton)
		{
			ReadyButton->SetIsEnabled(CanPressReady(*Room, EP->bIsReady));
		}
	}
}

bool URoomWidget::CanPressReady(const FRoomInfo& Room, bool bSelfReady) const
{
	if (Room.bMatchStarted)
	{
		return false; // 로딩 중
	}

	// 혼자면 Ready 불가. 단 이미 Ready인 상태로 상대가 나갔다면 해제는 가능해야 하므로 활성
	return Room.Members.Num() >= MinPlayersToReady || bSelfReady;
}

void URoomWidget::UpdateLoadingState(bool bLoading)
{
	const ESlateVisibility Vis = bLoading ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;

	// 패널은 Visible이라 뒤의 버튼 클릭을 막아줌. 텍스트/이미지는 따로 이름 붙였을 때만 같이 켜고 끔
	if (LoadingPanel) LoadingPanel->SetVisibility(Vis);
	if (LoadingImage) LoadingImage->SetVisibility(bLoading ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (LoadingText)
	{
		LoadingText->SetVisibility(bLoading ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bLoading)
		{
			// 0.5초 폴링마다 점 1~3개 순환
			LoadingDotCount = LoadingDotCount % 3 + 1;
			LoadingText->SetText(FText::FromString(LoadingMessage.ToString() + FString::ChrN(LoadingDotCount, TEXT('.'))));
		}
	}
	if (!bLoading) LoadingDotCount = 0;

	// 로딩이 막 시작/종료된 순간에만 이미지 슬라이드 타이머를 켜고 끔
	// (로딩 대기 중에도 클라 로비 월드는 정상 Tick - 기다리는 건 서버 쪽 타이머라서)
	if (bLoading != bLoadingShown)
	{
		bLoadingShown = bLoading;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(LoadingImageTimerHandle);
			if (bLoading && LoadingImage && LoadingImages.Num() > 0)
			{
				LoadingImageIndex = 0;
				LoadingImage->SetBrushFromTexture(LoadingImages[0]);
				if (LoadingImages.Num() > 1)
				{
					World->GetTimerManager().SetTimer(LoadingImageTimerHandle, this, &URoomWidget::ShowNextLoadingImage,
						FMath::Max(LoadingImageInterval, 0.1f), true);
				}
			}
		}
	}

	// 로딩 중 Ready 해제/방 나가기 막기 (매치 서버는 이미 이 인원 기준으로 뜨는 중)
	if (ReadyButton) ReadyButton->SetIsEnabled(!bLoading);
	if (LeaveRoomButton) LeaveRoomButton->SetIsEnabled(!bLoading);
}

void URoomWidget::RefreshMemberList(const TArray<TObjectPtr<APlayerState>>& Members)
{
	if (!MemberListScrollBox || !MemberEntryWidgetClass)
	{
		return;
	}

	int32 VisibleCount = 0;

	for (const TObjectPtr<APlayerState>& Member : Members)
	{
		// 클라 기준 아직 리플리케이트 안 된 PlayerState는 nullptr - 다음 폴링 때 자연스럽게 채워짐
		if (!Member)
		{
			continue;
		}

		// 엔트리가 모자라면 하나 새로 만들어서 스크롤박스에 추가
		if (!MemberEntries.IsValidIndex(VisibleCount))
		{
			URoomMemberEntryWidget* NewEntry = CreateWidget<URoomMemberEntryWidget>(GetOwningPlayer(), MemberEntryWidgetClass);
			if (!NewEntry)
			{
				break;
			}

			if (UScrollBoxSlot* EntrySlot = Cast<UScrollBoxSlot>(MemberListScrollBox->AddChild(NewEntry)))
			{
				EntrySlot->SetHorizontalAlignment(HAlign_Fill);
				EntrySlot->SetPadding(FMargin(0.f, 4.f));
			}
			MemberEntries.Add(NewEntry);
		}

		const auto* MemberEP = Cast<AEscapeRoomPlayerState>(Member);
		const bool bIsReady = MemberEP && MemberEP->bIsReady;

		URoomMemberEntryWidget* Entry = MemberEntries[VisibleCount];
		Entry->SetMemberInfo(Member->GetPlayerName(), bIsReady);
		Entry->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		++VisibleCount;
	}

	// 나간 사람 몫으로 남은 엔트리는 숨김 (다음에 사람이 들어오면 재사용)
	for (int32 i = VisibleCount; i < MemberEntries.Num(); ++i)
	{
		if (MemberEntries[i])
		{
			MemberEntries[i]->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void URoomWidget::RequestToggleReady()
{
	APlayerController* PC = GetOwningPlayer();
	auto* SSC = Cast<AStartScreenController>(PC);
	auto* EP = PC ? PC->GetPlayerState<AEscapeRoomPlayerState>() : nullptr;
	if (!SSC || !EP) return;
	if (IsMatchLoading()) return; // 버튼 애니메이션 Delay 중에 로딩이 시작된 경우

	// 버튼 애니메이션 Delay 중에 상대가 나가서 혼자가 된 경우 (Ready 해제는 허용)
	auto* GS = GetWorld() ? GetWorld()->GetGameState<AEscapeRoomGameState>() : nullptr;
	const FRoomInfo* Room = GS ? GS->FindRoom(EP->CurrentRoomId) : nullptr;
	if (!Room || !CanPressReady(*Room, EP->bIsReady)) return;

	SSC->Server_SetReady(!EP->bIsReady);
}

void URoomWidget::RequestLeaveRoom()
{
	if (IsMatchLoading()) return;
	if (auto* SSC = Cast<AStartScreenController>(GetOwningPlayer()))
	{
		SSC->Server_LeaveRoom();
	}
}

bool URoomWidget::IsMatchLoading() const
{
	APlayerController* PC = GetOwningPlayer();
	auto* EP = PC ? PC->GetPlayerState<AEscapeRoomPlayerState>() : nullptr;
	auto* GS = GetWorld() ? GetWorld()->GetGameState<AEscapeRoomGameState>() : nullptr;
	if (!EP || !GS || EP->CurrentRoomId < 0) return false;

	const FRoomInfo* Room = GS->FindRoom(EP->CurrentRoomId);
	return Room && Room->bMatchStarted;
}

void URoomWidget::ShowNextLoadingImage()
{
	if (!LoadingImage || LoadingImages.Num() == 0) return;

	LoadingImageIndex = (LoadingImageIndex + 1) % LoadingImages.Num();
	if (UTexture2D* Tex = LoadingImages[LoadingImageIndex])
	{
		LoadingImage->SetBrushFromTexture(Tex);
	}
}
