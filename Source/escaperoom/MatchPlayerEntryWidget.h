// [설계 메모] MatchWidget의 PlayerListScrollBox에 들어가는 "러너 한 줄" 위젯.
// 구성은 두 가지: 상태 이미지(JailStateImage - 자유/감옥/탈출 3가지 중 하나) + 플레이어 이름 텍스트(PlayerNameText).
// 로비의 RoomMemberEntryWidget(레디 이미지)과 같은 구조 - UMatchWidget::RefreshPlayerList(0.5초 폴링)가
// 매번 SetPlayerInfo()를 부르고, 값이 실제로 바뀔 때만 위젯을 다시 칠한다.
// 텍스처 세 장(FreeTexture / JailedTexture / EscapedTexture)은 WBP의 Class Defaults에서 지정.
// 텍스처를 안 넣어두면 같은 이미지에 색(FreeTint / JailedTint / EscapedTint)만 바꿔서 구분한다.
// 우선순위: 탈출 > 감옥 > 자유 (탈출한 러너는 감옥 여부와 상관없이 항상 탈출 이미지)

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MatchPlayerEntryWidget.generated.h"

class UImage;
class UTextBlock;
class UTexture2D;

// 목록 한 줄이 표시할 러너 상태
enum class EMatchPlayerEntryState : uint8
{
	Free,
	Jailed,
	Escaped
};

UCLASS()
class ESCAPEROOM_API UMatchPlayerEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPlayerInfo(const FString& PlayerName, bool bIsInJail, bool bHasEscaped = false);

protected:
	virtual void NativeConstruct() override;

	// 상태 이미지 (자유/감옥/탈출). WBP에서 이 이름 그대로 만들어야 함 - 기존 WBP 호환을 위해 이름은 그대로 둠
	UPROPERTY(meta = (BindWidget))
	UImage* JailStateImage;

	// 러너 이름 (WBP에서 이 이름 그대로 만들어야 함)
	UPROPERTY(meta = (BindWidget))
	UTextBlock* PlayerNameText;

	// 감옥 밖(자유) 상태일 때 텍스처
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MatchPlayer")
	TObjectPtr<UTexture2D> FreeTexture;

	// 감옥에 갇힌 상태일 때 텍스처
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MatchPlayer")
	TObjectPtr<UTexture2D> JailedTexture;

	// 탈출에 성공한 상태일 때 텍스처
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MatchPlayer")
	TObjectPtr<UTexture2D> EscapedTexture;

	// 텍스처 미지정 시 대신 쓰는 색
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MatchPlayer")
	FLinearColor FreeTint = FLinearColor(0.2f, 0.9f, 0.3f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MatchPlayer")
	FLinearColor JailedTint = FLinearColor(0.9f, 0.15f, 0.15f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MatchPlayer")
	FLinearColor EscapedTint = FLinearColor(0.25f, 0.6f, 1.f, 1.f);

private:
	void ApplyStateVisual(EMatchPlayerEntryState State);

	FString CachedName;
	EMatchPlayerEntryState CachedState = EMatchPlayerEntryState::Free;
	bool bHasCachedState = false;
};
