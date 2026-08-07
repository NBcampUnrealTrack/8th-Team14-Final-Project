#include "Systems/Inventory/CSStoreStockSubsystem.h"

#include "EngineUtils.h"
#include "Features/Delivery/CSDeliveryCrateActor.h"
#include "Features/Interaction/CSShelfPlaceable.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Features/Items/CSProductPlacementPreviewActor.h"

namespace
{
void AddStockQuantity(int32& Target, int32 Amount, FName ProductId)
{
	const int64 NewQuantity = static_cast<int64>(Target) + Amount;
	if (NewQuantity > MAX_int32)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StoreStockSubsystem] %s 상품의 재고 수량이 int32 범위를 초과했습니다."),
			*ProductId.ToString());
		Target = MAX_int32;
		return;
	}

	Target = static_cast<int32>(NewQuantity);
}

void UpdateTotalQuantity(FCSProductStockInfo& StockInfo)
{
	const int64 TotalQuantity =
		static_cast<int64>(StockInfo.CrateQuantity) +
		StockInfo.ShelfQuantity +
		StockInfo.LooseQuantity;
	if (TotalQuantity > MAX_int32)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StoreStockSubsystem] %s 상품의 총 재고 수량이 int32 범위를 초과했습니다."),
			*StockInfo.ProductId.ToString());
		StockInfo.TotalQuantity = MAX_int32;
		return;
	}

	StockInfo.TotalQuantity = static_cast<int32>(TotalQuantity);
}
}

FCSProductStockInfo UCSStoreStockSubsystem::GetProductStock(FName ProductId) const
{
	FCSProductStockInfo StockInfo;
	if (ProductId.IsNone())
	{
		return StockInfo;
	}

	StockInfo.ProductId = ProductId;
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return StockInfo;
	}

	for (TActorIterator<ACSDeliveryCrateActor> It(World); It; ++It)
	{
		const ACSDeliveryCrateActor* Crate = *It;
		if (IsValid(Crate) && Crate->GetProductId() == ProductId)
		{
			AddStockQuantity(
				StockInfo.CrateQuantity,
				Crate->GetRemainingQuantity(),
				ProductId);
		}
	}

	for (TActorIterator<ACSProductPickupActor> It(World); It; ++It)
	{
		const ACSProductPickupActor* Product = *It;
		if (!IsValid(Product) ||
			Product->IsA<ACSProductPlacementPreviewActor>() ||
			Product->GetProductId() != ProductId)
		{
			continue;
		}

		const AActor* AttachParent = Product->GetAttachParentActor();
		if (IsValid(AttachParent) && AttachParent->Implements<UCSShelfPlaceable>())
		{
			AddStockQuantity(StockInfo.ShelfQuantity, 1, ProductId);
		}
		else
		{
			AddStockQuantity(StockInfo.LooseQuantity, 1, ProductId);
		}
	}

	UpdateTotalQuantity(StockInfo);
	return StockInfo;
}

TArray<FCSProductStockInfo> UCSStoreStockSubsystem::GetAllProductStock() const
{
	TArray<FCSProductStockInfo> Result;
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return Result;
	}

	TMap<FName, FCSProductStockInfo> StockByProductId;
	for (TActorIterator<ACSDeliveryCrateActor> It(World); It; ++It)
	{
		const ACSDeliveryCrateActor* Crate = *It;
		if (!IsValid(Crate))
		{
			continue;
		}

		const FName ProductId = Crate->GetProductId();
		if (ProductId.IsNone())
		{
			continue;
		}

		FCSProductStockInfo& StockInfo = StockByProductId.FindOrAdd(ProductId);
		StockInfo.ProductId = ProductId;
		AddStockQuantity(
			StockInfo.CrateQuantity,
			Crate->GetRemainingQuantity(),
			ProductId);
	}

	for (TActorIterator<ACSProductPickupActor> It(World); It; ++It)
	{
		const ACSProductPickupActor* Product = *It;
		if (!IsValid(Product) ||
			Product->IsA<ACSProductPlacementPreviewActor>())
		{
			continue;
		}

		const FName ProductId = Product->GetProductId();
		if (ProductId.IsNone())
		{
			continue;
		}

		FCSProductStockInfo& StockInfo = StockByProductId.FindOrAdd(ProductId);
		StockInfo.ProductId = ProductId;
		const AActor* AttachParent = Product->GetAttachParentActor();
		if (IsValid(AttachParent) && AttachParent->Implements<UCSShelfPlaceable>())
		{
			AddStockQuantity(StockInfo.ShelfQuantity, 1, ProductId);
		}
		else
		{
			AddStockQuantity(StockInfo.LooseQuantity, 1, ProductId);
		}
	}

	StockByProductId.GenerateValueArray(Result);
	for (FCSProductStockInfo& StockInfo : Result)
	{
		UpdateTotalQuantity(StockInfo);
	}
	Result.Sort([](const FCSProductStockInfo& Left, const FCSProductStockInfo& Right)
	{
		return Left.ProductId.ToString() < Right.ProductId.ToString();
	});

	return Result;
}
