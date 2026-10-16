// [설계 메모] Ready/LeaveRoom 버튼의 클릭 애니메이션은 블루프린트 이벤트 그래프에서 재생합니다.
// 애니메이션은 블루프린트로 만드는 게 훨씬 편해서, C++은 버튼 OnClicked에 직접 바인딩하지 않고
// 실제 동작(레디 토글/방나가기)만 BlueprintCallable 함수로 노출해둡니다.
// 블루프린트 쪽에서 "On Clicked -> Play Animation -> Delay -> 아래 함수 호출" 순서로
// 실행 흐름을 직접 구성해야 합니다 (실행 로직은 블루프린트가 갖고 있음).
// 방 이름/인원수 표시는 버튼 클릭과 무관하게 NativeConstruct에서 반복 타이머로 갱신합니다.
//
// [멤버 목록] 같은 0.5초 폴링(RefreshRoomInfo)에서 Room->Members를 돌며 MemberListScrollBox를 채움.
// 클라에서 아직 리플리케이트되지 않은 PlayerState(nullptr)는 건너뛰고, 유효한 멤버만 표시한다.
// 방 목록(CreateJoinRoomWidget)처럼 매번 ClearChildren으로 전부 다시 만들지 않고, 만들어둔 엔트리를
// 재사용(모자라면 추가 생성, 남으면 Collapsed)해서 0.5초마다 위젯이 생성/파괴되지 않게 함.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Engine/TimerHandle.h"
#include "RoomWidget.generated.h"

class UButton;
class UTextBlock;
class UScrollBox;
class UImage;
class UWidget;
class UTexture2D;
class APlayerState;
class URoomMemberEntryWidget;

UCLASS()
class ESCAPEROOM_API URoomWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ReadyButton 애니메이션 재생이 끝난 뒤 블루프린트에서 호출: 레디 상태 토글
	UFUNCTION(BlueprintCallable, Category = "Room")
	void RequestToggleReady();

	// LeaveRoomButton 애니메이션 재생이 끝난 뒤 블루프린트에서 호출: 방 나가기 (뒤로가기 겸용)
	UFUNCTION(BlueprintCallable, Category = "Room")
	void RequestLeaveRoom();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	UTextBlock* RoomNameText;

	UPROPERTY(meta = (BindWidget))
	UTextBlock* MemberCountText;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* ReadyButton;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* LeaveRoomButton;

	// 방에 들어와 있는 멤버 목록 (각 줄 = RoomMemberEntryWidget)
	UPROPERTY(meta = (BindWidget))
	UScrollBox* MemberListScrollBox;

	// 스크롤박스에 넣을 멤버 한 줄 위젯 클래스 (WBP_RoomMemberEntry 등을 Class Defaults에서 지정)
	UPROPERTY(EditDefaultsOnly, Category = "Room")
	TSubclassOf<URoomMemberEntryWidget> MemberEntryWidgetClass;

	// ---- 게임 로딩 표시 (전원 Ready -> 서버가 매치 시작(bMatchStarted)하면 표시) ----
	// WBP 디자이너에서 같은 이름으로 추가. 기본 Visibility는 Collapsed로 두면 됨 (C++이 켜고 끔)
	// LoadingPanel: 텍스트/이미지를 감싸는 패널(Overlay 등). 이 패널 하나만 켜고 끔
	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* LoadingPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* LoadingText;

	// 그림은 디자이너에서 Brush로 지정 (C++은 표시만 같이 켜고 끔)
	UPROPERTY(meta = (BindWidgetOptional))
	UImage* LoadingImage;

	// 로딩 문구. 뒤에 점(. .. ...)이 0.5초마다 붙었다 빠짐
	UPROPERTY(EditAnywhere, Category = "Room|Loading")
	FText LoadingMessage = FText::FromString(TEXT("게임 로딩중"));

	void RefreshRoomInfo();

	// bMatchStarted에 맞춰 로딩 패널 표시/숨김 + Ready/나가기 버튼 잠금
	void UpdateLoadingState(bool bLoading);
	bool IsMatchLoading() const;

	int32 LoadingDotCount = 0;

	// ---- 로딩 이미지 슬라이드 (선택) ----
	// 비워두면 디자이너에서 LoadingImage에 지정한 그림 그대로. 채우면 로딩 중 순서대로 바꿔가며 보여줌
	UPROPERTY(EditAnywhere, Category = "Room|Loading")
	TArray<TObjectPtr<UTexture2D>> LoadingImages;

	// 이미지 하나당 보여주는 시간(초)
	UPROPERTY(EditAnywhere, Category = "Room|Loading", meta = (ClampMin = "0.1"))
	float LoadingImageInterval = 2.0f;

	void ShowNextLoadingImage();

	FTimerHandle LoadingImageTimerHandle;
	int32 LoadingImageIndex = 0;
	bool bLoadingShown = false;

	// Room->Members 중 nullptr가 아닌 멤버만 MemberListScrollBox에 반영
	void RefreshMemberList(const TArray<TObjectPtr<APlayerState>>& Members);

	FTimerHandle RefreshTimerHandle;

	// 재사용하는 멤버 엔트리들 (스크롤박스 자식 순서와 동일)
	UPROPERTY()
	TArray<TObjectPtr<URoomMemberEntryWidget>> MemberEntries;
};
