#include "Systems/SaveLoad/CSWorldStateSaveHandler.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"

#include "Data/CSSaveTypes.h"
#include "Features/Delivery/CSDeliveryCrateActor.h"
#include "Features/Ending/CSEndingActors.h"
#include "Features/Facilities/CSBuildableFacility.h"
#include "Features/Facilities/CSSaveIdComponent.h"
#include "Features/Interaction/CSShelfPlaceable.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Features/Items/CSProductPlacementPreviewActor.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "Features/Shelves/CSShelfActor.h"
#include "Features/Storage/CSFreshnessSafeZoneActor.h"
#include "Features/Trash/CSTrashActor.h"
#include "Systems/SaveLoad/CSSaveGame.h"
#include "Systems/SaveLoad/CSSaveRecordUtils.h"

// ---------------------------------------------------------------------------
// 캡처
// ---------------------------------------------------------------------------

void UCSWorldStateSaveHandler::CaptureSaveData(UCSSaveGame& OutSave) const
{
	FCSWorldSaveRecord& Record = OutSave.World;
	Record = FCSWorldSaveRecord();

	CaptureShelves(Record);
	CaptureLooseProducts(Record);
	CaptureCrates(Record);
	CaptureFacilities(Record);
	CaptureTrash(Record);
	CaptureQuickbar(Record);
	CaptureEnding(OutSave);

	UE_LOG(LogTemp, Log,
		TEXT("[CSSave] 월드 캡처: 진열대 %d, 낱개 상품 %d, 상자 %d, 시설 %d, 쓰레기 %d, 퀵바 %d칸"),
		Record.Shelves.Num(),
		Record.LooseProducts.Num(),
		Record.Crates.Num(),
		Record.Facilities.Num(),
		Record.Trash.Num(),
		Record.Quickbar.Slots.Num());
}

void UCSWorldStateSaveHandler::CaptureShelves(FCSWorldSaveRecord& OutRecord) const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	for (TActorIterator<ACSShelfActor> It(World); It; ++It)
	{
		const ACSShelfActor* Shelf = *It;
		if (!IsValid(Shelf))
		{
			continue;
		}

		FCSShelfRecord ShelfRecord;
		Shelf->CaptureSaveRecord(ShelfRecord);

		// 식별자가 없으면 복원 시 대상 진열대를 특정할 수 없어 제외
		if (!ShelfRecord.SaveId.IsValid())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[CSSave] 진열대 %s에 SaveId가 없어 저장에서 제외합니다. 레벨을 저장하면 식별자가 발급됩니다."),
				*Shelf->GetName());
			continue;
		}

		OutRecord.Shelves.Add(ShelfRecord);
	}
}

void UCSWorldStateSaveHandler::CaptureLooseProducts(FCSWorldSaveRecord& OutRecord) const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	for (TActorIterator<ACSProductPickupActor> It(World); It; ++It)
	{
		const ACSProductPickupActor* Product = *It;
		if (!IsValid(Product))
		{
			continue;
		}

		// 표시 전용 프리뷰와 계산대 연출 상품은 재고가 아니므로 제외
		if (Product->IsA<ACSProductPlacementPreviewActor>() || Product->IsExcludedFromStockCount())
		{
			continue;
		}

		// 진열대 슬롯 상품은 진열대 레코드가 기록하므로 중복 기록을 방지
		const AActor* ParentActor = Product->GetAttachParentActor();
		if (IsValid(ParentActor) && ParentActor->IsA<ACSShelfActor>())
		{
			continue;
		}

		const FDataTableRowHandle ProductRow = Product->GetProductRow();
		if (ProductRow.DataTable == nullptr || ProductRow.RowName.IsNone())
		{
			continue;
		}

		FCSLooseProductRecord ProductRecord;
		ProductRecord.ActorClass = FSoftClassPath(Product->GetClass());
		ProductRecord.ProductRow = FCSSaveRowRef::FromHandle(ProductRow);
		ProductRecord.Transform = Product->GetActorTransform();
		ProductRecord.StorageShelfType = Product->GetStorageShelfType();

		// 신선도 감소 기준 시각이 월드 시간에 묶여 있어 경과분을 반영한 값으로 기록
		ProductRecord.ProductState = CSSaveRecordUtils::ToRecord(
			Product->GetProductRuntimeState(),
			Product->GetCurrentFreshness());

		if (const ACSFreshnessSafeZoneActor* Storage = Cast<ACSFreshnessSafeZoneActor>(ParentActor))
		{
			// 창고 보관 상품은 복원 시 같은 창고에 다시 보관
			ProductRecord.StorageActorName = Storage->GetFName();
		}
		else if (const APawn* CarrierPawn = Cast<APawn>(ParentActor))
		{
			// 플레이어·직원이 들고 있던 상품은 부착 상태 대신 운반자 위치의 바닥 상품으로 기록
			ProductRecord.Transform = FTransform(
				Product->GetActorRotation(),
				CarrierPawn->GetActorLocation(),
				Product->GetActorScale3D());
			ProductRecord.bWorldDropPhysics = true;
		}
		else if (const UStaticMeshComponent* ProductMesh = Product->FindComponentByClass<UStaticMeshComponent>();
			IsValid(ProductMesh) && ProductMesh->IsSimulatingPhysics())
		{
			// 물리로 분리된 메시가 실제 위치이므로 메시 기준으로 기록
			ProductRecord.Transform = FTransform(
				ProductMesh->GetComponentRotation(),
				ProductMesh->GetComponentLocation(),
				Product->GetActorScale3D());
			ProductRecord.bWorldDropPhysics = true;
		}

		OutRecord.LooseProducts.Add(ProductRecord);
	}
}

void UCSWorldStateSaveHandler::CaptureCrates(FCSWorldSaveRecord& OutRecord) const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const ACSPlayerCharacter* PlayerCharacter = FindPlayerCharacter();
	for (TActorIterator<ACSDeliveryCrateActor> It(World); It; ++It)
	{
		const ACSDeliveryCrateActor* Crate = *It;
		if (!IsValid(Crate) || Crate->GetRemainingQuantity() <= 0)
		{
			continue;
		}

		const FDataTableRowHandle ProductRow = Crate->GetProductRow();
		if (ProductRow.DataTable == nullptr || ProductRow.RowName.IsNone())
		{
			continue;
		}

		// 퀵바에 보관한 상자는 퀵바 레코드가 기록하므로 중복 기록을 방지
		if (IsValid(PlayerCharacter) && PlayerCharacter->IsQuickbarItem(Crate))
		{
			continue;
		}

		FCSCrateRecord CrateRecord;
		Crate->CaptureSaveRecord(CrateRecord);

		const AActor* ParentActor = Crate->GetAttachParentActor();
		if (const ACSFreshnessSafeZoneActor* Storage = Cast<ACSFreshnessSafeZoneActor>(ParentActor))
		{
			// 창고 보관 상자는 복원 시 같은 창고에 다시 보관
			CrateRecord.StorageActorName = Storage->GetFName();
			CrateRecord.bWorldDropPhysics = false;
		}
		else if (IsValid(ParentActor))
		{
			// 운반자·수레에 실린 상자는 부착 상태 대신 현재 위치의 바닥 상자로 기록
			CrateRecord.Transform = FTransform(
				Crate->GetActorRotation(),
				Crate->GetActorLocation(),
				Crate->GetActorScale3D());
			CrateRecord.bWorldDropPhysics = true;
		}

		OutRecord.Crates.Add(CrateRecord);
	}
}

void UCSWorldStateSaveHandler::CaptureFacilities(FCSWorldSaveRecord& OutRecord) const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || !Actor->Implements<UCSBuildableFacility>())
		{
			continue;
		}

		// 진열대는 슬롯 정보까지 담는 전용 레코드로 따로 기록
		if (Actor->IsA<ACSShelfActor>())
		{
			continue;
		}

		// 식별자 컴포넌트가 없는 시설은 복원 대상을 특정할 수 없어 제외
		const UCSSaveIdComponent* SaveIdComponent = Actor->FindComponentByClass<UCSSaveIdComponent>();
		if (SaveIdComponent == nullptr || !SaveIdComponent->GetSaveId().IsValid())
		{
			continue;
		}

		FCSFacilityRecord FacilityRecord;
		FacilityRecord.SaveId = SaveIdComponent->GetSaveId();
		FacilityRecord.FacilityId = ICSBuildableFacility::Execute_GetFacilityId(Actor);
		FacilityRecord.ActorClass = FSoftClassPath(Actor->GetClass());
		FacilityRecord.Transform = Actor->GetActorTransform();
		FacilityRecord.bIsInstalled = ICSBuildableFacility::Execute_IsFacilityInstalled(Actor);
		FacilityRecord.bWasSpawnedAtRuntime = SaveIdComponent->WasSpawnedAtRuntime();
		OutRecord.Facilities.Add(FacilityRecord);
	}
}

void UCSWorldStateSaveHandler::CaptureTrash(FCSWorldSaveRecord& OutRecord) const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	for (TActorIterator<ACSTrashActor> It(World); It; ++It)
	{
		const ACSTrashActor* Trash = *It;
		if (!IsValid(Trash) || Trash->IsActorBeingDestroyed())
		{
			continue;
		}

		FCSTrashRecord TrashRecord;
		Trash->CaptureSaveRecord(TrashRecord);

		const AActor* ParentActor = Trash->GetAttachParentActor();
		if (const APawn* CarrierPawn = Cast<APawn>(ParentActor))
		{
			// 손에 들거나 퀵바에 보관한 쓰레기는 부착 상태 대신 운반자 위치의 바닥 쓰레기로 기록
			TrashRecord.Transform = FTransform(
				Trash->GetActorRotation(),
				CarrierPawn->GetActorLocation(),
				Trash->GetActorScale3D());
			TrashRecord.MeshRelativeTransform = FTransform::Identity;
			TrashRecord.bWorldDropPhysics = true;
		}
		else if (IsValid(ParentActor))
		{
			// 다른 액터에 부착된 쓰레기는 현재 위치의 바닥 쓰레기로 기록
			TrashRecord.bWorldDropPhysics = true;
		}

		OutRecord.Trash.Add(TrashRecord);
	}
}

void UCSWorldStateSaveHandler::CaptureQuickbar(FCSWorldSaveRecord& OutRecord) const
{
	if (const ACSPlayerCharacter* PlayerCharacter = FindPlayerCharacter())
	{
		PlayerCharacter->CaptureQuickbarRecord(OutRecord.Quickbar);
	}
}

void UCSWorldStateSaveHandler::CaptureEnding(UCSSaveGame& OutSave) const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// 레벨에 하나만 배치하는 엔딩 관리자를 조회
	for (TActorIterator<ACSEndingManager> It(World); It; ++It)
	{
		if (const ACSEndingManager* EndingManager = *It; IsValid(EndingManager))
		{
			EndingManager->CaptureSaveRecord(OutSave.Ending);
			return;
		}
	}
}

// ---------------------------------------------------------------------------
// 복원
// ---------------------------------------------------------------------------

void UCSWorldStateSaveHandler::RestoreSaveData(const UCSSaveGame& InSave)
{
	if (GetWorld() == nullptr)
	{
		return;
	}

	const FCSWorldSaveRecord& Record = InSave.World;

	// 상품을 제거하기 전에 폴백 테이블을 확보
	const UDataTable* FallbackProductTable = FindFallbackProductTable();

	DestroyAllProductsAndCrates();
	DestroyAllTrash();

	RestoreFacilities(Record);
	RestoreShelves(Record, FallbackProductTable);
	RestoreLooseProducts(Record, FallbackProductTable);
	RestoreCrates(Record, FallbackProductTable);
	RestoreTrash(Record);
	RestoreQuickbar(Record, FallbackProductTable);
	RestoreEnding(InSave);

	UE_LOG(LogTemp, Log,
		TEXT("[CSSave] 월드 복원: 진열대 %d, 낱개 상품 %d, 상자 %d, 시설 %d, 쓰레기 %d, 퀵바 %d칸"),
		Record.Shelves.Num(),
		Record.LooseProducts.Num(),
		Record.Crates.Num(),
		Record.Facilities.Num(),
		Record.Trash.Num(),
		Record.Quickbar.Slots.Num());
}

void UCSWorldStateSaveHandler::DestroyAllProductsAndCrates()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// 진열대가 BeginPlay에서 채운 초기 상품을 진열대 점유 해제 경로로 먼저 제거
	for (TActorIterator<ACSShelfActor> It(World); It; ++It)
	{
		if (ACSShelfActor* Shelf = *It; IsValid(Shelf))
		{
			Shelf->ClearAllSlotsForRestore();
		}
	}

	TArray<AActor*> ActorsToDestroy;
	for (TActorIterator<ACSProductPickupActor> It(World); It; ++It)
	{
		// 프리뷰는 플레이어 캐릭터가 관리하므로 제외
		if (ACSProductPickupActor* Product = *It; IsValid(Product) && !Product->IsA<ACSProductPlacementPreviewActor>())
		{
			ActorsToDestroy.Add(Product);
		}
	}

	for (TActorIterator<ACSDeliveryCrateActor> It(World); It; ++It)
	{
		if (ACSDeliveryCrateActor* Crate = *It; IsValid(Crate))
		{
			ActorsToDestroy.Add(Crate);
		}
	}

	for (AActor* Actor : ActorsToDestroy)
	{
		if (!IsValid(Actor))
		{
			continue;
		}

		// 창고·수레의 점유 목록과 신선도 보호 등록을 해제한 뒤 제거
		AActor* ParentActor = Actor->GetAttachParentActor();
		if (ACSFreshnessSafeZoneActor* Storage = Cast<ACSFreshnessSafeZoneActor>(ParentActor))
		{
			if (ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(Actor))
			{
				Storage->RemoveStoredLooseProduct(Product);
			}
			else if (ACSDeliveryCrateActor* Crate = Cast<ACSDeliveryCrateActor>(Actor))
			{
				Storage->RemoveStoredCrate(Crate);
			}
		}
		else if (IsValid(ParentActor) && ParentActor->Implements<UCSShelfPlaceable>())
		{
			ICSShelfPlaceable::Execute_RemovePlacedItem(ParentActor, Actor);
		}

		Actor->Destroy();
	}
}

void UCSWorldStateSaveHandler::DestroyAllTrash()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	TArray<ACSTrashActor*> TrashToDestroy;
	for (TActorIterator<ACSTrashActor> It(World); It; ++It)
	{
		if (ACSTrashActor* Trash = *It; IsValid(Trash))
		{
			TrashToDestroy.Add(Trash);
		}
	}

	for (ACSTrashActor* Trash : TrashToDestroy)
	{
		if (IsValid(Trash))
		{
			Trash->Destroy();
		}
	}
}

void UCSWorldStateSaveHandler::RestoreTrash(const FCSWorldSaveRecord& InRecord)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	for (const FCSTrashRecord& TrashRecord : InRecord.Trash)
	{
		UClass* TrashClass = TrashRecord.ActorClass.TryLoadClass<ACSTrashActor>();
		if (TrashClass == nullptr)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[CSSave] 쓰레기 클래스를 불러오지 못했습니다: %s"),
				*TrashRecord.ActorClass.ToString());
			continue;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACSTrashActor* Trash = World->SpawnActor<ACSTrashActor>(TrashClass, TrashRecord.Transform, SpawnParameters);
		if (!IsValid(Trash))
		{
			continue;
		}

		Trash->RestoreSaveRecord(TrashRecord);
	}
}

void UCSWorldStateSaveHandler::RestoreShelves(
	const FCSWorldSaveRecord& InRecord,
	const UDataTable* FallbackProductTable)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// 레벨 배치 진열대는 식별자로 수집하고 런타임 스폰 진열대는 제거 대상으로 분류
	TMap<FGuid, ACSShelfActor*> ShelvesBySaveId;
	TArray<ACSShelfActor*> RuntimeSpawnedShelves;
	for (TActorIterator<ACSShelfActor> It(World); It; ++It)
	{
		ACSShelfActor* Shelf = *It;
		if (!IsValid(Shelf))
		{
			continue;
		}

		const UCSSaveIdComponent* SaveIdComponent = Shelf->FindComponentByClass<UCSSaveIdComponent>();
		if (SaveIdComponent != nullptr && SaveIdComponent->WasSpawnedAtRuntime())
		{
			RuntimeSpawnedShelves.Add(Shelf);
			continue;
		}

		const FGuid SaveId = UCSSaveIdComponent::FindSaveId(Shelf);
		if (SaveId.IsValid())
		{
			ShelvesBySaveId.Add(SaveId, Shelf);
		}
	}

	// 튜토리얼 스폰 등 이 월드에서 새로 생긴 진열대는 세이브 기준으로 다시 생성하므로 제거
	for (ACSShelfActor* Shelf : RuntimeSpawnedShelves)
	{
		if (IsValid(Shelf))
		{
			Shelf->Destroy();
		}
	}

	for (const FCSShelfRecord& ShelfRecord : InRecord.Shelves)
	{
		ACSShelfActor* TargetShelf = nullptr;
		if (ACSShelfActor** Found = ShelvesBySaveId.Find(ShelfRecord.SaveId))
		{
			TargetShelf = *Found;
		}
		else if (ShelfRecord.bWasSpawnedAtRuntime)
		{
			// 발주로 구입한 진열대는 저장된 클래스와 위치로 다시 생성
			UClass* ShelfClass = ShelfRecord.ActorClass.TryLoadClass<ACSShelfActor>();
			if (ShelfClass == nullptr)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[CSSave] 진열대 클래스를 불러오지 못했습니다: %s"),
					*ShelfRecord.ActorClass.ToString());
				continue;
			}

			FActorSpawnParameters SpawnParameters;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			TargetShelf = World->SpawnActor<ACSShelfActor>(ShelfClass, ShelfRecord.Transform, SpawnParameters);
		}

		if (!IsValid(TargetShelf))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[CSSave] 진열대를 찾지 못해 건너뜁니다. SaveId %s"),
				*ShelfRecord.SaveId.ToString());
			continue;
		}

		// 플레이어가 옮겼을 수 있는 발주 진열대는 저장 위치로 이동
		if (ShelfRecord.bWasSpawnedAtRuntime)
		{
			TargetShelf->SetActorTransform(ShelfRecord.Transform);
		}

		TargetShelf->RestoreSaveRecord(ShelfRecord, FallbackProductTable);
	}
}

void UCSWorldStateSaveHandler::RestoreLooseProducts(
	const FCSWorldSaveRecord& InRecord,
	const UDataTable* FallbackProductTable)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	TMap<FName, ACSFreshnessSafeZoneActor*> StoragesByName;
	CollectStoragesByName(StoragesByName);

	const float CurrentWorldTimeSeconds = World->GetTimeSeconds();
	for (const FCSLooseProductRecord& ProductRecord : InRecord.LooseProducts)
	{
		UClass* ProductClass = ProductRecord.ActorClass.TryLoadClass<ACSProductPickupActor>();
		if (ProductClass == nullptr)
		{
			continue;
		}

		const FDataTableRowHandle ProductRow = ProductRecord.ProductRow.ToHandle(FallbackProductTable);
		if (ProductRow.DataTable == nullptr || ProductRow.RowName.IsNone())
		{
			continue;
		}

		ACSProductPickupActor* Product = World->SpawnActorDeferred<ACSProductPickupActor>(
			ProductClass,
			ProductRecord.Transform,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!IsValid(Product))
		{
			continue;
		}

		if (!Product->InitializeProduct(ProductRow))
		{
			Product->Destroy();
			continue;
		}

		Product->FinishSpawning(ProductRecord.Transform);
		Product->SetStorageShelfType(ProductRecord.StorageShelfType);
		Product->ApplyProductRuntimeState(
			CSSaveRecordUtils::ToRuntime(ProductRecord.ProductState, CurrentWorldTimeSeconds));

		// 창고 보관 상품은 같은 창고의 빈 격자에 다시 보관
		if (!ProductRecord.StorageActorName.IsNone())
		{
			ACSFreshnessSafeZoneActor** Storage = StoragesByName.Find(ProductRecord.StorageActorName);
			if (Storage != nullptr && (*Storage)->TryStoreLooseProduct(Product))
			{
				continue;
			}

			// 창고를 찾지 못하거나 가득 차면 저장 위치의 바닥 상품으로 유지
			UE_LOG(LogTemp, Warning,
				TEXT("[CSSave] 창고 %s에 %s 상품을 다시 보관하지 못해 바닥에 둡니다."),
				*ProductRecord.StorageActorName.ToString(),
				*ProductRow.RowName.ToString());
		}

		if (ProductRecord.bWorldDropPhysics)
		{
			Product->EnableWorldDropPhysics();
		}
	}
}

void UCSWorldStateSaveHandler::RestoreCrates(
	const FCSWorldSaveRecord& InRecord,
	const UDataTable* FallbackProductTable)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	TMap<FName, ACSFreshnessSafeZoneActor*> StoragesByName;
	CollectStoragesByName(StoragesByName);

	for (const FCSCrateRecord& CrateRecord : InRecord.Crates)
	{
		ACSDeliveryCrateActor* Crate =
			SpawnCrateFromRecord(*World, CrateRecord, CrateRecord.Transform, FallbackProductTable);
		if (!IsValid(Crate))
		{
			continue;
		}

		// 창고 보관 상자는 같은 창고의 빈 슬롯 또는 바닥 보관 위치에 다시 보관
		if (!CrateRecord.StorageActorName.IsNone())
		{
			ACSFreshnessSafeZoneActor** Storage = StoragesByName.Find(CrateRecord.StorageActorName);
			if (Storage != nullptr && (*Storage)->TryStoreCrate(Crate, true))
			{
				continue;
			}

			// 창고를 찾지 못하거나 가득 차면 저장 위치의 바닥 상자로 유지
			UE_LOG(LogTemp, Warning,
				TEXT("[CSSave] 창고 %s에 상자를 다시 보관하지 못해 바닥에 둡니다."),
				*CrateRecord.StorageActorName.ToString());
		}

		if (CrateRecord.bWorldDropPhysics)
		{
			Crate->EnableWorldDropPhysics();
		}
	}
}

ACSDeliveryCrateActor* UCSWorldStateSaveHandler::SpawnCrateFromRecord(
	UWorld& InWorld,
	const FCSCrateRecord& InRecord,
	const FTransform& InSpawnTransform,
	const UDataTable* FallbackProductTable)
{
	UClass* CrateClass = InRecord.ActorClass.TryLoadClass<ACSDeliveryCrateActor>();
	if (CrateClass == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CSSave] 상자 클래스를 불러오지 못했습니다: %s"),
			*InRecord.ActorClass.ToString());
		return nullptr;
	}

	const FDataTableRowHandle ProductRow = InRecord.ProductRow.ToHandle(FallbackProductTable);
	if (ProductRow.DataTable == nullptr || ProductRow.RowName.IsNone())
	{
		return nullptr;
	}

	ACSDeliveryCrateActor* Crate = InWorld.SpawnActorDeferred<ACSDeliveryCrateActor>(
		CrateClass,
		InSpawnTransform,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(Crate))
	{
		return nullptr;
	}

	// 발주 배송과 같은 순서로 상품·수량을 확정한 뒤 BeginPlay를 실행
	if (!Crate->InitializeCrate(ProductRow, FMath::Max(1, InRecord.RemainingQuantity)))
	{
		Crate->Destroy();
		return nullptr;
	}

	Crate->FinishSpawning(InSpawnTransform);
	if (!IsValid(Crate))
	{
		return nullptr;
	}

	// BeginPlay가 신선도를 초기화한 뒤 저장 시점 상태로 덮어씀
	Crate->RestoreSaveRecord(InRecord);
	return Crate;
}

void UCSWorldStateSaveHandler::RestoreFacilities(const FCSWorldSaveRecord& InRecord)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// 레벨 배치 시설은 식별자로 수집하고 런타임 스폰 시설은 제거 대상으로 분류
	TMap<FGuid, AActor*> FacilitiesBySaveId;
	TArray<AActor*> RuntimeSpawnedFacilities;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || !Actor->Implements<UCSBuildableFacility>() || Actor->IsA<ACSShelfActor>())
		{
			continue;
		}

		const UCSSaveIdComponent* SaveIdComponent = Actor->FindComponentByClass<UCSSaveIdComponent>();
		if (SaveIdComponent == nullptr)
		{
			continue;
		}

		if (SaveIdComponent->WasSpawnedAtRuntime())
		{
			RuntimeSpawnedFacilities.Add(Actor);
			continue;
		}

		if (SaveIdComponent->GetSaveId().IsValid())
		{
			FacilitiesBySaveId.Add(SaveIdComponent->GetSaveId(), Actor);
		}
	}

	for (AActor* Actor : RuntimeSpawnedFacilities)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}

	for (const FCSFacilityRecord& FacilityRecord : InRecord.Facilities)
	{
		AActor* TargetFacility = nullptr;
		if (AActor** Found = FacilitiesBySaveId.Find(FacilityRecord.SaveId))
		{
			TargetFacility = *Found;
		}
		else if (FacilityRecord.bWasSpawnedAtRuntime)
		{
			UClass* FacilityClass = FacilityRecord.ActorClass.TryLoadClass<AActor>();
			if (FacilityClass == nullptr)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[CSSave] 시설 클래스를 불러오지 못했습니다: %s"),
					*FacilityRecord.ActorClass.ToString());
				continue;
			}

			FActorSpawnParameters SpawnParameters;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			TargetFacility = World->SpawnActor<AActor>(FacilityClass, FacilityRecord.Transform, SpawnParameters);

			// 다시 생성한 시설이 저장 당시 식별자를 이어받도록 설정
			if (IsValid(TargetFacility))
			{
				if (UCSSaveIdComponent* SaveIdComponent = UCSSaveIdComponent::FindOrAddComponent(TargetFacility))
				{
					SaveIdComponent->OverrideSaveId(FacilityRecord.SaveId, true);
				}
			}
		}

		if (!IsValid(TargetFacility) || !TargetFacility->Implements<UCSBuildableFacility>())
		{
			continue;
		}

		if (FacilityRecord.bWasSpawnedAtRuntime)
		{
			TargetFacility->SetActorTransform(FacilityRecord.Transform);
		}

		// 설치 여부에 따라 기존 건축 경로를 실행해 물리·등록 상태를 맞춤
		if (FacilityRecord.bIsInstalled)
		{
			ICSBuildableFacility::Execute_FinalizeBuildPlacement(TargetFacility);
		}
		else
		{
			ICSBuildableFacility::Execute_InitializeAsUninstalledFacility(TargetFacility);
		}
	}
}

void UCSWorldStateSaveHandler::RestoreQuickbar(
	const FCSWorldSaveRecord& InRecord,
	const UDataTable* FallbackProductTable)
{
	if (ACSPlayerCharacter* PlayerCharacter = FindPlayerCharacter())
	{
		PlayerCharacter->RestoreQuickbarRecord(InRecord.Quickbar, FallbackProductTable);
	}
}

void UCSWorldStateSaveHandler::RestoreEnding(const UCSSaveGame& InSave)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	for (TActorIterator<ACSEndingManager> It(World); It; ++It)
	{
		if (ACSEndingManager* EndingManager = *It; IsValid(EndingManager))
		{
			EndingManager->RestoreSaveRecord(InSave.Ending);
			return;
		}
	}
}

// ---------------------------------------------------------------------------
// 헬퍼
// ---------------------------------------------------------------------------

ACSPlayerCharacter* UCSWorldStateSaveHandler::FindPlayerCharacter() const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	for (TActorIterator<ACSPlayerCharacter> It(World); It; ++It)
	{
		if (ACSPlayerCharacter* PlayerCharacter = *It; IsValid(PlayerCharacter))
		{
			return PlayerCharacter;
		}
	}

	return nullptr;
}

void UCSWorldStateSaveHandler::CollectStoragesByName(
	TMap<FName, ACSFreshnessSafeZoneActor*>& OutStorages) const
{
	OutStorages.Reset();
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	for (TActorIterator<ACSFreshnessSafeZoneActor> It(World); It; ++It)
	{
		if (ACSFreshnessSafeZoneActor* Storage = *It; IsValid(Storage))
		{
			OutStorages.Add(Storage->GetFName(), Storage);
		}
	}
}

const UDataTable* UCSWorldStateSaveHandler::FindFallbackProductTable() const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	// 월드 상품이 실제로 참조하는 테이블을 우선 사용
	for (TActorIterator<ACSProductPickupActor> It(World); It; ++It)
	{
		const ACSProductPickupActor* Product = *It;
		if (IsValid(Product) && Product->GetProductRow().DataTable != nullptr)
		{
			return Product->GetProductRow().DataTable.Get();
		}
	}

	// 상품이 없으면 진열대 단 설정에서 조회
	for (TActorIterator<ACSShelfActor> It(World); It; ++It)
	{
		const ACSShelfActor* Shelf = *It;
		if (!IsValid(Shelf))
		{
			continue;
		}

		for (const FCSShelfLevelConfiguration& Configuration : Shelf->GetLevelConfigurations())
		{
			if (Configuration.ProductRow.DataTable != nullptr)
			{
				return Configuration.ProductRow.DataTable.Get();
			}
		}
	}

	return nullptr;
}
