#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DungeonPlayerController.generated.h"

class AWFCGridManager;
class APropSpawnManagerActor;
class ULoadingScreenWidget;
class UMatchWidget;
class UEscapeFadeWidget;
class AResultRoomActor;
class USkeletalMesh;
class UMaterialInterface;
class UInputAction;
class UInputMappingContext;
class APlayerState;
class ACharacter;
class AChaserCharacter;
class UAudioComponent;
class USoundBase;
struct FInputActionValue;

UCLASS()
class ADungeonPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	// 로딩 화면 클래스 (클라 전용 UI)
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<ULoadingScreenWidget> LoadingWidgetClass;

	// 매치 HUD(러너/주사위/탈출구) 클래스 - 로딩이 끝난 시점에 화면에 띄운다
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UMatchWidget> MatchWidgetClass;


	// 러너/술래 캐릭터가 홀드 상호작용 시작/종료 시점에 MatchWidget의 홀드 게이지를 켜고 끄기 위해 참조함
	UMatchWidget* GetMatchWidget() const { return MatchWidget; }

	// ===================== 탈출 후 페이드 + 관전 =====================
	//
	// 흐름: ARunnerCharacter::SetEscaped()(서버) -> ClientBeginEscape()(본인 클라)
	//   -> EscapeFadeWidget 비네트 암전 -> 완전히 검어진 순간 ServerSpectateNext()
	//   -> 서버가 다음 관전 대상 폰으로 SetViewTarget (서버에서 해야 네트 relevancy도 그 시점 기준으로 계산됨)
	//   -> 비네트가 다시 열리며 관전 화면 표시. 이후 Space(SpectateNextAction)마다 ServerSpectateNext()
	// 탈출한 러너 폰은 숨김+조작 차단 상태로 빙의된 채 남아있고, 카메라(ViewTarget)만 다른 폰에 붙는다.
	// 관전 대상은 "탈출하지 않은 다른 플레이어" 전부(술래, 러너, 감옥에 갇힌 러너 포함).

	// 비네트 암전 위젯 클래스 (WBP_EscapeFade). 비워두면 페이드 없이 바로 관전으로 넘어감
	UPROPERTY(EditDefaultsOnly, Category = "Escape|UI")
	TSubclassOf<UEscapeFadeWidget> EscapeFadeWidgetClass;

	// 다음 관전 대상으로 넘기는 입력 (IA_SpectateNext, Space)
	UPROPERTY(EditDefaultsOnly, Category = "Escape|Input")
	TObjectPtr<UInputAction> SpectateNextAction;

	// SpectateNextAction이 들어있는 매핑 컨텍스트 (IMC_Spectate). 탈출 시점에 높은 우선순위로 추가됨
	UPROPERTY(EditDefaultsOnly, Category = "Escape|Input")
	TObjectPtr<UInputMappingContext> SpectateMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Escape|Input")
	int32 SpectateMappingPriority = 10;

	// 관전 대상 전환 블렌드 시간 (0 = 즉시 컷)
	UPROPERTY(EditDefaultsOnly, Category = "Escape|Spectate", meta = (ClampMin = "0.0"))
	float SpectateBlendTime = 0.f;

	// 관전 대상의 카메라가 자기 캡슐 "안쪽"에 있을 때만(=진짜 1인칭) 내 화면에서 그 몸통 메시를 숨김.
	// 캐릭터 뒤에 있는 3인칭 카메라(러너 -140)나 술래 부감 스킬처럼 몸 밖에 있는 카메라는 절대 숨기지 않음
	UPROPERTY(EditDefaultsOnly, Category = "Escape|Spectate")
	bool bHideSpectatedMeshInFirstPerson = true;

	// "캡슐 안쪽" 판정 여유값(cm). 카메라가 캡슐 중심축에서 (반지름 + 이 값) 이내, 높이도 (반높이 + 이 값)
	// 이내에 있어야 1인칭으로 판단
	UPROPERTY(EditDefaultsOnly, Category = "Escape|Spectate", meta = (ClampMin = "0.0"))
	float HideSpectatedMeshCapsuleMargin = 20.f;

	// 서버 -> 본인 클라: 탈출 성공. 비네트 암전 시작 + 관전 입력 준비
	UFUNCTION(Client, Reliable)
	void ClientBeginEscape();

	// ===================== 게임 종료 / 로비 복귀 =====================
	//
	// 게임 종료(전원 탈출 or 남은 러너 전원 감금) 시 AescaperoomGameMode가 모든 컨트롤러에 호출.
	// 술래/감옥 러너/관전자 모두 EscapeFadeWidget(게임 종료 모드)으로 넘어가 검은 화면 + 결과 + 로비 버튼.
	// ViewTarget은 건드리지 않음(화면이 완전히 검어서 안 보임) - 관전 대상 전환만 멈춤
	// [수정] bIsChaser/bWon은 서버가 판정해서 보냄. 클라에서 PlayerState->HasEscaped()를 읽으면
	// 마지막 탈출자는 bHasEscaped 복제가 이 RPC보다 늦게 도착해서 패배로 잘못 나왔음
	UFUNCTION(Client, Reliable)
	void ClientShowMatchEnd(int32 EscapedCount, int32 CaughtCount, int32 TotalRunners, bool bIsChaser, bool bWon);

	// 서버 전용: 관전 대상 자동 갱신 타이머 정지 (게임 종료 시 GameMode가 호출)
	void StopSpectating();

	// 로컬 전용: 로비 서버로 돌아감 (EscapeFadeWidget의 로비 버튼이 호출 - 로비로 가는 유일한 경로).
	// 로비 주소는 매치로 넘어올 때 UEscapeRoomSessionSubsystem(GameInstance)에 메모해둔 값을 쓰고,
	// 메모가 없을 때만 아래 LobbyServerAddress(예비값)를 씀
	void ReturnToLobby();

	// 예비 로비 주소 ("호스트:포트") - 로비를 거치지 않고 PIE로 바로 매치에 들어온 경우 등 메모가 없을 때만 사용.
	// [AWS] EC2로 옮길 때는 이 값을 EC2 공인 IP(탄력적 IP 권장):로비포트로 바꿔둘 것 (BP Class Defaults에서 변경 가능)
	// [로컬 테스트 시절 - AWS 전환으로 주석처리]
	// FString LobbyServerAddress = TEXT("127.0.0.1:7777");
	// [AWS] 비워두면 DefaultGame.ini의 로비 주소(UEscapeRoomSessionSubsystem::GetConfiguredLobbyAddress)를 씀
	UPROPERTY(EditDefaultsOnly, Category = "Escape|Lobby")
	FString LobbyServerAddress;

	// 게임 종료 결과 문구. {0}=탈출 인원, {1}=잡힌 인원, {2}=전체 러너 수
	// [테스트용] 켜면 게임이 어떻게 끝나든(시간 초과 포함) 러너는 RunnerWin, 술래는 ChaserLose 연출을 보여줌.
	// 러너 승리/술래 패배 결과 방을 확인하기 위한 임시 스위치 - 확인 끝나면 반드시 끌 것
	UPROPERTY(EditDefaultsOnly, Category = "Debug|MatchEnd")
	bool bDebugForceRunnerWinResult = false;

	UPROPERTY(EditDefaultsOnly, Category = "Escape|UI")
	FText MatchResultFormat = NSLOCTEXT("MatchEnd", "Result", "Escaped {0}  :  Caught {1}  :  Total {2}");

	// 본인 클라 -> 서버: 다음 관전 대상으로 전환 (탈출한 플레이어만 허용)
	UFUNCTION(Server, Reliable)
	void ServerSpectateNext();

	// ===================== 배경음악 =====================
	//
	// 로딩이 끝나면 로컬에서 2D로 재생. 술래 공포 반경 안에 들어가면 일시정지(아우라 BGM만 들리게),
	// 벗어나면 멈췄던 지점부터 다시 재생. 술래 본인 화면은 아우라를 안 들으므로 항상 배경음악 재생

	// 매치 배경음악 (사운드 에셋에서 Looping 체크할 것)
	UPROPERTY(EditDefaultsOnly, Category = "Audio|BGM")
	TObjectPtr<USoundBase> MatchBGM;

	UPROPERTY(EditDefaultsOnly, Category = "Audio|BGM", meta = (ClampMin = "0.0"))
	float BGMFadeTime = 0.5f;

	// 반경 경계에서 들락날락할 때 음악이 깜빡이지 않도록, 나갈 때는 반경 + 이 값(cm)만큼 멀어져야 재개
	UPROPERTY(EditDefaultsOnly, Category = "Audio|BGM", meta = (ClampMin = "0.0"))
	float TerrorRadiusExitMargin = 100.f;

	// ---- 로딩 카메라 ----
	// 맵 생성(로딩) 중 폰이 없을 때 바라볼 카메라의 Actor Tag (GameMode의 LoadingCameraTag와 같은 값으로 둘 것)
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	FName LoadingCameraTag = TEXT("LoadingCamera");

	// 엔진이 시점을 자동으로 바꾸는 지점(SpectatorPawn 스폰, Possess 등)에서 호출됨.
	// 폰이 없는 동안(로딩 중)에는 엔진이 SpectatorPawn으로 시점을 옮기려 해도 LoadingCamera를 유지하고,
	// 폰이 생긴 뒤에는 엔진 기본 동작 그대로 (매치 시작 시 폰 시점 전환, 탈출 후 관전 등 영향 없음)
	virtual void AutoManageActiveCameraTarget(AActor* SuggestedTarget) override;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// 클라이언트(리슨 호스트 포함)에서 새 폰 빙의가 확정되는 시점. 캐비넷이 아닌 폰(러너로 복귀 등)이면
	// 이 컨트롤러가 캐비넷에 남겨둔 IMC_Cabinet을 확실하게 제거 (남아있으면 마우스 입력을 캐비넷 Look이 가져감)
	virtual void AcknowledgePossession(APawn* P) override;
	virtual void SetupInputComponent() override;

private:
	UPROPERTY()
	ULoadingScreenWidget* LoadingWidget;

	// LoadingCameraTag가 붙은 레벨 카메라 (한 번 찾으면 캐시)
	TWeakObjectPtr<AActor> CachedLoadingCamera;
	AActor* FindLoadingCamera();

	UPROPERTY()
	UMatchWidget* MatchWidget;

	UPROPERTY()
	AWFCGridManager* MapGenerator;

	UPROPERTY()
	APropSpawnManagerActor* PropSpawner;

	AWFCGridManager* FindMapGenerator() const;
	APropSpawnManagerActor* FindPropSpawner() const;

	void ShowLoadingWidget();

	// 로딩이 끝난 뒤(FinishLoading) 매치 HUD를 화면에 띄운다. 로컬 컨트롤러에서만 호출됨
	// (BeginPlay 맨 위에서 이미 !IsLocalController()면 리턴하므로 이 클래스 전체가 로컬 전용)
	void ShowMatchWidget();

	// 진행바 0~90% 구간: 구조(방/빅룸/문) 생성. WFCGridManager::OnMapGenProgress 구독.
	void HandleProgress(int32 Current, int32 Total);

	// 구조 생성이 끝나는 시점. 레벨에 PropSpawner가 있으면 그쪽 진행률 구독을 마저 걸고,
	// 없으면 바로 로딩을 끝낸다. PropSpawner는 자기가 알아서 소품 배치를 시작하므로
	// 여기서 PopulateProps()를 직접 호출하지 않는다 - 진행률만 구독해서 UI에 반영.
	void HandleGenComplete();

	// 진행바 90~100% 구간: 소품(캐비닛/다이스/Exit) 배치. APropSpawnManagerActor::OnPropSpawnProgress 구독.
	// NOTE: 90/100 분기점은 정확한 비율 계산이 아니라 임의로 정한 값 (구조/소품 Total을 합산하는
	// 방식도 가능하지만, 소품 Total은 구조 생성이 끝나야 확정돼서 그 시점에 퍼센트가 튀는 부작용이 있음).
	void HandlePropProgress(int32 Current, int32 Total);

	// 구조+소품 생성이 전부 끝나는 시점 - 실제로 로딩 위젯 제거 + 입력 모드 복귀
	void HandlePropComplete();
	void FinishLoading();

	// HandleGenComplete / FinishLoading 중복 실행 방지 (직접 호출 + 델리게이트로 두 번 들어올 수 있음)
	bool bGenCompleteHandled = false;
	bool bLoadingFinished = false;

private:
	FTimerHandle TimerHandle_FindMapGenerator;

	// ---- 탈출/관전 (클라 전용) ----
	UPROPERTY()
	TObjectPtr<UEscapeFadeWidget> EscapeFadeWidget;

	// 페이드가 끝나 관전 입력(Space)을 받을 수 있는 상태인지
	bool bLocalSpectateInputEnabled = false;

	// 내 화면에서 메시를 숨겨둔(RenderInMainPass=false) 관전 대상 - 대상이 바뀌면 원상복구
	TWeakObjectPtr<ACharacter> LocallyHiddenSpectateCharacter;

	FTimerHandle LocalSpectateVisualTimer;

	// EscapeFadeWidget을 (없으면 만들어서) 화면에 띄워두고 반환. 클라 전용
	UEscapeFadeWidget* EnsureEscapeFadeWidget();

	void HandleEscapeFadeOutFinished();

	// ---- 게임 종료 결과 방 연출 (로컬 전용) ----
	// 빙의 시점(AcknowledgePossession)에 내 캐릭터 외형을 저장 - 탈출 후 폰이 사라져도 결과 방에서 쓸 수 있게
	UPROPERTY()
	TObjectPtr<USkeletalMesh> CachedResultMesh;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> CachedResultMaterials;

	void CacheResultCharacter(APawn* P);

	// 이번 판 결과에 맞는 방 (ClientShowMatchEnd에서 결정)
	TWeakObjectPtr<AResultRoomActor> ActiveResultRoom;

	// 결과 방을 보여주는 중인지 - 엔진/관전 로직이 시점을 다른 데로 옮겨도 결과 방으로 되돌림
	bool bShowingResultRoom = false;

	// EscapeFadeWidget이 게임 종료 암전을 끝낸 순간 - 메시 교체 + 결과 방 카메라로 전환
	void HandleMatchEndBlack();
	void OnSpectateNextInput(const FInputActionValue& Value);
	void UpdateLocalSpectateVisuals();
	void RestoreLocallyHiddenSpectateMesh();

	// ---- 탈출/관전 (서버 전용) ----
	bool bIsSpectating = false;

	// 관전 중인 "플레이어" (폰이 아니라 PlayerState로 기억 - 캐비닛에 숨으면 조종 폰이 캐비닛으로
	// 바뀌어도 같은 플레이어를 계속 따라가도록)
	TWeakObjectPtr<APlayerState> SpectatedPlayerState;

	FTimerHandle SpectateValidateTimer;

	bool IsEscapedPlayer() const;
	bool IsValidSpectateTarget(const APlayerState* PS) const;
	APlayerState* FindNextSpectateTarget(const APlayerState* Current) const;
	void ApplySpectateViewTarget();

	// 0.5초마다: 관전 대상이 탈출/퇴장했으면 다음 대상으로, 조종 폰이 바뀌었으면(캐비닛 등) 그 폰으로 갱신
	void ValidateSpectateTarget();

	// WFC 매니저를 찾아 이벤트 바인딩을 시도하는 함수
	void InitializeMapGeneratorBinding();

	// ---- 배경음악 (로컬 전용) ----
	UPROPERTY()
	TObjectPtr<UAudioComponent> BGMAudio;

	TWeakObjectPtr<AChaserCharacter> CachedChaser;

	// 지금 공포 반경 안이라서 배경음악을 멈춘 상태인지
	bool bBGMSuppressedByTerror = false;

	FTimerHandle BGMPauseTimer;

	void StartMatchBGM();
	void UpdateBGMForTerrorRadius();
	void SetBGMSuppressed(bool bSuppress);
	void PauseBGMAfterFade();
};
