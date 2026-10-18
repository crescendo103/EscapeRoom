// ResultRoomActor.h
//
// [설계 메모] 게임 종료 결과 연출용 "무대". 레벨(GameLV)의 던전 영역 밖에 4개를 배치해 두고
// (RoomType: 러너 승/패, 술래 승/패), 게임이 끝나면 각 클라가 자기 결과에 맞는 방을 골라
//   1) CharacterMesh를 "내가 플레이한 캐릭터" 메시로 교체 (ADungeonPlayerController가 빙의 시점에 저장해둔 것)
//   2) LoopAnimation(제자리 걷기 등)을 루프 재생
//   3) 카메라를 이 액터(= 안의 ResultCamera)로 전환
// 하는 방식으로 쓴다. 전부 로컬(내 화면) 전용이라 복제하지 않는다 - 같은 방을 여러 명이 동시에 봐도
// 각자 자기 캐릭터가 보인다.
//
// [에디터 작업] 이 클래스를 부모로 BP_ResultRoom을 만들고, 레벨에 배치한 방마다 RoomType / LoopAnimations / 결과 문구를 지정.
// 조명/안개/배경은 BP 안이나 레벨에 자유롭게 꾸미면 됨. LoopAnimation은 루트 모션이 꺼진 "제자리" 애니를 쓸 것.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ResultRoomActor.generated.h"

class USceneComponent;
class UCameraComponent;
class USkeletalMeshComponent;
class USkeletalMesh;
class UMaterialInterface;
class UAnimationAsset;

UENUM(BlueprintType)
enum class EMatchResultRoomType : uint8
{
	RunnerWin,
	RunnerLose,
	ChaserWin,
	ChaserLose
};

// 바닥 스크롤(러닝머신 연출)에 쓸 축 - Root 기준 상대 좌표
UENUM(BlueprintType)
enum class EResultScrollAxis : uint8
{
	X,
	Y,
	Z
};

// 결과 방에서 재생할 애니 + 그 캐릭터용 위치 보정값 한 묶음.
// 메시를 교체하면 그 메시와 같은 스켈레톤의 Animation을 가진 항목을 골라, 애니 재생 + 오프셋 적용.
// 오프셋은 BP에 놓은 CharacterMesh 위치(= 기본 캐릭터 기준)에 더해지므로 기본 캐릭터 항목은 0으로 두면 됨
USTRUCT(BlueprintType)
struct FResultLoopEntry
{
	GENERATED_BODY()

	// 루프 재생할 애니 (애니 시퀀스 권장, 루트 모션 OFF)
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimationAsset> Animation;

	// 기본 메시 위치에 더할 값 (피벗이 다른 캐릭터면 보통 Z만 조절)
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector LocationOffset = FVector::ZeroVector;

	// 기본 메시 회전에 더할 값
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FRotator RotationOffset = FRotator::ZeroRotator;

	// 기본 메시 스케일에 곱할 값
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector ScaleMultiplier = FVector::OneVector;
};

UCLASS()
class ESCAPEROOM_API AResultRoomActor : public AActor
{
	GENERATED_BODY()

public:
	AResultRoomActor();

	// 이 방이 어떤 결과용인지 (Tag 대신 이 값으로 찾음). 레벨에 타입별로 하나씩 둘 것 (같은 타입이 여러 개면 처음 찾은 것 사용).
	// 자식 BP(방별 BP)의 Class Defaults에서 미리 정해도 되고, 레벨에 배치한 인스턴스에서 정해도 됨
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ResultRoom")
	EMatchResultRoomType RoomType = EMatchResultRoomType::RunnerWin;

	// 캐릭터별 애니 + 위치 보정 (순서 상관없음). 교체된 메시와 스켈레톤이 같은 Animation의 항목이 자동 선택됨.
	// 예) RunnerLose 방: [기본러너_패배(오프셋 0), 러너1_패배(+Z 보정), 러너2_패배, ...]
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ResultRoom")
	TArray<FResultLoopEntry> LoopAnimations;

	// 화면에 크게 뜨는 결과 문구 (예: "ESCAPED" / "SACRIFICED" / "VICTORY" / "DEFEAT")
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ResultRoom")
	FText ResultTitle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ResultRoom")
	FLinearColor ResultTitleColor = FLinearColor::White;


	// 내 캐릭터 외형으로 교체 + 애니 재생. Mesh가 null이면 BP에 지정된 기본 메시 그대로 애니만 재생.
	// 위치는 BP의 CharacterMesh 위치 + 선택된 LoopAnimations 항목의 오프셋
	void ApplyCharacter(USkeletalMesh* Mesh, const TArray<TObjectPtr<UMaterialInterface>>& Materials);

	// 레벨에서 해당 타입의 방을 찾음 (없으면 nullptr)
	static AResultRoomActor* FindRoom(const UObject* WorldContext, EMatchResultRoomType Type);

	UCameraComponent* GetResultCamera() const { return ResultCamera; }

	// ---- 바닥 스크롤 (캐릭터는 제자리 걷기, 바닥/소품이 대신 흘러감) ----
	// ScrollTag가 붙은 컴포넌트들이 BackPosition -> FrontPosition 방향으로 이동하다가,
	// FrontPosition에 도달하면 (Front - Back) 거리만큼 되돌아감 (= BackPosition부터 다시 출발).
	// 판자를 여러 장 쓸 때는 Front - Back = 판자 길이 x 장수 로 맞추면 틈 없이 이어짐

	// 켜면 Tick이 돌면서 스크롤. 끄면 Tick 자체가 꺼짐
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ResultRoom|Scroll")
	bool bEnableScroll = false;

	// 이 태그(컴포넌트 디테일 > Tags)가 붙은 컴포넌트만 움직임. 자식 컴포넌트는 부모 따라 같이 움직이므로
	// 부모/자식 둘 다 태그를 달면 두 배로 움직임 - 부모에만 달 것
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ResultRoom|Scroll", meta = (EditCondition = "bEnableScroll"))
	FName ScrollTag = TEXT("Scroll");

	// 이동 축 (Root 기준 상대 좌표)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ResultRoom|Scroll", meta = (EditCondition = "bEnableScroll"))
	EResultScrollAxis ScrollAxis = EResultScrollAxis::X;

	// 초당 이동 거리(cm). 걷기 애니 속도에 맞출 것.
	// 양수 = Back -> Front 방향 (Front 도달 시 Back으로), 음수 = Front -> Back 방향 (Back 도달 시 Front로)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ResultRoom|Scroll", meta = (EditCondition = "bEnableScroll"))
	float ScrollSpeed = 150.f;

	// 되돌아갈 뒷위치 (축 좌표값)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ResultRoom|Scroll", meta = (EditCondition = "bEnableScroll"))
	float ScrollBackPosition = -1000.f;

	// 여기 도달하면 뒷위치로 (축 좌표값). Back보다 작으면 반대 방향으로 이동
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ResultRoom|Scroll", meta = (EditCondition = "bEnableScroll"))
	float ScrollFrontPosition = 1000.f;

	// 런타임에 켜고 끄기 (블루프린트에서도 호출 가능)
	UFUNCTION(BlueprintCallable, Category = "ResultRoom|Scroll")
	void SetScrollEnabled(bool bEnable);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ResultRoom")
	TObjectPtr<USceneComponent> Root;

	// 연출 시점. SetViewTarget(이 액터) 시 엔진이 이 카메라 컴포넌트를 사용함
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ResultRoom")
	TObjectPtr<UCameraComponent> ResultCamera;

	// 걷는 캐릭터 (폰이 아니라 메시만 - 이동/복제 없음)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ResultRoom")
	TObjectPtr<USkeletalMeshComponent> CharacterMesh;

private:
	void PlayLoopAnimation();

	// ScrollTag가 붙은 컴포넌트 캐시 (BeginPlay에서 수집)
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> ScrollComponents;

	// BP에 배치된 CharacterMesh의 원래 상대 트랜스폼 (보정 기준)
	FTransform DefaultMeshTransform = FTransform::Identity;
};
