#include "Features/Artifact/CSCleaningFairy.h"

#include "Features/Artifact/CSFairyHouse.h"
#include "Features/Trash/CSTrashActor.h"
#include "Features/Trash/CSTrashCanActor.h"
#include "Features/Store/CSStoreInteriorActor.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NiagaraComponent.h"
#include "Features/Customers/CSCustomerAnimInstance.h"
#include "Misc/ScopeExit.h"
#include "Components/WidgetComponent.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "UI/HUD/CSFairyDialogueWidget.h"
#include "HAL/PlatformTime.h"

ACSCleaningFairy::ACSCleaningFairy()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	FairyMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FairyMesh"));
	FairyMesh->SetupAttachment(RootComponent);
	FairyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// 원본 모델의 높이가 약 1.3m이므로 작은 요정 크기로 표시하고 기준점을 몸 중심에 맞춥니다.
	FairyMesh->SetRelativeScale3D(FVector(.25f));
	FairyMesh->SetRelativeLocation(FVector(0,0,-17));
	FairyMesh->SetSkeletalMeshAsset(LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Demo/KJW/Fairy/SK_ConvenienceStore_Fairy_Compact.SK_ConvenienceStore_Fairy_Compact")));
	FairyMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	FairyMesh->AnimationData.AnimToPlay = LoadObject<UAnimSequence>(nullptr,
		TEXT("/Game/Demo/KJW/Fairy/AN_Fairy_HoverIdle_Compact.AN_Fairy_HoverIdle_Compact"));
	FairyMesh->AnimationData.bSavedLooping = true;
	FairyMesh->AnimationData.bSavedPlaying = true;
	CarryPoint = CreateDefaultSubobject<USceneComponent>(TEXT("CarryPoint"));
	CarryPoint->SetupAttachment(RootComponent);
	CarryPoint->SetRelativeLocation(FVector(8, 0, -14));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/ConvenienceStore/Feature/Gacha/M_GachaGlow.M_GachaGlow"));
	Orb = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Orb"));
	Orb->SetupAttachment(RootComponent);
	Orb->SetStaticMesh(Sphere);
	Orb->SetRelativeScale3D(FVector(0.12));
	Orb->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (Glow) Orb->SetMaterial(0, Glow);
	OrbEffect = CreateDefaultSubobject<UNiagaraComponent>(TEXT("OrbEffect"));
	OrbEffect->SetupAttachment(RootComponent);
	OrbEffect->SetAutoActivate(false);
	// 전용 Niagara가 없어도 구체 뒤에 짧은 반짝이 꼬리가 보이도록 제공합니다.
	for (int32 Index = 0; Index < 12; ++Index)
	{
		auto* Spark = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Spark%d"), Index));
		Spark->SetupAttachment(RootComponent);
		Spark->SetStaticMesh(Sphere);
		Spark->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (Glow) Spark->SetMaterial(0, Glow);
		Spark->SetVisibility(false);
		Sparks.Add(Spark);
	}
	// 블루프린트 편집 중에는 모델과 운반 위치를 확인하실 수 있도록 표시합니다.
	FairyMesh->SetVisibility(true);
	Orb->SetVisibility(false);
	InteractionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBox"));
	InteractionBox->SetupAttachment(FairyMesh);
	InteractionBox->SetBoxExtent(FVector(80, 80, 140));
	InteractionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InteractionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionBox->SetCanEverAffectNavigation(false);
	SpeechBubble = CreateDefaultSubobject<UWidgetComponent>(TEXT("SpeechBubble"));
	SpeechBubble->SetupAttachment(RootComponent);
	SpeechBubble->SetWidgetSpace(EWidgetSpace::Screen);
	SpeechBubble->SetDrawSize(FVector2D(520, 230));
	SpeechBubble->SetDrawAtDesiredSize(true);
	SpeechBubble->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpeechBubble->SetVisibility(false);
}

void ACSCleaningFairy::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (GetWorld() && !GetWorld()->IsGameWorld()) FairyMesh->SetVisibility(true);
}

void ACSCleaningFairy::Initialize(ACSFairyHouse* InHouse)
{
	House = InHouse;
	Status = NSLOCTEXT("Fairy", "Waiting", "청소할 쓰레기를 기다리고 있습니다.");
	SetActorLocation(InHouse->DoorPoint->GetComponentLocation());
}

void ACSCleaningFairy::SetIntroductionActive(bool bActive)
{
	// 첫 등장 연출 중에는 쓰레기를 예약하지 않고 공중 대기 애니메이션만 재생합니다.
	bIntroductionActive = bActive;
	FairyMesh->SetTickableWhenPaused(bActive);
	OrbEffect->SetTickableWhenPaused(bActive);
	InteractionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpeechBubble->SetVisibility(false);
	if (BubbleWidget) BubbleWidget->SetVisibility(ESlateVisibility::Collapsed);
	if (bActive)
	{
		State = ECSFairyState::Emerging;
		ShowOrb(true);
	}
	else if (House.IsValid())
	{
		// 집에서 다시 나오지 않고 연출이 끝난 비행 지점에서 바로 작업을 찾습니다.
		SetActorLocation(House->FlightPoint->GetComponentLocation());
		PreviousVisualLocation = GetActorLocation();
		State = ECSFairyState::ToTrash;
		ShowOrb(false);
		if (!FindJob())
		{
			State = ECSFairyState::Entering;
			TransitionStart = GetActorLocation();
			TransitionTime = 0.f;
			ShowOrb(true);
		}
	}
}

void ACSCleaningFairy::UpdateIntroductionOrb(float Progress, float DeltaSeconds)
{
	if (!bIntroductionActive) return;
	const bool bEmerging = Progress < 1.f;
	if (Orb->IsVisible() != bEmerging) ShowOrb(bEmerging);
	if (bEmerging)
	{
		Orb->SetRelativeScale3D(FVector(.12f + .12f * FMath::Square(Progress)));
		UpdateOrb(DeltaSeconds);
	}
}

bool ACSCleaningFairy::CanTalkWhileWorking() const
{
	return !bIntroductionActive && !IsAtHome() && FairyMesh->IsVisible();
}

void ACSCleaningFairy::UpdateInteraction()
{
	const bool bCanTalk = CanTalkWhileWorking();
	InteractionBox->SetCollisionEnabled(bCanTalk ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	if (!bCanTalk || FPlatformTime::Seconds() >= BubbleUntil)
	{
		SpeechBubble->SetVisibility(false);
		if (BubbleWidget) BubbleWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (SpeechBubble->IsVisible())
		SpeechBubble->SetWorldLocation(FairyMesh->Bounds.Origin + FVector(0, 0, FairyMesh->Bounds.BoxExtent.Z + SpeechBubbleHeightOffset));
}

void ACSCleaningFairy::Interact_Implementation(AActor* Interactor)
{
	const ACSPlayerCharacter* Player = Cast<ACSPlayerCharacter>(Interactor);
	if (!Player || !Player->IsLocallyControlled() || !CanTalkWhileWorking() || !House.IsValid() || !BubbleWidget || FPlatformTime::Seconds() < BubbleUntil) return;
	FCSFairyDialogueRow Line;
	if (!House->ChooseRandomDialogue(ECSFairyDialogueGroup::Working, Line)) return;
	// 청소 동작은 계속 진행하고 요정 위의 말풍선만 표시합니다.
	BubbleWidget->SetDialogue(Line.Text);
	BubbleWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
	SpeechBubble->SetVisibility(true);
	BubbleUntil = FPlatformTime::Seconds() + FMath::Max(1.f, Line.DisplayDuration);
	UpdateInteraction();
}

FText ACSCleaningFairy::GetInteractionText_Implementation() const
{
	return CanTalkWhileWorking() ? NSLOCTEXT("Fairy", "TalkWhileWorking", "일하는 요정과 대화하기") : FText::GetEmpty();
}

void ACSCleaningFairy::BeginPlay()
{
	Super::BeginPlay();
	BubbleWidget = CreateWidget<UCSFairyDialogueWidget>(GetWorld(), SpeechBubbleWidgetClass ? SpeechBubbleWidgetClass.Get() : UCSFairyDialogueWidget::StaticClass());
	if (BubbleWidget) BubbleWidget->SetVisibility(ESlateVisibility::Collapsed);
	SpeechBubble->SetWidget(BubbleWidget);
	if (const USkeletalMesh* Mesh = FairyMesh->GetSkeletalMeshAsset())
	{
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		InteractionBox->SetRelativeLocation(Bounds.Origin);
		InteractionBox->SetBoxExtent(Bounds.BoxExtent + FVector(10.f));
	}
	MeshRestRotation = FairyMesh->GetRelativeRotation().Quaternion();
	PreviousVisualLocation = GetActorLocation();
	HoverAnimation = Cast<UAnimSequence>(FairyMesh->AnimationData.AnimToPlay);
	bVisualAnimationsReady = HoverAnimation && PickupTrashAnimation && CarryTrashAnimation &&
		PickupTrashAnimation->GetSkeleton() == HoverAnimation->GetSkeleton() &&
		CarryTrashAnimation->GetSkeleton() == HoverAnimation->GetSkeleton() &&
		PickupAttachTime < PickupTrashAnimation->GetPlayLength();
	if (bVisualAnimationsReady)
	{
		// Reuse the existing native single-sequence crossfade; no new AnimBP or rig.
		FairyMesh->SetAnimInstanceClass(UCSCustomerAnimInstance::StaticClass());
		PlayVisualAnimation(HoverAnimation, true);
		// Still one CarryPoint. Follow only the visual mesh, including its flight tilt.
		CarryPoint->AttachToComponent(FairyMesh, FAttachmentTransformRules::KeepWorldTransform);
		CarryPoint->SetAbsolute(false, false, true);
		CarryPoint->SetWorldScale3D(FVector::OneVector);
		CarryPoint->SetRelativeRotation(MeshRestRotation.Inverse());
		CarryPoint->SetRelativeLocation(PickupGripOffset);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[CleaningFairy] %s: compatible pickup/carry animations are missing; preserving the existing cleanup behavior."), *GetName());
	}
	// 실제 플레이가 시작된 뒤에만 대기 상태의 요정을 숨깁니다.
	ShowOrb(false);
	if (auto* Material = Orb->CreateDynamicMaterialInstance(0)) Material->SetScalarParameterValue(TEXT("Glow"), 6.f);
	for (UStaticMeshComponent* Spark : Sparks)
		if (auto* Material = Spark->CreateDynamicMaterialInstance(0)) Material->SetScalarParameterValue(TEXT("Glow"), 4.f);
}

FVector ACSCleaningFairy::GetCarryOffset() const
{
	// CarryPoint may be below FairyMesh; relative location alone is no longer actor space.
	return GetActorQuat().UnrotateVector(CarryPoint->GetComponentLocation() - GetActorLocation());
}

void ACSCleaningFairy::PlayVisualAnimation(UAnimSequence* Animation, bool bLooping, float StartTime)
{
	if (UCSCustomerAnimInstance* Instance = Cast<UCSCustomerAnimInstance>(FairyMesh->GetAnimInstance()))
	{
		if (Animation && Instance->GetAnimationAsset() != Animation)
		{
			Instance->PlaySequence(Animation, bLooping, .18f);
			Instance->SetPosition(FMath::Fmod(StartTime, FMath::Max(.001f, Animation->GetPlayLength())), false);
		}
	}
}

void ACSCleaningFairy::UpdateVisuals(float DeltaSeconds)
{
	const FVector Velocity = (GetActorLocation() - PreviousVisualLocation) / FMath::Max(DeltaSeconds, SMALL_NUMBER);
	PreviousVisualLocation = GetActorLocation();
	const float HorizontalSpeed = Velocity.Size2D();
	const bool bFlightState = State == ECSFairyState::ToTrash || State == ECSFairyState::ToBin ||
		State == ECSFairyState::Returning || State == ECSFairyState::LeavingBin;
	bHorizontalFlight = bFlightState && HorizontalSpeed > (bHorizontalFlight ? 5.f : 10.f) &&
		HorizontalSpeed > FMath::Abs(Velocity.Z) * (bHorizontalFlight ? 1.15f : 1.5f);
	FQuat TargetTilt = FQuat::Identity;
	if (bHorizontalFlight)
	{
		const FVector Direction = GetActorQuat().UnrotateVector(Velocity.GetSafeNormal2D());
		TargetTilt = FQuat(FVector::CrossProduct(FVector::UpVector, Direction).GetSafeNormal(), FMath::DegreesToRadians(MovementTiltAngle));
	}
	VisualTilt = FQuat::Slerp(VisualTilt, TargetTilt, 1.f - FMath::Exp(-8.f * DeltaSeconds)).GetNormalized();
	FairyMesh->SetRelativeRotation(VisualTilt * MeshRestRotation);
	if (!bVisualAnimationsReady) return;

	float LiftAlpha = bCarrying && !bPickupPlaying ? 1.f : 0.f;
	if (bPickupPlaying)
	{
		// These times match AN_Fairy_PickupTrash's authored reach and lift.
		LiftAlpha = FMath::SmoothStep(.8f, 1.5f, PickupElapsed);
	}
	const FVector HoverOffset = FairyMesh->GetSocketTransform(TEXT("Root"), RTS_Component).GetLocation();
	CarryPoint->SetRelativeLocation(FMath::Lerp(PickupGripOffset, CarryGripOffset, LiftAlpha) + HoverOffset);
	if (!bPickupPlaying)
	{
		UAnimSequence* Desired = bCarrying ? CarryTrashAnimation.Get() : HoverAnimation.Get();
		if (auto* Instance = Cast<UCSCustomerAnimInstance>(FairyMesh->GetAnimInstance()))
			PlayVisualAnimation(Desired, true, Instance->GetCurrentTime());
	}
}

bool ACSCleaningFairy::BuildPath(const FVector& Start, const FVector& End, TArray<FVector>& OutPath) const
{
	UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
	FNavLocation From, To;
	if (!Nav || !Nav->ProjectPointToNavigation(Start, From, FVector(80,80,500)) ||
		!Nav->ProjectPointToNavigation(End, To, FVector(80,80,500))) return false;
	UNavigationPath* Result = Nav->FindPathToLocationSynchronously(GetWorld(), From.Location, To.Location);
	if (!Result || !Result->IsValid() || Result->IsPartial() || Result->PathPoints.IsEmpty()) return false;
	OutPath = Result->PathPoints;
	return true;
}

void ACSCleaningFairy::SetPath(TArray<FVector>&& NewPath, ECSFairyState NewState)
{
	Path = MoveTemp(NewPath);
	PathIndex = 0;
	State = NewState;
	float Distance = 0;
	FVector Previous = GetActorLocation();
	for (const FVector& Point : Path) { Distance += FVector::Distance(Previous, Point + FVector(0,0,FlightHeight)); Previous = Point + FVector(0,0,FlightHeight); }
	Deadline = GetWorld()->GetTimeSeconds() + Distance / FMath::Max(10.f, MoveSpeed) +
		Path.Num() * 180.f / FMath::Max(10.f, TurnSpeed) + 8;
}

bool ACSCleaningFairy::FindBinApproach(ACSTrashCanActor* TrashCan, const FVector& Start, TArray<FVector>& OutPath, float& OutDistance) const
{
	OutDistance = MAX_flt;
	if (!IsValid(TrashCan) || !TrashCan->IsAvailableForFairy()) return false;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	Params.AddIgnoredActor(TrashCan);
	if (House.IsValid()) Params.AddIgnoredActor(House.Get());
	if (Trash.IsValid()) Params.AddIgnoredActor(Trash.Get());
	for (UArrowComponent* Point : TrashCan->GetFairyApproachPoints())
	{
		TArray<FVector> CandidatePath;
		if (!IsValid(Point) || !BuildPath(Start, Point->GetComponentLocation(), CandidatePath)) continue;
		const FVector Approach = CandidatePath.Last() + FVector(0,0,FlightHeight);
		const FVector PointLocation = Point->GetComponentLocation();
		const FVector RequestedApproach(PointLocation.X, PointLocation.Y, Approach.Z);
		const FVector Drop = TrashCan->FairyDropPoint->GetComponentLocation();
		const FRotator Facing(0, (Drop - Approach).Rotation().Yaw, 0);
		const FVector DropGoal = Drop - Facing.RotateVector(GetCarryOffset());
		// 바닥 경로가 있어도 입구까지의 비행 구간이 벽에 막힌 면은 선택하지 않습니다.
		FHitResult Hit;
		// NavMesh가 벽 안의 지점을 옆으로 투영하더라도 실제 지정 지점이 막혀 있으면 제외합니다.
		if (GetWorld()->SweepSingleByChannel(Hit, Approach, RequestedApproach, FQuat::Identity,
			ECC_WorldStatic, FCollisionShape::MakeSphere(FMath::Max(1.f, BodyRadius)), Params)) continue;
		if (GetWorld()->SweepSingleByChannel(Hit, Approach, DropGoal, FQuat::Identity,
			ECC_WorldStatic, FCollisionShape::MakeSphere(FMath::Max(1.f, BodyRadius)), Params)) continue;
		float Distance = FVector::Distance(Approach, DropGoal);
		for (int32 Index = 1; Index < CandidatePath.Num(); ++Index)
			Distance += FVector::Distance(CandidatePath[Index - 1], CandidatePath[Index]);
		if (Distance < OutDistance) { OutDistance = Distance; OutPath = MoveTemp(CandidatePath); }
	}
	return OutDistance < MAX_flt;
}

bool ACSCleaningFairy::TurnTowards(float Yaw, float DeltaSeconds)
{
	const float CurrentYaw = FMath::FixedTurn(GetActorRotation().Yaw, Yaw, FMath::Max(10.f, TurnSpeed) * DeltaSeconds);
	SetActorRotation(FRotator(0, CurrentYaw, 0));
	return FMath::Abs(FMath::FindDeltaAngleDegrees(CurrentYaw, Yaw)) <= 2.f;
}

bool ACSCleaningFairy::MoveTowards(const FVector& Goal, float Speed, float DeltaSeconds, bool bAtBin)
{
	const FVector Direction = Goal - GetActorLocation();
	// 접근·투하 후 복귀 구간에서도 목표 방향으로 먼저 회전한 뒤 이동합니다.
	if ((State == ECSFairyState::ToTrash || State == ECSFairyState::ToBin || State == ECSFairyState::Returning || State == ECSFairyState::LeavingBin) &&
		Direction.SizeSquared2D() > 4 && !TurnTowards(Direction.Rotation().Yaw, DeltaSeconds)) return false;
	const FVector Next = FMath::VInterpConstantTo(GetActorLocation(), Goal, DeltaSeconds, FMath::Max(10.f, Speed));
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	if (House.IsValid()) Params.AddIgnoredActor(House.Get());
	if (Trash.IsValid()) Params.AddIgnoredActor(Trash.Get());
	if (bAtBin && Bin.IsValid()) Params.AddIgnoredActor(Bin.Get());
	FHitResult Hit;
	// 바닥 경로가 열려 있어도 비행 높이에 벽이나 가구가 있으면 통과하지 않습니다.
	if (GetWorld()->SweepSingleByChannel(Hit, GetActorLocation(), Next, FQuat::Identity,
		ECC_WorldStatic, FCollisionShape::MakeSphere(FMath::Max(1.f, BodyRadius)), Params)) return false;
	SetActorLocation(Next);
	return FVector::DistSquared(Next, Goal) < 4;
}

bool ACSCleaningFairy::FollowPath(float DeltaSeconds)
{
	if (PathIndex >= Path.Num()) return true;
	if (MoveTowards(Path[PathIndex] + FVector(0,0,FlightHeight), MoveSpeed, DeltaSeconds)) ++PathIndex;
	return PathIndex >= Path.Num();
}

bool ACSCleaningFairy::FindJob()
{
	if (!House.IsValid()) return false;
	const double Now = GetWorld()->GetTimeSeconds();
	for (auto It = RetryAfter.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
	for (auto It = MissedDrops.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
	TArray<ACSTrashActor*> Candidates;
	const ACSStoreInteriorActor* StoreInterior = nullptr;
	// 청결도 판정과 동일하게 첫 번째 매장 영역을 사용합니다. 레벨에는 하나만 배치해 주세요.
	for (TActorIterator<ACSStoreInteriorActor> It(GetWorld()); It; ++It)
	{
		if (!IsValid(*It)) continue;
		StoreInterior = *It;
		break;
	}
	const FTransform AreaTransform = House->CleaningArea->GetComponentTransform();
	const FVector Extent = House->CleaningArea->GetUnscaledBoxExtent();
	auto InArea = [&](const FVector& Position)
	{
		if (StoreInterior) return StoreInterior->EncompassesPoint(Position);
		// 매장 영역이 없는 레벨에서만 요정의 집에 지정한 청소 범위를 사용합니다.
		return FBox(-Extent, Extent).IsInsideOrOn(AreaTransform.InverseTransformPosition(Position));
	};
	int32 TrashInArea = 0;
	int32 AvailableTrash = 0;
	for (TActorIterator<ACSTrashActor> It(GetWorld()); It; ++It)
	{
		if (!IsValid(*It) || !InArea(It->GetCleaningLocation())) continue;
		++TrashInArea;
		if (!It->CanBeCleanedBy(this)) continue;
		++AvailableTrash;
		if (RetryAfter.FindRef(*It) <= Now) Candidates.Add(*It);
	}
	if (Candidates.IsEmpty())
	{
		SetWaitingStatus(TrashInArea == 0
			? NSLOCTEXT("Fairy", "NoTrashInArea", "매장 영역 안에 청소할 쓰레기가 없습니다.")
			: AvailableTrash == 0
				? NSLOCTEXT("Fairy", "TrashUnavailable", "매장의 쓰레기가 운반 중이거나 다른 청소 담당자가 예약했습니다.")
				: NSLOCTEXT("Fairy", "WaitingToRetry", "접근 또는 투하에 실패한 쓰레기의 재시도 시간을 기다리고 있습니다."));
		return false;
	}
	TArray<ACSTrashCanActor*> AvailableBins;
	for (TActorIterator<ACSTrashCanActor> It(GetWorld()); It; ++It)
	{
		if (IsValid(*It) && It->IsAvailableForFairy() && InArea(It->GetActorLocation())) AvailableBins.Add(*It);
	}
	if (AvailableBins.IsEmpty())
	{
		SetWaitingStatus(NSLOCTEXT("Fairy", "NoInstalledBinInArea", "매장 영역 안에 설치된 쓰레기통이 없습니다. 운반 중이거나 미설치 상태의 쓰레기통은 사용할 수 없습니다."));
		return false;
	}
	Candidates.Sort([&](const ACSTrashActor& A, const ACSTrashActor& B) {
		return FVector::DistSquared(GetActorLocation(), A.GetCleaningLocation()) < FVector::DistSquared(GetActorLocation(), B.GetCleaningLocation());
	});
	bool bReachedTrash = false;
	for (ACSTrashActor* Candidate : Candidates)
	{
		TArray<FVector> ToTrash;
		const FVector Start = State == ECSFairyState::Waiting ? House->FlightPoint->GetComponentLocation() : GetActorLocation();
		if (!BuildPath(Start, Candidate->GetCleaningLocation(), ToTrash)) { RetryAfter.Add(Candidate, Now + 15); continue; }
		bReachedTrash = true;
		ACSTrashCanActor* BestBin = nullptr;
		float BestDistance = MAX_flt;
		for (ACSTrashCanActor* TrashCan : AvailableBins)
		{
			TArray<FVector> ToBin;
			float Distance;
			if (!FindBinApproach(TrashCan, Candidate->GetCleaningLocation(), ToBin, Distance)) continue;
			if (Distance < BestDistance) { BestDistance = Distance; BestBin = TrashCan; }
		}
		if (!BestBin) continue;
		if (!Candidate->ReserveForCleaning(this)) continue;
		Trash = Candidate;
		Bin = BestBin;
		Bin->OnItemDisposed.AddUniqueDynamic(this, &ACSCleaningFairy::OnDisposed);
		bDisposed = false;
		SetPath(MoveTemp(ToTrash), ECSFairyState::ToTrash);
		Status = NSLOCTEXT("Fairy", "Cleaning", "매장의 쓰레기를 청소하고 있습니다.");
		return true;
	}
	SetWaitingStatus(bReachedTrash
		? NSLOCTEXT("Fairy", "BinPathUnavailable", "쓰레기통까지의 이동 경로를 찾지 못했습니다. FairyApproachPoint와 NavMesh 범위를 확인해 주세요.")
		: NSLOCTEXT("Fairy", "TrashPathUnavailable", "쓰레기까지의 이동 경로를 찾지 못했습니다. 집의 FlightPoint와 쓰레기 위치 아래의 NavMesh를 확인해 주세요."));
	return false;
}

void ACSCleaningFairy::SetWaitingStatus(const FText& Message)
{
	// 매초 같은 로그를 반복하지 않고 대기 이유가 바뀔 때만 출력합니다.
	if (!Status.EqualTo(Message))
	{
		UE_LOG(LogTemp, Display, TEXT("[CleaningFairy] %s: %s"), *GetName(), *Message.ToString());
	}
	Status = Message;
}

bool ACSCleaningFairy::HasUsableBin() const { return Bin.IsValid() && Bin->IsAvailableForFairy(); }

void ACSCleaningFairy::ShowOrb(bool bVisible)
{
	Orb->SetVisibility(bVisible);
	FairyMesh->SetVisibility(!bVisible && State != ECSFairyState::Waiting);
	Trail.Reset();
	for (UStaticMeshComponent* Spark : Sparks) Spark->SetVisibility(false);
	if (bVisible && OrbEffect->GetAsset()) OrbEffect->Activate(true); else OrbEffect->Deactivate();
}

void ACSCleaningFairy::UpdateOrb(float DeltaSeconds)
{
	SparkTime += DeltaSeconds;
	if (SparkTime >= 0.05f) { SparkTime = 0; Trail.Insert(GetActorLocation(), 0); if (Trail.Num() > Sparks.Num()) Trail.Pop(); }
	for (int32 Index = 0; Index < Sparks.Num(); ++Index)
	{
		Sparks[Index]->SetVisibility(Index < Trail.Num());
		if (Index >= Trail.Num()) continue;
		const float Phase = GetWorld()->GetTimeSeconds() * 12 + Index * 2;
		Sparks[Index]->SetWorldLocation(Trail[Index] + FVector(FMath::Sin(Phase)*3, FMath::Cos(Phase)*3,0));
		Sparks[Index]->SetWorldScale3D(FVector(0.035f * (1.f - float(Index) / Sparks.Num()) * (0.6f + 0.4f * FMath::Abs(FMath::Sin(Phase)))));
	}
}

void ACSCleaningFairy::ReleaseTrash(bool bRestorePickup, bool bReleaseReservation)
{
	bPickupPlaying = false;
	if (Trash.IsValid())
	{
		if (bCarrying && Trash->GetAttachParentActor() == this)
		{
			Trash->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			if (bRestorePickup) Trash->SetActorLocation(PickupLocation);
			Trash->SetActorEnableCollision(true);
			Trash->ShowMeshAfterDrop();
			Trash->EnableWorldDropPhysics();
		}
		if (bReleaseReservation && Trash.IsValid()) Trash->ReleaseCleaningReservation(this);
	}
	bCarrying = false;
}

void ACSCleaningFairy::OnDisposed(AActor* Item, int32 TotalCount)
{
	if (Item == Trash.Get()) bDisposed = true;
}

void ACSCleaningFairy::FinishJob(bool bFailed)
{
	if (bFailed && Trash.IsValid()) RetryAfter.Add(Trash, GetWorld()->GetTimeSeconds() + 15);
	ReleaseTrash(true);
	if (Bin.IsValid()) Bin->OnItemDisposed.RemoveDynamic(this, &ACSCleaningFairy::OnDisposed);
	Trash.Reset(); Bin.Reset();
	if (!FindJob()) BeginReturn();
}

void ACSCleaningFairy::LeaveBin(bool bFailed)
{
	// 투하 위치 바로 아래는 쓰레기통 때문에 NavMesh가 비어 있을 수 있습니다.
	// 이전 경로의 마지막 접근 지점으로 먼저 돌아온 뒤 다음 쓰레기나 집으로의 경로를 찾습니다.
	bDisposalFailed = bFailed;
	State = ECSFairyState::LeavingBin;
	Deadline = GetWorld()->GetTimeSeconds() + 8;
}

void ACSCleaningFairy::BeginReturn()
{
	if (!House.IsValid()) { Destroy(); return; }
	if (bIntroductionActive) return;
	TArray<FVector> HomePath;
	if (BuildPath(GetActorLocation(), House->FlightPoint->GetComponentLocation(), HomePath)) SetPath(MoveTemp(HomePath), ECSFairyState::Returning);
	else { State = ECSFairyState::Returning; Path.Reset(); Deadline = GetWorld()->GetTimeSeconds() + 8; }
}

void ACSCleaningFairy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ON_SCOPE_EXIT { UpdateVisuals(DeltaSeconds); UpdateInteraction(); };
	if (!House.IsValid()) { Destroy(); return; }
	if (bIntroductionActive) return;
	const double Now = GetWorld()->GetTimeSeconds();
	if (State == ECSFairyState::Waiting)
	{
		if (Now < NextSearch) return;
		NextSearch = Now + FMath::Max(.2f, SearchInterval);
		if (FindJob()) { State = ECSFairyState::Emerging; TransitionTime = 0; TransitionStart = House->DoorPoint->GetComponentLocation(); SetActorLocation(TransitionStart); ShowOrb(true); }
		return;
	}
	if (State == ECSFairyState::Emerging || State == ECSFairyState::Entering)
	{
		TransitionTime += DeltaSeconds;
		const float Alpha = FMath::Clamp(TransitionTime / FMath::Max(.1f, TransitionDuration), 0.f, 1.f);
		const FVector End = State == ECSFairyState::Emerging ? House->FlightPoint->GetComponentLocation() : House->DoorPoint->GetComponentLocation();
		const FVector Control = (TransitionStart + End) * .5 + FVector(0,0,60);
		SetActorLocation(FMath::Lerp(FMath::Lerp(TransitionStart, Control, Alpha), FMath::Lerp(Control, End, Alpha), Alpha));
		UpdateOrb(DeltaSeconds);
		Orb->SetRelativeScale3D(FVector(.12f + .12f * FMath::Square(Alpha)));
		if (Alpha >= 1)
		{
			if (State == ECSFairyState::Emerging) { State = ECSFairyState::ToTrash; ShowOrb(false); }
			else { State = ECSFairyState::Waiting; ShowOrb(false); FairyMesh->SetVisibility(false); NextSearch = Now + SearchInterval; }
		}
		return;
	}
	if (State == ECSFairyState::Returning)
	{
		if (FollowPath(DeltaSeconds) && MoveTowards(House->FlightPoint->GetComponentLocation(), VerticalSpeed, DeltaSeconds))
		{
			State = ECSFairyState::Entering; TransitionStart = GetActorLocation(); TransitionTime = 0; ShowOrb(true);
		}
		else if (Now > Deadline) { Deadline = Now + 8; BeginReturn(); }
		return;
	}
	if (State == ECSFairyState::LeavingBin)
	{
		if (!Path.IsEmpty() && MoveTowards(Path.Last() + FVector(0,0,FlightHeight), VerticalSpeed, DeltaSeconds, true))
		{
			FinishJob(bDisposalFailed);
		}
		else if (Now > Deadline)
		{
			UE_LOG(LogTemp, Warning, TEXT("[CleaningFairy] 쓰레기통 투하 위치에서 접근 지점으로 복귀하지 못했습니다. FairyApproachPoint와 주변 장애물을 확인해 주세요."));
			FinishJob(true);
		}
		return;
	}
	if (State == ECSFairyState::AwaitingDisposal)
	{
		if (bDisposed) { LeaveBin(false); return; }
		if (!Trash.IsValid()) { LeaveBin(true); return; }
		if (Now > Deadline)
		{
			// 투하 실패는 기존 쓰레기를 다시 대상으로 삼으며 직접 삭제하거나 보상을 지급하지 않습니다.
			int32& Attempts = MissedDrops.FindOrAdd(Trash);
			++Attempts;
			if (Attempts >= 3) RetryAfter.Add(Trash, Now + 60);
			LeaveBin(false);
		}
		return;
	}
	if (!Trash.IsValid() || (!bCarrying && !Trash->CanBeCleanedBy(this)) || !HasUsableBin() || Now > Deadline)
	{
		FinishJob(true); return;
	}
	switch (State)
	{
	case ECSFairyState::ToTrash:
		if (FollowPath(DeltaSeconds))
		{
			if (FVector::DistSquared2D(GetActorLocation(), Trash->GetCleaningLocation()) > FMath::Square(100.f)) { FinishJob(true); break; }
			State = ECSFairyState::Descending; Deadline = Now + 8;
		}
		break;
	case ECSFairyState::Descending:
	{
		const FVector Goal = Trash->GetCleaningLocation() - GetActorRotation().RotateVector(GetCarryOffset());
		if (!bPickupPlaying)
		{
			if (!MoveTowards(Goal, VerticalSpeed, DeltaSeconds)) break;
			if (bVisualAnimationsReady)
			{
				bPickupPlaying = true;
				PickupElapsed = 0.f;
				Deadline = Now + PickupTrashAnimation->GetPlayLength() + 8;
				PlayVisualAnimation(PickupTrashAnimation, false);
			}
		}
		else PickupElapsed += DeltaSeconds;
		if (!bCarrying && (!bVisualAnimationsReady || PickupElapsed >= PickupAttachTime))
		{
			PickupLocation = Trash->GetCleaningLocation();
			// 물리 메시의 실제 위치를 보존한 뒤 루트에 복원하고 운반 지점에 붙입니다.
			Trash->PrepareForPickupPhysics(); Trash->SetActorLocation(PickupLocation);
			Trash->SetActorEnableCollision(false);
			bCarrying = Trash->AttachToComponent(CarryPoint, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			if (!bCarrying) { Trash->SetActorEnableCollision(true); Trash->EnableWorldDropPhysics(); FinishJob(true); break; }
		}
		if (bCarrying && (!bPickupPlaying || PickupElapsed >= PickupTrashAnimation->GetPlayLength()))
		{
			bPickupPlaying = false;
			if (bVisualAnimationsReady) PlayVisualAnimation(CarryTrashAnimation, true, PickupElapsed);
			State = ECSFairyState::Ascending; Deadline = Now + 8;
		}
		break;
	}
	case ECSFairyState::Ascending:
		if (MoveTowards(Path.Last() + FVector(0,0,FlightHeight), VerticalSpeed, DeltaSeconds))
		{
			TArray<FVector> ToBin;
			float Distance;
			// 운반하는 동안 쓰레기통이 이동했을 수 있으므로 네 면의 접근 가능 여부를 다시 확인합니다.
			if (FindBinApproach(Bin.Get(), GetActorLocation(), ToBin, Distance)) SetPath(MoveTemp(ToBin), ECSFairyState::ToBin);
			else FinishJob(true);
		}
		break;
	case ECSFairyState::ToBin:
		if (FollowPath(DeltaSeconds))
		{
			State = ECSFairyState::Aligning;
			AlignmentYaw = (Bin->FairyDropPoint->GetComponentLocation() - GetActorLocation()).Rotation().Yaw;
			Deadline = Now + 8;
		}
		break;
	case ECSFairyState::Aligning:
	{
		// 회전 중 운반 지점이 바뀌어 목표 좌표가 흔들리지 않도록 투하 방향을 고정합니다.
		if (!TurnTowards(AlignmentYaw, DeltaSeconds)) break;
		// 요정의 몸 대신 쓰레기의 운반 지점이 투하 위치와 일치하도록 정렬합니다.
		const FVector Goal = Bin->FairyDropPoint->GetComponentLocation() - FRotator(0, AlignmentYaw, 0).RotateVector(GetCarryOffset());
		if (MoveTowards(Goal, VerticalSpeed, DeltaSeconds, true))
		{
			State = ECSFairyState::AwaitingDisposal; Deadline = Now + FMath::Max(1.f, DisposalTimeout);
			ReleaseTrash(false, false);
		}
		break;
	}
	default: break;
	}
}

void ACSCleaningFairy::EndPlay(const EEndPlayReason::Type Reason)
{
	ReleaseTrash(true);
	if (Bin.IsValid()) Bin->OnItemDisposed.RemoveDynamic(this, &ACSCleaningFairy::OnDisposed);
	Super::EndPlay(Reason);
}
