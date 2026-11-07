// ExitActor.cpp
#include "ExitActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "NiagaraComponent.h"
#include "ActorSequenceComponent.h"
#include "ActorSequencePlayer.h"
#include "RunnerCharacter.h"
#include "Net/UnrealNetwork.h"
#include "EscapeRoomGameState.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"

AExitActor::AExitActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	// 즉시 발동이 아니라 홀드로 처리 - 기본값(0, 즉시)이면 MiscActor::CanInteract 경로에서
	// HoldTime<=0으로 판정돼서 RunnerCharacter가 홀드 게이지(MatchWidget)를 아예 띄우지 않는다.
	// ClimbMontage를 더 이상 루프시키지 않고 한 번만 재생하기로 해서, 홀드 시간을 그 몽타주
	// 재생 시간(MT_Climb 길이)에 맞춰 6초로 설정 - 홀드가 끝나는 시점과 몽타주가 끝까지
	// 재생되는 시점이 대략 맞아떨어지도록 함
	InteractHoldTime = 5.9f;

	ExitMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ExitMesh"));
	ExitMesh->SetupAttachment(Root);
	// 상호작용 라인 트레이스(RunnerCharacter::FindInteractableTarget)가 맞을 수 있도록
	// 콜리전은 기본값(Mesh 임포트 설정)을 그대로 사용

	AuraComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("AuraComponent"));
	AuraComponent->SetupAttachment(Root);
	AuraComponent->bAutoActivate = false; // ApplyOpenVisuals에서 상태에 맞춰 재생/정지 제어

	// 범위 안에 들어오면 정방향 재생, 나가면 역재생하는 순수 비주얼용 시퀀스.
	// HasAuthority() 체크 없음 (DiceActor의 bobbing 애니메이션과 동일한 이유: 서버/클라 각자
	// 동일하게 리플리케이트된 위치를 보고 로컬에서 재생하므로 서버 권한이 필요 없음)
	// RangeSequence 자체는 여기서 만들지 않음 - 블루프린트(BP_Exit)에 직접 추가된
	// ActorSequenceComponent를 BeginPlay에서 FindComponentByClass로 찾아 씀 (에디터 이슈 회피)

	SequenceTriggerRange = CreateDefaultSubobject<USphereComponent>(TEXT("SequenceTriggerRange"));
	SequenceTriggerRange->SetupAttachment(Root);
	SequenceTriggerRange->SetSphereRadius(200.f);
	SequenceTriggerRange->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SequenceTriggerRange->SetCollisionResponseToAllChannels(ECR_Ignore);
	SequenceTriggerRange->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	// 사다리 오르기 시작 위치/각도 마커. 실제 배치는 블루프린트에서 사다리 메시에 맞춰 조정
	ClimbStartPoint = CreateDefaultSubobject<USceneComponent>(TEXT("ClimbStartPoint"));
	ClimbStartPoint->SetupAttachment(Root);
}

void AExitActor::BeginPlay()
{
	Super::BeginPlay();

	if (AuraEffect)
	{
		AuraComponent->SetAsset(AuraEffect);
	}

	// 시작 상태(기본 닫힘) 반영
	ApplyOpenVisuals();

	// 남은 탈출구 수 +1 (서버 전용 - 복제되어 클라 HUD에 반영됨)
	if (HasAuthority())
	{
		if (AEscapeRoomGameState* GS = GetWorld()->GetGameState<AEscapeRoomGameState>())
		{
			GS->AddActiveExitCount(1);
			bCountedAsActiveExit = true;
		}
	}

	// 블루프린트에 직접 추가된 ActorSequenceComponent를 찾아 캐싱 (없으면 RangeSequence는 nullptr로
	// 남고, 아래 콜백들은 null 체크로 안전하게 무시함)
	RangeSequence = FindComponentByClass<UActorSequenceComponent>();

	SequenceTriggerRange->OnComponentBeginOverlap.AddDynamic(this, &AExitActor::OnSequenceRangeBeginOverlap);
	SequenceTriggerRange->OnComponentEndOverlap.AddDynamic(this, &AExitActor::OnSequenceRangeEndOverlap);
}

void AExitActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 탈출에 쓰여 Destroy되면 남은 탈출구 수 -1
	if (HasAuthority() && bCountedAsActiveExit)
	{
		if (UWorld* World = GetWorld())
		{
			if (AEscapeRoomGameState* GS = World->GetGameState<AEscapeRoomGameState>())
			{
				GS->AddActiveExitCount(-1);
			}
		}
		bCountedAsActiveExit = false;
	}

	Super::EndPlay(EndPlayReason);
}

bool AExitActor::IsOccupied() const
{
	const APawn* Climber = CurrentClimber.Get();
	if (!IsValid(Climber))
	{
		return false;
	}

	// 오르던 러너가 도중에 잡혔으면(목조르기 연출 중 포함) 잠금이 풀린 것으로 취급 - 취소 RPC가
	// 안 들어오는 경우(잡혀서 조종권이 끊기는 등)에도 탈출구가 영원히 잠기지 않게 하는 안전장치
	if (const ARunnerCharacter* Runner = Cast<ARunnerCharacter>(Climber))
	{
		if (Runner->IsCaught() || Runner->IsBeingCaught() || Runner->IsEscaped())
		{
			return false;
		}
	}
	return true;
}

bool AExitActor::CanInteractInternal(const APawn* Interactor) const
{
	if (!Interactor || IsChaser(Interactor) || !bIsOpen)
	{
		return false;
	}

	// 다른 러너가 오르는 중이면 불가 (본인이 오르는 중인 건 허용 - 홀드 완료 시 재검증을 통과해야 하므로)
	return !IsOccupied() || CurrentClimber.Get() == Interactor;
}

void AExitActor::OnInteractInternal(APawn* Interactor)
{
	// 호출 시점엔 이미 MiscActor::OnInteract_Implementation에서 HasAuthority() 확인이 끝난 뒤임
	// (+ ServerInteractWithTarget에서 CanInteract 재검증도 끝남 = 러너 + 아직 열림 + 잠금 통과)
	ARunnerCharacter* Runner = Cast<ARunnerCharacter>(Interactor);
	if (!Runner || !bIsOpen || Runner->IsEscaped())
	{
		return;
	}

	// 같은 프레임에 다른 요청이 또 들어와도 통과 못 하게 먼저 잠금 (파괴는 프레임 끝에 처리됨)
	bIsOpen = false;
	CurrentClimber = nullptr;

	// 1) 러너 탈출 확정 - 숨김/조작 차단/남은 인원 갱신/본인 화면 페이드+관전 전환까지 러너 쪽이 처리
	Runner->SetEscaped();

	// 알림: 남은 탈출구 수. EndPlay의 -1은 Destroy 이후에 반영되므로 이 탈출구를 미리 빼고 계산.
	// SetEscaped가 거부됐으면(이미 잡힘 등) 보내지 않고, 0개면(마지막 탈출구) 표시하지 않음
	if (Runner->IsEscaped())
	{
		if (AEscapeRoomGameState* GS = GetWorld()->GetGameState<AEscapeRoomGameState>())
		{
			const int32 RemainingExits = GS->ActiveExitCount - (bCountedAsActiveExit ? 1 : 0);
			if (RemainingExits > 0)
			{
				GS->MulticastMatchNotify(FText::Format(
					NSLOCTEXT("MatchNotify", "ExitsRemaining", "Exits remaining: {0}"),
					FText::AsNumber(RemainingExits)));
			}
		}
	}

	// 2) 탈출구 제거 (1인 1탈출구). 서버에서 파괴하면 모든 클라이언트에서도 사라지고,
	//    EndPlay에서 GameState의 남은 탈출구 수가 -1 됨
	Destroy();
}

void AExitActor::OnInteractHoldUpdateInternal(APawn* Interactor, bool bHolding)
{
	// 호출 시점엔 이미 MiscActor::OnInteractHoldUpdate_Implementation에서 HasAuthority() 확인이 끝난 뒤임
	// (bHolding=true일 때는 CanInteractInternal 재검증까지 끝난 뒤)

	if (ARunnerCharacter* Runner = Cast<ARunnerCharacter>(Interactor))
	{
		if (bHolding)
		{
			// 한 번에 한 명만 - 홀드를 시작한 러너로 잠금
			CurrentClimber = Runner;
			OnRep_CurrentClimber(); // 서버 자신의 화면에도 사다리 내림
			// 사다리 앞(ClimbStartPoint)으로 스냅한 뒤 오르기 시작 - 없으면 러너 현재 위치/방향 그대로
			const FVector StartLocation = ClimbStartPoint ? ClimbStartPoint->GetComponentLocation() : Runner->GetActorLocation();
			const FRotator StartRotation = ClimbStartPoint ? ClimbStartPoint->GetComponentRotation() : Runner->GetActorRotation();
			Runner->MulticastBeginLadderHold(StartLocation, StartRotation);
		}
		else
		{
			// 본인이 잡고 있던 잠금만 해제 (다른 러너의 취소 신호로 남의 잠금이 풀리지 않게)
			if (CurrentClimber.Get() == Runner)
			{
				CurrentClimber = nullptr;
				OnRep_CurrentClimber(); // 범위 밖이면 사다리 닫음
			}
			Runner->MulticastCancelLadderHold();
		}
	}
}

void AExitActor::OnRep_CurrentClimber()
{
	if (IsValid(CurrentClimber.Get()))
	{
		// 누군가 오르기 시작 - 범위 오버랩과 상관없이 사다리를 내림 (이미 내려와 있으면 소리/재생 생략됨)
		if (bIsOpen)
		{
			PlayRangeSequenceOpen();
		}
		return;
	}

	// 오르기가 끝남(취소) - 스냅 때문에 범위 밖에 서 있을 수 있으므로, 범위 안에 러너가 아무도 없을 때만 닫음
	if (!IsAnyRunnerInSequenceRange())
	{
		PlayRangeSequenceClose();
	}
}

bool AExitActor::IsAnyRunnerInSequenceRange() const
{
	if (!SequenceTriggerRange)
	{
		return false;
	}

	TArray<AActor*> Overlapping;
	SequenceTriggerRange->GetOverlappingActors(Overlapping, APawn::StaticClass());
	for (AActor* Actor : Overlapping)
	{
		if (const APawn* Pawn = Cast<APawn>(Actor))
		{
			if (!IsChaser(Pawn))
			{
				return true;
			}
		}
	}
	return false;
}

void AExitActor::OnRep_IsOpen()
{
	ApplyOpenVisuals();
}

void AExitActor::ApplyOpenVisuals()
{
	if (bIsOpen && AuraEffect)
	{
		AuraComponent->Activate(true);
	}
	else
	{
		AuraComponent->Deactivate();

		// 이미 쓰인 탈출구 - 근처 연출(문 열림)이 열린 채로 남아있으면 닫아둠
		PlayRangeSequenceClose();
	}
}

void AExitActor::OnSequenceRangeBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	APawn* OtherPawn = Cast<APawn>(OtherActor);
	if (!OtherPawn)
	{
		return;
	}

	// 이 연출 시퀀스는 탈출구를 실제로 쓸 수 있는 러너 전용 - 술래는 애초에 상호작용도 못 하므로
	// (CanInteractInternal) 진입/이탈 연출도 재생되지 않게 함. 이미 쓰인 탈출구도 더 이상 열리지 않음
	if (IsChaser(OtherPawn) || !bIsOpen)
	{
		return;
	}

	PlayRangeSequenceOpen();
}

void AExitActor::OnSequenceRangeEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	APawn* OtherPawn = Cast<APawn>(OtherActor);
	if (!OtherPawn)
	{
		return;
	}

	// BeginOverlap과 동일한 이유로 술래는 제외
	if (IsChaser(OtherPawn))
	{
		return;
	}

	// 누군가 사다리를 오르는 중이면 닫지 않음 (홀드 시작 스냅으로 범위 밖으로 나가는 경우 포함).
	// 오르기가 취소되면 OnRep_CurrentClimber에서 범위를 다시 확인해 닫음.
	// 다른 러너가 아직 범위 안에 남아있어도 닫지 않음
	if (IsValid(CurrentClimber.Get()) || IsAnyRunnerInSequenceRange())
	{
		return;
	}

	PlayRangeSequenceClose();
}

void AExitActor::PlayRangeSequenceOpen()
{
	UActorSequencePlayer* Player = RangeSequence ? RangeSequence->GetSequencePlayer() : nullptr;
	if (!Player)
	{
		return;
	}

	// 이미 정방향 재생 중이거나 끝까지 열려있으면 소리 생략
	const bool bAlreadyOpening = Player->IsPlaying() && !Player->IsReversed();
	const bool bAlreadyOpen = !Player->IsPlaying() && Player->GetCurrentTime().AsSeconds() >= Player->GetDuration().AsSeconds() - KINDA_SMALL_NUMBER;
	// 이미 열리는 중/다 열린 상태면 Play()도 부르지 않음 - 끝까지 재생된 시퀀스에 Play()를 다시 부르면
	// 처음부터 재생돼서, 내려와 있던 사다리가 위로 순간이동했다가 다시 내려오는 문제가 있었음
	// (오르는 중 캡슐 보정 텔레포트로 오버랩이 다시 계산될 때 BeginOverlap/OnRep_CurrentClimber가 여기로 들어옴)
	if (bAlreadyOpening || bAlreadyOpen)
	{
		return;
	}

	PlayOpenSound();
	Player->Play();
}

void AExitActor::PlayRangeSequenceClose()
{
	UActorSequencePlayer* Player = RangeSequence ? RangeSequence->GetSequencePlayer() : nullptr;
	if (!Player)
	{
		return;
	}

	// 이미 역재생 중이거나 완전히 닫혀있으면 소리/역재생 생략
	const bool bAlreadyClosing = Player->IsPlaying() && Player->IsReversed();
	const bool bAlreadyClosed = Player->GetCurrentTime().AsSeconds() <= KINDA_SMALL_NUMBER;
	if (bAlreadyClosing || bAlreadyClosed)
	{
		return;
	}

	PlayCloseSound();
	Player->PlayReverse();
}

void AExitActor::PlayOpenSound()
{
	if (OpenSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, OpenSound, GetActorLocation(), FRotator::ZeroRotator, 1.f, 1.f, 0.f, DoorSoundAttenuation);
	}
}

void AExitActor::PlayCloseSound()
{
	if (CloseSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, CloseSound, GetActorLocation(), FRotator::ZeroRotator, 1.f, 1.f, 0.f, DoorSoundAttenuation);
	}
}

void AExitActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AExitActor, bIsOpen);
	DOREPLIFETIME(AExitActor, CurrentClimber);
}
