// JailGateComponent.cpp
#include "JailGateComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "RunnerCharacter.h"
#include "EscapeRoomGameState.h"

// 런타임: 오너 액터에서 이름으로 컴포넌트 찾기 (SCS로 만든 컴포넌트의 이름 = BP 변수 이름)
template<typename T>
T* UJailGateComponent::FindOwnerComponentByName(FName Name) const
{
	const AActor* Owner = GetOwner();
	if (!Owner || Name.IsNone())
	{
		return nullptr;
	}

	TInlineComponentArray<T*> Comps;
	Owner->GetComponents(Comps);
	for (T* Comp : Comps)
	{
		if (Comp && Comp->GetFName() == Name)
		{
			return Comp;
		}
	}
	return nullptr;
}

UJailGateComponent::UJailGateComponent()
{
	// 문이 움직이는 동안에만 틱을 켬 (ApplyOpenState에서 켜고, 목표 각도 도달 시 끔)
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	SetIsReplicatedByDefault(true);
}

void UJailGateComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// 컴포넌트 복제/Multicast는 오너가 복제될 때만 동작함
	if (Owner->HasAuthority() && !Owner->GetIsReplicated())
	{
		UE_LOG(LogTemp, Warning, TEXT("UJailGateComponent: %s의 Replicates가 꺼져 있어 강제로 켬 - 블루프린트 Class Defaults에서 Replicates를 체크해두는 것을 권장"), *Owner->GetName());
		Owner->SetReplicates(true);
	}

	// ---- 에디터에서 연결한 문/스위치를 실제 컴포넌트로 변환 ----
	for (int32 i = 0; i < Doors.Num(); ++i)
	{
		UStaticMeshComponent* Comp = FindOwnerComponentByName<UStaticMeshComponent>(Doors[i].Door);
		if (!Comp)
		{
			UE_LOG(LogTemp, Warning, TEXT("UJailGateComponent: %s의 Doors[%d] (%s)를 찾지 못함 - 디테일 패널에서 다시 선택 필요"), *Owner->GetName(), i, *Doors[i].Door.ToString());
			continue;
		}

		if (Comp->Mobility != EComponentMobility::Movable)
		{
			UE_LOG(LogTemp, Warning, TEXT("UJailGateComponent: 문 컴포넌트 %s의 Mobility가 Movable이 아니라서 변경함 - 블루프린트에서 Movable로 설정 권장"), *Comp->GetName());
			Comp->SetMobility(EComponentMobility::Movable);
		}

		FJailDoorEntry Entry;
		Entry.Component = Comp;
		Entry.ClosedRelativeLocation = Comp->GetRelativeLocation();
		// OpenOffset은 문 자신의 로컬 축 기준 -> 부모 기준 상대 좌표로 변환해서 저장
		Entry.OpenRelativeLocation = Entry.ClosedRelativeLocation + Comp->GetRelativeRotation().RotateVector(Doors[i].OpenOffset);
		ResolvedDoors.Add(Entry);
	}

	for (int32 i = 0; i < Switches.Num(); ++i)
	{
		UBoxComponent* Box = FindOwnerComponentByName<UBoxComponent>(Switches[i]);
		if (!Box)
		{
			UE_LOG(LogTemp, Warning, TEXT("UJailGateComponent: %s의 Switches[%d] (%s)를 찾지 못했거나 Box 컴포넌트가 아님"), *Owner->GetName(), i, *Switches[i].ToString());
			continue;
		}
		ResolvedSwitches.Add(Box);
	}

	for (int32 i = 0; i < JailAreas.Num(); ++i)
	{
		UBoxComponent* Box = FindOwnerComponentByName<UBoxComponent>(JailAreas[i]);
		if (!Box)
		{
			UE_LOG(LogTemp, Warning, TEXT("UJailGateComponent: %s의 JailAreas[%d] (%s)를 찾지 못했거나 Box 컴포넌트가 아님"), *Owner->GetName(), i, *JailAreas[i].ToString());
			continue;
		}
		ResolvedJailAreas.Add(Box);
	}

	UE_LOG(LogTemp, Log, TEXT("[JailGate] %s: 문 %d개, 스위치 %d개, 감옥 구역 %d개 연결됨 (Authority=%d)"), *Owner->GetName(), ResolvedDoors.Num(), ResolvedSwitches.Num(), ResolvedJailAreas.Num(), Owner->HasAuthority() ? 1 : 0);

	if (ResolvedJailAreas.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("UJailGateComponent: %s에 연결된 감옥 구역이 없음 - 잡혀도 bIsCaught가 true가 되지 않음"), *Owner->GetName());
	}

	// ---- 감옥 구역 오버랩: 잡힘/풀림 판정은 서버에서만 ----
	for (UBoxComponent* Box : ResolvedJailAreas)
	{
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Ignore);
		Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		Box->SetGenerateOverlapEvents(true);

		if (Owner->HasAuthority())
		{
			Box->OnComponentBeginOverlap.AddDynamic(this, &UJailGateComponent::OnJailAreaBeginOverlap);
			Box->OnComponentEndOverlap.AddDynamic(this, &UJailGateComponent::OnJailAreaEndOverlap);
		}
	}

	if (ResolvedDoors.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("UJailGateComponent: %s에 연결된 문이 없음"), *Owner->GetName());
	}
	if (ResolvedSwitches.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("UJailGateComponent: %s에 연결된 스위치가 없음"), *Owner->GetName());
	}

	// ---- 스위치 오버랩: 판정은 서버에서만 ----
	for (UBoxComponent* Box : ResolvedSwitches)
	{
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Ignore);
		Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		Box->SetGenerateOverlapEvents(true);

		if (Owner->HasAuthority())
		{
			Box->OnComponentBeginOverlap.AddDynamic(this, &UJailGateComponent::OnSwitchBeginOverlap);
		}
	}

	// ---- 스위치 대기 이펙트: 각 화면 로컬 처리 (데디케이티드 서버는 그릴 화면이 없음) ----
	if (GetNetMode() != NM_DedicatedServer)
	{
		if (SwitchIdleEffect)
		{
			for (UBoxComponent* Box : ResolvedSwitches)
			{
				UNiagaraComponent* FX = UNiagaraFunctionLibrary::SpawnSystemAttached(
					SwitchIdleEffect, Box, NAME_None, SwitchIdleEffectOffset, FRotator::ZeroRotator,
					EAttachLocation::KeepRelativeOffset, /*bAutoDestroy*/ false, /*bAutoActivate*/ true);
				if (FX)
				{
					// Box를 스케일로 키웠어도 이펙트가 같이 늘어나지 않게 스케일만 월드 기준으로 고정
					FX->SetUsingAbsoluteScale(true);
					FX->SetWorldScale3D(SwitchIdleEffectScale);
					FX->SetVisibility(false); // 로컬 폰 확인 전까지는 숨김
					SwitchIdleFXComponents.Add(FX);
				}
			}
		}

		RefreshLocalFXVisibility();
		GetWorld()->GetTimerManager().SetTimer(LocalFXRefreshTimer, this, &UJailGateComponent::RefreshLocalFXVisibility, LocalFXRefreshInterval, true);
	}
}

void UJailGateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoCloseTimer);
		World->GetTimerManager().ClearTimer(LocalFXRefreshTimer);
	}

	for (UNiagaraComponent* FX : SwitchIdleFXComponents)
	{
		if (FX)
		{
			FX->DestroyComponent();
		}
	}
	SwitchIdleFXComponents.Reset();

	Super::EndPlay(EndPlayReason);
}

// =========================== 스위치 판정 (서버) ===========================

void UJailGateComponent::OnSwitchBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !CanTrigger(OtherActor))
	{
		return;
	}

	if (bDebugLog)
	{
		UE_LOG(LogTemp, Warning, TEXT("[JailGate] %s가 스위치를 밟아 문 열림"), *GetNameSafe(OtherActor));
	}

	OpenDoors();

	const FTransform SwitchTransform = OverlappedComp ? OverlappedComp->GetComponentTransform() : GetOwner()->GetActorTransform();
	MulticastPlaySwitchActivated(SwitchTransform.GetLocation(), SwitchTransform.Rotator());
}

void UJailGateComponent::OnJailAreaBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	ARunnerCharacter* Runner = Cast<ARunnerCharacter>(OtherActor);
	if (!Runner || Runner->ActorHasTag(ChaserTag) || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (bDebugLog && !Runner->IsCaught())
	{
		UE_LOG(LogTemp, Warning, TEXT("[JailGate] %s 감옥 구역 진입 -> 잡힘"), *Runner->GetName());
	}
	Runner->SetCaught(true);
}

void UJailGateComponent::OnJailAreaEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	ARunnerCharacter* Runner = Cast<ARunnerCharacter>(OtherActor);
	if (!Runner || Runner->ActorHasTag(ChaserTag) || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// 감옥 구역 Box가 여러 개이고 서로 붙어 있으면, 한 Box에서 나가도 다른 Box 안에 있을 수 있음
	for (UBoxComponent* Box : ResolvedJailAreas)
	{
		if (Box && Box != OverlappedComp && Box->IsOverlappingActor(Runner))
		{
			return;
		}
	}

	if (bDebugLog && Runner->IsCaught())
	{
		UE_LOG(LogTemp, Warning, TEXT("[JailGate] %s 감옥 구역 이탈 -> 풀려남"), *Runner->GetName());
	}
	Runner->SetCaught(false);
}

bool UJailGateComponent::CanTrigger(const AActor* OtherActor) const
{
	auto Reject = [this, OtherActor](const TCHAR* Reason)
	{
		if (bDebugLog)
		{
			UE_LOG(LogTemp, Warning, TEXT("[JailGate] %s 스위치 진입 무시: %s"), *GetNameSafe(OtherActor), Reason);
		}
		return false;
	};

	// 이미 열려 있는 10초 동안은 다시 밟아도 무시
	if (bIsOpen)
	{
		return Reject(TEXT("gate already open"));
	}

	// 러너만. 술래는 무시
	const ARunnerCharacter* Runner = Cast<ARunnerCharacter>(OtherActor);
	if (!Runner)
	{
		return Reject(TEXT("not an ARunnerCharacter"));
	}
	if (Runner->ActorHasTag(ChaserTag))
	{
		return Reject(TEXT("chaser"));
	}

	// 밟는 사람은 잡히지 않은 동료여야 함 (목조르기 연출 중인 러너도 제외)
	if (Runner->IsCaught() || Runner->IsBeingCaught())
	{
		return Reject(TEXT("the runner on the switch is caught"));
	}

	// 구해줄 사람이 있어야 발동
	if (!bDebugIgnoreCaughtCheck && !IsAnyRunnerCaught())
	{
		return Reject(TEXT("no caught runners (for solo testing enable Jail|Debug > bDebugIgnoreCaughtCheck)"));
	}

	return true;
}

bool UJailGateComponent::IsAnyRunnerCaught() const
{
	for (TActorIterator<ARunnerCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsCaught())
		{
			return true;
		}
	}
	return false;
}

// =========================== 문 개폐 ===========================

void UJailGateComponent::OpenDoors()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// 이미 열려있는 상태에서 다시 밟은 거면(타이머 연장만) 알림은 다시 안 보냄
	const bool bWasOpen = bIsOpen;

	bIsOpen = true;
	ApplyOpenState(); // 리슨 서버 자신의 화면 (OnRep은 클라이언트에서만 자동 호출)

	// 알림: 감옥문 열림 (전원에게)
	if (!bWasOpen)
	{
		if (AEscapeRoomGameState* GS = GetWorld()->GetGameState<AEscapeRoomGameState>())
		{
			// 알림음은 공통 대신 WBP_Match의 JailDoorOpenedSound
			GS->MulticastMatchNotifyWithSound(NSLOCTEXT("MatchNotify", "JailDoorOpened", "The jail door opened!"), EMatchNotifySound::JailDoorOpened);
		}
	}

	GetWorld()->GetTimerManager().SetTimer(AutoCloseTimer, this, &UJailGateComponent::CloseDoors, OpenDuration, false);
}

void UJailGateComponent::CloseDoors()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !bIsOpen)
	{
		return;
	}

	GetWorld()->GetTimerManager().ClearTimer(AutoCloseTimer);
	bIsOpen = false;
	ApplyOpenState();
}

void UJailGateComponent::OnRep_IsOpen()
{
	ApplyOpenState();
}

void UJailGateComponent::ApplyOpenState()
{
	// 문 사운드는 술래 포함 모두에게 (데디케이티드 서버는 소리 낼 필요 없음)
	if (GetNetMode() != NM_DedicatedServer && ResolvedDoors.Num() > 0)
	{
		if (USoundBase* Sound = bIsOpen ? DoorOpenSound.Get() : DoorCloseSound.Get())
		{
			UGameplayStatics::PlaySoundAtLocation(this, Sound, GetDoorsCenter());
		}
	}

	// 서버도 움직여야 함 - 문 콜리전이 서버에서도 실제로 비켜줘야 캐릭터 이동이 막히지 않음
	SetComponentTickEnabled(true);
}

void UJailGateComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const float TargetAlpha = bIsOpen ? 1.f : 0.f;
	OpenAlpha = FMath::FInterpConstantTo(OpenAlpha, TargetAlpha, DeltaTime, 1.f / SlideDuration);

	// 양 끝에서 부드럽게 가감속
	const float Eased = FMath::InterpEaseInOut(0.f, 1.f, OpenAlpha, 2.f);

	for (const FJailDoorEntry& Door : ResolvedDoors)
	{
		if (USceneComponent* Comp = Door.Component.Get())
		{
			Comp->SetRelativeLocation(FMath::Lerp(Door.ClosedRelativeLocation, Door.OpenRelativeLocation, Eased));
		}
	}

	if (FMath::IsNearlyEqual(OpenAlpha, TargetAlpha))
	{
		SetComponentTickEnabled(false);
	}
}

FVector UJailGateComponent::GetDoorsCenter() const
{
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	for (const FJailDoorEntry& Door : ResolvedDoors)
	{
		if (const USceneComponent* Comp = Door.Component.Get())
		{
			Sum += Comp->GetComponentLocation();
			++Count;
		}
	}
	return Count > 0 ? Sum / Count : (GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector);
}

// =========================== 이펙트/사운드 (각 화면) ===========================

void UJailGateComponent::MulticastPlaySwitchActivated_Implementation(FVector SwitchLocation, FRotator SwitchRotation)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// 사운드는 모두에게
	if (SwitchActivateSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, SwitchActivateSound, SwitchLocation);
	}

	// 이펙트는 러너 화면에만
	if (SwitchActivateEffect && ShouldLocalViewerSeeRunnerFX())
	{
		const FVector FXLocation = SwitchLocation + SwitchRotation.RotateVector(SwitchActivateEffectOffset);
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, SwitchActivateEffect, FXLocation, SwitchRotation, SwitchActivateEffectScale);
	}
}

bool UJailGateComponent::ShouldLocalViewerSeeRunnerFX() const
{
	const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC || !PC->IsLocalController())
	{
		return false;
	}

	// 로컬 폰이 아직 없으면(스폰 대기 등) 숨김 - LocalFXRefreshTimer가 다시 확인함.
	// 캐비닛에 숨은 러너는 캐비닛 폰을 조종 중이지만 술래 태그가 없으므로 계속 보임
	const APawn* LocalPawn = PC->GetPawn();
	return LocalPawn && !LocalPawn->ActorHasTag(ChaserTag);
}

void UJailGateComponent::RefreshLocalFXVisibility()
{
	const bool bShow = ShouldLocalViewerSeeRunnerFX();
	for (UNiagaraComponent* FX : SwitchIdleFXComponents)
	{
		if (FX && FX->IsVisible() != bShow)
		{
			FX->SetVisibility(bShow);
		}
	}
}

void UJailGateComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UJailGateComponent, bIsOpen);
}

// =========================== 에디터 드롭다운 / 이름으로 찾기 ===========================

TArray<FString> UJailGateComponent::GetStaticMeshComponentOptions() const
{
	return GatherComponentNames(UStaticMeshComponent::StaticClass());
}

TArray<FString> UJailGateComponent::GetBoxComponentOptions() const
{
	return GatherComponentNames(UBoxComponent::StaticClass());
}

TArray<FString> UJailGateComponent::GatherComponentNames(UClass* ComponentClass) const
{
	TArray<FString> Names;

	// 레벨에 배치된 인스턴스/플레이 중: 실제 오너 액터의 컴포넌트
	if (const AActor* Owner = GetOwner())
	{
		TInlineComponentArray<UActorComponent*> Comps;
		Owner->GetComponents(Comps);
		for (const UActorComponent* Comp : Comps)
		{
			if (Comp && Comp->IsA(ComponentClass))
			{
				Names.AddUnique(Comp->GetName());
			}
		}
	}
	else
	{
		// BP 편집 화면: 이 컴포넌트는 템플릿이고 Outer가 BlueprintGeneratedClass.
		// 부모 BP까지 올라가며 SCS(블루프린트에서 추가한 컴포넌트) 노드를 훑는다
		for (UClass* Class = Cast<UClass>(GetOuter()); Class; Class = Class->GetSuperClass())
		{
			const UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(Class);
			if (!BPGC)
			{
				break;
			}
			if (BPGC->SimpleConstructionScript)
			{
				for (const USCS_Node* Node : BPGC->SimpleConstructionScript->GetAllNodes())
				{
					if (Node && Node->ComponentTemplate && Node->ComponentTemplate->IsA(ComponentClass))
					{
						Names.AddUnique(Node->GetVariableName().ToString());
					}
				}
			}
		}
	}

	Names.Sort();
	Names.Insert(TEXT("None"), 0);
	return Names;
}
