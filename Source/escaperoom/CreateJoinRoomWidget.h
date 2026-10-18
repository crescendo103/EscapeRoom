// [설계 메모] 각 버튼의 클릭 애니메이션은 블루프린트 이벤트 그래프에서 재생합니다.
// 애니메이션은 블루프린트로 만드는 게 훨씬 편해서, C++은 버튼 OnClicked에 직접 바인딩하지 않고
// 실제 동작(방 생성/입장/뒤로가기)만 BlueprintCallable 함수로 노출해둡니다.
// 블루프린트 쪽에서 "On Clicked -> Play Animation -> Delay -> 아래 함수 호출" 순서로
// 실행 흐름을 직접 구성해야 합니다 (실행 로직은 블루프린트가 갖고 있음).
//
// [방 입장 UI 변경] RoomId를 직접 숫자로 입력받던 방식은 폐기하고, GameState->Rooms(리플리케이트된
// 방 목록)를 폴링해서 RoomListScrollBox에 RoomListEntryWidget을 방 개수만큼 채우는 방식으로 바꿈.
// 각 항목을 클릭하면 RoomListEntryWidget이 두 가지를 함:
//   1) HandleRoomJoinRequested(RoomId) [BlueprintImplementableEvent] 호출 -> 블루프린트가 연출용
//      애니메이션만 재생 (입장 요청과는 무관, 그냥 알림용)
//   2) RequestJoinRoomDelayed(RoomId) 호출 -> 이 위젯이 0.5초 타이머를 걸어놨다가 실제 Server_JoinRoom을 보냄
// 딜레이를 항목(RoomListEntryWidget) 쪽이 아니라 항상 살아있는 여기(부모)에서 거는 이유: 항목 위젯은
// RoomListScrollBox가 0.5초 폴링마다 통째로 재생성하면서 파괴될 수 있어서, 거기서 지연 호출을 걸면
// 애니메이션 재생 도중 위젯이 사라져 콜백이 못 불릴 위험이 있음.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Engine/TimerHandle.h"
#include "CreateJoinRoomWidget.generated.h"

class UButton;
class UEditableTextBox;
class UScrollBox;
class URoomListEntryWidget;

UCLASS()
class ESCAPEROOM_API UCreateJoinRoomWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// CreateRoomButton 애니메이션 재생이 끝난 뒤 블루프린트에서 호출: 방 생성 요청
	UFUNCTION(BlueprintCallable, Category = "Room")
	void RequestCreateRoom();

	// BackButton 애니메이션 재생이 끝난 뒤 블루프린트에서 호출: StartScreen으로 복귀
	UFUNCTION(BlueprintCallable, Category = "Navigation")
	void GoBackToStartScreen();

	// RoomListEntryWidget의 JoinButton이 클릭됐다는 알림용 이벤트. 여기서는 블루프린트 이벤트
	// 그래프에서 연출용 애니메이션만 재생하면 됨 (실제 입장 요청은 RequestJoinRoomDelayed가 별도로 처리).
	UFUNCTION(BlueprintImplementableEvent, Category = "Room")
	void HandleRoomJoinRequested(int32 RoomId);

	// RoomListEntryWidget이 클릭 시 호출: 애니메이션 재생 시간(0.5초)만큼 늦춘 뒤 실제 서버 입장 요청을 보냄
	void RequestJoinRoomDelayed(int32 RoomId);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// Create Room section
	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* RoomNameTextBox;

	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* MaxMembersTextBox;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* CreateRoomButton;

	// Join Room section - 방 목록을 스크롤 리스트로 보여주고, 각 항목(RoomListEntryWidget) 클릭으로 입장
	UPROPERTY(meta = (BindWidget))
	UScrollBox* RoomListScrollBox;

	UPROPERTY(EditDefaultsOnly, Category = "Room")
	TSubclassOf<URoomListEntryWidget> RoomListEntryWidgetClass;

	// Navigation
	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BackButton;

private:
	// 입력 필터: 방 이름 = 영문만, 최대 인원 = 숫자만
	UFUNCTION()
	void OnRoomNameChanged(const FText& Text);

	UFUNCTION()
	void OnMaxMembersChanged(const FText& Text);

	// GameState->Rooms(리플리케이트된 방 목록)를 폴링해서 RoomListScrollBox를 다시 채움
	void RefreshRoomList();

	// RequestJoinRoomDelayed의 타이머가 끝난 뒤 실제로 서버에 입장 요청을 보내는 함수
	void RequestJoinRoomById(int32 RoomId);

	FTimerHandle RoomListRefreshTimerHandle;
	FTimerHandle JoinRequestDelayTimerHandle;
};
