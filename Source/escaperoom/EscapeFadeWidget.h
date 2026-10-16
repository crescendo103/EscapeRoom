// EscapeFadeWidget.h
//
// [설계 메모] 매치에서 빠져나가는 모든 플레이어가 반드시 거쳐가는 "비네트 암전 + 안내 + 로비 복귀" 위젯.
// ADungeonPlayerController가 생성해서 화면에 붙이며, 로비로 돌아가는 유일한 경로가 이 위젯의 버튼이다.
//
// 두 가지 모드:
//   [탈출 모드] StartEscapeFade() - 러너가 탈출에 성공했을 때 (ClientBeginEscape)
//     1) FadeOut  : 비네트 반경 1 -> 0 (가장자리부터 조여 들어와 완전히 암전)
//     2) HoldBlack: 검은 화면 유지. 진입 순간 OnFadeOutFinished -> 컨트롤러가 이 뒤에서 관전 시점으로 전환
//     3) FadeIn   : 0 -> SpectateRadius (다시 열리며 관전 화면)
//     4) Spectating: 탈출 문구 + 관전 대상 이름 + 로비 버튼 표시
//   [게임 종료 모드] StartMatchEndFade() - 전원 탈출 or 남은 러너 전원 감금 (ClientShowMatchEnd)
//     지금 반경에서 0까지 암전 -> 검은 화면에서 멈춤 -> 결과 문구 + 로비 버튼 표시 (관전 없음)
//     bRevealResultRoom=true면: 암전 완료 순간 OnMatchEndBlack -> 컨트롤러가 결과 방 카메라로 전환 ->
//     HoldBlack -> FadeIn(0 -> MatchEndRevealRadius)으로 결과 방이 드러난 뒤 결과 타이틀(+애니) + 문구 + 버튼
//     탈출 모드 도중(관전 중 포함)에 불려도 그 자리에서 게임 종료 모드로 덮어씀.
//
// 비네트 모양은 머티리얼(MT_FadeOut)이 담당하고 C++은 스칼라 파라미터(VignetteParamName)만 조절한다.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EscapeFadeWidget.generated.h"

class UImage;
class UTextBlock;
class UButton;
class UMaterialInstanceDynamic;

DECLARE_MULTICAST_DELEGATE(FOnEscapeFadeOutFinished);
DECLARE_MULTICAST_DELEGATE(FOnMatchEndBlack);

UCLASS()
class ESCAPEROOM_API UEscapeFadeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 탈출 모드 시작 (처음부터 다시 재생)
	void StartEscapeFade();

	// 게임 종료 모드 시작. ResultMessage는 결과 문구(예: "탈출 2명 / 잡힘 3명")
	// Title/TitleColor: 결과 타이틀(승리/패배 등). bRevealResultRoom=false면 예전처럼 검은 화면에서 멈춤
	void StartMatchEndFade(const FText& ResultMessage, const FText& Title = FText::GetEmpty(),
		FLinearColor TitleColor = FLinearColor::White, bool bRevealResultRoom = false);

	bool IsInMatchEndMode() const { return bMatchEndMode; }

	// [탈출 모드 전용] 완전히 검어진 순간 1회 브로드캐스트 (게임 종료 모드에서는 안 쏨)
	FOnEscapeFadeOutFinished OnFadeOutFinished;

	// [게임 종료 모드 + 결과 방 연출] 완전히 검어진 순간 1회 - 컨트롤러가 이 뒤에서 결과 방으로 시점 전환
	FOnMatchEndBlack OnMatchEndBlack;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 전체 화면을 덮는 이미지 (Brush = 비네트 머티리얼). WBP에서 이 이름 그대로 만들어야 함
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> VignetteImage;

	// 상단 안내 문구 - 탈출 모드: EscapedStatusText / 게임 종료 모드: MatchEndStatusText (없어도 됨)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	// "관전 중: 이름   [Space] 다음 플레이어" - 탈출 모드에서만 표시 (없어도 됨)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SpectateTargetText;

	// 게임 결과 문구 - 게임 종료 모드에서만 표시 (없어도 됨)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultText;

	// 결과 타이틀 (승리/패배 등 크게 뜨는 문구). 게임 종료 모드에서만 표시 (없어도 됨)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultTitleText;

	// 결과 타이틀 등장 애니메이션 - WBP 애니메이션 이름을 이것과 똑같이 만들면 자동 연결 (없어도 됨)
	UPROPERTY(Transient, meta = (BindWidgetAnimOptional))
	TObjectPtr<UWidgetAnimation> ResultTitleAnim;

	// 결과 방이 드러날 때 열리는 비네트 반경 (1 = 완전히 걷힘)
	UPROPERTY(EditAnywhere, Category = "EscapeFade", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MatchEndRevealRadius = 1.0f;

	// 결과 방 공개 시 열리는 시간 (FadeInDuration보다 천천히 여는 게 분위기 있음)
	UPROPERTY(EditAnywhere, Category = "EscapeFade", meta = (ClampMin = "0.01"))
	float MatchEndRevealDuration = 2.0f;

	// 로비(시작 화면)로 돌아가는 버튼. 탈출 모드는 관전 화면이 열린 뒤, 게임 종료 모드는 결과가 뜬 뒤 표시.
	// [WBP 설정] Is Focusable 체크를 꺼둘 것 - 켜져 있으면 클릭 후 포커스가 남아서 Space(관전 전환)를
	// 누를 때 버튼이 눌려버림
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> ReturnToLobbyButton;

	// 머티리얼 스칼라 파라미터 이름 (1 = 완전히 열림/투명, 0 = 완전히 검음)
	UPROPERTY(EditAnywhere, Category = "EscapeFade")
	FName VignetteParamName = TEXT("Radius");

	UPROPERTY(EditAnywhere, Category = "EscapeFade", meta = (ClampMin = "0.01"))
	float FadeOutDuration = 2.0f;

	UPROPERTY(EditAnywhere, Category = "EscapeFade", meta = (ClampMin = "0.0"))
	float HoldBlackDuration = 0.6f;

	UPROPERTY(EditAnywhere, Category = "EscapeFade", meta = (ClampMin = "0.01"))
	float FadeInDuration = 1.0f;

	// 관전 중에 남겨둘 비네트 반경 (1이면 완전히 걷힘, 0.8 정도면 가장자리만 살짝 어두움)
	UPROPERTY(EditAnywhere, Category = "EscapeFade", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SpectateRadius = 0.85f;

	// 보간 곡선 지수 (1 = 선형, 2 이상이면 끝으로 갈수록 빨라짐 - 조여드는 느낌)
	UPROPERTY(EditAnywhere, Category = "EscapeFade", meta = (ClampMin = "0.1"))
	float EaseExponent = 2.0f;

	UPROPERTY(EditAnywhere, Category = "EscapeFade")
	FText EscapedStatusText = NSLOCTEXT("EscapeFade", "Status", "You escaped! Waiting for other players...");

	UPROPERTY(EditAnywhere, Category = "EscapeFade")
	FText MatchEndStatusText = NSLOCTEXT("EscapeFade", "MatchEnd", "Game Over");

	// {0} 자리에 관전 대상 이름이 들어감
	UPROPERTY(EditAnywhere, Category = "EscapeFade")
	FText SpectateFormatText = NSLOCTEXT("EscapeFade", "Spectate", "Spectating: {0}    Press Space for next player");

private:
	enum class EPhase : uint8 { Idle, FadeOut, HoldBlack, FadeIn, Spectating, MatchEnd };

	UFUNCTION()
	void HandleReturnToLobbyClicked();

	void SetRadius(float Radius);
	void SetEscapeInfoVisible(bool bVisible);
	void ShowMatchEndInfo();
	void SetLobbyButtonVisible(bool bVisible);
	void UpdateSpectateTargetText();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> VignetteMID;

	EPhase Phase = EPhase::Idle;
	float PhaseTime = 0.f;

	// 지금 화면에 적용된 반경 / FadeOut을 시작한 반경 (게임 종료가 관전 도중에 오면 1이 아닌 곳에서 시작)
	float CurrentRadius = 1.f;
	float FadeOutStartRadius = 1.f;

	bool bMatchEndMode = false;
	bool bRevealResultRoom = false;
	FText PendingResultMessage;
	FText PendingTitle;
	FLinearColor PendingTitleColor = FLinearColor::White;

	// 게임 종료 모드에서 완전히 검어졌을 때: 결과 방 연출이면 HoldBlack->FadeIn, 아니면 바로 결과 표시
	void HandleMatchEndBlack();
	FString CachedTargetName;
};
