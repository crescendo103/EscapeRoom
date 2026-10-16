#include "ResultRoomActor.h"
#include "Components/SceneComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Animation/AnimationAsset.h"
#include "Animation/Skeleton.h"
#include "EngineUtils.h"

AResultRoomActor::AResultRoomActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // 내 화면 전용 연출

#if WITH_EDITORONLY_DATA
	// World Partition: 결과 방은 던전과 멀리 떨어져 있어서, 거리 기반 스트리밍이면 플레이어 근처가 아니라
	// 로드가 안 됨 -> 항상 로드되도록 (게임 끝나는 순간 바로 보여야 함)
	bIsSpatiallyLoaded = false;
#endif

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	ResultCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ResultCamera"));
	ResultCamera->SetupAttachment(Root);

	CharacterMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh"));
	CharacterMesh->SetupAttachment(Root);
	CharacterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CharacterMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
}

void AResultRoomActor::BeginPlay()
{
	Super::BeginPlay();

	// BP에서 잡아둔 기본 메시 위치를 기준으로 기억
	DefaultMeshTransform = CharacterMesh->GetRelativeTransform();

	// 데디 서버는 화면이 없으니 메시 애니 계산할 필요 없음
	if (IsNetMode(NM_DedicatedServer))
	{
		CharacterMesh->SetComponentTickEnabled(false);
		return;
	}

	// 기본 메시로 미리 걷게 해둠 (결과 시점에 메시만 바꿔 끼우면 다시 재생)
	PlayLoopAnimation();
}

void AResultRoomActor::ApplyCharacter(USkeletalMesh* Mesh, const TArray<TObjectPtr<UMaterialInterface>>& Materials)
{

	if (Mesh)
	{
		// BP 기본 메시에 걸려 있던 머티리얼 오버라이드가 새 메시에 섞이지 않게 먼저 비움
		CharacterMesh->EmptyOverrideMaterials();
		CharacterMesh->SetSkeletalMeshAsset(Mesh);

		for (int32 i = 0; i < Materials.Num(); ++i)
		{
			if (Materials[i])
			{
				CharacterMesh->SetMaterial(i, Materials[i]);
			}
		}
	}

	CharacterMesh->SetVisibility(true);
	CharacterMesh->SetComponentTickEnabled(true);

	// 메시를 바꾸면 재생 중이던 애니가 초기화되므로 반드시 교체 뒤에 다시 재생
	PlayLoopAnimation();
}

void AResultRoomActor::PlayLoopAnimation()
{
	const USkeletalMesh* Mesh = CharacterMesh->GetSkeletalMeshAsset();
	const USkeleton* MeshSkeleton = Mesh ? Mesh->GetSkeleton() : nullptr;

	// 지금 메시와 같은 스켈레톤의 애니 항목을 찾음 (다른 스켈레톤 애니는 재생이 안 되거나 포즈가 깨짐)
	const FResultLoopEntry* Found = nullptr;
	for (const FResultLoopEntry& Entry : LoopAnimations)
	{
		if (Entry.Animation && Entry.Animation->GetSkeleton() == MeshSkeleton)
		{
			Found = &Entry;
			break;
		}
	}

	// 위치: BP 기본 위치 + 이 캐릭터용 오프셋 (항목이 없으면 기본 위치 그대로)
	FTransform NewTransform = DefaultMeshTransform;
	if (Found)
	{
		NewTransform.SetLocation(DefaultMeshTransform.GetLocation() + Found->LocationOffset);
		NewTransform.SetRotation((DefaultMeshTransform.Rotator() + Found->RotationOffset).Quaternion());
		NewTransform.SetScale3D(DefaultMeshTransform.GetScale3D() * Found->ScaleMultiplier);
	}
	CharacterMesh->SetRelativeTransform(NewTransform);

	if (!Found)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ResultRoom] %s: 메시 %s(스켈레톤 %s)에 맞는 애니가 LoopAnimations에 없음"),
			*GetName(), *GetNameSafe(Mesh), *GetNameSafe(MeshSkeleton));
		return;
	}

	CharacterMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	CharacterMesh->PlayAnimation(Found->Animation, /*bLooping=*/true);
}

AResultRoomActor* AResultRoomActor::FindRoom(const UObject* WorldContext, EMatchResultRoomType Type)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AResultRoomActor> It(World); It; ++It)
	{
		if (It->RoomType == Type)
		{
			return *It;
		}
	}
	return nullptr;
}
