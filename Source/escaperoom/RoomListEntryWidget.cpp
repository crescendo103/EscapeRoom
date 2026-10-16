#include "RoomListEntryWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "StartScreenController.h"
#include "CreateJoinRoomWidget.h"

void URoomListEntryWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (JoinButton)
	{
		JoinButton->OnClicked.AddDynamic(this, &URoomListEntryWidget::HandleJoinButtonClicked);
	}
}

void URoomListEntryWidget::SetRoomInfo(int32 InRoomId, const FString& InRoomName, int32 InCurrentMembers, int32 InMaxMembers)
{
	RoomId = InRoomId;

	if (RoomNameText)
	{
		RoomNameText->SetText(FText::FromString(InRoomName));
	}
	if (MemberCountText)
	{
		MemberCountText->SetText(FText::FromString(FString::Printf(TEXT("%d : %d"), InCurrentMembers, InMaxMembers)));
	}
}

void URoomListEntryWidget::HandleJoinButtonClicked()
{
	if (OwningListWidget)
	{
		// 부모에게 알려서 블루프린트가 연출용 애니메이션을 재생하게 함
		OwningListWidget->HandleRoomJoinRequested(RoomId);
		// 실제 서버 입장 요청은 애니메이션 재생 시간(0.5초)만큼 늦춰서 부모가 보내도록 위임
		// (이 항목 위젯은 0.5초 폴링마다 통째로 재생성될 수 있어 여기서 직접 Delay를 걸면 위험함)
		OwningListWidget->RequestJoinRoomDelayed(RoomId);
	}
	else
	{
		// 부모 참조가 없는 예외 상황을 대비한 폴백: 지연 없이 바로 입장 요청
		RequestJoinThisRoom();
	}
}

void URoomListEntryWidget::RequestJoinThisRoom()
{
	if (AStartScreenController* PC = Cast<AStartScreenController>(GetOwningPlayer()))
	{
		PC->Server_JoinRoom(RoomId);
	}
}
