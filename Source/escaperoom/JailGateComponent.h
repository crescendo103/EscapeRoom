// JailGateComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "JailGateComponent.generated.h"

class UBoxComponent;
class USceneComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class USoundBase;
class UPrimitiveComponent;
struct FHitResult;

/** 에디터에서 연결하는 감옥문 한 짝 */
USTRUCT(BlueprintType)
struct FJailDoorSetting
{
	GENERATED_BODY()

	// 드롭다운에서 문 StaticMesh 컴포넌트(cagedoor) 이름 선택. Mobility = Movable 이어야 함
	// (목록은 UJailGateComponent::GetStaticMeshComponentOptions가 이 BP의 StaticMesh 컴포넌트를 모아서 채움)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jail", meta = (GetOptions = "GetStaticMeshComponentOptions"))
	FName Door;

	// 열렸을 때 닫힌 위치에서 밀려날 거리(cm). 문 컴포넌트 자신의 로컬 축 기준이라 문이 회전돼 있어도
	// Y = 문 옆 방향. 양쪽 문을 서로 반대로 열려면 한쪽은 (0, 100, 0), 다른 쪽은 (0, -100, 0) 처럼 부호를 반대로
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jail")
	FVector OpenOffset = FVector(0.f, 100.f, 0.f);
};

/**
 * 감옥 구출 스위치 + 감옥문 개폐 관리.
 *
 * WFC가 런타임에 스폰하는 빅룸(BP_scientest) 안에서 동작하므로, 레벨이 아니라 BP_scientest 블루프린트 안에서
 * 같은 BP의 컴포넌트끼리 연결한다. BP_scientest에 이 컴포넌트를 추가하고 디테일 패널에서:
 *  - Doors    : + 로 칸을 만들고 각 칸의 Door 드롭다운에서 문 컴포넌트 선택, OpenOffset으로 밀릴 방향/거리 지정
 *  - Switches : 동료가 밟을 Box 컴포넌트 선택 (감옥 바깥)
 * 드롭다운은 컴포넌트 "변수 이름"을 저장하므로, BP에서 컴포넌트 이름을 바꾸면 다시 선택해야 함
 * (못 찾으면 BeginPlay에서 경고 로그).
 *
 * 흐름: 러너(술래 X, 본인 안 잡힘)가 스위치에 들어옴 + 잡힌 러너가 1명 이상 + 문이 닫혀 있음
 *       -> 서버에서 bIsOpen = true (복제) -> 모든 머신에서 문 슬라이드 + 문 사운드
 *       -> Multicast로 발동 사운드(모두) + 발동 이펙트(러너 화면에만)
 *       -> OpenDuration(10초) 뒤 자동으로 닫힘
 *
 * 나이아가라는 복제되는 게 아니라 각 화면에서 로컬로 그리는 것이라, "이 화면의 로컬 폰이 술래인지"만
 * 보고 켜고 끈다 (MiscActor 하이라이트와 같은 방식). 사운드는 술래도 들리게 함.
 *
 * [주의] 컴포넌트 복제/Multicast는 오너 액터가 복제될 때만 동작하므로 BP_scientest의 Replicates가 켜져
 * 있어야 한다. 꺼져 있으면 BeginPlay에서 경고 로그를 찍고 서버에서 강제로 켠다.
 */
UCLASS(ClassGroup = (EscapeRoom), meta = (BlueprintSpawnableComponent))
class ESCAPEROOM_API UJailGateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UJailGateComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Jail")
	bool IsOpen() const { return bIsOpen; }

	// 서버 전용. 문을 열고 OpenDuration 뒤에 자동으로 닫는 타이머를 건다
	UFUNCTION(BlueprintCallable, Category = "Jail")
	void OpenDoors();

	// 서버 전용
	UFUNCTION(BlueprintCallable, Category = "Jail")
	void CloseDoors();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// AChaserCharacter가 생성자에서 붙이는 태그와 동일해야 함 (술래 판별용)
	UPROPERTY(EditAnywhere, Category = "Jail")
	FName ChaserTag = TEXT("Chaser");

	// ---- 문 ----
	UPROPERTY(EditAnywhere, Category = "Jail|Door")
	TArray<FJailDoorSetting> Doors;

	// 열려 있는 시간(초). 이 시간이 지나면 자동으로 닫힘
	UPROPERTY(EditAnywhere, Category = "Jail|Door", meta = (ClampMin = "0.1"))
	float OpenDuration = 10.f;

	// 문이 완전히 열리거나 닫히는 데 걸리는 시간(초)
	UPROPERTY(EditAnywhere, Category = "Jail|Door", meta = (ClampMin = "0.05"))
	float SlideDuration = 1.f;

	UPROPERTY(EditAnywhere, Category = "Jail|Sound")
	TObjectPtr<USoundBase> DoorOpenSound;

	UPROPERTY(EditAnywhere, Category = "Jail|Sound")
	TObjectPtr<USoundBase> DoorCloseSound;

	// ---- 스위치 ----
	// 동료가 밟는 발판 Box Collision 이름 (드롭다운 선택)
	UPROPERTY(EditAnywhere, Category = "Jail|Switch", meta = (GetOptions = "GetBoxComponentOptions"))
	TArray<FName> Switches;

	// 발판 위에 항상 떠 있는 루프 이펙트 (러너 화면에만 보임)
	UPROPERTY(EditAnywhere, Category = "Jail|Switch")
	TObjectPtr<UNiagaraSystem> SwitchIdleEffect;

	// ---- 감옥 구역 ----
	// 감옥 방 안쪽을 덮는 Box Collision (드롭다운 선택). 러너가 들어오면(잡혀서 순간이동 포함) 잡힌 상태,
	// 나가면 풀려난 상태가 됨. 문 밖 발판까지 덮지 않도록 감옥 안쪽만 덮을 것
	UPROPERTY(EditAnywhere, Category = "Jail|Area", meta = (GetOptions = "GetBoxComponentOptions"))
	TArray<FName> JailAreas;

	// 대기 이펙트 크기 (Box 스케일과 상관없이 이 값이 월드 스케일로 적용됨)
	UPROPERTY(EditAnywhere, Category = "Jail|Switch")
	FVector SwitchIdleEffectScale = FVector::OneVector;

	// 대기 이펙트 위치 오프셋 (Box 중심 기준, cm). 바닥에 깔고 싶으면 Z를 음수로
	UPROPERTY(EditAnywhere, Category = "Jail|Switch")
	FVector SwitchIdleEffectOffset = FVector::ZeroVector;

	// 발판을 밟아 문이 열릴 때 한 번 터지는 이펙트 (러너 화면에만 보임)
	UPROPERTY(EditAnywhere, Category = "Jail|Switch")
	TObjectPtr<UNiagaraSystem> SwitchActivateEffect;

	// 발동 이펙트 크기
	UPROPERTY(EditAnywhere, Category = "Jail|Switch")
	FVector SwitchActivateEffectScale = FVector::OneVector;

	// 발동 이펙트 위치 오프셋 (Box 중심 기준, cm)
	UPROPERTY(EditAnywhere, Category = "Jail|Switch")
	FVector SwitchActivateEffectOffset = FVector::ZeroVector;

	// 발판 발동 사운드 (술래 포함 모두에게 들림)
	UPROPERTY(EditAnywhere, Category = "Jail|Switch")
	TObjectPtr<USoundBase> SwitchActivateSound;

	// [테스트용] 체크하면 잡힌 러너가 없어도 발판을 밟으면 문이 열림 (혼자 테스트할 때). 실제 게임에선 끌 것
	UPROPERTY(EditAnywhere, Category = "Jail|Debug")
	bool bDebugIgnoreCaughtCheck = false;

	// 발판 판정 결과를 출력 로그에 찍음 (LogTemp)
	UPROPERTY(EditAnywhere, Category = "Jail|Debug")
	bool bDebugLog = true;

	// 로컬 폰이 바뀌는 경우(스폰 지연, 캐비닛 빙의 등)를 위해 이펙트 표시 여부를 다시 확인하는 주기(초)
	UPROPERTY(EditAnywhere, Category = "Jail|Switch", meta = (ClampMin = "0.1"))
	float LocalFXRefreshInterval = 0.5f;

	// ---- 에디터 드롭다운 목록 (GetOptions) ----
	UFUNCTION()
	TArray<FString> GetStaticMeshComponentOptions() const;

	UFUNCTION()
	TArray<FString> GetBoxComponentOptions() const;

private:
	// 이 컴포넌트가 붙은 BP(또는 액터)에서 ComponentClass 타입 컴포넌트의 이름을 모음.
	// 에디터의 BP 편집 화면에서는 오너 액터가 없으므로 BlueprintGeneratedClass의 SCS 노드에서 읽는다
	TArray<FString> GatherComponentNames(UClass* ComponentClass) const;

	// 런타임: 오너 액터에서 이름으로 컴포넌트 찾기
	template<typename T>
	T* FindOwnerComponentByName(FName Name) const;

	UPROPERTY(ReplicatedUsing = OnRep_IsOpen)
	bool bIsOpen = false;

	UFUNCTION()
	void OnRep_IsOpen();

	// 서버(OpenDoors/CloseDoors)와 클라(OnRep) 공통 처리: 사운드 재생 + 문 슬라이드 틱 켜기
	void ApplyOpenState();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlaySwitchActivated(FVector SwitchLocation, FRotator SwitchRotation);

	UFUNCTION()
	void OnSwitchBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnJailAreaBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnJailAreaEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	bool CanTrigger(const AActor* OtherActor) const;
	bool IsAnyRunnerCaught() const;

	// 이 화면의 로컬 폰이 술래가 아니면 true (데디케이티드 서버/로컬 폰 없음은 false)
	bool ShouldLocalViewerSeeRunnerFX() const;
	void RefreshLocalFXVisibility();

	FVector GetDoorsCenter() const;

	struct FJailDoorEntry
	{
		TWeakObjectPtr<USceneComponent> Component;
		FVector ClosedRelativeLocation = FVector::ZeroVector;
		FVector OpenRelativeLocation = FVector::ZeroVector;
	};
	TArray<FJailDoorEntry> ResolvedDoors;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBoxComponent>> ResolvedSwitches;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBoxComponent>> ResolvedJailAreas;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UNiagaraComponent>> SwitchIdleFXComponents;

	// 모든 문이 공유하는 현재 열림 정도 (0 = 닫힘, 1 = 열림)
	float OpenAlpha = 0.f;

	FTimerHandle AutoCloseTimer;
	FTimerHandle LocalFXRefreshTimer;
};
