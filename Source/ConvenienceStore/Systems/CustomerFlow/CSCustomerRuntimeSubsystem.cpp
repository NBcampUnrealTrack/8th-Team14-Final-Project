// Fill out your copyright notice in the Description page of Project Settings.


#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "Features/Customers/Interface/CSCustomerCounterInterface.h"
#include "Features/Customers/CSCustomerCharacter.h"
#include "Features/Customers/CSCustomerStateComponent.h"
#include "Data/CSFreshnessSettingsDataAsset.h"
#include "Data/CSProductTypes.h"
#include "EngineUtils.h"
#include "Components/ArrowComponent.h"
#include "NavigationSystem.h"
// 아래 3개는 팀원 파일에 따라 수정해야함
#include "Features/Shelves/CSShelfActor.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Features/Interaction/CSShelfPlaceable.h"
#include "Features/Trash/CSTrashActor.h"
#include "Features/Store/CSStoreInteriorActor.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/CapsuleComponent.h"
#include "Systems/Pricing/CSStorePricingSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Features/Customers/CSPurchaseDecisionComponent.h"
#include "Data/CSCustomerArchetypeDataAsset.h"
#include "Engine/Engine.h"
#include "Features/Customers/CSInnerThoughtWidget.h"
#include "Blueprint/UserWidget.h"
#include "TimerManager.h"

TAutoConsoleVariable<int32> CVarCS_FixedSeed(
	TEXT("cs.Seed"),
	-1,
	TEXT("0 이상이면 이 값을 손님 관련 굴림(종족 배정/예산 추첨/진열대 선택)의 고정 시드로 사용. -1(기본값)이면 매번 다른 시드"),
	ECVF_Default);

UCSCustomerRuntimeSubsystem::UCSCustomerRuntimeSubsystem()
{
	// UWorldSubsystem은 디테일 패널이 없어 TrashActorClass를 에디터에서 지정할 수 없으므로, 여기서 BP_Trash를 기본값으로 채운다.
	static ConstructorHelpers::FClassFinder<AActor> TrashActorClassFinder(
		TEXT("/Game/ConvenienceStore/Feature/Trash/BP_Trash"));
	if (TrashActorClassFinder.Succeeded())
	{
		TrashActorClass = TrashActorClassFinder.Class;
	}
}

void UCSCustomerRuntimeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 이 서브시스템이 살아있는 동안 한 번만 시드를 잡음
	const int32 FixedSeed = CVarCS_FixedSeed.GetValueOnGameThread();
	ShelfRandomStream.Initialize(FixedSeed >= 0 ? FixedSeed : FMath::Rand());

	// ShelfRandomStream과 같은 FixedSeed 값을 쓰지만 별개의 객체라 시퀀스가 서로 얽히지 않는다
	TrashWitnessRollStream.Initialize(FixedSeed >= 0 ? FixedSeed : FMath::Rand());

	// 시드 초기화와 같은 "PIE 시작 시 한 번"으로 묶는다 - 손님 스폰은 이보다 뒤에 일어난다
	LoadAndValidateCategoryAffinityTable();

	RefreshShelves();//레벨 시작할때 레벨에 있는 진열대를 바로 등록 (만약 타이쿤 시스템 도입시 생각)

	// 매장 실내 영역도 진열대와 마찬가지로 레벨 시작 시 한 번만 찾아 캐싱한다(정적 마커라 재탐색 불필요)
	CachedStoreInteriorActor = nullptr;
	for (TActorIterator<ACSStoreInteriorActor> It(&InWorld); It; ++It)
	{
		CachedStoreInteriorActor = *It;
		break;
	}

	if (!CachedStoreInteriorActor.IsValid())
	{
		// 레벨에 배치가 안 되어 있으면 청결도 판정이 월드 전체 쓰레기를 세는 것으로 폴백된다 -
		// 매장 밖 쓰레기까지 섞여 들어갈 수 있으니 레벨 설정 누락을 바로 알아챌 수 있게 경고로 남긴다
		UE_LOG(LogTemp, Warning, TEXT("[청결도] 레벨에 ACSStoreInteriorActor가 없어 쓰레기를 매장 안으로 한정하지 못하고 월드 전체를 셉니다"));
	}
}

void UCSCustomerRuntimeSubsystem::RefreshShelves()
{
	Shelves.Reset();

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// 진열대가 스스로 등록하는 방식이 아니라 여기서 찾아서 수집한다.
	for (TActorIterator<ACSShelfActor> It(World); It; ++It)
	{
		RegisterShelf(*It);
	}
}

void UCSCustomerRuntimeSubsystem::RegisterShelf(ACSShelfActor* Shelf)
{
	Shelves.RemoveAll([](const TObjectPtr<AActor>& RegisteredShelf)
	{
		return !IsValid(RegisteredShelf);
	});

	if (!IsValid(Shelf) ||
		Shelf->IsActorBeingDestroyed() ||
		!ICSBuildableFacility::Execute_IsFacilityInstalled(Shelf))
	{
		return;
	}

	Shelves.AddUnique(Shelf);
}

void UCSCustomerRuntimeSubsystem::UnregisterShelf(ACSShelfActor* Shelf)
{
	Shelves.RemoveAll([Shelf](const TObjectPtr<AActor>& RegisteredShelf)
	{
		return !IsValid(RegisteredShelf) || RegisteredShelf.Get() == Shelf;
	});
}

const TArray<TObjectPtr<AActor>>& UCSCustomerRuntimeSubsystem::GetShelves() const
{
	return Shelves;
}

bool UCSCustomerRuntimeSubsystem::TryGetShelfBrowsePoint(const AActor* ShelfActor,FVector& OutLocation,FRotator& OutFacing,float& OutRadius) const
{
	OutLocation = FVector::ZeroVector;
	OutFacing = FRotator::ZeroRotator;
	OutRadius = 0.0f;

	const ACSShelfActor* Shelf = Cast<ACSShelfActor>(ShelfActor);
	if (Shelf == nullptr)
	{
		return false;
	}

	return Shelf->TryGetBrowseZone(OutLocation, OutFacing, OutRadius);
}

bool UCSCustomerRuntimeSubsystem::IsCustomerInsideBrowseZone(const AActor* ShelfActor,
	const AActor* Customer) const
{
	const ACSShelfActor* Shelf = Cast<ACSShelfActor>(ShelfActor);
	if (Shelf == nullptr || !IsValid(Customer))
	{
		return false;
	}

	return Shelf->IsPointInsideBrowseZone(Customer->GetActorLocation());
}

bool UCSCustomerRuntimeSubsystem::IsShelfSelectable(const AActor* ShelfActor) const
{
	// 접근 영역 유효성은 여기서 보지 않습니다.
	// PickNextShelf가 TryGetShelfBrowsePoint 실패 시 MarkVisited 후 Failed로 끝내므로
	// 못 갈 진열대가 다시 뽑히는 일이 없고, 그쪽에서 원인 로그도 남습니다.
	// 여기서 미리 거르면 그 로그가 영영 안 찍히고, 이 함수가 StateTree 조건에서
	// 매번 호출되는 만큼 진열대 수 × 손님 수만큼 NavMesh 투영이 발생합니다.
	return IsValid(ShelfActor) &&
		ShelfActor->Implements<UCSBuildableFacility>() &&
		ICSBuildableFacility::Execute_IsFacilityInstalled(ShelfActor);
}

// 계획이 없거나 계획에 맞는 진열대를 찾지 못했을 때의 폴백. 짝 대상 카테고리를 우선하지 않는
// 버전이 필요한 호출부를 위해 남겨두고, 아래 오버로드에 빈 집합을 넘겨 그대로 위임한다
AActor* UCSCustomerRuntimeSubsystem::PickRandomUnvisitedShelf(const TArray<TObjectPtr<AActor>>& Visited) const
{
	return PickRandomUnvisitedShelf(Visited, TSet<ECSProductCategory>(), TMap<ECSProductCategory, float>());
}

AActor* UCSCustomerRuntimeSubsystem::PickRandomUnvisitedShelf(
	const TArray<TObjectPtr<AActor>>& Visited,
	const TSet<ECSProductCategory>& AffinityTargetCategories,
	const TMap<ECSProductCategory, float>& CategoryAttractionWeights,
	const ACSCustomerCharacter* Customer) const
{
	//방문하지 않은 선택 가능 진열대 배열 - 아래 짝 진열대 목록은 반드시 이 목록의 부분집합이어야 한다
	// (따로 만들면 "짝 목록은 있는데 전체 목록은 0개"인 경우가 생겨 굴림 횟수가 달라진다)
	TArray<AActor*> Candidates;

	for (int32 i = 0; i < Shelves.Num(); i++)
	{
		AActor* Shelf = Shelves[i];
		if (!IsShelfSelectable(Shelf))
		{
			continue;
		}

		// 현재 선반이 이미 방문한 목록(Visited)에 포함되어 있는지 확인
		bool bVisited = false;
		for (int32 j = 0; j < Visited.Num(); j++)
		{
			if (Visited[j] == Shelf)
			{
				bVisited = true;
				break;
			}
		}

		// 방문한 적이 없는 진열대라면 후보군에 추가
		if (bVisited == false)
		{
			Candidates.Add(Shelf);
		}
	}

	// 조건에 맞는 진열대가 하나도 없다면 굴리지 않고 nullptr 반환
	// (여기서 반환하지 않으면 아래에서 빈 배열에 RandRange를 굴려 공용 스트림 순서가 깨진다)
	if (Candidates.Num() == 0)
	{
		return nullptr;
	}

	// 짝 대상 카테고리가 있으면 위 목록 안에서만(딱 이 부분집합에서만) 짝 진열대를 추린다
	// GetProductsOnShelf 결과 중 카테고리가 대상 집합에 속하는 상품이 하나라도 있으면 짝 진열대다 -
	// 가격, 강도, 차단은 여기서 보지 않는다
	TArray<AActor*> AffinityCandidates;
	if (AffinityTargetCategories.Num() > 0)
	{
		for (AActor* Shelf : Candidates)
		{
			for (const AActor* ProductActor : GetProductsOnShelf(Shelf))
			{
				const ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(ProductActor);
				const FCSProductDataRow* ProductRow = Product != nullptr
					? Product->GetProductRow().GetRow<FCSProductDataRow>(TEXT("UCSCustomerRuntimeSubsystem::PickRandomUnvisitedShelf(Affinity)"))
					: nullptr;
				if (ProductRow != nullptr && AffinityTargetCategories.Contains(ProductRow->Category))
				{
					AffinityCandidates.Add(Shelf);
					break;
				}
			}
		}
	}

	// 짝 진열대가 1개 이상이면 그 목록에서, 아니면 전체 목록에서 - 어느 경우든 굴림은 정확히 1회
	const bool bPickedFromAffinity = AffinityCandidates.Num() > 0;
	const TArray<AActor*>& FinalCandidates = bPickedFromAffinity ? AffinityCandidates : Candidates;

	// 위에서 정해진 후보 목록 안에서, 카테고리 선호 가중치가 있으면 균등이 아니라 그 가중치로 뽑는다
	int32 PickIndex = 0;
	if (CategoryAttractionWeights.Num() > 0)
	{
		constexpr float NeutralShelfAttractionWeight = 0.5f;

		TArray<float> ShelfWeights;
		ShelfWeights.Reserve(FinalCandidates.Num());
		for (AActor* Shelf : FinalCandidates)
		{
			float BestWeight = -1.0f;
			for (const AActor* ProductActor : GetProductsOnShelf(Shelf))
			{
				const ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(ProductActor);
				const FCSProductDataRow* ProductRow = Product != nullptr
					? Product->GetProductRow().GetRow<FCSProductDataRow>(TEXT("UCSCustomerRuntimeSubsystem::PickRandomUnvisitedShelf(Attraction)"))
					: nullptr;
				if (ProductRow == nullptr)
				{
					continue;
				}
				if (const float* Weight = CategoryAttractionWeights.Find(ProductRow->Category))
				{
					BestWeight = FMath::Max(BestWeight, *Weight);
				}
			}
			ShelfWeights.Add(BestWeight >= 0.0f ? BestWeight : NeutralShelfAttractionWeight);
		}

		float TotalWeight = 0.0f;
		for (const float Weight : ShelfWeights)
		{
			TotalWeight += FMath::Max(Weight, 0.0f);
		}

		if (TotalWeight > 0.0f)
		{
			const float Roll = ShelfRandomStream.FRandRange(0.0f, TotalWeight);
			float Cumulative = 0.0f;
			PickIndex = ShelfWeights.Num() - 1; // 부동소수점 오차로 어느 구간에도 안 걸렸을 때의 폴백
			for (int32 Index = 0; Index < ShelfWeights.Num(); ++Index)
			{
				Cumulative += FMath::Max(ShelfWeights[Index], 0.0f);
				if (Roll <= Cumulative)
				{
					PickIndex = Index;
					break;
				}
			}
		}
		else
		{
			// 후보 전원이 차단 카테고리뿐인 극단 상황 - 균등 폴백(굴림은 여전히 1회)
			PickIndex = ShelfRandomStream.RandRange(0, FinalCandidates.Num() - 1);
		}
	}
	else
	{
		PickIndex = ShelfRandomStream.RandRange(0, FinalCandidates.Num() - 1);
	}

	AActor* Picked = FinalCandidates[PickIndex];

	return Picked;
}

// [임시/테스트용]
bool UCSCustomerRuntimeSubsystem::HasUnvisitedShelf(const TArray<TObjectPtr<AActor>>& Visited) const
{
	for (int32 i = 0; i < Shelves.Num(); i++)
	{
		AActor* Shelf = Shelves[i];
		// PickRandomUnvisitedShelf와 후보 조건이 어긋나면
		// "갈 곳이 있다"고 판정해놓고 정작 못 뽑는 상태가 되므로 같은 함수를 씁니다.
		if (!IsShelfSelectable(Shelf))
		{
			continue;
		}

		bool bVisited = false;
		for (int32 j = 0; j < Visited.Num(); j++)
		{
			if (Visited[j] == Shelf)
			{
				bVisited = true;
				break;
			}
		}

		// 방문하지 않은 진열대를 하나라도 발견하면 즉시 true를 반환합니다.
		if (bVisited == false)
		{
			return true;
		}
	}
	// 모든 진열대를 순회했으나 전부 Visited에 포함되어 있다면 false를 반환합니다.
	return false;
}

TArray<AActor*> UCSCustomerRuntimeSubsystem::GetProductsOnShelf(const AActor* ShelfActor) const
{
	TArray<AActor*> Result;

	if (ShelfActor == nullptr)
	{
		return Result;
	}

	// 팀원 설계: TryPlaceItem이 상품을 진열대에 Attach함 → Attach된 상품 = 진열된 재고
	// 진열대 액터에 부착(Attached)된 모든 자식 액터를 가져옴
	TArray<AActor*> Attached;
	ShelfActor->GetAttachedActors(Attached);

	// 부착된 액터들 중 상품인 것만 캐스팅하여 필터링
	for (int32 i = 0; i < Attached.Num(); i++)
	{
		ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(Attached[i]);
		if (Product != nullptr && Product->IsSellable())
		{
			Result.Add(Product);

			// 진열 이력을 여기서 모은다. 진열대를 훑는 모든 경로가 이 함수를 지나므로
			// 다른 파트(진열·발주)에 손대지 않고도 "이 매장이 취급하는 상품"을 알 수 있다
			ProductsEverStocked.Add(Product->GetProductId());
		}
	}

	return Result;
}

bool UCSCustomerRuntimeSubsystem::WasProductEverStocked(FName ProductRowName) const
{
	return ProductsEverStocked.Contains(ProductRowName);
}

TArray<FCSPurchaseCandidate> UCSCustomerRuntimeSubsystem::BuildPurchaseCandidates(const AActor* ShelfActor) const
{
	TArray<FCSPurchaseCandidate> Candidates;
	// 기존 GetProductsOnShelf()를 그대로 재사용해서 진열된 상품 액터들을 가져옴
	TArray<AActor*> Products = GetProductsOnShelf(ShelfActor);

	// 같은 SKU(ProductId)가 물리적으로 여러 개 진열돼 있어도 후보는 하나만 생성
	// (안 그러면 진열 개수가 많은 상품이 TopCandidateCount 컷을 통째로 독점함)
	// 반복 구매 시 점수를 깎는 건 이미 있는 RepeatCategoryPenalty가 담당하므로 여기선 중복 제거만 함
	TSet<FName> SeenProductIds;

	for (AActor* ProductActor : Products)
	{
		ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(ProductActor);
		if (Product == nullptr) continue;  // 상품이 아닌 액터 방어

		const FName ProductId = Product->GetProductId();
		if (SeenProductIds.Contains(ProductId))
		{
			continue;  // 같은 SKU는 먼저 발견된 액터 하나만 대표로 남김
		}
		SeenProductIds.Add(ProductId);

		// TakeProduct()가 쓰는 것과 동일한 GetProductRow()로 상품 마스터 데이터 참조를 가져옴
		FCSPurchaseCandidate Candidate;
		Candidate.ProductRowHandle = Product->GetProductRow();
		Candidate.ProductActor = ProductActor;
		Candidates.Add(Candidate);
	}
	return Candidates;
}

bool UCSCustomerRuntimeSubsystem::HasShelfStock(const AActor* ShelfActor) const
{
	return GetProductsOnShelf(ShelfActor).Num() > 0;
}

// 캡슐화를 위해 OnPurchaseRejected를 직접 Broadcast하지 않고 이 래퍼를 거치게 함
void UCSCustomerRuntimeSubsystem::NotifyPurchaseRejected(ACSCustomerCharacter* Customer, const FCSPurchaseRejectionInfo& Info)
{
	if (Customer == nullptr)
	{
		return;
	}

	OnPurchaseRejected.Broadcast(Customer, Info);
}

// =====================================================================
// 카테고리 연관구매
// =====================================================================

void UCSCustomerRuntimeSubsystem::LoadAndValidateCategoryAffinityTable()
{
	ValidatedAffinityRows.Reset();

	const UDataTable* AffinityTable = CategoryAffinityDataTableAsset.LoadSynchronous();
	if (!IsValid(AffinityTable))
	{
		UE_LOG(LogTemp, Warning, TEXT("[연관구매] DT_CategoryAffinity를 찾지 못했습니다: %s"),
			*CategoryAffinityDataTableAsset.ToSoftObjectPath().ToString());
	}
	else if (AffinityTable->GetRowStruct() != FCSCategoryAffinityRow::StaticStruct())
	{
		UE_LOG(LogTemp, Warning, TEXT("[연관구매] DT_CategoryAffinity의 행 구조체가 FCSCategoryAffinityRow가 아닙니다: %s"),
			AffinityTable->GetRowStruct() != nullptr ? *AffinityTable->GetRowStruct()->GetName() : TEXT("None"));
	}
	else
	{
		int32 ExcludedCount = 0;
		TArray<FCSCategoryAffinityRow*> Rows;
		AffinityTable->GetAllRows(TEXT("UCSCustomerRuntimeSubsystem::LoadAndValidateCategoryAffinityTable"), Rows);

		for (const FCSCategoryAffinityRow* Row : Rows)
		{
			if (Row == nullptr)
			{
				continue;
			}

			// 원본 DT 행(에디터가 들고 있는 실제 메모리)을 직접 고치지 않도록 복사본에서 검증·클램프한다
			FCSCategoryAffinityRow Candidate = *Row;

			if (Candidate.AnchorCategory == Candidate.TargetCategory)
			{
				// 반복 페널티(RepeatCategoryPenalty)와 정면으로 충돌하는 조합이라 제외
				UE_LOG(LogTemp, Warning, TEXT("[연관구매] Anchor와 Target이 같은 행을 제외했습니다: %s"),
					*UEnum::GetDisplayValueAsText(Candidate.AnchorCategory).ToString());
				ExcludedCount++;
				continue;
			}

			if (Candidate.Strength <= 0.0f)
			{
				// 의도적으로 끈 쌍 - 경고가 아니라 일반 로그
				UE_LOG(LogTemp, Log, TEXT("[연관구매] Strength<=0인 행을 제외했습니다: %s → %s"),
					*UEnum::GetDisplayValueAsText(Candidate.AnchorCategory).ToString(),
					*UEnum::GetDisplayValueAsText(Candidate.TargetCategory).ToString());
				ExcludedCount++;
				continue;
			}

			if (Candidate.Strength > 1.0f)
			{
				UE_LOG(LogTemp, Warning, TEXT("[연관구매] Strength>1인 행을 1로 클램프했습니다: %s → %s (%.2f)"),
					*UEnum::GetDisplayValueAsText(Candidate.AnchorCategory).ToString(),
					*UEnum::GetDisplayValueAsText(Candidate.TargetCategory).ToString(), Candidate.Strength);
				Candidate.Strength = 1.0f;
			}

			const bool bDuplicate = ValidatedAffinityRows.ContainsByPredicate(
				[&Candidate](const FCSCategoryAffinityRow& Existing)
				{
					return Existing.AnchorCategory == Candidate.AnchorCategory && Existing.TargetCategory == Candidate.TargetCategory;
				});
			if (bDuplicate)
			{
				UE_LOG(LogTemp, Warning, TEXT("[연관구매] 중복된 Anchor→Target 행을 제외했습니다: %s → %s"),
					*UEnum::GetDisplayValueAsText(Candidate.AnchorCategory).ToString(),
					*UEnum::GetDisplayValueAsText(Candidate.TargetCategory).ToString());
				ExcludedCount++;
				continue;
			}

			ValidatedAffinityRows.Add(Candidate);
		}

		UE_LOG(LogTemp, Log, TEXT("[연관구매] 유효 쌍=%d 제외=%d"),
			ValidatedAffinityRows.Num(), ExcludedCount);
		return;
	}

	// DT를 못 찾았거나 행 구조체가 다른 경우에도 결과(0쌍)는 로그로 남긴다
	UE_LOG(LogTemp, Log, TEXT("[연관구매] 유효 쌍=%d"), ValidatedAffinityRows.Num());
}

TMap<ECSProductCategory, int32> UCSCustomerRuntimeSubsystem::CountCategoriesInBasket(const TArray<FCSSelectProduct>& SelectProducts) const
{
	TMap<ECSProductCategory, int32> Counts;
	for (const FCSSelectProduct& Selected : SelectProducts)
	{
		const FCSProductDataRow* Row = Selected.ProductRow.GetRow<FCSProductDataRow>(
			TEXT("UCSCustomerRuntimeSubsystem::CountCategoriesInBasket"));
		if (Row == nullptr)
		{
			continue;
		}
		Counts.FindOrAdd(Row->Category)++;
	}
	return Counts;
}

TArray<float> UCSCustomerRuntimeSubsystem::GetAffinityStrengthsForTarget(
	ECSProductCategory TargetCategory,
	const TArray<FCSSelectProduct>& SelectProducts,
	FString* OutAnchorSummary) const
{
	TArray<float> Strengths;
	if (OutAnchorSummary != nullptr)
	{
		OutAnchorSummary->Reset();
	}

	if (ValidatedAffinityRows.IsEmpty())
	{
		return Strengths;
	}

	const TMap<ECSProductCategory, int32> BasketCounts = CountCategoriesInBasket(SelectProducts);
	TArray<FString> AnchorSummaryParts;

	for (const FCSCategoryAffinityRow& Row : ValidatedAffinityRows)
	{
		if (Row.TargetCategory != TargetCategory)
		{
			continue;
		}

		const int32* AnchorCount = BasketCounts.Find(Row.AnchorCategory);
		if (AnchorCount == nullptr || *AnchorCount <= 0)
		{
			continue;
		}

		for (int32 i = 0; i < *AnchorCount; i++)
		{
			Strengths.Add(Row.Strength);
		}

		if (OutAnchorSummary != nullptr)
		{
			AnchorSummaryParts.Add(FString::Printf(TEXT("%s×%d"),
				*UEnum::GetDisplayValueAsText(Row.AnchorCategory).ToString(), *AnchorCount));
		}
	}

	Strengths.Sort([](const float A, const float B) { return A > B; });

	if (OutAnchorSummary != nullptr && !AnchorSummaryParts.IsEmpty())
	{
		*OutAnchorSummary = FString::Join(AnchorSummaryParts, TEXT(", "));
	}

	return Strengths;
}

TSet<ECSProductCategory> UCSCustomerRuntimeSubsystem::GetAffinityTargetCategories(const TArray<FCSSelectProduct>& SelectProducts) const
{
	TSet<ECSProductCategory> Targets;
	if (ValidatedAffinityRows.IsEmpty())
	{
		return Targets;
	}

	const TMap<ECSProductCategory, int32> BasketCounts = CountCategoriesInBasket(SelectProducts);
	for (const FCSCategoryAffinityRow& Row : ValidatedAffinityRows)
	{
		if (BasketCounts.Contains(Row.AnchorCategory))
		{
			Targets.Add(Row.TargetCategory);
		}
	}
	return Targets;
}

void UCSCustomerRuntimeSubsystem::SpawnTrashNearCustomer(ACSCustomerCharacter* Customer)
{
	if (!IsValid(Customer))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// Customer->GetActorLocation()은 캡슐 중심이라 그대로 스폰하면 공중에 뜬 채로 보인다.
	// 캡슐 반높이만큼 내려서 바닥 근처에서 시작시키고, 남은 오차는 아래 물리 낙하가 보정한다.
	FVector SpawnLocation = Customer->GetActorLocation();
	if (const UCapsuleComponent* Capsule = Customer->GetCapsuleComponent())
	{
		SpawnLocation.Z -= Capsule->GetScaledCapsuleHalfHeight();
	}

	TSubclassOf<AActor> SpawnClass = TrashActorClass;
	if (SpawnClass == nullptr)
	{
		SpawnClass = ACSTrashActor::StaticClass();
	}

	const FTransform SpawnTransform(FRotator::ZeroRotator, SpawnLocation);
	AActor* SpawnedActor = World->SpawnActor<AActor>(SpawnClass, SpawnTransform);

	// 캐시에는 실제로 청결도 판정에 쓰이는 ACSTrashActor(파생 포함)만 등록한다
	if (ACSTrashActor* SpawnedTrash = Cast<ACSTrashActor>(SpawnedActor))
	{
		// 물리 낙하 + Pawn 충돌 무시 + 내비메시 영향 제거를 여기서 한 번에 정리한다
		// (레벨에 미리 배치된 튜토리얼용 인스턴스는 그대로 두고, 런타임 스폰 인스턴스만 손댐)
		SpawnedTrash->PrepareForCustomerDrop();
		DroppedTrashCache.Add(SpawnedTrash);
	}
}

bool UCSCustomerRuntimeSubsystem::IsTrashNearby(const FVector& Location, float Radius) const
{
	// 플레이어가 주워서 이미 파괴된 항목을 검사 시점에 걸러낸다.
	DroppedTrashCache.RemoveAll([](const TWeakObjectPtr<ACSTrashActor>& Trash)
	{
		return !Trash.IsValid();
	});

	const float RadiusSquared = FMath::Square(Radius);
	for (const TWeakObjectPtr<ACSTrashActor>& Trash : DroppedTrashCache)
	{
		const ACSTrashActor* TrashActor = Trash.Get();
		if (TrashActor != nullptr &&
			FVector::DistSquared(TrashActor->GetActorLocation(), Location) <= RadiusSquared)
		{
			return true;
		}
	}

	return false;
}

bool UCSCustomerRuntimeSubsystem::RollTrashWitnessed() const
{
	return TrashWitnessRollStream.FRand() < TrashWitnessChance;
}

void UCSCustomerRuntimeSubsystem::NotifyTrashWitnessed(ACSCustomerCharacter* Customer)
{
	if (Customer == nullptr)
	{
		return;
	}

	OnTrashWitnessed.Broadcast(Customer);
}

// 화면 하단 대화창 스타일 위젯 인스턴스 하나를 캐싱해서 재사용
void UCSCustomerRuntimeSubsystem::ShowInnerThought(TSubclassOf<UCSInnerThoughtWidget> WidgetClass, const FText& InText, float Duration)
{
	if (!WidgetClass)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// 캐싱된 인스턴스가 없거나(최초 호출), 클래스가 바뀌었으면 새로 만들어 뷰포트에 추가
	if (!IsValid(CachedInnerThoughtWidget) || CachedInnerThoughtWidget->GetClass() != WidgetClass)
	{
		CachedInnerThoughtWidget = CreateWidget<UCSInnerThoughtWidget>(World, WidgetClass);
		if (IsValid(CachedInnerThoughtWidget))
		{
			CachedInnerThoughtWidget->AddToViewport();
		}
	}

	if (!IsValid(CachedInnerThoughtWidget))
	{
		return;
	}

	CachedInnerThoughtWidget->SetThoughtText(InText);
	CachedInnerThoughtWidget->SetVisibility(ESlateVisibility::Visible);

	World->GetTimerManager().SetTimer(
		InnerThoughtHideTimerHandle,
		this,
		&UCSCustomerRuntimeSubsystem::HideInnerThought,
		FMath::Max(Duration, 0.01f),
		false);
}

void UCSCustomerRuntimeSubsystem::HideInnerThought()
{
	// 뷰포트에서 완전히 제거하지 않고 숨기기만 함 - 다음 ShowInnerThought() 호출 때
	// 같은 인스턴스를 그대로 재사용하기 위함(CreateWidget/AddToViewport 재호출 비용 절약)
	if (IsValid(CachedInnerThoughtWidget))
	{
		CachedInnerThoughtWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

FName UCSCustomerRuntimeSubsystem::TakeProduct(AActor* ShelfActor, AActor* ProductActor, ACSCustomerCharacter* Customer)
{
	ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(ProductActor);
	if (ShelfActor == nullptr || Product == nullptr || Customer == nullptr)
	{
		return NAME_None;
	}

	if (!Product->IsSellable())
	{
		return NAME_None;
	}

	// ProductId는 DT_ProductMaster에서 ProductRow가 가리키는 RowName입니다.
	const FName ProductId = Product->GetProductId();
	if (ProductId.IsNone())
	{
		return NAME_None;
	}

	UCSCustomerStateComponent* CustomerState = Customer->GetCustomerState();
	if (CustomerState == nullptr)
	{
		return NAME_None;
	}

	const FDataTableRowHandle ProductRow = Product->GetProductRow();
	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("UCSCustomerRuntimeSubsystem::TakeProduct"));
	if (ProductData == nullptr)
	{
		return NAME_None;
	}

	const UCSStorePricingSubsystem* PricingSubsystem =
		GetWorld() != nullptr
			? GetWorld()->GetSubsystem<UCSStorePricingSubsystem>()
			: nullptr;
	const int32 SalePriceAtPickup = IsValid(PricingSubsystem)
		? PricingSubsystem->GetSalePrice(ProductRow)
		: ProductData->RecommendedSalePrice;

	// 진열대의 슬롯 점유를 해제합니다. (플레이어가 진열된 물건을 다시 집을 때와 동일한 규약 유지)
	// 이 처리를 누락하면 물리적 상품 액터는 사라져도, 논리적으로 슬롯이 차 있다고 인식되어 재진열이 불가해집니다.
	// 이 액터가 진열대 규약을 구현했으면, 그 진열대한테 '이 상품 치웠으니 슬롯 비워' 라고 알리는 기능 가져옴
	if (ShelfActor->Implements<UCSShelfPlaceable>())
	{
		ICSShelfPlaceable::Execute_RemovePlacedItem(ShelfActor, Product);
	}

	FCSProductRuntimeState CustomerProductRuntimeState =
		Product->GetProductRuntimeState();
	UWorld* World = GetWorld();
	const ACSStoreGameState* StoreGameState =
		IsValid(World) ? World->GetGameState<ACSStoreGameState>() : nullptr;
	const UCSFreshnessSettingsDataAsset* FreshnessSettings =
		IsValid(StoreGameState) ? StoreGameState->GetFreshnessSettings() : nullptr;
	if (IsValid(World) && IsValid(FreshnessSettings))
	{
		CustomerProductRuntimeState.StopFreshnessDecay(
			World->GetTimeSeconds(),
			FreshnessSettings->DecayIntervalSeconds,
			FreshnessSettings->DecayAmountPerInterval);
	}

	// ProductRow(테이블+행)와 클래스를 함께 남겨야 나중에 원본 그대로 되돌릴 수 있다.
	// 액터를 파괴하기 전에 기록
	CustomerState->AddSelectProduct(
		ProductRow,
		Product->GetClass(),
		CustomerProductRuntimeState,
		SalePriceAtPickup);

	// 실제 지출 금액 기록
	CustomerState->RecordSpending(SalePriceAtPickup);

	if (CustomerState->SelectProducts.Num() > 0)
	{
		// 실제 상품을 제거하기 전에 같은 데이터로 장바구니 표시용 상품을 추가합니다.
		Customer->AddBasketDisplayProduct(CustomerState->SelectProducts.Last());
	}

	// 손님이 가져간 상품 액터를 월드에서 물리적으로 완전히 제거합니다.
	// (상품 판매 및 인벤토리 처리는 델리게이트를 통해 외부에서 처리하도록 분리)
	Product->Destroy();

	// 상품 획득 이벤트를 브로드캐스트하여 통계, 재고 등 외부 시스템에 알립니다.
	OnItemTaken.Broadcast(Customer, ProductId);
	return ProductId;
}

void UCSCustomerRuntimeSubsystem::RegisterCounter(AActor* CounterActor)
{
	// 계산대 등록. ICSCustomerCounterInterface를 구현하지 않은 액터는 등록을 거부
	// (등록되면 GetQueueSlotLocation이 이 액터에게 줄 좌표를 위임함)
	if (CounterActor != nullptr && Cast<ICSCustomerCounterInterface>(CounterActor) == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CSCustomerRuntimeSubsystem] %s 는 ICSCustomerCounterInterface를 구현하지 않아 등록 불가."),
			*CounterActor->GetName());
		return;
	}

	Counter = CounterActor;
}

void UCSCustomerRuntimeSubsystem::UnregisterCounter(AActor* CounterActor)
{
	// 다른 계산대가 등록된 상태에서 잘못 해제되지 않도록 현재 등록된 액터와 일치하는지 확인합니다.
	if (Counter == CounterActor)
	{
		Counter = nullptr;
	}
}

// 서브시스템 틱의 성능 측정에 사용할 통계 식별자를 반환
TStatId UCSCustomerRuntimeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCSCustomerRuntimeSubsystem, STATGROUP_Tickables);
}

void UCSCustomerRuntimeSubsystem::Tick(float DeltaTime)
{
	// 누적된 순번 확정 후보를 일괄 처리
	ResolvePendingQueueCommits();
}

// 접근 목록에서 손님의 인덱스를 조회
int32 UCSCustomerRuntimeSubsystem::GetApproachIndex(const ACSCustomerCharacter* Customer) const
{
	for (int32 i = 0; i < Approaching.Num(); ++i)
	{
		if (Approaching[i].Customer == Customer)
		{
			return i;
		}
	}
	return -1;
}

// 손님이 순번 확정 전 접근 단계에 있는지 확인
bool UCSCustomerRuntimeSubsystem::IsApproachingQueue(const ACSCustomerCharacter* Customer) const
{
	return GetApproachIndex(Customer) != -1;
}

// 순번이 확정된 대기열 인원 수를 반환
int32 UCSCustomerRuntimeSubsystem::GetCommittedQueueLength() const
{
	return Queue.Num();
}

bool UCSCustomerRuntimeSubsystem::BeginQueueApproach(ACSCustomerCharacter* Customer)
{
	if (Customer == nullptr)
	{
		return false;
	}

	// 이미 순번이 확정된 손님은 접근 단계로 되돌리지 않고 유지
	if (GetQueueIndex(Customer) != -1)
	{
		return true;
	}

	// 접근 중인 손님이 재진입하면 기존 번호표를 유지
	if (IsApproachingQueue(Customer))
	{
		return true;
	}

	// 접근 인원을 포함한 정원이 가득 차면 등록을 거부
	if (IsQueueFull())
	{
		return false;
	}

	// 접근 목록에 손님을 등록하고 번호표를 순서대로 발급
	FCSQueueApproachEntry& Entry = Approaching.AddDefaulted_GetRef();
	Entry.Customer = Customer;
	Entry.Ticket = NextQueueTicket++;
	return true;
}

void UCSCustomerRuntimeSubsystem::CancelQueueApproach(ACSCustomerCharacter* Customer)
{
	// 접근 목록에서 손님을 제거
	const int32 Index = GetApproachIndex(Customer);
	if (Index != -1)
	{
		Approaching.RemoveAt(Index);
	}

	// 이탈한 손님이 순번을 받지 않도록 확정 후보에서도 제거
	PendingQueueCommits.Remove(Customer);
}

// 현재 확정 인원 수를 기준으로 집결 지점의 중심과 반경을 조회
bool UCSCustomerRuntimeSubsystem::TryGetQueueEntryZone(FVector& OutCenter, float& OutRadius) const
{
	OutCenter = FVector::ZeroVector;
	OutRadius = 0.0f;

	const ICSCustomerCounterInterface* CounterInterface = Cast<ICSCustomerCounterInterface>(Counter);
	if (CounterInterface == nullptr)
	{
		return false;
	}

	return CounterInterface->TryGetQueueEntryZone(Queue.Num(), OutCenter, OutRadius);
}

// 손님이 현재 집결 지점 영역 안에 있는지 판정
bool UCSCustomerRuntimeSubsystem::IsCustomerInsideQueueEntryZone(const ACSCustomerCharacter* Customer) const
{
	const ICSCustomerCounterInterface* CounterInterface = Cast<ICSCustomerCounterInterface>(Counter);
	if (CounterInterface == nullptr || !IsValid(Customer))
	{
		return false;
	}

	return CounterInterface->IsPointInsideQueueEntryZone(Queue.Num(), Customer->GetActorLocation());
}

void UCSCustomerRuntimeSubsystem::RequestQueueCommit(ACSCustomerCharacter* Customer)
{
	// 유효하고 접근 목록에 등록된 손님만 확정 요청을 허용
	if (!IsValid(Customer) || !IsApproachingQueue(Customer))
	{
		return;
	}

	// 반복 요청으로 같은 손님이 중복 등록되지 않도록 확정 후보에 추가
	PendingQueueCommits.AddUnique(Customer);
}

void UCSCustomerRuntimeSubsystem::ResolvePendingQueueCommits()
{
	if (PendingQueueCommits.Num() == 0)
	{
		return;
	}

	// 계산대에 가까운 손님을 우선하도록 0번 슬롯을 거리 기준점으로 사용
	const FVector FrontSlotLocation = GetQueueSlotLocation(0);

	// 정렬 중 반복 계산을 줄이도록 후보별 거리와 번호표를 보관
	struct FCommitCandidate
	{
		ACSCustomerCharacter* Customer = nullptr;
		float Distance = 0.0f;
		int32 DistanceBucket = 0;
		int32 Ticket = 0;
	};

	const float Epsilon = FMath::Max(QueueCommitTieEpsilon, 0.0f);

	TArray<FCommitCandidate> Candidates;
	Candidates.Reserve(PendingQueueCommits.Num());

	for (const TObjectPtr<ACSCustomerCharacter>& Pending : PendingQueueCommits)
	{
		ACSCustomerCharacter* Customer = Pending.Get();
		const int32 ApproachIndex = GetApproachIndex(Customer);

		// 요청 이후 파괴되거나 접근 목록에서 제거된 손님을 제외
		if (!IsValid(Customer) || ApproachIndex == -1)
		{
			continue;
		}

		FCommitCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.Customer = Customer;
		Candidate.Distance = FVector::Dist2D(Customer->GetActorLocation(), FrontSlotLocation);

		// 거리를 일정 크기의 구간으로 나누어 같은 구간의 후보를 동률로 처리
		// 직접적인 근사 비교 대신 구간 번호를 사용해 정렬 순서의 일관성을 유지
		if (Epsilon > 0.0f)
		{
			Candidate.DistanceBucket = FMath::FloorToInt(Candidate.Distance / Epsilon);
		}
		else
		{
			Candidate.DistanceBucket = 0;
		}

		Candidate.Ticket = Approaching[ApproachIndex].Ticket;
	}

	// 후보 정보를 옮긴 뒤 대기 중인 요청 목록을 초기화
	PendingQueueCommits.Reset();

	if (Candidates.Num() == 0)
	{
		return;
	}

	// 거리 구간을 사용하면 구간순으로 정렬하고 사용하지 않으면 실제 거리순으로 정렬
	Candidates.Sort([Epsilon](const FCommitCandidate& A, const FCommitCandidate& B)
	{
		if (Epsilon > 0.0f)
		{
			if (A.DistanceBucket != B.DistanceBucket)
			{
				return A.DistanceBucket < B.DistanceBucket;
			}
		}
		else if (A.Distance != B.Distance)
		{
			return A.Distance < B.Distance;
		}

		// 같은 거리 구간이거나 실제 거리가 같으면 번호표 발급 순으로 정렬
		return A.Ticket < B.Ticket;
	});

	for (const FCommitCandidate& Candidate : Candidates)
	{
		// 런타임 정원 변경을 반영하도록 순번 확정 직전에 정원을 다시 확인
		const int32 Limit = GetEffectiveMaxQueueLength();
		if (Limit > 0 && Queue.Num() >= Limit)
		{
			// 정원을 초과한 손님은 계산을 포기하고 접근 목록을 정리
			AbandonCheckout(Candidate.Customer, 0.0f);
			continue;
		}

		// 접근 목록에서 제거하고 대기열 끝에 추가해 순번을 확정
		CancelQueueApproach(Candidate.Customer);
		Queue.Add(Candidate.Customer);
	}
}

int32 UCSCustomerRuntimeSubsystem::JoinQueue(ACSCustomerCharacter* Customer)
{
	if (Customer == nullptr)
	{
		return -1;
	}

	// 이미 대기열에 포함되어 있는 손님이라면 중복 추가를 방지하고 기존 인덱스 반환
	const int32 ExistingIndex = GetQueueIndex(Customer);
	if (ExistingIndex != -1)
	{
		return ExistingIndex;
	}

	// 안전망. 정상 흐름은 WaitInQueue::EnterState의 IsQueueFull()에서 이미 걸러집니다.
	// 여기서 -1을 받은 호출부는 반드시 AbandonCheckout으로 포기 처리를 해야 합니다.
	// 안 그러면 bCheckoutAbandoned가 false로 남아 Checkout 상태로 매 틱 재진입합니다.
	if (IsQueueFull())
	{
		return -1;
	}

	// 대기열 맨 끝에 손님을 추가하고, 해당 인덱스를 반환합니다.
	Queue.Add(Customer);
	return Queue.Num() - 1;
}

void UCSCustomerRuntimeSubsystem::LeaveQueue(ACSCustomerCharacter* Customer)
{
	Queue.Remove(Customer); // 배열 중간에서 삭제되더라도 TArray가 자동으로 빈 공간을 당겨주고 뒷 순번도 같이 갱신됨
}

int32 UCSCustomerRuntimeSubsystem::GetQueueIndex(const ACSCustomerCharacter* Customer) const
{
	// 순차 검색을 통해 손님의 위치를 찾습니다.
	for (int32 i = 0; i < Queue.Num(); i++)
	{
		if (Queue[i] == Customer)
		{
			return i;
		}
	}
	return -1;

}

FVector UCSCustomerRuntimeSubsystem::GetQueueSlotLocation(const int32 Index) const
{
	const ICSCustomerCounterInterface* CounterInterface = Cast<ICSCustomerCounterInterface>(Counter);
	if (CounterInterface == nullptr || Index < 0)
	{
		return FVector::ZeroVector;
	}

	// 대기열의 위치 계산 로직 자체는 계산대 액터가 가지고 있으므로, 인터페이스 함수를 호출하여 좌표를 받아옵니다.
	return CounterInterface->GetQueueSlotLocation(Index);
}

bool UCSCustomerRuntimeSubsystem::TryGetQueueSlotZone(const int32 Index,FVector& OutCenter,float& OutRadius) const
{
	OutCenter = FVector::ZeroVector;
	OutRadius = 0.0f;

	const ICSCustomerCounterInterface* CounterInterface = Cast<ICSCustomerCounterInterface>(Counter);
	if (CounterInterface == nullptr || Index < 0)
	{
		return false;
	}

	// 진열대의 TryGetShelfBrowsePoint와 같은 역할입니다.
	// 이동 목표(중심)와 정지·도착 반경을 한 번에 받아, 두 값이 어긋날 여지를 없앱니다.
	return CounterInterface->TryGetQueueSlotZone(Index, OutCenter, OutRadius);
}

float UCSCustomerRuntimeSubsystem::GetQueueSlotStopRadius() const
{
	const ICSCustomerCounterInterface* CounterInterface = Cast<ICSCustomerCounterInterface>(Counter);
	if (CounterInterface == nullptr)
	{
		return 0.0f;
	}

	// 손님 AI는 이 값을 그대로 정지 반경으로 씁니다. 불변식 계산은 계산대가 책임집니다.
	return CounterInterface->GetQueueSlotStopRadius();
}

ACSCustomerCharacter* UCSCustomerRuntimeSubsystem::GetFrontCustomer() const
{
	// 대기열의 가장 첫 번째(계산 차례인) 손님을 반환
	if (Queue.Num() == 0)
	{
		return nullptr;
	}
	return Queue[0];
}

ACSCustomerCharacter* UCSCustomerRuntimeSubsystem::GetCheckoutReadyCustomer() const
{
	ACSCustomerCharacter* Front = GetFrontCustomer();
	if (!IsValid(Front))
	{
		return nullptr;
	}

	const ICSCustomerCounterInterface* CounterInterface = Cast<ICSCustomerCounterInterface>(Counter);
	if (CounterInterface == nullptr)
	{
		return nullptr;
	}

	// 0번 자리에 도착하지 않은 손님은 아직 계산 대상이 아닙니다.
	// "줄 순번 확정"은 결심 시점, "계산 가능"은 도착 시점으로 두 시점을 나누는 지점입니다.
	if (!CounterInterface->IsPointInsideQueueSlotZone(0, Front->GetActorLocation()))
	{
		return nullptr;
	}

	return Front;
}

bool UCSCustomerRuntimeSubsystem::IsCustomerAtQueueSlot(const ACSCustomerCharacter* Customer) const
{
	const ICSCustomerCounterInterface* CounterInterface = Cast<ICSCustomerCounterInterface>(Counter);
	if (CounterInterface == nullptr || !IsValid(Customer))
	{
		return false;
	}

	const int32 Index = GetQueueIndex(Customer);
	if (Index < 0)
	{
		return false;
	}
	// 해당 순번의 영역 안에 손님이 들어가 있는지 계산대에게 판정 요청
	return CounterInterface->IsPointInsideQueueSlotZone(Index, Customer->GetActorLocation());
}

bool UCSCustomerRuntimeSubsystem::TryGetQueueSlotFacing(const int32 Index, FRotator& OutFacing) const
{
	OutFacing = FRotator::ZeroRotator;

	const ICSCustomerCounterInterface* CounterInterface = Cast<ICSCustomerCounterInterface>(Counter);
	if (CounterInterface == nullptr || Index < 0)
	{
		return false;
	}

	return CounterInterface->TryGetQueueSlotFacing(Index, OutFacing);
}

int32 UCSCustomerRuntimeSubsystem::GetEffectiveMaxQueueLength() const
{
	const ICSCustomerCounterInterface* CounterInterface = Cast<ICSCustomerCounterInterface>(Counter);
	if (CounterInterface == nullptr)
	{
		// 계산대가 없으면 정원 개념이 없습니다. IsQueueFull이 무제한으로 처리
		return 0;
	}
	// NavMesh나 벽에 막힌 슬롯을 제외한 '실제 설 수 있는 정원 수'를 가져옴
	return CounterInterface->GetEffectiveMaxQueueLength();
}

bool UCSCustomerRuntimeSubsystem::IsQueueFull() const
{
	const int32 Limit = GetEffectiveMaxQueueLength();

	// 정원을 못 구했으면(계산대 없음 등) 예전처럼 무제한으로 둡니다.
	// 여기서 true를 반환하면 계산대가 없는 맵에서 모든 손님이 포기
	if (Limit <= 0)
	{
		return false;
	}
	// 순번이 확정된 인원 + 집결 지점으로 걸어오는 중인 인원으로 셉니다.
	// 걸어오는 인원을 빼면 정원 제한이 사실상 사라져, 몇 명이든 계산대로 몰려온 뒤
	// 서로 통과하며 한 지점에 겹쳐 쌓입니다.
	return Queue.Num() + Approaching.Num() >= Limit;
}

void UCSCustomerRuntimeSubsystem::CompleteFrontCheckout()
{
	if (Queue.Num() == 0)
	{
		return;
	}

	ACSCustomerCharacter* Front = Queue[0];

	// 첫 번째 요소를 제거합니다. (TArray 특성상 뒤에 있던 원소들이 자동으로 한 칸씩 당겨짐)
	Queue.RemoveAt(0);

	if (Front != nullptr)
	{
		UCSCustomerStateComponent* State = Front->GetCustomerState();
		if (State != nullptr)
		{
			// 손님의 상태 컴포넌트에 계산이 완료되었음을 마킹
			State->bCheckoutDone = true;
		}
		// 결제가 완료된 상품은 고객 장바구니에서도 즉시 제거합니다.
		Front->ClearBasketDisplayProducts();

		// 계산이 완료되었음을 브로드캐스트하여, UI 갱신이나 매출 증가 등의 후속 시스템 처리 유도
		OnCheckoutCompleted.Broadcast(Front);

		// 결제가 끝난 상품은 더 이상 월드로 되돌릴 대상이 아니다.
		// SelectProducts는 "월드에서 지운 상품의 복원 데이터"이므로 남겨두면
		// 이후 어떤 경로로 파괴되든 DropCustomerItems가 실물을 다시 스폰해
		// 판 물건이 공짜로 재고에 돌아온다.
		//
		// 반드시 방송 뒤여야 한다. CSPurchaseVerificationSubsystem이 방송 중에
		// SelectProducts.Num()으로 담은 개수를 읽으므로, 앞에 두면 통계가 전부 0이 된다.
		//
		// StateTree 영향 없음. ST_Customer에서 HasItemsInCart는 Checkout / Shoplift /
		// ReturnItems 세 상태의 Enter Condition이고, 결제 후 목적지인 Leave는 조건이 없다.
		if (State != nullptr)
		{
			State->ClearSelectItems();
		}
	}
}

float UCSCustomerRuntimeSubsystem::GetPatienceSeconds(const ACSCustomerCharacter* Customer) const
{
	const UCSPurchaseDecisionComponent* Decision = IsValid(Customer)
		? Customer->FindComponentByClass<UCSPurchaseDecisionComponent>()
		: nullptr;

	// 종족 카드가 없으면(비정상 상황) 계산할 재료가 없으니 예전처럼 고정값 그대로 반환
	if (Decision == nullptr || Decision->Archetype == nullptr)
	{
		return DefaultPatienceSeconds;
	}

	// 충동성 0(침착)~1(충동적)을 MaxPatienceSeconds~MinPatienceSeconds 사이로 부드럽게 매핑
	return FMath::Lerp(MaxPatienceSeconds, MinPatienceSeconds, Decision->Archetype->Impulsiveness);
}

void UCSCustomerRuntimeSubsystem::AbandonCheckout(ACSCustomerCharacter* Customer, const float WaitedSeconds)
{

	if (Customer == nullptr)
	{
		return;
	}

	// 대기열에서 먼저 제거
	LeaveQueue(Customer);

	// 아직 순번을 못 받고 걸어오던 중일 수도 있습니다.
	// 여기서 안 빼면 떠난 손님이 계속 정원을 차지해 뒷사람이 입구에서 포기합니다.
	CancelQueueApproach(Customer);

	// 계산에 실패한 손님은 결제 성공 때와 같이 장바구니를 숨긴다(액터는 손님 파괴 때 함께 정리).
	// 상품 목록(SelectProducts)은 남겨 두므로 아래 진열대 반환/바닥 낙하는 기존대로 동작한다.
	Customer->SetBasketDisplayVisible(false);

	//  들고 있던 상품을 떨어뜨린다. (진열대에 되돌려놓는 기능이 들어오면 이 호출은 그쪽 Task로 이동)
	//DropCustomerItems(Customer);

	// 다시 줄 서지 않도록 true로 지정
	UCSCustomerStateComponent* State = Customer->GetCustomerState();
	if (State != nullptr)
	{
		State->bCheckoutAbandoned = true;
	}

	// 진열대에 되돌려놓을지, 그 자리에 버릴지 결정
	// 되돌려놓기로 하면 상품 목록을 그대로 두고, ReturnItems 상태가 처리한다
	// 확률은 GetReturnToShelfChance(Customer)에서 종족 Impulsiveness 기반으로 계산된다
	const bool bWillReturnToShelf = (ShelfRandomStream.FRand() < GetReturnToShelfChance(Customer));

	if (bWillReturnToShelf == false)
	{
		DropCustomerItems(Customer);
	}

	//계산을 포기했다고 방송을 전파
	OnCheckoutAbandoned.Broadcast(Customer, WaitedSeconds);

	UE_LOG(LogTemp, Log,TEXT("[CSCustomerRuntimeSubsystem] %s 가 %.1f초 대기 후 계산을 포기했습니다."),*Customer->GetName(), WaitedSeconds);
}

void UCSCustomerRuntimeSubsystem::NotifyCustomerDespawned(ACSCustomerCharacter* Customer)
{
	const UCSCustomerStateComponent* CustomerState = IsValid(Customer)
		? Customer->GetCustomerState()
		: nullptr;

	// 파괴되는 손님이 접근 목록이나 확정 후보에 남아 있으면
	// 사라진 사람이 계속 정원을 차지하거나, 다음 프레임에 순번을 받게 됩니다.
	CancelQueueApproach(Customer);

	// CustomerState가 없으면(비정상 상황) 빈손으로 취급
	const bool bCheckoutDone = IsValid(CustomerState) && CustomerState->bCheckoutDone;


	// [백무송] 절도범이 상품을 든 채 퇴장에 성공한 경우 여기서 손실이 확정
	// 모든 경로가 반드시 지나는 이 함수에 둔다. 절도범이 아니면 내부에서 선별됨
	ReportShopliftEscaped(Customer);

	// 매장에 들어온 적이 없는 손님은 구매 통계 대상이 아니므로 방송 X
	// 이 검사가 없으면 가게에 들어오지도 않은 사람이 빈손 퇴장률에 잡힌다.
	if (CustomerState != nullptr && CustomerState->HasEnteredStore() == false)
	{
		return;
	}


	OnCustomerDespawned.Broadcast(Customer, bCheckoutDone);
}

int32 UCSCustomerRuntimeSubsystem::DropCustomerItems(ACSCustomerCharacter* Customer)
{
		if (Customer == nullptr)
	{
		return 0;
	}

	UCSCustomerStateComponent* State = Customer->GetCustomerState();
	if (State == nullptr)
	{
		return 0;
	}

	const int32 ItemCount = State->SelectProducts.Num();
	if (ItemCount == 0)
	{
		return 0;
	}

	// 좌우 두 갈래로 나눠 쌓는다. 인덱스 0 = 왼쪽, 1 = 오른쪽.
	// 각 갈래는 자기 화살표를 기준으로 독립적으로 열을 늘려간다.
	float ColumnTopZ[2] = { 0.0f, 0.0f };        // 그 열에서 다음 상품을 놓을 높이 (기준점 기준)
	int32 ColumnIndex[2] = { 0, 0 };             // 그 갈래에서 지금 몇 번째 열인지
	float MaxItemRadius[2] = { 0.0f, 0.0f };     // 그 갈래에서 본 가장 큰 상품 반경

	int32 SpawnedCount = 0;

	for (int32 i = 0; i < ItemCount; i++)
	{
		// 좌우 번갈아 배정해서 양쪽이 고르게 쌓이게 한다.
		const int32 SideIndex = i % 2;

		// 시작 위치는 BP에서 지정한 화살표가 알려준다.
		// 종족마다 키와 팔 위치가 달라서 코드에 숫자로 박지 않는다.
		const UArrowComponent* Origin = Customer->GetDropOrigin(SideIndex);
		if (Origin == nullptr)
		{
			continue;
		}

		// 열 간격은 그 갈래에서 본 가장 큰 상품 지름 기준으로 잡는다.
		// 고정값을 쓰면 큰 상품이 섞였을 때 옆 열과 겹친다.
		const float ColumnSpacing = FMath::Max(
			DropMinColumnSpacing,
			MaxItemRadius[SideIndex] * 2.0f + DropColumnGap);

		// 열이 넘치면 화살표 정면 방향으로 새 열을 만든다.
		const FVector ColumnBase =
			Origin->GetComponentLocation() +
			Origin->GetForwardVector() * (ColumnSpacing * static_cast<float>(ColumnIndex[SideIndex]));

		// 완전히 정렬돼 보이지 않게 아주 살짝 흔들어준다.
		const FVector Jitter(
			FMath::FRandRange(-DropJitterRadius, DropJitterRadius),
			FMath::FRandRange(-DropJitterRadius, DropJitterRadius),
			0.0f);

		const FVector SpawnLocation =
			ColumnBase + Jitter + FVector(0.0f, 0.0f, ColumnTopZ[SideIndex]);

		// 전부 같은 방향을 보고 있으면 인위적으로 보이므로 Yaw만 랜덤하게 준다.
		const FRotator RandomRotation(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f);

		const FTransform SpawnTransform(RandomRotation, SpawnLocation);

		AActor* Spawned = RespawnSelectProduct(State->SelectProducts[i], SpawnTransform);
		if (Spawned == nullptr)
		{
			continue;
		}

		SpawnedCount++;

		// 실제 크기를 재서 다음 상품의 높이와 열 간격을 정한다.
		// 상품마다 크기가 달라 고정값으로는 겹치거나 공중에 뜬다.
		const FVector Extent = Spawned->GetComponentsBoundingBox(false).GetExtent();
		const float ItemHeight = Extent.Z * 2.0f;
		const float ItemRadius = FMath::Max(Extent.X, Extent.Y);

		if (ItemRadius > MaxItemRadius[SideIndex])
		{
			MaxItemRadius[SideIndex] = ItemRadius;
		}

		ColumnTopZ[SideIndex] += ItemHeight + DropStackGap;

		// 팔 높이를 넘으면 같은 쪽에 새 열을 만들고 다시 아래부터 쌓는다.
		if (ColumnTopZ[SideIndex] >= DropMaxStackHeight)
		{
			ColumnIndex[SideIndex]++;
			ColumnTopZ[SideIndex] = 0.0f;
		}
	}

	// 일부 생성에 실패해도 목록은 비운다.
	// 남겨두면 손님이 계속 "상품을 들고 있는" 상태가 되어 이후 판정이 꼬인다.
	State->ClearSelectItems();
	Customer->ClearBasketDisplayProducts();

	return SpawnedCount;

}

ACSProductPickupActor* UCSCustomerRuntimeSubsystem::RespawnSelectProduct(const FCSSelectProduct& Selected,const FTransform& SpawnTransform, const bool bEnableDropPhysics)
{
	UWorld* World = GetWorld();
	if (World == nullptr || Selected.ProductClass == nullptr)
	{
		return nullptr;
	}

	// 배송 상자(ACSDeliveryCrateActor::SpawnProductActor)와 동일한 패턴을 쓴다.
	// SpawnActorDeferred는 BeginPlay 전에 데이터를 세팅할 수 있어서,
	// 상품이 초기화 전 상태로 한 프레임 존재하는 상황을 막는다.
	ACSProductPickupActor* Product = World->SpawnActorDeferred<ACSProductPickupActor>(
		Selected.ProductClass,
		SpawnTransform,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);

	if (!IsValid(Product))
	{
		return nullptr;
	}

	// 기록해둔 ProductRow로 초기화하면 내부에서 DataAsset 기반 메시와 스케일까지 적용된다.
	if (!Product->InitializeProduct(Selected.ProductRow))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CSCustomerRuntimeSubsystem] 상품 초기화 실패로 되돌리지 못했습니다: %s"),
			*Selected.ProductRow.RowName.ToString());
		Product->Destroy();
		return nullptr;
	}

	Product->ApplyProductRuntimeState(Selected.ProductRuntimeState);
	Product->FinishSpawning(SpawnTransform);
	if (!IsValid(Product))
	{
		return nullptr;
	}

	// 플레이어가 바닥에 내려놓을 때와 동일하게 처리한다.
	// 콜리전을 켜야 다시 집을 수 있고, 물리를 켜야 자연스럽게 안착한다.
	Product->SetActorEnableCollision(true);

	// 진열대에 놓을 상품은 물리 기능 X
	if (bEnableDropPhysics)
	{
		Product->EnableWorldDropPhysics();
	}

	return Product;
}

float UCSCustomerRuntimeSubsystem::GetReturnToShelfChance(const ACSCustomerCharacter* Customer) const
{
	const UCSPurchaseDecisionComponent* Decision = IsValid(Customer)
		? Customer->FindComponentByClass<UCSPurchaseDecisionComponent>()
		: nullptr;

	// 종족 카드가 없으면(비정상 상황) 계산할 재료가 없으니 예전처럼 고정값 그대로 반환
	if (Decision == nullptr || Decision->Archetype == nullptr)
	{
		return DefaultReturnToShelfChance;
	}

	// 충동성 0(침착)~1(충동적)을 MaxReturnToShelfChance~MinReturnToShelfChance 사이로 부드럽게 매핑
	return FMath::Lerp(MaxReturnToShelfChance, MinReturnToShelfChance, Decision->Archetype->Impulsiveness);
}

AActor* UCSCustomerRuntimeSubsystem::PickRandomShelf() const
{
	TArray<AActor*> Candidates;

	for (int32 i = 0; i < Shelves.Num(); i++)
	{
		AActor* Shelf = Shelves[i];
		if (!IsShelfSelectable(Shelf))
		{
			continue;
		}
		//만약 빈진열대나 원하는 진열대를 선택하고 싶으면 여기서 조건을 달아주면 된다.
		Candidates.Add(Shelf);
	}

	if (Candidates.Num() == 0)
	{
		return nullptr;
	}

	const int32 PickIndex = ShelfRandomStream.RandRange(0, Candidates.Num() - 1);
	return Candidates[PickIndex];
}

int32 UCSCustomerRuntimeSubsystem::ReturnCustomerItemsToShelf(ACSCustomerCharacter* Customer, AActor* ShelfActor)
{
	if (Customer == nullptr || ShelfActor == nullptr)
	{
		return 0;
	}

	UCSCustomerStateComponent* State = Customer->GetCustomerState();
	if (State == nullptr || State->SelectProducts.Num() == 0)
	{
		return 0;
	}

	if (ShelfActor->Implements<UCSShelfPlaceable>() == false)
	{
		// 진열대가 아니면 그냥 그 자리에 떨군다.
		DropCustomerItems(Customer);
		return 0;
	}

	const FVector ShelfLocation = ShelfActor->GetActorLocation();
	int32 PlacedCount = 0;

	// 슬롯에 못 들어간 상품은 따로 모아뒀다가 마지막에 떨어뜨림
	TArray<FCSSelectProduct> LeftoverProducts;

	for (int32 i = 0; i < State->SelectProducts.Num(); i++)
	{
		// 진열대에 놓을 상품은 물리를 끈 상태로 생성
		ACSProductPickupActor* Product =
			RespawnSelectProduct(State->SelectProducts[i], FTransform(ShelfLocation), false);

		if (Product == nullptr)
		{
			continue;
		}

		// 진열대가 빈 슬롯을 찾아 Attach하고 슬롯 점유까지 처리한다.
		// 빈 슬롯이 없으면 false를 반환하므로 슬롯 초과 배치는 구조적으로 불가능
		if (ICSShelfPlaceable::Execute_TryPlaceItem(ShelfActor, Product, ShelfLocation)) //손님 AI가 반환하는 물건들은 진열대의 원점과 가까운 빈 슬롯부터 채움
		{
			PlacedCount++;
			continue;
		}

		// 슬롯이 꽉 찼다. 방금 만든 액터는 지우고 나중에 떨어뜨릴 목록에 추가
		LeftoverProducts.Add(State->SelectProducts[i]);
		Product->Destroy();
	}

	// 슬롯에 들어간 상품은 목록에서 지우고, 남은 것만 다시 채워 넣음
	State->ClearSelectItems();
	Customer->ClearBasketDisplayProducts();
	for (int32 i = 0; i < LeftoverProducts.Num(); i++)
	{
		State->SelectProducts.Add(LeftoverProducts[i]);
	}

	// 남은 상품이 있으면 그 자리에 떨어뜨린다. (내부에서 목록을 다시 비운다)
	if (State->SelectProducts.Num() > 0)
	{
		DropCustomerItems(Customer);
	}

	return PlacedCount;

}

bool UCSCustomerRuntimeSubsystem::IsBusinessHoursOver() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	const ACSStoreGameState* StoreState = World->GetGameState<ACSStoreGameState>();
	if (StoreState == nullptr)
	{
		return false;
	}

	// 24:00 강제 마감 판정
	// 입간판을 돌려 폐점한 경우, 손님은 하던 쇼핑을 끝내고 나감
	// 구분하기 위해 자정 마감 시에만 활성화되는 변수 확인
	if (StoreState->bIsDailySummaryPending)
	{
		return true;
	}

	// 정산 확인 이후. 마감 자유활동 구간
	// 마감 구간 내내 true를 유지
	// 단계는 Open → ClosingFreeRoam → Settlement → Finished 순으로만 진행된다.
	// ClosingFreeRoam에서 이미 손님을 내보냈으므로 그 뒤엔 손님이 남을 수 없다.
	const ECStorePhase Phase = StoreState->CurrentPhase;
	return (Phase == ECStorePhase::ClosingFreeRoam);
}

bool UCSCustomerRuntimeSubsystem::ShouldForceLeaveStore(const ACSCustomerCharacter* Customer) const
{
	if (IsValid(Customer) == false)
	{
		return false;
	}

	const UCSCustomerStateComponent* State = Customer->GetCustomerState();
	if (State == nullptr)
	{
		return false;
	}

	// 이미 강제 퇴장이 확정된 손님은 다시 판정하지 X
	// 이게 없으면 Root의 On Tick 매 틱마다 확인해 손님이 퇴장 상태에 재진입하며 제자리에서 좌우로 움직인다.
	if (State->IsForcedToLeave())
	{
		return false;
	}

	// 제압된 손님은 이미 소멸 대기 중이다.
	// 이 부분이 없으면 자정 마감이 겹칠 때 ForceLeave가 쓰러진 손님을 다시 걷게 만든다.
	if (State->IsSubdued())
	{
		return false;
	}

	// 계산을 마친 손님은 어차피 나가는 중이다.
	// 체크를 해야 이미 계산완료한 손님은 봉투를 든 채 나간다.
	if (State->bCheckoutDone)
	{
		return false;
	}

	//    절도를 하고있는 빌런은 도주 중이므로 건드리지 않는다.(고민중)
	//    이 검사가 빠지면 자정마다 절도범이 훔친 물건을 스스로 내려놓고 걸어 나가
	//    빌런 시스템이 무력화된다.
	if (State->PlannedVillainAction == ECSVillainAction::Shoplift
		&& State->bVillainActionDone)
	{
		return false;
	}

	return IsBusinessHoursOver();
}

void UCSCustomerRuntimeSubsystem::PrepareForcedLeave(ACSCustomerCharacter* Customer)
{
	if (IsValid(Customer) == false)
	{
		return;
	}

	UCSCustomerStateComponent* State = Customer->GetCustomerState();
	if (State == nullptr)
	{
		return;
	}

	// 중복 정리 방지. 상품이 두 번 떨어져 재고가 늘어나는 사고를 막는다.
	if (State->IsForcedToLeave())
	{
		return;
	}

	State->MarkForcedToLeave();

	// 줄에서 빠진다.
	LeaveQueue(Customer);

	// 진열대로 되돌리지 않고 손님 발밑에 그대로 떨어뜨린다.
	DropCustomerItems(Customer);

	OnCustomerForcedToLeave.Broadcast(Customer);

}

bool UCSCustomerRuntimeSubsystem::IsEscapingShoplifter(const ACSCustomerCharacter* Customer) const
{
	if (IsValid(Customer) == false)
	{
		return false;
	}

	const UCSCustomerStateComponent* State = Customer->GetCustomerState();
	if (State == nullptr)
	{
		return false;
	}

	// 절도를 실행한 빌런만 손실 대상이다.
	// 아직 저지르지 않은 빌런은 겉보기에 일반 손님과 구분되지 않으므로 제외한다.
	// CanSubdueCustomer가 실행 전 빌런을 제압 대상에서 빼는 것과 같은 기준.
	if (State->PlannedVillainAction != ECSVillainAction::Shoplift
		|| State->bVillainActionDone == false)
	{
		return false;
	}

	// 제압된 빌런은 상품을 이미 떨어뜨렸으므로 손실이 없다.
	// 금액상으로는 SelectProducts가 비어 자동으로 걸러지지만,
	// SubduedDespawn 타이머가 도는 중에 밖에서 파괴하면 그 태스크가
	// Data.Actor == nullptr을 만나 Failed를 뱉으므로 여기서 먼저 제외한다.
	if (State->IsSubdued())
	{
		return false;
	}

	// 정상적으로 계산을 마친 손님은 절도범이 아니다.
	if (State->bCheckoutDone)
	{
		return false;
	}

	return true;
}

void UCSCustomerRuntimeSubsystem::DespawnAllCustomers()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// 순회 중 파괴하면 이터레이터가 깨지므로 먼저 전부 모은 뒤 두 번째 루프에서 처리
	// RefreshShelves()와 같은 TActorIterator 방식이라 추가 include가 필요 없다.
	TArray<ACSCustomerCharacter*> RemainingCustomers;
	for (TActorIterator<ACSCustomerCharacter> It(World); It; ++It)
	{
		ACSCustomerCharacter* Customer = *It;
		if (IsValid(Customer))
		{
			RemainingCustomers.Add(Customer);
		}
	}

	for (int32 Index = 0; Index < RemainingCustomers.Num(); Index++)
	{
		ACSCustomerCharacter* Customer = RemainingCustomers[Index];
		if (IsValid(Customer) == false)
		{
			continue;
		}

		// 절도를 실행하고 아직 도주 중인 손님인가.
		if (IsEscapingShoplifter(Customer))
		{
			// 훔친 물건을 떨어뜨리지 않는다.
			//
			// NotifyCustomerDespawned는 내부에서 ReportShopliftEscaped를 부르는데,
			// 그 함수는 SelectProducts가 비어 있으면 조기 return한다.
			// 여기서 DropCustomerItems를 먼저 부르면 목록이 비워져
			// 절도 손실이 아예 집계되지 않는다.
			//
			// 마감까지 잡히지 않았으면 "도주 성공"으로 보는 것이
			// ShouldForceLeaveStore에서 절도범을 제외한 것과 일관된다.
			// 물건을 회수하고 손실을 없애려면 이 분기도 PrepareForcedLeave로 바꾸면 된다.
			LeaveQueue(Customer);
		}
		else
		{
			// 파괴 전에 반드시 정리부터. 이 호출 없이 Destroy하면
			// 장바구니에 붙어 있던 상품이 함께 사라져 재고가 증발한다.
			PrepareForcedLeave(Customer);
		}

		NotifyCustomerDespawned(Customer);
		Customer->Destroy();
	}

	if (RemainingCustomers.Num() > 0)
	{
		// 마감 종 시점에 매장과 거리에 남아 있던 인원이다. 0이 아닌 것이 정상이다.
		UE_LOG(LogTemp, Warning, TEXT("[영업일전환] 남아 있던 손님 %d명 정리"),
			RemainingCustomers.Num());
	}

}

// GetProductsOnShelf와 달리 IsSellable() 필터 없이 Attach된 상품 액터 수만 센다
int32 UCSCustomerRuntimeSubsystem::CountAttachedProducts(const AActor* ShelfActor) const
{
	int32 Count = 0;
	if (ShelfActor == nullptr)
	{
		return Count;
	}

	TArray<AActor*> Attached;
	ShelfActor->GetAttachedActors(Attached);
	for (const AActor* AttachedActor : Attached)
	{
		if (Cast<ACSProductPickupActor>(AttachedActor) != nullptr)
		{
			++Count;
		}
	}

	return Count;
}

// 등록된 진열대 전체의 충진율을 계산. 등록된 진열대가 없거나 전체 슬롯이 0이면 0.0(가장 빈 가게로 취급)
float UCSCustomerRuntimeSubsystem::CalculateShelfFillRatio(int32& OutShelfCount, int32& OutFilledSlots, int32& OutTotalSlots) const
{
	OutShelfCount = 0;
	OutFilledSlots = 0;
	OutTotalSlots = 0;

	for (const TObjectPtr<AActor>& RegisteredShelf : Shelves)
	{
		// const 함수라 여기서 RemoveAll로 정리할 수 없으니 무효 항목은 건너뛰기만 한다
		if (!IsValid(RegisteredShelf))
		{
			continue;
		}

		const ACSShelfActor* Shelf = Cast<ACSShelfActor>(RegisteredShelf);
		if (Shelf == nullptr)
		{
			continue;
		}

		++OutShelfCount;
		const int32 Occupied = CountAttachedProducts(Shelf);
		OutFilledSlots += Occupied;
		OutTotalSlots += Occupied + Shelf->GetEmptySlotCount();
	}

	if (OutTotalSlots <= 0)
	{
		return 0.0f;
	}

	return static_cast<float>(OutFilledSlots) / static_cast<float>(OutTotalSlots);
}

// DroppedTrashCache는 손님이 흘린 쓰레기만 등록되고 레벨에 사전 배치된 쓰레기는 빠지므로,
// 충진율과 같은 기준("눈에 보이는 것")에 맞추려고 매 판정마다 월드를 직접 순회한다
// ACSStoreInteriorActor가 배치되어 있으면 그 영역 안의 쓰레기만 세고, 없으면 월드 전체를 센다(폴백)
int32 UCSCustomerRuntimeSubsystem::CountTrashInWorld() const
{
	int32 Count = 0;
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return Count;
	}

	const ACSStoreInteriorActor* InteriorActor = CachedStoreInteriorActor.Get();

	for (TActorIterator<ACSTrashActor> It(World); It; ++It)
	{
		if (!IsValid(*It))
		{
			continue;
		}

		if (InteriorActor != nullptr && !InteriorActor->EncompassesPoint(It->GetActorLocation()))
		{
			continue;
		}

		++Count;
	}

	return Count;
}

bool UCSCustomerRuntimeSubsystem::ShouldEnterStore(const ACSCustomerCharacter* Customer) const
{
	if (Customer == nullptr)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	// 로그에 시드를 찍기 위해 컴포넌트 조회를 영업 체크보다 앞으로 옮김 - 난수를 소비하지 않는 조회라 순서를 옮겨도 무방
	const UCSPurchaseDecisionComponent* Decision =
		Customer->FindComponentByClass<UCSPurchaseDecisionComponent>();
	if (Decision == nullptr)
	{
		return false;
	}

	const UCSCustomerStateComponent* State = Customer->GetCustomerState();
	if (State == nullptr)
	{
		return false;
	}

	const int32 CustomerSeed = Decision->GetCustomerSeed();

	// 매장 상태로 판단 open인지 close인지 - 영업 미개시면 굴리지 않는다
	const ACSStoreGameState* StoreState = World->GetGameState<ACSStoreGameState>();
	if (StoreState == nullptr || StoreState->bIsStoreOpen == false)
	{
		UE_LOG(LogTemp, Log, TEXT("[입장판정] 손님=%s | 시드=%d | 결과=영업미개시"),
			*Customer->GetName(), CustomerSeed);
		return false;
	}

	// 계획 유무 - 스폰 시점에 이미 정해진 값이라 여기서 새로 굴리지 않는다
	// PlannedProductIds가 아니라 이 플래그를 보는 이유: 계획 상품 목록은 이벤트가 있는 날만
	// 채워져서, 목록으로 판정하면 이벤트 없는 날 손님 전원이 무계획 구간(하한 0.05)에 걸린다
	const bool bHasPlan = State->bHasShoppingIntent;

	// 매장 상태는 판정 순간마다 새로 계산한다(캐시 금지) - 손님이 걸어오는 동안 플레이어가
	// 진열대를 채우거나 쓰레기를 치우면 그게 바로 반영되게 하려는 것. 판정이 손님당 1회라 비용 문제는 없다
	int32 ShelfCount = 0;
	int32 FilledSlots = 0;
	int32 TotalSlots = 0;
	const float FillRatio = CalculateShelfFillRatio(ShelfCount, FilledSlots, TotalSlots);

	const int32 TrashCount = CountTrashInWorld();
	const int32 TrashDenominator = FMath::Max(1, DefaultTrashCountForZeroCleanliness);
	const float Cleanliness = FMath::Clamp(1.0f - static_cast<float>(TrashCount) / static_cast<float>(TrashDenominator), 0.0f, 1.0f);

	// 진열대가 비면 청결도와 무관하게 0이어야 하고, 두 입력 중 어느 쪽을 개선해도 결과가 나아져야 해서 곱을 쓴다
	const float StoreCondition = FillRatio * Cleanliness;

	const float EntryChanceMin = bHasPlan ? DefaultPlannedEntryChanceMin : DefaultUnplannedEntryChanceMin;
	const float EntryChanceMax = bHasPlan ? DefaultPlannedEntryChanceMax : DefaultUnplannedEntryChanceMax;
	const float FinalChance = FMath::Lerp(EntryChanceMin, EntryChanceMax, StoreCondition);

	// 이 손님 마스터 스트림의 첫 굴림 - 계획 있음/없음 어느 쪽이든 굴림 횟수가 1회로 같다
	// 살 목적이 있는 손님이 확률 때문에 튕겨나가면 버그처럼 보이므로 계획 유무로 범위를 나누고,
	// 판정을 가게 앞 한 번으로 고정해 "들어올까 말까"를 지켜보는 순간을 만든다
	const bool bEntered = Decision->RollChance(FinalChance);

	const FString LogLine = FString::Printf(
		TEXT("[입장판정] 손님=%s | 시드=%d | 결과=%s | 계획=%s | 진열대=%d개 | 슬롯=%d/%d | 충진율=%.2f | 쓰레기=%d개 | 청결도=%.2f | 매장상태=%.2f | 확률=%.2f"),
		*Customer->GetName(), CustomerSeed, bEntered ? TEXT("입장") : TEXT("확률탈락"),
		bHasPlan ? TEXT("있음") : TEXT("없음"), ShelfCount, FilledSlots, TotalSlots,
		FillRatio, TrashCount, Cleanliness, StoreCondition, FinalChance);

	// 전체 슬롯이 0일 때만 Warning - 진열대 등록이 빠지는 버그를 콘솔에서 바로 보이게 하려는 것
	if (TotalSlots <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s"), *LogLine);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("%s"), *LogLine);
	}

	return bEntered;
}

void UCSCustomerRuntimeSubsystem::CommitShoplift(ACSCustomerCharacter* Customer)
{
	if (Customer == nullptr)
	{
		return;
	}

	UCSCustomerStateComponent* State = Customer->GetCustomerState();
	if (State == nullptr)
	{
		return;
	}

	State->MarkVillainActionDone();

	// 절도가 시작된 순간부터 빨간 외곽선으로 추격 대상을 표시
	Customer->SetVillainHighlight(true);

	// 디버그용
	// 들고 나가려는 상품의 판매가 합계.
	int64 AtRiskGoldTotal = 0;
	for (int32 i = 0; i < State->SelectProducts.Num(); i++)
	{
		AtRiskGoldTotal += State->SelectProducts[i].SalePriceAtPickup;
	}
	const int32 AtRiskGold = static_cast<int32>(FMath::Min<int64>(AtRiskGoldTotal, MAX_int32));

	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Orange,
			FString::Printf(TEXT("[빌런 발생] %s 가 계산하지 않고 나가려 합니다 (%d G)"),
				*Customer->GetName(), AtRiskGold));
	}

	//빌런 생성 했다고 전파
	OnShopliftStarted.Broadcast(Customer, AtRiskGold);
}

void UCSCustomerRuntimeSubsystem::ReportShopliftEscaped(ACSCustomerCharacter* Customer)
{
	if (Customer == nullptr)
	{
		return;
	}

	UCSCustomerStateComponent* State = Customer->GetCustomerState();
	if (State == nullptr)
	{
		return;
	}

	// 빌런(절도범용)이 아니면 무시. 모든 손님의 퇴장 경로에서 불리므로 여기서 걸러야 함
	if (State->PlannedVillainAction != ECSVillainAction::Shoplift)
	{
		return;
	}

	// 상품이 없으면 손실도 없음
	if (State->SelectProducts.Num() == 0)
	{
		return;
	}

	// 손실 확정 지점이 정문 퇴장 / 마감 종 / 영업일 전환 세 곳이므로
	// 한 손님의 손실이 두 번 장부에 올라가지 않도록 첫 호출에서만 통과시킨다.
	if (State->MarkShopliftLossReported() == false)
	{
		return;
	}

	// 디버그용
	int64 LostGoldTotal = 0;
	for (int32 i = 0; i < State->SelectProducts.Num(); i++)
	{
		LostGoldTotal += State->SelectProducts[i].SalePriceAtPickup;
	}
	const int32 LostGold = static_cast<int32>(FMath::Min<int64>(LostGoldTotal, MAX_int32));

	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red,
			FString::Printf(TEXT("[빌런 도망침] %s | 손실 %d G"),
				*Customer->GetName(), LostGold));
	}

	//빌러 도주 성공했다고 전파
	OnShopliftEscaped.Broadcast(Customer, LostGold);
}

TArray<FCSStolenProductRecord> UCSCustomerRuntimeSubsystem::GetStolenProductRecords(
	const ACSCustomerCharacter* Customer) const
{
	TArray<FCSStolenProductRecord> StolenProducts;

	if (IsValid(Customer) == false)
	{
		return StolenProducts;
	}

	const UCSCustomerStateComponent* State = Customer->GetCustomerState();
	if (State == nullptr)
	{
		return StolenProducts;
	}

	// 같은 상품을 여러 개 훔쳤을 때 행이 갈라지지 않도록 상품ID로 접는다.
	TMap<FName, int32> RecordIndexByProductId;
	for (int32 i = 0; i < State->SelectProducts.Num(); i++)
	{
		const FCSSelectProduct& Selected = State->SelectProducts[i];

		// ProductRow.RowName이 ProductId다. 비어 있으면 분석 화면에서 행을 만들 수 없다.
		const FName ProductId = Selected.ProductRow.RowName;
		if (ProductId.IsNone())
		{
			continue;
		}

		if (const int32* ExistingIndex = RecordIndexByProductId.Find(ProductId))
		{
			FCSStolenProductRecord& Existing = StolenProducts[*ExistingIndex];
			if (Existing.Quantity < MAX_int32)
			{
				++Existing.Quantity;
			}
			continue;
		}

		// 손님이 파괴되면 ProductId로 이름을 되찾을 방법이 없으므로 지금 스냅샷을 뜬다.
		const FCSProductDataRow* ProductData = Selected.ProductRow.GetRow<FCSProductDataRow>(
			TEXT("UCSCustomerRuntimeSubsystem::GetStolenProductRecords"));

		FCSStolenProductRecord& NewRecord = StolenProducts.AddDefaulted_GetRef();
		NewRecord.ProductId = ProductId;
		// 행이 사라진 상품이라도 목록에서 빠지지 않게 ProductId를 표시 이름으로 대체한다.
		if (ProductData != nullptr)
		{
			NewRecord.DisplayName = ProductData->DisplayName;
		}
		else
		{
			NewRecord.DisplayName = FText::FromName(ProductId);
		}
		NewRecord.Quantity = 1;
		// 다음 번에 똑같은 상품이 나오면 바로 찾을 수 있도록 인덱스 사전에 등록
		RecordIndexByProductId.Add(ProductId, StolenProducts.Num() - 1);
	}

	return StolenProducts;
}

bool UCSCustomerRuntimeSubsystem::CanSubdueCustomer(const ACSCustomerCharacter* Customer) const
{
	if (IsValid(Customer) == false)
	{
		return false;
	}

	const UCSCustomerStateComponent* State = Customer->GetCustomerState();
	if (State == nullptr)
	{
		return false;
	}

	// 이미 쓰러진 손님을 다시 때려도 아무 일도 일어나지 않게 함
	if (State->IsSubdued())
	{
		return false;
	}

	// 일반 손님은 제압 대상이 아님, 기존 피격 디버그만 뜬다.
	if (State->PlannedVillainAction != ECSVillainAction::Shoplift)
	{
		return false;
	}

	// 실행 전 빌런은 겉보기에 일반 손님과 구분되지 않으므로 기본적으로 제외
	if (State->bVillainActionDone == false && bAllowSubdueBeforeShoplift == false)
	{
		return false;
	}

	// 정상적으로 계산을 마친 손님
	if (State->bCheckoutDone)
	{
		return false;
	}

	return true;
}

bool UCSCustomerRuntimeSubsystem::HandleMagicHitOnCustomer(ACSCustomerCharacter* Customer,const FVector& ImpactDirection,const float KnockbackStrength)
{
	if (CanSubdueCustomer(Customer) == false)
	{
		return false;
	}

	UCSCustomerStateComponent* State = Customer->GetCustomerState();

	// 방송은 첫 공격에서만 처리
	if (State->MarkSubdued() == false)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[제압] %s 는 이미 쓰러진 상태"), *Customer->GetName());
		return true;
	}

	// 쓰러진 빌런은 더 이상 추격 대상이 아니므로 외곽선을 끈다.
	Customer->SetVillainHighlight(false);

	// 회수액은 반드시 낙하 전에 계산한다.
	// DropCustomerItems가 SelectProducts를 비우기 때문에 뒤에서 계산하면 항상 0이 됨.
	int64 RecoveredGoldTotal = 0;
	for (int32 i = 0; i < State->SelectProducts.Num(); i++)
	{
		RecoveredGoldTotal += State->SelectProducts[i].SalePriceAtPickup;
	}
	const int32 RecoveredGold = static_cast<int32>(FMath::Min<int64>(RecoveredGoldTotal, MAX_int32));

	// 줄에서 맞았을 때 파괴된 손님이 대기열에 남아 뒷사람 순번이 밀리는 것을 막는다.
	LeaveQueue(Customer);

	// 훔친 상품을 전부 발밑에 떨어뜨린다. 목록과 장바구니 표시 정리까지 이 함수가 처리한다.
	// 쓰러지는 연출보다 먼저 하는 이유: 낙하 기준점이 손님에 붙은 화살표 컴포넌트라서
	// 나중에 랙돌로 몸이 뒤집힌 뒤에 부르면 상품이 땅밑이나 벽에 박힌다.
	const int32 DroppedCount = DropCustomerItems(Customer);

	// TODO(제압 연출): 랙돌/몬타주 작업 시 주석 해제. 상세는 VillainSubdue_Design.md 4-6절
	// 쓰러지는 연출을 넣을 자리. 연출이 후순위라 훅 자체를 주석 처리해 둔 상태
	// 종족별 분기는 캐릭터 쪽 C++ 구현이 담당

	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green,
			FString::Printf(TEXT("[빌런 제압] %s | 상품 %d개 회수 (%d G)"),
				*Customer->GetName(), DroppedCount, RecoveredGold));
	}

	// 제압 성공을 전파. 평판 상승이나 통계는 구독하는 쪽에서 처리한다.
	OnVillainSubdued.Broadcast(Customer, RecoveredGold);

	return true;
}



