#include "Features/Trash/CSTrashActor.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Data/CSSaveTypes.h"
#include "Data/CSCraftFailureVisualRegistry.h"
#include "Engine/Texture2D.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "Systems/Crafting/CSCraftingSubsystem.h"

ACSTrashActor::ACSTrashActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	TrashMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TrashMesh"));
	TrashMesh->SetupAttachment(SceneRoot);
	// E키 조준(라인트레이스)이 이 메시를 인식하도록 Visibility 채널을 막아둠
	TrashMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// [백무송]
	// 별도 드롭 처리를 거치지 않는 배치·투척 쓰레기도 포함해 내비게이션 영향을 차단
	// 생성자에서 설정하고 이후 콜리전 응답 재설정에도 유지
	TrashMesh->SetCanEverAffectNavigation(false);

	TrashDisplayName = NSLOCTEXT("Trash", "DefaultDisplayName", "쓰레기");
}

void ACSTrashActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyTrashRow();
}

void ACSTrashActor::BeginPlay()
{
	// 레벨에 저장된 액터는 런타임에 Construction을 거치지 않을 수 있습니다.
	ApplyTrashRow();
	Super::BeginPlay();
}

bool ACSTrashActor::InitializeTrash(FDataTableRowHandle InTrashRow)
{
	TrashRow = InTrashRow;
	return ApplyTrashRow();
}

bool ACSTrashActor::ApplyTrashRow()
{
	const FCSCraftFailureRow* Row = nullptr;
	if (IsValid(TrashRow.DataTable) && !TrashRow.RowName.IsNone() &&
		TrashRow.DataTable->GetRowStruct() == FCSCraftFailureRow::StaticStruct())
	{
		Row = TrashRow.DataTable->FindRow<FCSCraftFailureRow>(TrashRow.RowName, TEXT("ApplyTrashRow"), false);
	}

	if (Row == nullptr && !bHasAppliedTrashRow)
	{
		// 기존 일반 쓰레기 및 제조대가 직접 설정한 이름/메시는 건드리지 않습니다.
		return false;
	}

	// 행 변경/해제 시 이전 종류의 메시, 아이콘, 크기가 남지 않도록 BP 기본값부터 적용합니다.
	const ACSTrashActor* Defaults = GetClass()->GetDefaultObject<ACSTrashActor>();
	TrashDisplayName = Defaults->TrashDisplayName;
	TrashIcon = nullptr;
	FailureMeshRelativeTransform = FTransform::Identity;
	bHasFailureMeshTransform = false;
	if (IsValid(TrashMesh) && IsValid(Defaults->TrashMesh))
	{
		TrashMesh->SetStaticMesh(Defaults->TrashMesh->GetStaticMesh());
		TrashMesh->SetRelativeTransform(Defaults->TrashMesh->GetRelativeTransform());
	}
	bHasAppliedTrashRow = Row != nullptr;
	if (Row == nullptr) return false;

	TrashDisplayName = Row->DisplayName.IsEmpty() ? FText::FromName(TrashRow.RowName) : Row->DisplayName;
	SetTrashMesh(Row->Mesh.LoadSynchronous());
	// 제조대와 같은 Registry를 사용하되 Editor 월드에서도 조회할 수 있도록 CDO에서 가져옵니다.
	const UCSCraftFailureVisualRegistry* Registry = GetDefault<UCSCraftingSubsystem>()->GetFailureVisualRegistry();
	const int64 OutcomeValue = StaticEnum<ECSCraftOutcome>()->GetValueByName(TrashRow.RowName);
	if (Registry != nullptr && OutcomeValue != INDEX_NONE)
	{
		if (const FCSCraftFailureVisualEntry* Visual = Registry->FindVisualData(static_cast<ECSCraftOutcome>(OutcomeValue)))
		{
			ApplyCraftFailureVisual(*Visual);
		}
	}
	return true;
}

void ACSTrashActor::SetTrashMesh(UStaticMesh* InMesh)
{
	if (IsValid(TrashMesh) && InMesh != nullptr)
	{
		TrashMesh->SetStaticMesh(InMesh);
	}
}

void ACSTrashActor::ApplyCraftFailureVisual(const FCSCraftFailureVisualEntry& Visual)
{
	if (IsValid(TrashMesh))
	{
		if (UStaticMesh* Mesh = Visual.StaticMesh.LoadSynchronous())
		{
			TrashMesh->SetStaticMesh(Mesh);
			FailureMeshRelativeTransform = Visual.MeshRelativeTransform;
			bHasFailureMeshTransform = true;
			TrashMesh->SetRelativeTransform(FailureMeshRelativeTransform);
		}
	}
	TrashIcon = Visual.Icon.LoadSynchronous();
}

FVector ACSTrashActor::GetCleaningLocation() const
{
	return IsValid(TrashMesh) ? TrashMesh->GetComponentLocation() : GetActorLocation();
}

bool ACSTrashActor::CanBeCleanedBy(AActor* Cleaner) const
{
	return !IsActorBeingDestroyed() && GetActorEnableCollision() && !GetAttachParentActor() &&
		(!CleaningReservation.IsValid() || CleaningReservation.Get() == Cleaner);
}

bool ACSTrashActor::ReserveForCleaning(AActor* Cleaner)
{
	if (!IsValid(Cleaner) || !CanBeCleanedBy(Cleaner)) return false;
	CleaningReservation = Cleaner;
	return true;
}

void ACSTrashActor::ReleaseCleaningReservation(AActor* Cleaner)
{
	if (CleaningReservation.Get() == Cleaner) CleaningReservation.Reset();
}

bool ACSTrashActor::IsCarriedByCleaner() const
{
	return CleaningReservation.IsValid() && GetAttachParentActor() == CleaningReservation.Get();
}

void ACSTrashActor::Interact_Implementation(AActor* Interactor)
{
	if (ACSPlayerCharacter* PlayerCharacter = Cast<ACSPlayerCharacter>(Interactor))
	{
		PlayerCharacter->TryPickUp(this);
	}
}

FText ACSTrashActor::GetInteractionText_Implementation() const
{
	return FText::Format(NSLOCTEXT("Interaction", "PickUpTrashNamed", "{0} 줍기"), TrashDisplayName);
}

void ACSTrashActor::PrepareForPickupPhysics()
{
	if (!IsValid(TrashMesh))
	{
		return;
	}

	if (TrashMesh->IsSimulatingPhysics())
	{
		TrashMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		TrashMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
	TrashMesh->SetSimulatePhysics(false);

	if (!IsValid(SceneRoot))
	{
		return;
	}

	TrashMesh->AttachToComponent(
		SceneRoot,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	if (bHasFailureMeshTransform)
	{
		TrashMesh->SetRelativeTransform(FailureMeshRelativeTransform);
	}
}

void ACSTrashActor::EnableWorldDropPhysics()
{
	if (!IsValid(TrashMesh) || TrashMesh->GetStaticMesh() == nullptr)
	{
		return;
	}

	TrashMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TrashMesh->SetSimulatePhysics(true);
}

void ACSTrashActor::HideMeshForPickup()
{
	if (!IsValid(TrashMesh))
	{
		return;
	}

	TrashMesh->SetVisibility(false, true);
}

void ACSTrashActor::ShowMeshAfterDrop()
{
	if (!IsValid(TrashMesh))
	{
		return;
	}

	TrashMesh->SetVisibility(true, true);
}

void ACSTrashActor::PrepareForCustomerDrop()
{
	if (!IsValid(TrashMesh))
	{
		return;
	}

	// 통로에 떨어진 쓰레기가 손님/플레이어 이동을 막으면 안 되므로 Pawn 채널만 무시한다
	// (E키 상호작용이 쓰는 Visibility 채널은 그대로 Block 유지 - 줍기 기능 유지)
	TrashMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

	// 정적 블로킹 메시가 런타임에 스폰될 때마다 내비메시를 다시 구우면 그때마다 프레임이 끊긴다
	TrashMesh->SetCanEverAffectNavigation(false);

	// 캡슐 중심(스폰 좌표)에서 바닥까지 물리로 자연스럽게 떨어뜨린다
	EnableWorldDropPhysics();
}

void ACSTrashActor::CaptureSaveRecord(FCSTrashRecord& OutRecord) const
{
	OutRecord = FCSTrashRecord();
	OutRecord.ActorClass = FSoftClassPath(GetClass());
	OutRecord.Transform = GetActorTransform();

	// [백무송]
	// 메시가 없어도 이름은 유지되도록 메시 확인 전에 현재 표시 이름을 기록
	OutRecord.DisplayName = TrashDisplayName;

	if (!IsValid(TrashMesh))
	{
		return;
	}

	OutRecord.StaticMesh = FSoftObjectPath(TrashMesh->GetStaticMesh());
	OutRecord.bIgnorePawnCollision = TrashMesh->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore;

	if (TrashMesh->IsSimulatingPhysics())
	{
		// 물리로 분리된 메시가 실제 위치이므로 메시 기준으로 기록
		OutRecord.Transform = FTransform(
			TrashMesh->GetComponentRotation(),
			TrashMesh->GetComponentLocation(),
			GetActorScale3D());
		OutRecord.MeshRelativeTransform = FTransform(
			FQuat::Identity,
			FVector::ZeroVector,
			TrashMesh->GetRelativeScale3D());
		OutRecord.bWorldDropPhysics = true;
		return;
	}

	OutRecord.MeshRelativeTransform = TrashMesh->GetRelativeTransform();
}

void ACSTrashActor::RestoreSaveRecord(const FCSTrashRecord& InRecord)
{
	// [백무송]
	// 저장된 이름이 있으면 표시 이름을 복원
	// 이름 칸이 없던 이전 세이브는 빈 값이므로 클래스 기본 이름을 유지
	if (!InRecord.DisplayName.IsEmpty())
	{
		TrashDisplayName = InRecord.DisplayName;
	}

	if (!IsValid(TrashMesh))
	{
		return;
	}

	if (InRecord.StaticMesh.IsValid())
	{
		UStaticMesh* SavedMesh = Cast<UStaticMesh>(InRecord.StaticMesh.TryLoad());
		if (SavedMesh != nullptr && SavedMesh != TrashMesh->GetStaticMesh())
		{
			TrashMesh->SetStaticMesh(SavedMesh);
		}
	}

	TrashMesh->SetRelativeTransform(InRecord.MeshRelativeTransform);

	if (InRecord.bIgnorePawnCollision)
	{
		TrashMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}

	if (InRecord.bWorldDropPhysics)
	{
		EnableWorldDropPhysics();
	}
}
