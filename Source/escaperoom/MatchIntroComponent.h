// MatchIntroComponent.h
//
// [시작 연출] 로딩이 끝난 직후, 내 캐릭터 주위를 3인칭 카메라로 한 바퀴 비춘 뒤
// 1인칭 시점으로 빨려 들어가듯 돌아오는 연출 (데드 바이 데이라이트 시작 연출 참고).
//
// - 캐릭터 BP에 붙이는 씬 컴포넌트. 이 컴포넌트의 위치 = 카메라가 바라보며 도는 중심점
//   (BP 뷰포트에서 가슴 높이쯤에 두면 됨)
// - 연출용 카메라는 Play() 때만 만들어 쓰고 끝나면 지움 -> 평소엔 다른 캐릭터에 부담 없음
// - 카메라 위치는 스프링암 대신 직접 계산 + 구체 스윕으로 벽 충돌 검사 (좁은 방에서 벽을 뚫고 보이지 않게).
//   마지막 ReturnDuration 동안은 1인칭 카메라 위치/방향으로 보간해서 끊김 없이 1인칭으로 복귀
// - 텍스트(위젯)와 사운드도 여기서 같이 처리. 캐릭터 BP마다 문구/사운드를 다르게 지정 가능
// - 각 클라이언트 화면에서만 도는 연출이라 복제/RPC 없음. 호출은 ADungeonPlayerController::TryStartMatchIntro
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "MatchIntroComponent.generated.h"

class UCameraComponent;
class UUserWidget;
class UWidgetAnimation;
class USoundBase;
class UAudioComponent;
class USkeletalMeshComponent;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent, DisplayName = "Match Intro"))
class ESCAPEROOM_API UMatchIntroComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMatchIntroComponent();

	// 연출 시작. 끝나면(중간에 캐릭터가 사라져 취소돼도) OnFinished가 정확히 한 번 불림.
	// 로컬로 조종 중인 폰에서만 호출할 것
	void Play(FSimpleDelegate OnFinished);

	bool IsPlaying() const { return bPlaying; }
	bool IsIntroEnabled() const { return bEnableIntro; }

	// ---------------- 설정 (BP 디테일 패널) ----------------

	// 끄면 연출 없이 바로 게임 시작
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro")
	bool bEnableIntro = true;

	// 전체 연출 시간(초). 마지막 ReturnDuration 구간이 1인칭 복귀
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Camera", meta = (ClampMin = "0.5"))
	float Duration = 4.f;

	// 연출 끝부분에 1인칭 시점으로 빨려 들어가는 시간(초)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Camera", meta = (ClampMin = "0.0"))
	float ReturnDuration = 0.6f;

	// 캐릭터 정면 기준 카메라 시작 각도(도). 0 = 등 뒤, 180 = 정면(얼굴)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Camera")
	float StartYawOffset = 60.f;

	// 연출 동안 캐릭터 주위를 도는 총 각도(도). 기본 60 + 300 = 360 -> 등 뒤에서 끝나서 1인칭 복귀가 자연스러움
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Camera")
	float OrbitDegrees = 300.f;

	// 카메라 거리(cm)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Camera", meta = (ClampMin = "50.0"))
	float ArmLength = 250.f;

	// 카메라 상하 각도(도). 음수 = 위에서 내려다봄
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Camera")
	float Pitch = -15.f;

	// 벽 충돌 검사용 구체 반지름(cm)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Camera", meta = (ClampMin = "1.0"))
	float ProbeRadius = 12.f;

	// 화면에 띄울 한 줄 문구 (비우면 위젯 문구를 바꾸지 않음)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Text")
	FText IntroText;

	// 문구를 띄울 위젯. 안에 TextWidgetName 이름의 TextBlock이 있어야 함
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Text")
	TSubclassOf<UUserWidget> IntroWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Text")
	FName TextWidgetName = TEXT("IntroText");

	// 위젯에 이 이름의 애니메이션이 있으면 시작 때 재생, 1인칭 복귀 시작 때 역재생 (없어도 동작)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Text")
	FName FadeAnimationName = TEXT("FadeIn");

	// 연출 시작 때 2D로 재생할 사운드 (나에게만 들림)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro|Sound")
	TObjectPtr<USoundBase> IntroSound;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void Finish();

	// 연출용 카메라로 전환/복구 (기존 활성 카메라 끄기/켜기)
	void BeginIntroCamera();
	void EndIntroCamera();

	// 내 전신 메시 보이기 + 1인칭 팔 숨기기 / 원래대로
	void ApplyIntroMeshVisibility();
	void RestoreMeshVisibility();

	void ShowIntroWidget(APlayerController* PC);
	void RemoveIntroWidget();

	// 회전 구간: OrbitAlpha(0~1)일 때 카메라 위치/방향 (벽 충돌 반영)
	FTransform ComputeOrbitTransform(float OrbitAlpha) const;

	// 1인칭 카메라가 있을 위치/방향 (복귀 목표)
	FTransform ComputeFirstPersonTransform() const;

	bool bPlaying = false;
	bool bReturnStarted = false;
	float Elapsed = 0.f;
	float OrbitBaseYaw = 0.f;
	FTransform ReturnStartTransform;
	FSimpleDelegate OnFinishedDelegate;

	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> IntroCamera;

	// 연출 동안 꺼둔 기존 카메라들 (끝나면 다시 켬). [0]이 1인칭 카메라 후보
	TArray<TWeakObjectPtr<UCameraComponent>> DisabledCameras;

	struct FSavedMeshState
	{
		TWeakObjectPtr<USkeletalMeshComponent> Mesh;
		bool bOwnerNoSee = false;
		bool bVisible = true;
	};
	TArray<FSavedMeshState> SavedMeshStates;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> IntroWidget;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetAnimation> IntroFadeAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> IntroAudio;
};
