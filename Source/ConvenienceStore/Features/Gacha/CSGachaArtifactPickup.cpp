#include "Features/Gacha/CSGachaArtifactPickup.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/CSJournalTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "Systems/Journal/CSJournalSubsystem.h"
#include "Systems/Artifact/CSArtifactSubsystem.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"

ACSGachaArtifactPickup::ACSGachaArtifactPickup()
{
	PrimaryActorTick.bCanEverTick = false;
	InteractionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBox"));
	SetRootComponent(InteractionBox);
	InteractionBox->SetBoxExtent(FVector(30, 30, 30));
	InteractionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	DisplayMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DisplayMesh"));
	DisplayMesh->SetupAttachment(InteractionBox);
	DisplayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FallbackMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	DisplayMesh->SetStaticMesh(FallbackMesh);
	DisplayMesh->SetRelativeScale3D(FVector(.3f));
	DisplayMesh->SetRelativeScale3D(FVector(0.3f));
}

void ACSGachaArtifactPickup::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyArtifactRow();
}

void ACSGachaArtifactPickup::BeginPlay()
{
	Super::BeginPlay();
	if (ApplyArtifactRow()) RegisterArtifactTable();
}

bool ACSGachaArtifactPickup::InitializeArtifact(FDataTableRowHandle InArtifactRow, int32 InQuantity)
{
	ArtifactRow = InArtifactRow;
	Quantity = FMath::Max(1, InQuantity);
	const bool bValidRow = ApplyArtifactRow();
	if (bValidRow && HasActorBegunPlay()) RegisterArtifactTable();
	return bValidRow;
}

bool ACSGachaArtifactPickup::ApplyArtifactRow()
{
	ArtifactId = NAME_None;
	ArtifactDisplayName = FText::GetEmpty();
	DisplayMesh->SetStaticMesh(FallbackMesh);
	Quantity = FMath::Max(1, Quantity);
	if (!ArtifactRow.DataTable || ArtifactRow.RowName.IsNone() || !ArtifactRow.DataTable->GetRowStruct() ||
		!ArtifactRow.DataTable->GetRowStruct()->IsChildOf(FCSJournalArtifactDataRow::StaticStruct())) return false;
	const FCSJournalArtifactDataRow* Definition = ArtifactRow.GetRow<FCSJournalArtifactDataRow>(TEXT("Artifact pickup"));
	if (!Definition) return false;
	ArtifactId = ArtifactRow.RowName;
	ArtifactDisplayName = Definition->DisplayName;
	if (Definition->WorldMesh) DisplayMesh->SetStaticMesh(Definition->WorldMesh);
	else if (Definition->Effect == ECSArtifactEffectType::FairyContract || Definition->Effect == ECSArtifactEffectType::FairyHouse)
	{
		// 전용 계약서 모델을 지정하기 전에는 얇은 종이 형태로 표시합니다.
		DisplayMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		DisplayMesh->SetRelativeScale3D(FVector(.3f, .22f, .015f));
	}
	return true;
}

void ACSGachaArtifactPickup::RegisterArtifactTable()
{
	// 가챠 기계가 없는 테스트 레벨에서도 획득과 도감 등록을 사용할 수 있게 합니다.
	if (UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		// 등록 함수의 포인터 형식에 맞추며 테이블 내용은 수정하지 않습니다.
		UDataTable* Table = const_cast<UDataTable*>(ArtifactRow.DataTable.Get());
		if (UCSArtifactSubsystem* Artifacts = GameInstance->GetSubsystem<UCSArtifactSubsystem>())
		{
			Artifacts->RegisterTable(Table);
		}
		if (UCSJournalSubsystem* Journal = GameInstance->GetSubsystem<UCSJournalSubsystem>())
		{
			Journal->RegisterArtifactTable(Table);
		}
	}
}

void ACSGachaArtifactPickup::Launch(FVector Impulse)
{
	DisplayMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	DisplayMesh->SetCollisionResponseToAllChannels(ECR_Block);
	DisplayMesh->SetSimulatePhysics(true);
	DisplayMesh->AddImpulse(Impulse);
}

FText ACSGachaArtifactPickup::GetInteractionText_Implementation() const
{
	// 획득 전에도 테이블에 등록된 이름을 상호작용 안내에 표시합니다.
	if (!ArtifactDisplayName.IsEmpty())
	{
		return FText::Format(NSLOCTEXT("Gacha", "TakeNamedArtifact", "{0} 획득하기"), ArtifactDisplayName);
	}
	return NSLOCTEXT("Gacha", "TakeArtifact", "아티팩트 획득하기");
}

void ACSGachaArtifactPickup::Interact_Implementation(AActor* Interactor)
{
	ACSPlayerCharacter* Player = Cast<ACSPlayerCharacter>(Interactor);
	if (!IsValid(Player) || !Player->IsLocallyControlled() || ArtifactId.IsNone() || !GetWorld())
	{
		return;
	}

	UGameInstance* GameInstance = GetWorld()->GetGameInstance();
	UCSJournalSubsystem* Journal = GameInstance ? GameInstance->GetSubsystem<UCSJournalSubsystem>() : nullptr;
	UCSArtifactSubsystem* Artifacts = GameInstance ? GameInstance->GetSubsystem<UCSArtifactSubsystem>() : nullptr;
	if (!Journal || !Artifacts)
	{
		return;
	}

	FCSJournalArtifact Artifact;
	if (!Journal->FindArtifact(ArtifactId, Artifact))
	{
		return;
	}

	FCSJournalArtifactDataRow Definition;
	const bool bDuplicateContract = Artifacts->IsFairyContractSigned() && Artifacts->GetDefinition(ArtifactId, Definition) &&
		Definition.Effect == ECSArtifactEffectType::FairyContract;
	// 도감 등록 또는 중복 계약서 교환이 성공한 뒤에만 월드의 보상을 없애 주세요.
	if (Artifacts->Acquire(ArtifactId, Quantity))
	{
		if (ACSStorePlayerController* Controller = Cast<ACSStorePlayerController>(Player->GetController()); Controller && !bDuplicateContract)
		{
			Controller->ShowGameplayToast(
				NSLOCTEXT("Gacha", "ArtifactAcquired", "아티팩트 획득"),
				FText::Format(
					NSLOCTEXT("Gacha", "ArtifactJournalHint", "{0} × {1} 획득\n수첩의 아티팩트 도감에서 사용할 수 있습니다."),
					Artifact.Name, FText::AsNumber(Quantity)));
		}
		Destroy();
	}
}
