// Fill out your copyright notice in the Description page of Project Settings.

// [설계 메모] 버튼 클릭 시 애니메이션(페이드아웃 등)은 블루프린트 이벤트 그래프에서 재생합니다.
// 애니메이션은 블루프린트로 만드는 게 훨씬 편해서, C++은 버튼 OnClicked에 직접 바인딩하지 않고
// 실제 동작(화면 전환)만 BlueprintCallable 함수로 노출해둡니다.
// 블루프린트 쪽에서 "On Clicked -> Play Animation -> Delay -> 아래 함수 호출" 순서로
// 실행 흐름을 직접 구성해야 합니다 (실행 로직은 블루프린트가 갖고 있음).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StartScreenWidget.generated.h"

class UButton;
class UEditableTextBox;
class UImage;
class UTexture2D;

UCLASS()
class ESCAPEROOM_API UStartScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 이동할 레벨 이름 (에디터에서 설정)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StartScreen")
	FName LevelToLoad;

	// StartButton 애니메이션 재생이 끝난 뒤 블루프린트에서 호출: PlayerNameTextBox에 입력된 이름을
	// 서버에 반영(ServerChangeName)한 뒤 CreateRoom/JoinRoom 화면으로 전환
	UFUNCTION(BlueprintCallable, Category = "Navigation")
	void GoToCreateJoinRoom();

protected:
	// 블루프린트 이벤트 그래프의 "On Clicked (StartButton)"이 이 위젯 이름을 참조하므로 남겨둠
	UPROPERTY(meta = (BindWidgetOptional))
	UButton* StartButton;

	// 시작 버튼 옆에 두는 플레이어 이름 입력란. 비워두면 GoToCreateJoinRoom()에서 기본값("Player")을 씀
	UPROPERTY(meta = (BindWidgetOptional))
	UEditableTextBox* PlayerNameTextBox;

	virtual void NativeConstruct() override;

	// ---------- 게임 종료 ----------
	// 누르면 게임 프로세스 종료 (HowToButton처럼 C++에서 OnClicked에 바로 연결)
	UPROPERTY(meta = (BindWidgetOptional))
	UButton* QuitButton;

	UFUNCTION()
	void OnQuitClicked();

	// 닉네임 입력 필터 (영문만)
	UFUNCTION()
	void OnPlayerNameChanged(const FText& Text);

	// ---------- 조작법 보기 ----------
	// 조작법 버튼: 누르면 술래 조작법 이미지부터 전체 화면으로 표시
	// 페이지 순서: 술래 조작법 -> 러너 조작법 -> 술래 목표 -> 러너 목표 -> 닫힘 (텍스처가 비어 있는 페이지는 건너뜀)
	UPROPERTY(meta = (BindWidgetOptional))
	UButton* HowToButton;

	// 화면 전체를 덮는 투명 버튼 (아무 데나 클릭 감지). 처음엔 Collapsed
	UPROPERTY(meta = (BindWidgetOptional))
	UButton* GuideOverlayButton;

	// GuideOverlayButton 안에 넣는 조작법 이미지
	UPROPERTY(meta = (BindWidgetOptional))
	UImage* GuideImage;

	// WBP Class Defaults에서 지정
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide")
	UTexture2D* TaggerGuideTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide")
	UTexture2D* RunnerGuideTexture;

	// 게임 목표 이미지 (조작법 2장 다음에 표시)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide")
	UTexture2D* TaggerGoalTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide")
	UTexture2D* RunnerGoalTexture;

private:
	// 0 = 닫힘, 1 = 술래 조작법, 2 = 러너 조작법, 3 = 술래 목표, 4 = 러너 목표
	int32 GuidePage = 0;
	static constexpr int32 GuidePageCount = 4;

	// 페이지 번호 -> 텍스처 (범위 밖이면 nullptr)
	UTexture2D* GetGuideTexture(int32 Page) const;

	UFUNCTION()
	void OnHowToClicked();

	UFUNCTION()
	void OnGuideOverlayClicked();

	void ShowGuidePage(int32 Page);
};
