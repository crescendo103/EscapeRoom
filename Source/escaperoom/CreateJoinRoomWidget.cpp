#include "CreateJoinRoomWidget.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "RoomListEntryWidget.h"
#include "StartScreenController.h"
#include "EscapeRoomGameState.h"
#include "TimerManager.h"

void UCreateJoinRoomWidget::NativeConstruct()
{
	Super::NativeConstruct();

	RefreshRoomList();

	if (UWorld* World = GetWorld())
	{
		// RoomWidget이 자기 방 정보를 폴링하는 것과 같은 패턴: 0.5초마다 리플리케이트된 방 목록을 다시 읽음
		World->GetTimerManager().SetTimer(RoomListRefreshTimerHandle, this, &UCreateJoinRoomWidget::RefreshRoomList, 0.5f, true);
	}
}

void UCreateJoinRoomWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RoomListRefreshTimerHandle);
		World->GetTimerManager().ClearTimer(JoinRequestDelayTimerHandle);
	}
	Super::NativeDestruct();
}

void UCreateJoinRoomWidget::RefreshRoomList()
{
	if (!RoomListScrollBox || !RoomListEntryWidgetClass)
	{
		return;
	}

	auto* GS = GetWorld() ? GetWorld()->GetGameState<AEscapeRoomGameState>() : nullptr;
	if (!GS)
	{
		return;
	}

	RoomListScrollBox->ClearChildren();

	for (const FRoomInfo& Room : GS->Rooms)
	{
		// 이미 매치가 시작된 방은 입장해봐야 JoinRoom에서 거부되니 목록에서 제외
		if (Room.bMatchStarted)
		{
			continue;
		}

		if (URoomListEntryWidget* Entry = CreateWidget<URoomListEntryWidget>(GetOwningPlayer(), RoomListEntryWidgetClass))
		{
			Entry->SetRoomInfo(Room.RoomId, Room.RoomName, Room.Members.Num(), Room.MaxMembers);
			Entry->SetOwningListWidget(this);
			RoomListScrollBox->AddChild(Entry);
		}
	}
}

void UCreateJoinRoomWidget::RequestCreateRoom()
{
	AStartScreenController* PC = Cast<AStartScreenController>(GetOwningPlayer());
	if (!PC) return;

	FString RoomName = RoomNameTextBox ? RoomNameTextBox->GetText().ToString() : FString();
	if (RoomName.IsEmpty())
	{
		RoomName = TEXT("New Room");
	}

	int32 MaxMembers = 4;
	if (MaxMembersTextBox)
	{
		const FString Text = MaxMembersTextBox->GetText().ToString();
		if (Text.IsNumeric())
		{
			MaxMembers = FCString::Atoi(*Text);
		}
	}
	// 서버(LobbyGameMode::CreateRoom)와 동일하게 5명 제한 - UI에서도 미리 막아서 사용자 혼란 방지
	MaxMembers = FMath::Clamp(MaxMembers, 1, 5);

	PC->Server_CreateRoom(RoomName, MaxMembers);
}

void UCreateJoinRoomWidget::RequestJoinRoomDelayed(int32 RoomId)
{
	if (UWorld* World = GetWorld())
	{
		FTimerDelegate Delegate = FTimerDelegate::CreateUObject(this, &UCreateJoinRoomWidget::RequestJoinRoomById, RoomId);
		// 같은 핸들을 재사용 -> 연타해도 마지막 클릭 기준으로 0.5초 뒤 한 번만 나감
		World->GetTimerManager().SetTimer(JoinRequestDelayTimerHandle, Delegate, 0.5f, false);
	}
}

void UCreateJoinRoomWidget::RequestJoinRoomById(int32 RoomId)
{
	if (AStartScreenController* PC = Cast<AStartScreenController>(GetOwningPlayer()))
	{
		PC->Server_JoinRoom(RoomId);
	}
}

void UCreateJoinRoomWidget::GoBackToStartScreen()
{
	if (AStartScreenController* PC = Cast<AStartScreenController>(GetOwningPlayer()))
	{
		PC->ShowStartScreenWidget();
	}
}
