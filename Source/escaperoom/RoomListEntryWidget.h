// [설계 메모] 방 목록(RoomListScrollBox)의 한 줄(row)을 나타내는 위젯. CreateJoinRoomWidget이
// GameState->Rooms를 폴링할 때마다 이 위젯을 방 개수만큼 새로 생성해서 채워넣는다.
// JoinButton을 누르면 곧바로 서버에 입장 요청을 보내지 않고, 부모(CreateJoinRoomWidget)에게
// "이 RoomId로 입장하고 싶다"고 알리기만 한다. 실제 애니메이션 재생 + 서버 요청은 부모 쪽에서 처리
// (이 위젯은 0.5초 폴링마다 통째로 재생성되므로, 여기서 Delay를 걸면 애니메이션 도중 위젯이
// 파괴될 수 있어 위험함).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RoomListEntryWidget.generated.h"

class UTextBlock;
class UImage;
class UButton;
class UCreateJoinRoomWidget;

UCLASS()
class ESCAPEROOM_API URoomListEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// CreateJoinRoomWidget이 방 목록을 채울 때마다 방 하나의 정보로 이 항목을 초기화
	UFUNCTION(BlueprintCallable, Category = "Room")
	void SetRoomInfo(int32 InRoomId, const FString& InRoomName, int32 InCurrentMembers, int32 InMaxMembers);

	// CreateJoinRoomWidget이 이 항목을 생성할 때 자기 자신을 등록 - 클릭 시 부모에게 알리기 위해 필요
	void SetOwningListWidget(UCreateJoinRoomWidget* InOwner) { OwningListWidget = InOwner; }

	// 부모의 애니메이션까지 다 끝난 뒤 최종적으로 호출되는 실제 입장 요청 (부모 쪽 Delay 이후 호출됨)
	UFUNCTION(BlueprintCallable, Category = "Room")
	void RequestJoinThisRoom();

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidget))
	UTextBlock* RoomNameText;

	UPROPERTY(meta = (BindWidget))
	UTextBlock* MemberCountText;

	// 방 모드/상태 등을 나타내는 아이콘 - 어떤 이미지를 어떻게 보여줄지는 순전히 블루프린트 몫이라
	// C++에서는 값을 세팅하지 않음(필요해지면 SetRoomInfo에 파라미터 추가해서 다루면 됨)
	UPROPERTY(meta = (BindWidgetOptional))
	UImage* RoomIcon;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* JoinButton;

private:
	UFUNCTION()
	void HandleJoinButtonClicked();

	UPROPERTY()
	UCreateJoinRoomWidget* OwningListWidget = nullptr;

	int32 RoomId = -1;
};
