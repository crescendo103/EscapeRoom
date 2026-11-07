// [설계 메모] 폴링(0.5초 타이머) 대신 GameState의 델리게이트를 구독해서 값이 실제로 바뀔 때만
// 텍스트를 갱신한다. 단, 클라이언트 기준으로는 위젯이 생성되는 시점에 GameState가 아직
// 리플리케이트되지 않았을 수 있으므로, GameState를 찾을 때까지 짧은 주기로 재시도하다가 찾으면
// 그 즉시 델리게이트를 바인딩하고 재시도 타이머는 끈다(그 뒤로는 폴링 없음).
//
// 주사위(모은 개수/필요 개수) - OnDiceProgressChanged에 연결 완료. 단계가 넘어가면 분모(필요 개수)도
//   같이 바뀌므로 HandleDiceProgressChanged가 그대로 다시 그려줌.
// 탈출구(현재 남은 개수) - AEscapeRoomGameState::OnActiveExitCountChanged에 연결. ActiveExitCount는
//   탈출구가 스폰되면 +1, 러너가 탈출에 써서 파괴되면 -1 되는 복제 값이라 클라이언트에서도 정확함.
//   1개 이상이면 섹션을 켜고 개수를 표시, 0이면(아직 안 열렸거나 다 쓰임) 섹션을 숨긴다.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Engine/TimerHandle.h"
#include "Containers/Queue.h"
#include "EscapeRoomGameState.h" // EMatchNotifySound
#include "MatchWidget.generated.h"

class UImage;
class UTextBlock;
class UProgressBar;
class UVerticalBox;
class AEscapeRoomGameState;
class UMatchPlayerEntryWidget;
class UMaterialInstanceDynamic;
class UWidgetAnimation;
class USoundBase;

UCLASS()
class ESCAPEROOM_API UMatchWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 탈출구 아이콘+텍스트를 한 번에 보이거나 숨김. 문이 처음 열리는 시점(예: GameState의
	// OnDiceThresholdReached 델리게이트)에서 true로 불러주면 됨 - 지금은 호출부 없음, 나중에 연결
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void SetExitSectionVisible(bool bVisible);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	// 홀드 게이지 바는 값이 매 프레임 바뀌므로 다른 섹션과 달리 델리게이트 대신 Tick에서 GetOwningPlayerPawn()을 직접 물어봄.
	// bHoldGaugeActive가 꺼져있으면(홀드 중이 아니면) 맨 위에서 바로 return - SetHoldGaugeActive() 참고
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

public:
	// 러너/술래 캐릭터가 홀드 상호작용을 시작/취소/완료할 때 호출: true면 매 프레임 게이지를 갱신하게 하고,
	// false면 NativeTick이 조기 return하도록 해서 평소엔 아무 것도 안 하고 바를 다시 숨김
	// HoldText가 비어있지 않으면 HoldActionText에 그 문구를 표시하고, 비어있거나 bActive가 false면
	// HoldActionText를 숨김(Collapsed) - 즉 홀드 게이지 바와 정확히 같은 조건에서만 같이 보임.
	// (단, PromptText가 활성 상태였다면 false 시점에 그쪽으로 넘어감 - RefreshHoldActionText 참고)
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void SetHoldGaugeActive(bool bActive, const FText& HoldText = FText::GetEmpty());

	// 홀드 시작 전, "지금 이 상호작용이 가능하다"를 알려주는 상시 문구(예: 캐비넷 안에서 "나가기").
	// 홀드가 시작되면 SetHoldGaugeActive의 HoldText가 우선권을 가져가고, 홀드가 끝나면 다시 이
	// 문구로 돌아옴. 게이지 바가 없는 문구 전용 상태라 HoldProgressBar는 건드리지 않음.
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void SetPromptTextActive(bool bActive, const FText& PromptText = FText::GetEmpty());

protected:

	// ---- 주사위 (틀만 - 아직 GameState 값과 연결 안 함) ----
	UPROPERTY(meta = (BindWidgetOptional))
	UImage* DiceIcon;

	UPROPERTY(meta = (BindWidget))
	UTextBlock* DiceCountText;

	// ---- 탈출구 (틀만 - 데이터 자체가 아직 없음, 기본 숨김) ----
	UPROPERTY(meta = (BindWidgetOptional))
	UImage* ExitIcon;

	UPROPERTY(meta = (BindWidget))
	UTextBlock* ExitCountText;

	// ---- 매치 알림 (한 번에 하나씩, 나머지는 큐에서 대기) ----
	// 흐름: Visible -> NotificationAnim 정방향 -> NotificationHoldSeconds 유지 -> 역재생 -> Collapsed -> 다음 알림
	// WBP에서 이름을 똑같이 맞추면 자동 연결됨. Optional이라 아직 안 만들어도 WBP 컴파일은 됨
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* NotificationText;

	// NotificationText를 감싸는 배경 Border(또는 Overlay 등 아무 컨테이너). 보이기/숨기기는 이 패널에 적용되고
	// 자식인 텍스트는 부모를 따라감. WBP에 없으면 NotificationText 자체를 켜고 끔
	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* NotificationPanel;

	// 등장 연출용 위젯 애니메이션 (퇴장은 이걸 역재생). 없으면 연출 없이 바로 보였다가 사라짐
	UPROPERTY(Transient, meta = (BindWidgetAnimOptional))
	UWidgetAnimation* NotificationAnim;

	// 등장 애니메이션이 끝난 뒤부터 역재생 시작까지 유지하는 시간
	UPROPERTY(EditAnywhere, Category = "HUD|Notification")
	float NotificationHoldSeconds = 2.f;

	// 알림이 화면에 뜰 때마다 재생할 알림음. 2D로 재생해서 맵 어디에 있든 똑같이 들림.
	// 각 클라이언트가 자기 화면에 알림을 띄우는 순간 로컬로 재생하므로 전원이 듣게 됨 (WBP에서 지정)
	UPROPERTY(EditAnywhere, Category = "HUD|Notification")
	TObjectPtr<USoundBase> NotificationSound;

	// 감옥문이 열렸을 때 알림음 (이 알림만 공통 알림음 대신 재생). 비워두면 공통 알림음
	UPROPERTY(EditAnywhere, Category = "HUD|Notification")
	TObjectPtr<USoundBase> JailDoorOpenedSound;

	// ---- 홀드 게이지 (러너/술래 공용 - 홀드 중일 때만 보임, 그 외엔 Collapsed) ----
	UPROPERTY(meta = (BindWidgetOptional))
	UProgressBar* HoldProgressBar;

	// 홀드 게이지 옆에 "캐비닛 열기"처럼 무엇을 하고 있는지 보여주는 텍스트. 홀드 중엔 HoldText를,
	// 홀드 중이 아니고 프롬프트가 활성화돼 있으면 PromptText를 보여줌(RefreshHoldActionText가 결정)
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* HoldActionText;

	// ---- 술래 부감 스킬 쿨타임 아이콘 (술래 화면에서만 보임, 러너/관전자는 Collapsed) ----
	// Brush에 쿨타임 머티리얼(MT_SkillCooldown)을 지정해두면 CooldownProgressParamName 파라미터를
	// 매 틱 갱신함 (1 = 쿨타임 막 시작해서 전부 어두움, 0 = 사용 가능). 머티리얼이 아니면 색으로만 구분
	UPROPERTY(meta = (BindWidgetOptional))
	UImage* OverheadSkillIcon;

	// 쿨타임 남은 초 (쿨타임 중에만 보임)
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* OverheadCooldownText;

	UPROPERTY(EditAnywhere, Category = "HUD|OverheadSkill")
	FName CooldownProgressParamName = TEXT("Progress");

	// 상태별 아이콘 색 (머티리얼의 Tint 파라미터 또는 이미지 ColorAndOpacity에 적용)
	UPROPERTY(EditAnywhere, Category = "HUD|OverheadSkill")
	FLinearColor SkillReadyTint = FLinearColor::White;

	UPROPERTY(EditAnywhere, Category = "HUD|OverheadSkill")
	FLinearColor SkillActiveTint = FLinearColor(1.f, 0.8f, 0.2f, 1.f);

	UPROPERTY(EditAnywhere, Category = "HUD|OverheadSkill")
	FLinearColor SkillCooldownTint = FLinearColor(0.6f, 0.6f, 0.6f, 1.f);

	// ---- 러너 목록 (감옥 여부 이미지 + 이름) - Vertical Box (러너 최대 4명이라 스크롤 불필요) ----
	// GameState->PlayerArray를 0.5초마다 폴링해서 채움 (로비 RoomWidget의 멤버 목록과 같은 방식).
	// 술래(AMatchPlayerState::IsChaser)와 아직 역할이 안 정해진 플레이어, nullptr는 표시하지 않음
	UPROPERTY(meta = (BindWidget))
	UVerticalBox* PlayerListBox;

	// PlayerListBox에 넣을 러너 한 줄 위젯 클래스 (WBP_MatchPlayerEntry 등을 Class Defaults에서 지정)
	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	TSubclassOf<UMatchPlayerEntryWidget> PlayerEntryWidgetClass;

	// SetHoldGaugeActive()가 관리하는 플래그. UUserWidget::TickFrequency는 UE5.4 기준 private이라 외부에서
	// 못 건드리므로, 대신 이 플래그로 NativeTick 맨 위에서 조기 return시켜 홀드 중이 아닐 때는 사실상 아무 일도 안 하게 함
	bool bHoldGaugeActive = false;

	// 부감 쿨타임 아이콘 갱신 (NativeTick에서 홀드 게이지 조기 return보다 먼저 호출)
	void UpdateOverheadSkillIcon();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> OverheadSkillIconMID;

	int32 LastShownCooldownSeconds = -1;

	// ---- 남은 시간 (제한시간) ----
	// 디자이너에서 이 이름으로 TextBlock을 배치하면 자동 바인딩 (없어도 컴파일됨)
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* MatchTimerText;

	UPROPERTY(EditAnywhere, Category = "HUD|Timer")
	FLinearColor TimerNormalColor = FLinearColor::White;

	// 남은 시간이 TimerWarningSeconds 이하일 때 색
	UPROPERTY(EditAnywhere, Category = "HUD|Timer")
	FLinearColor TimerWarningColor = FLinearColor(0.85f, 0.1f, 0.1f, 1.f);

	UPROPERTY(EditAnywhere, Category = "HUD|Timer", meta = (ClampMin = "0.0"))
	float TimerWarningSeconds = 60.f;

	// 표시값이 바뀔 때만 SetText (매 프레임 텍스트 갱신 방지)
	int32 LastShownTimerSeconds = -2;

	void UpdateMatchTimer();

	// SetHoldGaugeActive/SetPromptTextActive가 각자 기억해두는 상태 - 둘 중 뭘 HoldActionText에
	// 보여줄지는 RefreshHoldActionText가 우선순위(홀드 > 프롬프트)에 따라 결정함
	FText CurrentHoldText;
	bool bPromptTextActive = false;
	FText CurrentPromptText;

private:
	// HoldActionText의 실제 텍스트/가시성을 bHoldGaugeActive/CurrentHoldText/bPromptTextActive/
	// CurrentPromptText 네 값을 보고 갱신. 홀드 중이면 홀드 문구가 우선, 아니면 프롬프트 문구,
	// 둘 다 없으면 Collapsed
	void RefreshHoldActionText();

	// GameState를 찾을 때까지 재시도(클라 기준 아직 리플리케이트 전일 수 있음). 찾으면 델리게이트를
	// 바인딩하고, 그 시점의 현재 값으로 1회 초기화한 뒤 재시도 타이머는 끈다
	void TryBindToGameState();


	// OnDiceProgressChanged 델리게이트 콜백 - 다이스 진행 상황(현재 단계 기준)이 바뀔 때마다 호출됨
	void HandleDiceProgressChanged(int32 Collected, int32 Required);

	// OnActiveExitCountChanged 델리게이트 콜백 - 남은 탈출구 수가 바뀔 때마다(스폰/탈출) 호출됨
	void HandleActiveExitCountChanged(int32 Count);

	// ---- 매치 알림 ----
	// OnMatchNotification 콜백 - 절대 바로 표시하지 않고 항상 큐에 넣음. 표시 중이 아닐 때만 ShowNextNotification 호출
	void HandleMatchNotification(const FText& Message, EMatchNotifySound SoundType);

	// 표시를 시작하는 유일한 지점. 큐에서 하나 꺼내 Visible + 정방향 재생, 큐가 비었으면 표시 종료 상태로
	void ShowNextNotification();

	// NotificationAnim이 끝날 때마다 호출(정방향/역방향 둘 다) - bIsNotificationReversing으로 구분
	UFUNCTION()
	void OnNotificationAnimFinished();

	// 유지 시간이 끝나면 역재생 시작
	void OnNotificationHoldFinished();

	// 알림 문장 + 알림음 종류
	struct FPendingNotification
	{
		FText Message;
		EMatchNotifySound SoundType = EMatchNotifySound::Common;
	};
	TQueue<FPendingNotification> PendingNotifications;

	// 지금 알림 하나를 표시 중인지 (true면 새 알림은 큐에서 대기)
	bool bIsShowingNotification = false;

	// 방금 끝난 애니메이션이 역재생이었는지 - 애니메이션 종료 콜백이 정/역 구분 없이 호출되므로 필요
	bool bIsNotificationReversing = false;

	FTimerHandle NotificationHoldTimerHandle;

	// 알림의 보이기/숨기기 대상 (NotificationPanel이 있으면 패널, 없으면 NotificationText)
	UWidget* GetNotificationRoot() const;

	TWeakObjectPtr<AEscapeRoomGameState> BoundGameState;
	FTimerHandle BindRetryTimerHandle;

	// 러너 목록 폴링 (0.5초) - PlayerArray에서 러너만 골라 이름/감옥 여부를 반영
	void RefreshPlayerList();
	FTimerHandle PlayerListRefreshTimerHandle;

	// 재사용하는 러너 엔트리들 (모자라면 추가 생성, 남으면 Collapsed - 매번 새로 만들지 않음)
	UPROPERTY()
	TArray<TObjectPtr<UMatchPlayerEntryWidget>> PlayerEntries;
};
