#include "Features/Shelves/CSShelfActor.h"

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/CSProductTypes.h"
#include "Data/CSSaveTypes.h"
#include "Features/Delivery/CSDeliveryCrateActor.h"
#include "Features/Facilities/CSSaveIdComponent.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Systems/SaveLoad/CSSaveRecordUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "Systems/Employees/CSStoreEmployeeSubsystem.h"
#include "UI/Shelf/CSShelfConfigurationWidget.h"

ACSShelfActor::ACSShelfActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// 가구 이동 시 바닥에 맞출 기준점과 실제 메시를 분리합니다.
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// 세이브 복원 시 진열대를 다시 찾기 위한 식별자 컴포넌트
	SaveIdComponent = CreateDefaultSubobject<UCSSaveIdComponent>(TEXT("SaveIdComponent"));

	// 메시를 자식으로 두어 블루프린트마다 피벗 높이를 보정할 수 있게 합니다.
	ShelfMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShelfMesh"));
	ShelfMesh->SetupAttachment(SceneRoot);
	ShelfMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// 진열대 위에 NavMesh가 깔리는 것을 막기 위한 박스입니다.
	NavObstacleBox = CreateDefaultSubobject<UBoxComponent>(TEXT("NavObstacleBox"));
	NavObstacleBox->SetupAttachment(SceneRoot);
	// 충돌처리는 관여X
	NavObstacleBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NavObstacleBox->SetGenerateOverlapEvents(false);
	// Dynamic Obstacle은 충돌 여부와 무관하게 박스 모양 그대로 NavMesh에 반영됩니다.
	NavObstacleBox->SetCanEverAffectNavigation(true);
	NavObstacleBox->bDynamicObstacle = true;
	// 편집 중에만 보이는 표시물입니다.
	NavObstacleBox->SetHiddenInGame(true);

	// 손님이 설 자리를 가리키는 마커
	CustomerShoppingPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("CustomerShoppingPoint"));
	CustomerShoppingPoint->SetupAttachment(SceneRoot);
	// 진열대 정면 120cm. NavObstacleBox 바깥이면서 NavMesh가 깔린 자리여야 합니다.
	CustomerShoppingPoint->SetRelativeLocation(FVector(120.0f, 0.0f, 0.0f));
	// Yaw 180 = 손님이 진열대를 돌아보는 방향입니다.
	CustomerShoppingPoint->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
	CustomerShoppingPoint->SetCanEverAffectNavigation(false);
	CustomerShoppingPoint->SetHiddenInGame(true);
	// 에디터에서 슬롯 마커와 구분되도록 색과 크기를 다르게 설정
	CustomerShoppingPoint->ArrowColor = FColor(60, 200, 255);
	CustomerShoppingPoint->ArrowSize = 1.5f;

	// 손님이 설 자리를 건축 판정에 알리기 위한 박스
	BrowseZoneBox = CreateDefaultSubobject<UBoxComponent>(TEXT("BrowseZoneBox"));
	BrowseZoneBox->SetupAttachment(SceneRoot);
	BrowseZoneBox->ComponentTags.Add(TEXT("BuildPlacementAccessZone"));
	// 충돌에는 일절 관여하지 않음
	BrowseZoneBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BrowseZoneBox->SetGenerateOverlapEvents(false);
	BrowseZoneBox->SetCanEverAffectNavigation(false);
	// 편집 중에만 보이는 표시
	BrowseZoneBox->SetHiddenInGame(true);
	// 에디터에서 CustomerShoppingPoint와 같은 색으로 설정(구분용)
	BrowseZoneBox->ShapeColor = FColor(60, 200, 255);
}

void ACSShelfActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 시설 행을 먼저 읽어 두면 에디터 프리뷰와 실제 배치가 같은 보관 환경을 사용합니다.
	SyncFacilityData();
	// 블루프린트에서 배치한 슬롯 마커의 위치를 슬롯 데이터에 반영합니다.
	SyncSlotsFromMarkers();
	// 기존 진열대에도 상단·중단·하단 설정 데이터를 자동으로 마련합니다.
	EnsureLevelConfigurations();
	// 손님 도착 반경이 바뀌면 건축 판정용 영역 박스도 같이 따라갑니다.
	SyncBrowseZoneBox();
}

void ACSShelfActor::BeginPlay()
{
	Super::BeginPlay();
	EnsureLevelConfigurations();

	// 에디터가 아닌 실제 게임 시작 시에만 상품을 생성하여 중복 배치되지 않도록 합니다.
	SpawnInitialProducts();
}

void ACSShelfActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UCSCustomerRuntimeSubsystem* CustomerRuntimeSubsystem =
			World->GetSubsystem<UCSCustomerRuntimeSubsystem>())
		{
			CustomerRuntimeSubsystem->UnregisterShelf(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

FName ACSShelfActor::GetFacilityId_Implementation() const
{
	return FacilityRow.RowName;
}

bool ACSShelfActor::IsFacilityInstalled_Implementation() const
{
	return bIsInstalled;
}

void ACSShelfActor::InitializeAsUninstalledFacility_Implementation()
{
	bIsInstalled = false;
	if (UWorld* World = GetWorld())
	{
		if (UCSCustomerRuntimeSubsystem* CustomerRuntimeSubsystem =
			World->GetSubsystem<UCSCustomerRuntimeSubsystem>())
		{
			CustomerRuntimeSubsystem->UnregisterShelf(this);
		}
	}
	SetFacilityPhysicsEnabled(true);
}

void ACSShelfActor::PrepareForBuildPickup_Implementation()
{
	bIsInstalled = false;
	if (UWorld* World = GetWorld())
	{
		if (UCSCustomerRuntimeSubsystem* CustomerRuntimeSubsystem =
			World->GetSubsystem<UCSCustomerRuntimeSubsystem>())
		{
			CustomerRuntimeSubsystem->UnregisterShelf(this);
		}
	}
	SetFacilityPhysicsEnabled(false);
}

void ACSShelfActor::FinalizeBuildPlacement_Implementation()
{
	SetFacilityPhysicsEnabled(false);
	bIsInstalled = true;
	if (UWorld* World = GetWorld())
	{
		if (UCSCustomerRuntimeSubsystem* CustomerRuntimeSubsystem =
			World->GetSubsystem<UCSCustomerRuntimeSubsystem>())
		{
			CustomerRuntimeSubsystem->RegisterShelf(this);
		}
	}
}

void ACSShelfActor::SpawnInitialProducts()
{
	if (!bIsInstalled || InitialProducts.IsEmpty())
	{
		return;
	}

	if (InitialProductActorClass == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("%s: 시작 진열 상품을 생성할 액터 클래스가 지정되지 않았습니다."),
			*GetName());
		return;
	}

	for (const FCSInitialShelfProduct& InitialProduct : InitialProducts)
	{
		if (InitialProduct.ProductRow.DataTable == nullptr ||
			InitialProduct.ProductRow.RowName.IsNone())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("%s: 시작 진열 상품의 DataTable 행이 비어 있습니다."),
				*GetName());
			continue;
		}

		const int32 RequestedQuantity = FMath::Max(1, InitialProduct.Quantity);
		int32 PlacedQuantity = 0;
		const int32 FirstSlotIndex = FMath::Max(0, InitialProduct.StartSlotIndex);
		for (int32 SlotIndex = FirstSlotIndex;
			SlotIndex < Slots.Num() && PlacedQuantity < RequestedQuantity;
			++SlotIndex)
		{
			// 이미 사용 중인 슬롯은 건너뛰고 다음 빈 슬롯을 계속 찾습니다.
			if (IsValid(Slots[SlotIndex].OccupyingItem))
			{
				continue;
			}

			if (SpawnInitialProductInSlot(InitialProduct.ProductRow, SlotIndex))
			{
				++PlacedQuantity;
			}
		}

		if (PlacedQuantity < RequestedQuantity)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("%s: %s 상품의 시작 진열 수량이 부족한 슬롯으로 인해 일부만 배치되었습니다. 요청=%d, 배치=%d"),
				*GetName(),
				*InitialProduct.ProductRow.RowName.ToString(),
				RequestedQuantity,
				PlacedQuantity);
		}
	}
}

bool ACSShelfActor::SpawnInitialProductInSlot(
	const FDataTableRowHandle& ProductRow,
	int32 SlotIndex)
{
	if (GetWorld() == nullptr ||
		InitialProductActorClass == nullptr ||
		!Slots.IsValidIndex(SlotIndex) ||
		IsValid(Slots[SlotIndex].OccupyingItem))
	{
		return false;
	}

	const FTransform SlotWorldTransform =
		Slots[SlotIndex].LocalTransform * GetActorTransform();
	ACSProductPickupActor* ProductActor =
		GetWorld()->SpawnActorDeferred<ACSProductPickupActor>(
			InitialProductActorClass,
			SlotWorldTransform,
			this,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(ProductActor) || !ProductActor->InitializeProduct(ProductRow))
	{
		if (IsValid(ProductActor))
		{
			ProductActor->Destroy();
		}

		return false;
	}

	ProductActor->FinishSpawning(SlotWorldTransform);
	if (!TryPlaceItem_Implementation(ProductActor, SlotWorldTransform.GetLocation()))
	{
		ProductActor->Destroy();
		return false;
	}

	return true;
}

// ---------------------------------------------------------------------------
// 세이브 / 로드
// ---------------------------------------------------------------------------

void ACSShelfActor::CaptureSaveRecord(FCSShelfRecord& OutRecord) const
{
	OutRecord = FCSShelfRecord();
	OutRecord.SaveId = UCSSaveIdComponent::FindSaveId(this);
	OutRecord.ActorClass = FSoftClassPath(GetClass());
	OutRecord.Transform = GetActorTransform();
	OutRecord.bIsInstalled = bIsInstalled;
	OutRecord.bWasSpawnedAtRuntime = IsValid(SaveIdComponent) && SaveIdComponent->WasSpawnedAtRuntime();

	OutRecord.LevelConfigurations.Reserve(LevelConfigurations.Num());
	for (const FCSShelfLevelConfiguration& Configuration : LevelConfigurations)
	{
		FCSShelfLevelConfigRecord ConfigRecord;
		ConfigRecord.SlotLevel = Configuration.SlotLevel;
		ConfigRecord.ProductRow = FCSSaveRowRef::FromHandle(Configuration.ProductRow);
		ConfigRecord.bIsPlayerConfigured = Configuration.bIsPlayerConfigured;
		OutRecord.LevelConfigurations.Add(ConfigRecord);
	}

	// 상품이 올라가 있는 슬롯만 기록
	for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
	{
		const ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(Slots[SlotIndex].OccupyingItem);
		if (!IsValid(Product))
		{
			continue;
		}

		// 잠깐 쓰고 지워질 상품은 불러온 게임에서 지워 줄 곳이 없어 그대로 남는다
		if (Product->ActorHasTag(CSProductTags::TemporaryProduct))
		{
			continue;
		}

		FCSShelfSlotRecord SlotRecord;
		SlotRecord.SlotIndex = SlotIndex;
		SlotRecord.ActorClass = FSoftClassPath(Product->GetClass());
		SlotRecord.ProductRow = FCSSaveRowRef::FromHandle(Product->GetProductRow());

		// 신선도 감소 기준 시각이 월드 시간에 묶여 있어 경과분을 반영한 값으로 기록
		SlotRecord.ProductState = CSSaveRecordUtils::ToRecord(
			Product->GetProductRuntimeState(),
			Product->GetCurrentFreshness());
		OutRecord.OccupiedSlots.Add(SlotRecord);
	}
}

void ACSShelfActor::RestoreSaveRecord(
	const FCSShelfRecord& InRecord,
	const UDataTable* FallbackProductTable)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	if (IsValid(SaveIdComponent))
	{
		SaveIdComponent->OverrideSaveId(InRecord.SaveId, InRecord.bWasSpawnedAtRuntime);
	}

	// 단 설정을 먼저 복원해 이후 슬롯 배치와 직원 정리 판정이 같은 품목 규칙을 사용하도록 설정
	LevelConfigurations.Reset(InRecord.LevelConfigurations.Num());
	for (const FCSShelfLevelConfigRecord& ConfigRecord : InRecord.LevelConfigurations)
	{
		FCSShelfLevelConfiguration Configuration;
		Configuration.SlotLevel = ConfigRecord.SlotLevel;
		Configuration.ProductRow = ConfigRecord.ProductRow.ToHandle(FallbackProductTable);
		Configuration.bIsPlayerConfigured = ConfigRecord.bIsPlayerConfigured;
		LevelConfigurations.Add(Configuration);
	}

	EnsureLevelConfigurations();

	// 설치 여부만 대입하면 손님 런타임 등록과 물리 설정이 어긋나므로 기존 건축 경로로 처리
	if (InRecord.bIsInstalled)
	{
		FinalizeBuildPlacement_Implementation();
	}
	else
	{
		InitializeAsUninstalledFacility_Implementation();
	}

	// 초기 상품과 저장 상품이 겹치지 않도록 슬롯을 먼저 비움
	ClearAllSlotsForRestore();

	const float CurrentWorldTimeSeconds = World->GetTimeSeconds();
	for (const FCSShelfSlotRecord& SlotRecord : InRecord.OccupiedSlots)
	{
		const FDataTableRowHandle ProductRow = SlotRecord.ProductRow.ToHandle(FallbackProductTable);
		if (ProductRow.DataTable == nullptr || ProductRow.RowName.IsNone())
		{
			continue;
		}

		// 저장된 상품 클래스를 불러오지 못하면 시작 진열 상품 클래스를 사용
		TSubclassOf<ACSProductPickupActor> ProductClass = SlotRecord.ActorClass.TryLoadClass<ACSProductPickupActor>();
		if (ProductClass == nullptr)
		{
			ProductClass = InitialProductActorClass;
		}

		const FCSProductRuntimeState RuntimeState = CSSaveRecordUtils::ToRuntime(
			SlotRecord.ProductState,
			CurrentWorldTimeSeconds);
		if (!RestoreProductInSlot(ProductRow, SlotRecord.SlotIndex, RuntimeState, ProductClass))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[CSSave] %s의 %d번 슬롯에 %s 상품을 복원하지 못했습니다."),
				*GetName(),
				SlotRecord.SlotIndex,
				*ProductRow.RowName.ToString());
		}
	}
}

void ACSShelfActor::ClearAllSlotsForRestore()
{
	for (FCSShelfSlot& Slot : Slots)
	{
		AActor* OccupyingItem = Slot.OccupyingItem;
		if (!IsValid(OccupyingItem))
		{
			Slot.OccupyingItem = nullptr;
			continue;
		}

		// 기존 점유 해제 경로를 거친 뒤 액터를 제거
		RemovePlacedItem_Implementation(OccupyingItem);
		Slot.OccupyingItem = nullptr;
		OccupyingItem->Destroy();
	}
}

bool ACSShelfActor::RestoreProductInSlot(
	const FDataTableRowHandle& InProductRow,
	int32 SlotIndex,
	const FCSProductRuntimeState& InRuntimeState,
	TSubclassOf<ACSProductPickupActor> InProductClass)
{
	if (GetWorld() == nullptr ||
		InProductClass == nullptr ||
		!Slots.IsValidIndex(SlotIndex) ||
		IsValid(Slots[SlotIndex].OccupyingItem))
	{
		return false;
	}

	const FTransform SlotWorldTransform = Slots[SlotIndex].LocalTransform * GetActorTransform();
	ACSProductPickupActor* ProductActor = GetWorld()->SpawnActorDeferred<ACSProductPickupActor>(
		InProductClass,
		SlotWorldTransform,
		this,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(ProductActor) || !ProductActor->InitializeProduct(InProductRow))
	{
		if (IsValid(ProductActor))
		{
			ProductActor->Destroy();
		}

		return false;
	}

	ProductActor->FinishSpawning(SlotWorldTransform);

	// 가까운 빈 슬롯 탐색 없이 저장된 슬롯 번호에 그대로 배치
	if (!PlaceItemInSlot(ProductActor, SlotIndex))
	{
		ProductActor->Destroy();
		return false;
	}

	// 슬롯 배치가 보관 환경을 갱신한 뒤 저장 시점의 신선도·유통기한을 적용
	ProductActor->ApplyProductRuntimeState(InRuntimeState);
	return true;
}

void ACSShelfActor::SyncFacilityData()
{
	if (FacilityRow.DataTable == nullptr || FacilityRow.RowName.IsNone())
	{
		return;
	}

	const FCSFacilityDataRow* FacilityData =
		FacilityRow.GetRow<FCSFacilityDataRow>(TEXT("ACSShelfActor::SyncFacilityData"));
	if (FacilityData != nullptr && FacilityData->FacilityType == ECSFacilityType::Shelf)
	{
		ShelfType = FacilityData->ShelfType;
	}
}

void ACSShelfActor::SyncBrowseZoneBox()
{
	if (!IsValid(BrowseZoneBox) || !IsValid(CustomerShoppingPoint))
	{
		return;
	}

	// 원형 도착 판정(BrowseZoneRadius)에 외접하는 박스입니다.
	// 손님 도착 판정은 원 그대로 두고, 건축 판정만 이 박스를 사용합니다.
	const FVector MarkerLocation = CustomerShoppingPoint->GetRelativeLocation();
	BrowseZoneBox->SetRelativeLocation(
		MarkerLocation + FVector(0.0f, 0.0f, BrowseZoneHeight * 0.5f));
	// 박스가 기울지 않도록 Yaw만 남깁니다.
	BrowseZoneBox->SetRelativeRotation(
		FRotator(0.0f, CustomerShoppingPoint->GetRelativeRotation().Yaw, 0.0f));
	BrowseZoneBox->SetBoxExtent(
		FVector(BrowseZoneRadius, BrowseZoneRadius, BrowseZoneHeight * 0.5f));
}

bool ACSShelfActor::TryGetCustomerShoppingPoint(FVector& OutLocation) const
{
	OutLocation = FVector::ZeroVector;

	UWorld* World = GetWorld();
	if (World == nullptr || !IsValid(CustomerShoppingPoint))
	{
		return false;
	}

	const UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	if (NavSys == nullptr)
	{
		return false;
	}

	const FVector MarkerLocation = CustomerShoppingPoint->GetComponentLocation();

	// 마커의 XY는 고정하고 Z만 NavMesh 높이로 맞춥니다.
	const FVector QueryExtent(0.0f, 0.0f, 300.0f);
	FNavLocation Projected;
	if (!NavSys->ProjectPointToNavigation(MarkerLocation, Projected, QueryExtent))
	{
		// 마커 자리에 걸을 수 있는 바닥이 없음 → 손님과 직원 모두 접근할 수 없는 진열대입니다.
		return false;
	}

	OutLocation = FVector(MarkerLocation.X, MarkerLocation.Y, Projected.Location.Z);
	return true;
}

bool ACSShelfActor::IsCustomerShoppingPointOnNavMeshAt(const FTransform& ShelfTransform) const
{
	// 월드나 마커가 없으면 판정하지 않고 배치를 허용
	UWorld* World = GetWorld();
	if (World == nullptr || !IsValid(CustomerShoppingPoint))
	{
		return true;
	}

	// 내비게이션 시스템이 없는 맵에서는 판정하지 않고 배치를 허용
	const UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	if (NavSys == nullptr)
	{
		return true;
	}

	// 마커를 진열대 기준 상대 트랜스폼으로 변환한 뒤 배치 예정 트랜스폼에 적용
	// 들고 있는 동안의 손 위치가 아닌 배치 예정 위치를 검사하기 위해 사용
	const FTransform RelativeMarkerTransform =
		CustomerShoppingPoint->GetComponentTransform().GetRelativeTransform(GetActorTransform());
	const FVector MarkerLocation = (RelativeMarkerTransform * ShelfTransform).GetLocation();

	// 마커의 XY는 고정하고 위아래 300cm 범위에서 NavMesh 투영을 확인
	// 런타임 결과와 어긋나지 않도록 TryGetCustomerShoppingPoint와 같은 범위를 사용
	const FVector QueryExtent(0.0f, 0.0f, 300.0f);
	FNavLocation Projected;
	return NavSys->ProjectPointToNavigation(MarkerLocation, Projected, QueryExtent);
}

bool ACSShelfActor::TryGetBrowseZone(FVector& OutCenter, FRotator& OutFacing, float& OutRadius) const
{
	OutCenter = FVector::ZeroVector;
	OutFacing = FRotator::ZeroRotator;
	OutRadius = 0.0f;

	if (!TryGetCustomerShoppingPoint(OutCenter))
	{
		return false;
	}

	check(IsValid(CustomerShoppingPoint));
	// 캡슐이 기울지 않도록 Yaw만 남김
	OutFacing = FRotator(0.0f, CustomerShoppingPoint->GetComponentRotation().Yaw, 0.0f);
	OutRadius = BrowseZoneRadius;
	return true;
}

bool ACSShelfActor::IsPointInsideBrowseZone(const FVector& WorldPoint) const
{
	FVector Center;
	FRotator Facing;
	float Radius = 0.0f;
	if (!TryGetBrowseZone(Center, Facing, Radius))
	{
		return false;
	}

	// 종족마다 캡슐 높이가 달라 Z를 보면 판정이 종족별로 달라지므로 XY 거리만 비교
	return FVector::Dist2D(WorldPoint, Center) <= Radius;
}

bool ACSShelfActor::TryGetSlotMarkerIndex(
	const UArrowComponent* SlotMarker,
	int32& OutSlotIndex) const
{
	if (!IsValid(SlotMarker))
	{
		return false;
	}

	const FString MarkerName = SlotMarker->GetName();
	static const FString SlotPrefix = TEXT("Slot_");
	// Slot_00처럼 정해진 접두사로 시작하는 Arrow Component만 슬롯 마커로 사용합니다.
	if (!MarkerName.StartsWith(SlotPrefix))
	{
		return false;
	}

	const FString IndexSuffix = MarkerName.RightChop(SlotPrefix.Len());
	FString IndexText;
	// 블루프린트가 내부 접미사를 덧붙여도 앞의 숫자만 슬롯 번호로 사용합니다.
	for (const TCHAR Character : IndexSuffix)
	{
		if (!FChar::IsDigit(Character))
		{
			break;
		}

		IndexText.AppendChar(Character);
	}

	if (IndexText.IsEmpty())
	{
		return false;
	}

	OutSlotIndex = FCString::Atoi(*IndexText);
	return OutSlotIndex >= 0;
}

void ACSShelfActor::SyncSlotsFromMarkers()
{
	TArray<UArrowComponent*> ArrowComponents;
	GetComponents<UArrowComponent>(ArrowComponents);

	TArray<TPair<int32, UArrowComponent*>> SlotMarkers;
	// 진열대에 포함된 Arrow Component 중 슬롯 이름 규칙을 만족하는 마커만 모읍니다.
	for (UArrowComponent* ArrowComponent : ArrowComponents)
	{
		int32 SlotIndex = INDEX_NONE;
		if (TryGetSlotMarkerIndex(ArrowComponent, SlotIndex))
		{
			SlotMarkers.Emplace(SlotIndex, ArrowComponent);
		}
	}

	// 마커는 선택 사항이므로, 기존 진열대는 수동으로 지정한 슬롯 값을 유지합니다.
	if (SlotMarkers.IsEmpty())
	{
		return;
	}

	SlotMarkers.Sort([](const TPair<int32, UArrowComponent*>& Left,
		const TPair<int32, UArrowComponent*>& Right)
	{
		return Left.Key < Right.Key;
	});

	// 슬롯 번호가 중복되거나 건너뛰면 잘못된 슬롯 배열이 만들어질 수 있어 동기화를 중단합니다.
	for (int32 ExpectedIndex = 0; ExpectedIndex < SlotMarkers.Num(); ++ExpectedIndex)
	{
		if (SlotMarkers[ExpectedIndex].Key != ExpectedIndex)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("%s: 슬롯 마커는 Slot_00부터 번호를 건너뛰지 않고 순서대로 이름을 지정해 주세요."),
				*GetName());
			return;
		}
	}

	Slots.SetNum(SlotMarkers.Num());
	// 이름순으로 정렬된 마커의 상대 위치와 회전을 슬롯 데이터에 복사합니다.
	float LowestSlotHeight = TNumericLimits<float>::Max();
	float HighestSlotHeight = TNumericLimits<float>::Lowest();
	for (int32 SlotIndex = 0; SlotIndex < SlotMarkers.Num(); ++SlotIndex)
	{
		UArrowComponent* SlotMarker = SlotMarkers[SlotIndex].Value;
		Slots[SlotIndex].LocalTransform = SlotMarker->GetRelativeTransform();
		const float SlotHeight = Slots[SlotIndex].LocalTransform.GetLocation().Z;
		LowestSlotHeight = FMath::Min(LowestSlotHeight, SlotHeight);
		HighestSlotHeight = FMath::Max(HighestSlotHeight, SlotHeight);
		// 마커는 블루프린트 편집 전용이므로 게임 화면에서는 보이지 않게 설정합니다.
		SlotMarker->SetHiddenInGame(true);
	}

	// Slot Arrow의 높이를 세 구간으로 나누어 하단·중단·상단을 자동으로 지정합니다.
	// 따라서 에디터에서 Arrow를 다른 단으로 옮기면 SlotLevel도 같은 단으로 갱신됩니다.
	const float HeightRange = HighestSlotHeight - LowestSlotHeight;
	if (HeightRange > KINDA_SMALL_NUMBER)
	{
		const float LowerToMiddleBoundary = LowestSlotHeight + HeightRange / 3.0f;
		const float MiddleToUpperBoundary = LowestSlotHeight + HeightRange * 2.0f / 3.0f;
		for (FCSShelfSlot& Slot : Slots)
		{
			const float SlotHeight = Slot.LocalTransform.GetLocation().Z;
			Slot.SlotLevel = SlotHeight <= LowerToMiddleBoundary
				? ECSShelfSlotLevel::Lower
				: SlotHeight >= MiddleToUpperBoundary
					? ECSShelfSlotLevel::Upper
					: ECSShelfSlotLevel::Middle;
		}
	}
	else
	{
		// 모든 Arrow 높이가 같으면 단을 구분할 수 없으므로 기존 기본값인 중단으로 안전하게 통일합니다.
		for (FCSShelfSlot& Slot : Slots)
		{
			Slot.SlotLevel = ECSShelfSlotLevel::Middle;
		}
	}
}

bool ACSShelfActor::CanPlaceItem_Implementation(AActor* ItemActor) const
{
	const ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(ItemActor);
	if (!bIsInstalled || !IsValid(Product))
	{
		return false;
	}

	// 플레이어 수동 진열의 기존 허용 정책은 유지하되, 도구와 ToolRack 조합만 서로 전용으로 제한합니다.
	const FDataTableRowHandle ProductRow = Product->GetProductRow();
	const FCSProductDataRow* ProductData = nullptr;
	if (ProductRow.DataTable != nullptr && !ProductRow.RowName.IsNone())
	{
		ProductData = ProductRow.GetRow<FCSProductDataRow>(
			TEXT("ACSShelfActor::CanPlaceItem"));
	}

	const bool bIsToolRackShelf = ShelfType == ECSProductShelfType::ToolRack;
	if (bIsToolRackShelf)
	{
		if (ProductData == nullptr ||
			ProductData->ShelfType != ECSProductShelfType::ToolRack)
		{
			return false;
		}
	}
	else if (ProductData != nullptr &&
		ProductData->ShelfType == ECSProductShelfType::ToolRack)
	{
		return false;
	}

	// 플레이어 수동 진열은 단별 상품 지정·상품 중복·보관 타입 규칙을 적용하지 않습니다.
	// 진열대 전체에 실제 빈 슬롯이 하나라도 있으면 배치 대상으로 표시합니다.
	return GetEmptySlotCount() > 0;
}

FText ACSShelfActor::GetPlayerPlacementFailureText(
	const ACSProductPickupActor* Product,
	FVector PlacementHint) const
{
	if (!bIsInstalled || !IsValid(Product))
	{
		return NSLOCTEXT("Interaction", "ShelfPlacementUnavailable", "이 진열대에는 진열할 수 없습니다.");
	}

	const ECSShelfSlotLevel AimedLevel = FindAimedSlotLevel(PlacementHint);
	bool bHasEmptySlot = false;
	for (const FCSShelfSlot& Slot : Slots)
	{
		if (Slot.SlotLevel == AimedLevel && !IsValid(Slot.OccupyingItem))
		{
			bHasEmptySlot = true;
			break;
		}
	}
	if (!bHasEmptySlot)
	{
		// 선반 메시의 빈 공간이 아니라 Slot_XX Arrow가 만든 실제 슬롯만 수용량으로 계산합니다.
		return NSLOCTEXT("Interaction", "ShelfLevelFull", "조준한 단에 빈 슬롯이 없습니다.");
	}

	return NSLOCTEXT("Interaction", "ShelfLevelPlacementUnavailable",
		"이 상품은 조준한 단에 진열할 수 없습니다.");
}

int32 ACSShelfActor::GetEmptySlotCount() const
{
	int32 EmptySlotCount = 0;
	for (const FCSShelfSlot& Slot : Slots)
	{
		if (!IsValid(Slot.OccupyingItem))
		{
			++EmptySlotCount;
		}
	}

	return EmptySlotCount;
}

int32 ACSShelfActor::GetEmployeeRestockableSlotCount(
	const FDataTableRowHandle& ProductRow,
	bool bAllowCrossTierDuplicateFallback) const
{
	if (!bIsInstalled || ProductRow.DataTable == nullptr || ProductRow.RowName.IsNone())
	{
		return 0;
	}

	int32 RestockableSlotCount = 0;
	for (const FCSShelfSlot& Slot : Slots)
	{
		if (IsValid(Slot.OccupyingItem) || !CanPlaceProductInLevel(
			ProductRow, Slot.SlotLevel, bAllowCrossTierDuplicateFallback, false))
		{
			continue;
		}

		++RestockableSlotCount;
	}

	return RestockableSlotCount;
}

bool ACSShelfActor::GetRestockProductRow(FDataTableRowHandle& OutProductRow) const
{
	OutProductRow = FDataTableRowHandle();

	for (const FCSShelfSlot& Slot : Slots)
	{
		const ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(Slot.OccupyingItem);
		if (IsValid(Product))
		{
			// 이미 진열된 상품을 우선 기준으로 삼아, 초기 진열 설정을 나중에 바꿔도 현재 진열대 품목을 유지합니다.
			OutProductRow = Product->GetProductRow();
			return OutProductRow.DataTable != nullptr && !OutProductRow.RowName.IsNone();
		}
	}

	for (const FCSInitialShelfProduct& InitialProduct : InitialProducts)
	{
		if (InitialProduct.ProductRow.DataTable != nullptr && !InitialProduct.ProductRow.RowName.IsNone())
		{
			// 아직 한 번도 채워지지 않은 진열대는 레벨에 설정한 첫 초기 상품을 보충 기준으로 사용합니다.
			OutProductRow = InitialProduct.ProductRow;
			return true;
		}
	}

	return false;
}

int32 ACSShelfActor::TryRestockFromCrate(
	ACSDeliveryCrateActor* SourceCrate,
	int32 RequestedQuantity)
{
	if (!IsValid(SourceCrate) || RequestedQuantity <= 0 || !bIsInstalled)
	{
		return 0;
	}

	int32 RestockedQuantity = 0;
	for (int32 Index = 0; Index < RequestedQuantity && SourceCrate->GetRemainingQuantity() > 0; ++Index)
	{
		const int32 EmptySlotIndex = FindEmptyEmployeeRestockSlot(
			SourceCrate->GetProductRow(), false);
		if (EmptySlotIndex == INDEX_NONE)
		{
			break;
		}

		ACSProductPickupActor* ProductActor = SourceCrate->SpawnProductActor();
		if (!IsValid(ProductActor))
		{
			break;
		}

		if (!PlaceItemInSlot(ProductActor, EmptySlotIndex))
		{
			ProductActor->Destroy();
			break;
		}

		if (!SourceCrate->ConsumeOne())
		{
			// 차감에 실패한 경우 무료 상품이 남지 않도록 방금 배치한 상품을 다시 제거합니다.
			RemovePlacedItem_Implementation(ProductActor);
			ProductActor->Destroy();
			break;
		}

		++RestockedQuantity;
	}

	return RestockedQuantity;
}

bool ACSShelfActor::CanEmployeeRestockProduct(
	const FDataTableRowHandle& ProductRow,
	bool bAllowCrossTierDuplicateFallback) const
{
	return bIsInstalled && ProductRow.DataTable != nullptr && !ProductRow.RowName.IsNone() &&
		FindEmptyEmployeeRestockSlot(ProductRow, bAllowCrossTierDuplicateFallback) != INDEX_NONE;
}

bool ACSShelfActor::OpenShelfConfiguration(AActor* Interactor)
{
	return OpenShelfConfigurationWidget(Interactor);
}

bool ACSShelfActor::GetNextEmployeeRestockSlotLocation(
	const FDataTableRowHandle& ProductRow,
	bool bAllowCrossTierDuplicateFallback,
	FVector& OutSlotLocation) const
{
	OutSlotLocation = FVector::ZeroVector;
	const int32 SlotIndex = FindEmptyEmployeeRestockSlot(
		ProductRow, bAllowCrossTierDuplicateFallback);
	if (SlotIndex == INDEX_NONE)
	{
		return false;
	}

	OutSlotLocation = (Slots[SlotIndex].LocalTransform * GetActorTransform()).GetLocation();
	return true;
}

bool ACSShelfActor::TryPlaceEmployeeProduct(
	ACSProductPickupActor* Product,
	bool bAllowCrossTierDuplicateFallback)
{
	if (!IsValid(Product))
	{
		return false;
	}

	return PlaceItemInSlot(Product, FindEmptyEmployeeRestockSlot(
		Product->GetProductRow(), bAllowCrossTierDuplicateFallback));
}

bool ACSShelfActor::ShouldEmployeeReorganizeProduct(const ACSProductPickupActor* Product) const
{
	if (!bIsInstalled || !bAllowEmployeeRestock || !IsValid(Product))
	{
		return false;
	}

	for (const FCSShelfSlot& Slot : Slots)
	{
		if (Slot.OccupyingItem != Product)
		{
			continue;
		}

		if (const FCSShelfLevelConfiguration* Configuration = FindLevelConfiguration(Slot.SlotLevel);
			Configuration != nullptr && Configuration->bIsPlayerConfigured)
		{
			// 플레이어가 새 상품으로 단을 변경한 순간부터, 이전 상품은 해당 단의 설정과 다르므로
			// 직원이 다른 진열대 또는 창고로 정리해야 합니다. 설정과 같은 상품은 보호합니다.
			return Configuration->ProductRow.DataTable != nullptr &&
				!Configuration->ProductRow.RowName.IsNone() &&
				!IsProductRowEqual(Configuration->ProductRow, Product->GetProductRow());
		}

		// 자동 배치 단에 들어간 상품만 직원 공통 호환 규칙으로 검사해, 실제로 잘못 배치된 경우에만 정리합니다.
		return !IsProductCompatibleWithShelf(Product->GetProductRow());
	}

	return false;
}

bool ACSShelfActor::SetLevelProductConfiguration(
	ECSShelfSlotLevel SlotLevel,
	FDataTableRowHandle ProductRow)
{
	if (ProductRow.DataTable == nullptr || ProductRow.RowName.IsNone() ||
		!IsProductCompatibleWithShelf(ProductRow))
	{
		return false;
	}

	EnsureLevelConfigurations();
	if (FCSShelfLevelConfiguration* Configuration = FindLevelConfiguration(SlotLevel))
	{
		Configuration->ProductRow = ProductRow;
		Configuration->bIsPlayerConfigured = true;

		if (UWorld* World = GetWorld())
		{
			if (UCSStoreEmployeeSubsystem* EmployeeSubsystem =
				World->GetSubsystem<UCSStoreEmployeeSubsystem>())
			{
				// 새 설정과 다른 기존 상품을 즉시 정리 업무로 등록합니다.
				EmployeeSubsystem->RefreshTasksImmediately();
			}
		}
		return true;
	}

	return false;
}

int32 ACSShelfActor::GetMismatchedProductCountOnLevel(ECSShelfSlotLevel SlotLevel) const
{
	const FCSShelfLevelConfiguration* Configuration = FindLevelConfiguration(SlotLevel);
	if (Configuration == nullptr || !Configuration->bIsPlayerConfigured ||
		Configuration->ProductRow.DataTable == nullptr || Configuration->ProductRow.RowName.IsNone())
	{
		return 0;
	}

	int32 MismatchedCount = 0;
	for (const FCSShelfSlot& Slot : Slots)
	{
		const ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(Slot.OccupyingItem);
		if (Slot.SlotLevel == SlotLevel && IsValid(Product) &&
			!IsProductRowEqual(Configuration->ProductRow, Product->GetProductRow()))
		{
			++MismatchedCount;
		}
	}

	return MismatchedCount;
}

bool ACSShelfActor::ClearLevelProductConfiguration(ECSShelfSlotLevel SlotLevel)
{
	EnsureLevelConfigurations();
	if (FCSShelfLevelConfiguration* Configuration = FindLevelConfiguration(SlotLevel))
	{
		Configuration->ProductRow = FDataTableRowHandle();
		Configuration->bIsPlayerConfigured = false;
		return true;
	}

	return false;
}

bool ACSShelfActor::OpenShelfConfigurationWidget(AActor* Interactor)
{
	if (IsValid(ActiveShelfConfigurationWidget))
	{
		return true;
	}

	APawn* InteractingPawn = Cast<APawn>(Interactor);
	APlayerController* PlayerController = IsValid(InteractingPawn)
		? Cast<APlayerController>(InteractingPawn->GetController())
		: nullptr;
	if (!IsValid(PlayerController) || ShelfConfigurationWidgetClass == nullptr)
	{
		return false;
	}

	ActiveShelfConfigurationWidget = CreateWidget<UCShelfConfigurationWidget>(
		PlayerController, ShelfConfigurationWidgetClass);
	if (!IsValid(ActiveShelfConfigurationWidget))
	{
		return false;
	}

	ActiveShelfConfigurationWidget->SetShelfTarget(this);
	ActiveShelfConfigurationWidget->OnConfigurationClosed.AddDynamic(
		this, &ACSShelfActor::HandleShelfConfigurationClosed);
	ActiveShelfConfigurationWidget->AddToViewport();
	ShelfConfigurationPlayerController = PlayerController;

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(ActiveShelfConfigurationWidget->TakeWidget());
	PlayerController->SetInputMode(InputMode);
	// UI를 열기 직전에 누르고 있던 이동 키로 캐릭터가 계속 움직이지 않도록 입력과 현재 속도를 함께 멈춥니다.
	PlayerController->SetIgnoreMoveInput(true);
	if (ACharacter* InteractingCharacter = Cast<ACharacter>(InteractingPawn))
	{
		InteractingCharacter->GetCharacterMovement()->StopMovementImmediately();
	}
	PlayerController->SetShowMouseCursor(true);
	return true;
}

void ACSShelfActor::HandleShelfConfigurationClosed()
{
	if (APlayerController* PlayerController = ShelfConfigurationPlayerController.Get())
	{
		FInputModeGameOnly InputMode;
		PlayerController->SetInputMode(InputMode);
		// 진열 구성 관리 화면을 닫으면 화면을 열면서 잠가 둔 플레이어 이동 입력을 다시 허용합니다.
		PlayerController->SetIgnoreMoveInput(false);
		PlayerController->SetShowMouseCursor(false);
	}

	ActiveShelfConfigurationWidget = nullptr;
	ShelfConfigurationPlayerController = nullptr;
}

void ACSShelfActor::SetPlacementHighlight(
	ECSShelfPlacementPreviewState NewState)
{
	// M_PP_ShelfOutline은 1=초록, 2=노랑, 4=빨강을 사용합니다.
	// 4는 이미 손님 위험 표시에서 빨강으로 사용 중인 공통 스텐실 값입니다.
	const int32 StencilValue = NewState == ECSShelfPlacementPreviewState::Invalid
		? 4
		: NewState == ECSShelfPlacementPreviewState::Warning ? 2 : 1;

	TArray<UPrimitiveComponent*> PrimitiveComponents;
	GetComponents<UPrimitiveComponent>(PrimitiveComponents);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!IsValid(PrimitiveComponent))
		{
			continue;
		}

		// 최초 표시 시의 값만 저장하여 외곽선 해제 후 원래 설정으로 되돌립니다.
		if (!OriginalCustomDepthStates.Contains(PrimitiveComponent))
		{
			OriginalCustomDepthStates.Add(
				PrimitiveComponent,
				PrimitiveComponent->bRenderCustomDepth);
			OriginalCustomStencilValues.Add(
				PrimitiveComponent,
				PrimitiveComponent->CustomDepthStencilValue);
		}

		PrimitiveComponent->SetRenderCustomDepth(true);
		PrimitiveComponent->SetCustomDepthStencilValue(StencilValue);
	}
}

void ACSShelfActor::ClearPlacementHighlight()
{
	RestorePlacementHighlight();
}

void ACSShelfActor::RestorePlacementHighlight()
{
	for (const TPair<TObjectPtr<UPrimitiveComponent>, bool>& Pair : OriginalCustomDepthStates)
	{
		UPrimitiveComponent* PrimitiveComponent = Pair.Key.Get();
		if (!IsValid(PrimitiveComponent))
		{
			continue;
		}

		PrimitiveComponent->SetRenderCustomDepth(Pair.Value);
		if (const int32* OriginalStencilValue =
			OriginalCustomStencilValues.Find(PrimitiveComponent))
		{
			PrimitiveComponent->SetCustomDepthStencilValue(*OriginalStencilValue);
		}
	}

	OriginalCustomDepthStates.Empty();
	OriginalCustomStencilValues.Empty();
}

bool ACSShelfActor::IsProductRowEqual(
	const FDataTableRowHandle& Left,
	const FDataTableRowHandle& Right) const
{
	return Left.DataTable == Right.DataTable && Left.RowName == Right.RowName &&
		Left.DataTable != nullptr && !Left.RowName.IsNone();
}

const FCSShelfLevelConfiguration* ACSShelfActor::FindLevelConfiguration(
	ECSShelfSlotLevel SlotLevel) const
{
	return LevelConfigurations.FindByPredicate([SlotLevel](const FCSShelfLevelConfiguration& Configuration)
	{
		return Configuration.SlotLevel == SlotLevel;
	});
}

FCSShelfLevelConfiguration* ACSShelfActor::FindLevelConfiguration(ECSShelfSlotLevel SlotLevel)
{
	return LevelConfigurations.FindByPredicate([SlotLevel](const FCSShelfLevelConfiguration& Configuration)
	{
		return Configuration.SlotLevel == SlotLevel;
	});
}

void ACSShelfActor::EnsureLevelConfigurations()
{
	const ECSShelfSlotLevel Levels[] = {
		ECSShelfSlotLevel::Lower,
		ECSShelfSlotLevel::Middle,
		ECSShelfSlotLevel::Upper,
	};
	for (const ECSShelfSlotLevel Level : Levels)
	{
		if (FindLevelConfiguration(Level) == nullptr)
		{
			FCSShelfLevelConfiguration& Configuration = LevelConfigurations.AddDefaulted_GetRef();
			Configuration.SlotLevel = Level;
		}
	}
}

bool ACSShelfActor::GetProductRowOnLevel(
	ECSShelfSlotLevel SlotLevel,
	FDataTableRowHandle& OutProductRow) const
{
	OutProductRow = FDataTableRowHandle();
	for (const FCSShelfSlot& Slot : Slots)
	{
		if (Slot.SlotLevel != SlotLevel)
		{
			continue;
		}

		if (const ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(Slot.OccupyingItem))
		{
			OutProductRow = Product->GetProductRow();
			return OutProductRow.DataTable != nullptr && !OutProductRow.RowName.IsNone();
		}
	}

	return false;
}

bool ACSShelfActor::IsProductCompatibleWithShelf(const FDataTableRowHandle& ProductRow) const
{
	const FCSProductDataRow* ProductData = ProductRow.GetRow<FCSProductDataRow>(
		TEXT("ACSShelfActor::IsProductCompatibleWithShelf"));
	if (ProductData == nullptr)
	{
		return false;
	}

	return IsProductShelfTypeCompatible(ProductData->ShelfType);
}

bool ACSShelfActor::IsProductShelfTypeCompatible(ECSProductShelfType ProductShelfType) const
{
	switch (ShelfType)
	{
	case ECSProductShelfType::ChilledShelf:
		// 냉장 상품은 신선도 규칙을 지키기 위해 냉장 진열대에서만 자동 진열·지정할 수 있습니다.
		return ProductShelfType == ECSProductShelfType::ChilledShelf;

	case ECSProductShelfType::FreezerShelf:
		// 냉동 상품은 신선도 규칙을 지키기 위해 냉동 진열대에서만 자동 진열·지정할 수 있습니다.
		return ProductShelfType == ECSProductShelfType::FreezerShelf;

	case ECSProductShelfType::SnackShelf:
	case ECSProductShelfType::PotionShelf:
		// 과자·포션 진열대는 같은 상온 상품군으로 취급하여 서로의 상품을 자동 진열할 수 있습니다.
		return ProductShelfType == ECSProductShelfType::SnackShelf ||
			ProductShelfType == ECSProductShelfType::PotionShelf;

	case ECSProductShelfType::UtilityShelf:
	case ECSProductShelfType::ScrollRack:
		// 일반 진열대·스크롤 거치대는 공용 상온 상품군으로 취급하여 서로의 상품을 자동 진열할 수 있습니다.
		return ProductShelfType == ECSProductShelfType::UtilityShelf ||
			ProductShelfType == ECSProductShelfType::ScrollRack;

	case ECSProductShelfType::ToolRack:
		// 도구는 형태와 진열 방식이 달라 도구 거치대에서만 자동 진열·지정할 수 있습니다.
		return ProductShelfType == ECSProductShelfType::ToolRack;

	default:
		return false;
	}
}

bool ACSShelfActor::CanPlaceProductInLevel(
	const FDataTableRowHandle& ProductRow,
	ECSShelfSlotLevel SlotLevel,
	bool bAllowCrossTierDuplicateFallback,
	bool bIgnoreStorageCompatibility) const
{
	if (!bIgnoreStorageCompatibility && !IsProductCompatibleWithShelf(ProductRow))
	{
		return false;
	}

	if (const FCSShelfLevelConfiguration* Configuration = FindLevelConfiguration(SlotLevel);
		Configuration != nullptr && Configuration->bIsPlayerConfigured)
	{
		// 플레이어가 지정한 단은 같은 상품을 다른 단에도 배치할 수 있는 명시적인 중복 허용 상태입니다.
		return IsProductRowEqual(Configuration->ProductRow, ProductRow);
	}

	FDataTableRowHandle ExistingProductRow;
	if (GetProductRowOnLevel(SlotLevel, ExistingProductRow))
	{
		return IsProductRowEqual(ExistingProductRow, ProductRow);
	}

	for (const FCSShelfSlot& OtherSlot : Slots)
	{
		if (OtherSlot.SlotLevel == SlotLevel)
		{
			continue;
		}

		if (const ACSProductPickupActor* OtherProduct = Cast<ACSProductPickupActor>(OtherSlot.OccupyingItem);
			IsValid(OtherProduct) && IsProductRowEqual(OtherProduct->GetProductRow(), ProductRow))
		{
			return bAllowCrossTierDuplicateFallback;
		}
	}

	return true;
}

int32 ACSShelfActor::FindEmptySlotForProduct(
	const FDataTableRowHandle& ProductRow,
	bool bAllowCrossTierDuplicateFallback,
	ECSShelfSlotLevel PreferredLevel,
	bool bIgnoreStorageCompatibility,
	bool bAllowOtherLevelFallback) const
{
	TArray<ECSShelfSlotLevel> Levels;
	Levels.Add(PreferredLevel);
	if (bAllowOtherLevelFallback && PreferredLevel == ECSShelfSlotLevel::Lower)
	{
		// 직원 자동 보충은 하단부터 고유 품목을 찾되, 하단이 맞지 않으면 중단·상단까지 순서대로 검사합니다.
		Levels.Add(ECSShelfSlotLevel::Middle);
		Levels.Add(ECSShelfSlotLevel::Upper);
	}
	for (const ECSShelfSlotLevel Level : Levels)
	{
		if (!CanPlaceProductInLevel(
			ProductRow, Level, bAllowCrossTierDuplicateFallback, bIgnoreStorageCompatibility))
		{
			continue;
		}

		for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
		{
			if (Slots[SlotIndex].SlotLevel == Level && !IsValid(Slots[SlotIndex].OccupyingItem))
			{
				return SlotIndex;
			}
		}
	}

	return INDEX_NONE;
}

int32 ACSShelfActor::FindEmptyEmployeeRestockSlot(
	const FDataTableRowHandle& ProductRow,
	bool bAllowCrossTierDuplicateFallback) const
{
	const ECSShelfSlotLevel Levels[] = {
		ECSShelfSlotLevel::Lower,
		ECSShelfSlotLevel::Middle,
		ECSShelfSlotLevel::Upper
	};
	for (const ECSShelfSlotLevel Level : Levels)
	{
		const FCSShelfLevelConfiguration* Configuration = FindLevelConfiguration(Level);
		if (Configuration == nullptr || !Configuration->bIsPlayerConfigured ||
			!IsProductRowEqual(Configuration->ProductRow, ProductRow))
		{
			continue;
		}

		for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
		{
			if (Slots[SlotIndex].SlotLevel == Level && !IsValid(Slots[SlotIndex].OccupyingItem))
			{
				return SlotIndex;
			}
		}
	}

	// 일치하는 플레이어 지정 단이 없거나 모두 찼을 때만 기존의 하단 우선 배치를 사용합니다.
	return FindEmptySlotForProduct(
		ProductRow, bAllowCrossTierDuplicateFallback, ECSShelfSlotLevel::Lower);
}

bool ACSShelfActor::PlaceItemInSlot(AActor* ItemActor, int32 SlotIndex)
{
	if (!IsValid(ItemActor) || !Slots.IsValidIndex(SlotIndex) || IsValid(Slots[SlotIndex].OccupyingItem))
	{
		return false;
	}

	FCSShelfSlot& SelectedSlot = Slots[SlotIndex];
	const FTransform SlotWorldTransform = SelectedSlot.LocalTransform * GetActorTransform();
	ItemActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	ItemActor->SetActorLocationAndRotation(SlotWorldTransform.GetLocation(), SlotWorldTransform.GetRotation());
	ItemActor->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
	ItemActor->SetActorEnableCollision(true);
	if (ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(ItemActor))
	{
		Product->SetStorageShelfType(ShelfType);
	}

	SelectedSlot.OccupyingItem = ItemActor;
	OnItemPlaced.Broadcast(ItemActor, SlotIndex);
	return true;
}

ECSShelfPlacementPreviewState ACSShelfActor::GetPlacementHighlightState(
	AActor* ItemActor) const
{
	// 외곽선은 현재 조준한 단이 아닌, 진열대 전체의 배치 가능 여부를 알려 줍니다.
	if (!CanPlaceItem_Implementation(ItemActor))
	{
		return ECSShelfPlacementPreviewState::Invalid;
	}

	return ShouldWarnStorage(ItemActor)
		? ECSShelfPlacementPreviewState::Warning
		: ECSShelfPlacementPreviewState::Valid;
}

ECSShelfSlotLevel ACSShelfActor::FindAimedSlotLevel(FVector PlacementHint) const
{
	ECSShelfSlotLevel AimedSlotLevel = ECSShelfSlotLevel::Middle;
	float ClosestHeightDifference = TNumericLimits<float>::Max();

	for (const FCSShelfSlot& Slot : Slots)
	{
		// 단 선택은 선반 앞·뒤 깊이와 무관하게 조준한 높이만으로 판정해야 합니다.
		// 이미 찬 슬롯도 각 단의 높이를 알 수 있도록 판정 기준에 그대로 사용합니다.
		const FTransform SlotWorldTransform =
			Slot.LocalTransform * GetActorTransform();
		const float HeightDifference = FMath::Abs(
			SlotWorldTransform.GetLocation().Z - PlacementHint.Z);

		if (HeightDifference < ClosestHeightDifference)
		{
			ClosestHeightDifference = HeightDifference;
			AimedSlotLevel = Slot.SlotLevel;
		}
	}

	return AimedSlotLevel;
}

int32 ACSShelfActor::FindClosestEmptySlot(FVector PlacementHint) const
{
	return FindClosestSlotOnAimedLevel(PlacementHint, true);
}

int32 ACSShelfActor::FindClosestSlotOnAimedLevel(
	FVector PlacementHint,
	bool bRequireEmptySlot) const
{
	int32 ClosestSlotIndex = INDEX_NONE;
	float ClosestDistanceSquared = TNumericLimits<float>::Max();
	const ECSShelfSlotLevel AimedSlotLevel = FindAimedSlotLevel(PlacementHint);

	for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
	{
		const FCSShelfSlot& Slot = Slots[SlotIndex];
		// 다른 단은 비어 있어도 선택하지 않아 플레이어가 조준한 단만 채워집니다.
		if (Slot.SlotLevel != AimedSlotLevel ||
			(bRequireEmptySlot && IsValid(Slot.OccupyingItem)))
		{
			continue;
		}

		const FTransform SlotWorldTransform =
			Slot.LocalTransform * GetActorTransform();
		const float DistanceSquared = FVector::DistSquared(
			SlotWorldTransform.GetLocation(),
			PlacementHint);

		if (DistanceSquared < ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			ClosestSlotIndex = SlotIndex;
		}
	}

	return ClosestSlotIndex;
}

bool ACSShelfActor::GetPlacementPreview_Implementation(
	AActor* ItemActor,
	FVector PlacementHint,
	FTransform& OutSlotWorldTransform) const
{
	const int32 SlotIndex = FindClosestEmptySlot(PlacementHint);
	if (SlotIndex == INDEX_NONE)
	{
		// 가득 찬 단도 충돌 지점이 아닌 실제 슬롯에서 빨간 프리뷰를 보여 줍니다.
		const int32 OccupiedSlotIndex = FindClosestSlotOnAimedLevel(PlacementHint, false);
		if (OccupiedSlotIndex != INDEX_NONE)
		{
			OutSlotWorldTransform =
				Slots[OccupiedSlotIndex].LocalTransform * GetActorTransform();
		}
		return false;
	}

	if (!CanPlaceItem_Implementation(ItemActor))
	{
		return false;
	}

	OutSlotWorldTransform =
		Slots[SlotIndex].LocalTransform * GetActorTransform();
	return true;
}

ECSShelfPlacementPreviewState ACSShelfActor::GetPlacementPreviewState_Implementation(
	AActor* ItemActor,
	FVector PlacementHint,
	FTransform& OutSlotWorldTransform) const
{
	if (!GetPlacementPreview_Implementation(ItemActor, PlacementHint, OutSlotWorldTransform))
	{
		return ECSShelfPlacementPreviewState::Invalid;
	}

	// 모든 상품은 진열할 수 있으므로 보관 조건이 맞지 않아도 배치를 막지 않고 경고만 반환합니다.
	return ShouldWarnStorage(ItemActor)
		? ECSShelfPlacementPreviewState::Warning
		: ECSShelfPlacementPreviewState::Valid;
}

bool ACSShelfActor::TryPlaceItem_Implementation(
	AActor* ItemActor,
	FVector PlacementHint)
{
	if (!CanPlaceItem_Implementation(ItemActor))
	{
		return false;
	}

	const int32 SlotIndex = FindClosestEmptySlot(PlacementHint);
	if (SlotIndex == INDEX_NONE)
	{
		return false;
	}

	return PlaceItemInSlot(ItemActor, SlotIndex);
}

int32 ACSShelfActor::GetProductCountOnAimedLevel(FVector AimPoint) const
{
	const ECSShelfSlotLevel Level = FindAimedSlotLevel(AimPoint);
	int32 Count = 0;
	for (const FCSShelfSlot& Slot : Slots)
		if (Slot.SlotLevel == Level && IsValid(Slot.OccupyingItem)) ++Count;
	return Count;
}

ACSProductPickupActor* ACSShelfActor::FindRetrievableProduct(FVector AimPoint, const FDataTableRowHandle& RequiredRow, const AActor* Collector) const
{
	if (!bIsInstalled) return nullptr;
	const ECSShelfSlotLevel Level = FindAimedSlotLevel(AimPoint);
	for (int32 Index = Slots.Num() - 1; Index >= 0; --Index)
	{
		const FCSShelfSlot& Slot = Slots[Index];
		ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(Slot.OccupyingItem);
		if (Slot.SlotLevel == Level && ACSDeliveryCrateActor::CanPackProduct(Product, Collector) &&
			(RequiredRow.IsNull() || IsProductRowEqual(Product->GetProductRow(), RequiredRow))) return Product;
	}
	return nullptr;
}

void ACSShelfActor::RemovePlacedItem_Implementation(AActor* ItemActor)
{
	for (FCSShelfSlot& Slot : Slots)
	{
		if (Slot.OccupyingItem == ItemActor)
		{
			if (ACSProductPickupActor* ProductActor = Cast<ACSProductPickupActor>(ItemActor))
			{
				// 진열대에서 꺼낸 상품은 일반 환경으로 간주해 부적절한 보관 여부를 다시 계산합니다.
				ProductActor->SetStorageShelfType(ECSProductShelfType::UtilityShelf);
			}
			Slot.OccupyingItem = nullptr;
			return;
		}
	}
}

bool ACSShelfActor::ShouldWarnStorage(AActor* ItemActor) const
{
	const ACSProductPickupActor* ProductActor = Cast<ACSProductPickupActor>(ItemActor);
	if (!IsValid(ProductActor))
	{
		return false;
	}

	const FCSProductDataRow* ProductData =
		ProductActor->GetProductRow().GetRow<FCSProductDataRow>(TEXT("ACSShelfActor::ShouldWarnStorage"));
	if (ProductData == nullptr)
	{
		return false;
	}

	// 회의 규칙에 따라 냉장·냉동 상품만 경고 대상으로 두고, 나머지 상품은 모든 진열대에 정상 배치합니다.
	if (ProductData->ShelfType == ECSProductShelfType::ChilledShelf)
	{
		return ShelfType != ECSProductShelfType::ChilledShelf;
	}

	if (ProductData->ShelfType == ECSProductShelfType::FreezerShelf)
	{
		return ShelfType != ECSProductShelfType::FreezerShelf;
	}

	return false;
}

void ACSShelfActor::SetFacilityPhysicsEnabled(bool bEnabled)
{
	if (!IsValid(ShelfMesh))
	{
		return;
	}

	if (bEnabled)
	{
		// Blueprint에서 조정한 메시 피벗과 높이를 물리가 분리하기 전에 그대로 저장합니다.
		if (!ShelfMesh->IsSimulatingPhysics() &&
			ShelfMesh->GetAttachParent() == SceneRoot)
		{
			ShelfMeshRelativeTransformBeforePhysics = ShelfMesh->GetRelativeTransform();
			bHasCachedShelfMeshRelativeTransform = true;
		}

		ShelfMesh->SetMobility(EComponentMobility::Movable);
		ShelfMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		ShelfMesh->SetEnableGravity(true);
		ShelfMesh->SetSimulatePhysics(true);
		if (ShelfMesh->IsSimulatingPhysics())
		{
			ShelfMesh->WakeAllRigidBodies();
		}
		return;
	}

	if (ShelfMesh->IsSimulatingPhysics())
	{
		ShelfMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		ShelfMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		ShelfMesh->PutAllRigidBodiesToSleep();
	}
	ShelfMesh->SetSimulatePhysics(false);
	ShelfMesh->SetEnableGravity(false);

	bool bAttachedToSceneRoot = ShelfMesh->GetAttachParent() == SceneRoot;
	if (!bAttachedToSceneRoot && IsValid(SceneRoot))
	{
		bAttachedToSceneRoot = ShelfMesh->AttachToComponent(
			SceneRoot,
			FAttachmentTransformRules::KeepWorldTransform);
	}

	if (bAttachedToSceneRoot && bHasCachedShelfMeshRelativeTransform)
	{
		ShelfMesh->SetRelativeTransform(
			ShelfMeshRelativeTransformBeforePhysics,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
	}
}

bool ACSShelfActor::GetItemSlotLevel_Implementation(
	AActor* ItemActor,
	ECSShelfSlotLevel& OutSlotLevel) const
{
	// 슬롯을 찾지 못한 경우에도 호출 측에서 안전하게 사용할 수 있도록 중단을 기본값으로 설정합니다.
	OutSlotLevel = ECSShelfSlotLevel::Middle;

	if (!IsValid(ItemActor))
	{
		return false;
	}

	// 상품 액터와 일치하는 점유 슬롯을 찾아 해당 슬롯에 설정된 높이 정보를 반환합니다.
	for (const FCSShelfSlot& Slot : Slots)
	{
		if (Slot.OccupyingItem == ItemActor)
		{
			OutSlotLevel = Slot.SlotLevel;
			return true;
		}
	}

	return false;
}
