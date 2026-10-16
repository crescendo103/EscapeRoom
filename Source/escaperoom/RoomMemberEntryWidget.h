// [설계 메모] 로비 방(RoomWidget) 안의 MemberListScrollBox에 들어가는 "멤버 한 줄" 위젯.
// 구성은 두 가지뿐: 준비완료 여부 이미지(ReadyImage) + 플레이어 이름 텍스트(PlayerNameText).
// RoomWidget::RefreshRoomInfo(0.5초 폴링)가 매번 SetMemberInfo()를 불러서 이름/레디 상태를 갱신한다.
// 레디 여부에 따라 바뀔 텍스처 두 장(ReadyTexture / NotReadyTexture)은 WBP의 Class Defaults에서 지정.
// 텍스처를 안 넣어두면 같은 이미지에 색(ReadyTint / NotReadyTint)만 바꿔서 구분한다.
// SetMemberInfo는 값이 실제로 바뀌었을 때만 위젯을 건드리므로 0.5초마다 불려도 부담 없음.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RoomMemberEntryWidget.generated.h"

class UImage;
class UTextBlock;
class UTexture2D;

UCLASS()
class ESCAPEROOM_API URoomMemberEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// RoomWidget이 폴링할 때마다 호출: 이름과 준비완료 상태를 반영
	void SetMemberInfo(const FString& PlayerName, bool bIsReady);

protected:
	virtual void NativeConstruct() override;

	// 준비완료 여부를 보여주는 이미지 (WBP에서 이 이름 그대로 만들어야 함)
	UPROPERTY(meta = (BindWidget))
	UImage* ReadyImage;

	// 플레이어 이름 (WBP에서 이 이름 그대로 만들어야 함)
	UPROPERTY(meta = (BindWidget))
	UTextBlock* PlayerNameText;

	// 준비완료일 때 표시할 텍스처
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RoomMember")
	TObjectPtr<UTexture2D> ReadyTexture;

	// 준비 안 됐을 때 표시할 텍스처
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RoomMember")
	TObjectPtr<UTexture2D> NotReadyTexture;

	// 텍스처를 지정하지 않았을 때 대신 쓰는 색 (텍스처를 넣었다면 흰색 그대로 두면 됨)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RoomMember")
	FLinearColor ReadyTint = FLinearColor(0.2f, 0.9f, 0.3f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RoomMember")
	FLinearColor NotReadyTint = FLinearColor(0.35f, 0.35f, 0.35f, 1.f);

private:
	void ApplyReadyVisual(bool bIsReady);

	// 폴링 때마다 같은 값으로 위젯을 다시 칠하지 않도록 마지막 상태를 기억
	FString CachedName;
	bool bCachedReady = false;
	bool bHasCachedState = false;
};
