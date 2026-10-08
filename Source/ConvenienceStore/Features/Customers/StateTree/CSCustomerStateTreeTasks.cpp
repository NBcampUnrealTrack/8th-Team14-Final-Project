#include "Features/Customers/StateTree/CSCustomerStateTreeTasks.h"
#include "StateTreeExecutionContext.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "Features/Customers/CSCustomerCharacter.h"
#include "Features/Customers/CSCustomerStateComponent.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Features/Customers/CSPurchaseDecisionComponent.h"
#include "Data/CSPurchaseDecisionTypes.h"
#include "Data/CSProductTypes.h"
#include "Data/CSCustomerArchetypeDataAsset.h"
#include "Features/Items/CSProductPickupActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Systems/StoreFlow/CSStoreGameState.h"

// =====================================================================
// PickNextShelf
// =====================================================================

namespace
{
	// 계획 상품 우선 방문 판정 결과. 값이 클수록 우선순위가 높음(정확 일치가 최우선)
	enum class ECSPlannedShelfMatch : uint8
	{
		None,
		Category,
		Exact
	};

	// 진열대 하나가 PlannedProductIds 중 어느 것과 일치하는지 판정
	// 정확 일치가 하나라도 있으면 그 즉시 Exact(카테고리 일치 여부는 더 볼 필요 없음)
	// 정확 일치가 없으면 같은 카테고리 상품이 하나라도 있는지 확인
	ECSPlannedShelfMatch EvaluateShelfForPlannedProducts(
		const UCSCustomerRuntimeSubsystem& Subsystem,
		AActor& ShelfActor,
		const TArray<FName>& PlannedProductIds,
		const UDataTable* ProductMasterDataTable)
	{
		const TArray<AActor*> ShelfProducts = Subsystem.GetProductsOnShelf(&ShelfActor);
		if (ShelfProducts.Num() == 0)
		{
			return ECSPlannedShelfMatch::None;
		}

		bool bCategoryMatchFound = false;

		for (const FName PlannedId : PlannedProductIds)
		{
			for (const AActor* ProductActor : ShelfProducts)
			{
				const ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(ProductActor);
				if (Product != nullptr && Product->GetProductId() == PlannedId)
				{
					// 정확 일치는 이 진열대의 최종 판정이므로 다른 계획 상품은 더 볼 필요 없음
					return ECSPlannedShelfMatch::Exact;
				}
			}

			// 이 계획 상품의 카테고리를 조회해 이미 카테고리 일치를 찾지 못한 경우에만 비교
			// (ProductMasterDataTable이 안 배정돼 있으면 카테고리 조회 자체가 불가능 - 대체 방문 없이 조용히 건너뜀)
			if (!bCategoryMatchFound && ProductMasterDataTable != nullptr)
			{
				const FCSProductDataRow* PlannedRow = ProductMasterDataTable->FindRow<FCSProductDataRow>(
					PlannedId, TEXT("EvaluateShelfForPlannedProducts"));
				if (PlannedRow != nullptr)
				{
					for (const AActor* ProductActor : ShelfProducts)
					{
						const ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(ProductActor);
						if (Product == nullptr)
						{
							continue;
						}
						const FCSProductDataRow* ShelfRow = Product->GetProductRow().GetRow<FCSProductDataRow>(
							TEXT("EvaluateShelfForPlannedProducts"));
						if (ShelfRow != nullptr && ShelfRow->Category == PlannedRow->Category)
						{
							bCategoryMatchFound = true;
							break;
						}
					}
				}
			}
		}

		return bCategoryMatchFound ? ECSPlannedShelfMatch::Category : ECSPlannedShelfMatch::None;
	}

	// PlannedId가 안 가본 진열대 어딘가에 같은 카테고리 상품을 하나라도 갖고 있는지 확인
	// ("카테고리 텅 빔" 판정 전용 - EvaluateShelfForPlannedProducts와 별도로 계획 상품 하나만 검사)
	// 계획 상품 그 자체가 아직 안 가본 진열대 어딘가에 남아 있는지
	// 도착한 진열대에 "같은 카테고리 다른 상품"만 있다고 바로 계획을 지우면,
	// 정작 그 상품이 있는 진열대가 아직 남아 있어도 손님이 영영 그리로 가지 않는다
	// 한 카테고리가 호환 안 되는 두 진열대로 갈릴 때 반드시 생긴다
	// (잎 우산은 ToolRack 전용인데 같은 Supply인 횃불·화살은 ScrollRack에 있는 식)
	// 손님의 방문 여부는 보지 않는다 - "이 물건을 우리가 파느냐"는 동선과 무관한 질문이고,
	// 이미 지나친 진열대에 놓인 것까지 세야 진열해 둔 물건을 두고 "없다"고 하는 일이 없다
	bool HasExactProductAnywhere(
		const UCSCustomerRuntimeSubsystem& Subsystem,
		const FName PlannedId)
	{
		for (const TObjectPtr<AActor>& Shelf : Subsystem.GetShelves())
		{
			if (!Subsystem.IsShelfSelectable(Shelf))
			{
				continue;
			}

			for (const AActor* ProductActor : Subsystem.GetProductsOnShelf(Shelf))
			{
				const ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(ProductActor);
				if (Product != nullptr && Product->GetProductId() == PlannedId)
				{
					return true;
				}
			}
		}
		return false;
	}

	// 계획 상품을 못 찾았을 때의 Severity. DT_PurchaseRejectionDisplay가 이 값으로 문구를 가른다
	// 1.0 = 매장에 없고 일반 발주도 불가능한 상품
	// 0.5 = 지금 이 손님이 못 찾았을 뿐 -> "지금 채워 넣어라"
	// 두 문구가 같으면 플레이어가 발주하러 갔다가 목록에 없어서 버그로 오해한다
	float CS_PlannedMissingSeverity(
		const UCSCustomerRuntimeSubsystem& Subsystem,
		const UCSPurchaseDecisionComponent& Decision,
		FName ProductId)
	{
		// 호출부가 전부 "매장에 없을 때만" 부르므로 정상적으로는 항상 false다.
		// true가 찍히면 어딘가에서 검사를 빠뜨린 것이니 로그를 보고 그 경로를 찾을 것 -
		// 그 사이에도 최소한 "안 판다"는 거짓말은 안 하도록 여기서 한 번 더 막는다
		const bool bInStore = HasExactProductAnywhere(Subsystem, ProductId);

		// 지금은 품절이지만 한 번이라도 진열했던 상품이면 "안 판다"가 아니라 "떨어졌다"이다.
		const bool bEverStocked = Subsystem.WasProductEverStocked(ProductId);

		const FCSProductDataRow* Row = IsValid(Decision.ProductMasterDataTable)
			? Decision.ProductMasterDataTable->FindRow<FCSProductDataRow>(ProductId, TEXT("CS_PlannedMissingSeverity"), false)
			: nullptr;

		// 구매 판단은 바꾸지 않고, 품절 안내에서 폐지된 레벨 조건만 제거합니다.
		const bool bOrderable = Row != nullptr && Row->bOrderable;

		// 셋 중 하나라도 맞으면 플레이어가 지금 손쓸 수 있는 상황이라 "채워 넣어라" 쪽이다.
		const float Severity = (bInStore || bEverStocked || bOrderable) ? 0.5f : 1.0f;

		// 같은 문구가 왜 이 손님한테만 다르게 떴는지는 조건이 셋(진열·이력·발주)이라
		// 화면만 봐서는 역추적이 안 된다. 판정에 쓴 값을 그대로 남긴다
		UE_LOG(LogTemp, Log,
			TEXT("[계획미발견] 시드=%d 상품=%s 매장진열=%s 진열이력=%s 발주가능=%s 문구=%s"),
			Decision.GetCustomerSeed(), *ProductId.ToString(),
			bInStore ? TEXT("있음") : TEXT("없음"),
			bEverStocked ? TEXT("있음") : TEXT("없음"),
			bOrderable ? TEXT("됨") : TEXT("안됨"),
			Severity >= 1.0f ? TEXT("안파는구나") : TEXT("없네"));

		return Severity;
	}

	// Category가 매장 어딘가에 재고로 남아있는지 확인 ("선호 카테고리 텅 빔" 판정 전용)
	// HasExactProductAnywhere와 같은 이유로 방문 여부는 보지 않는다 - 이미 지나친 진열대에
	// 놓인 것까지 세야 진열해 둔 물건을 두고 "없다"고 하는 일이 없다
	bool HasCategoryStockAnywhere(
		const UCSCustomerRuntimeSubsystem& Subsystem,
		ECSProductCategory Category)
	{
		for (const TObjectPtr<AActor>& Shelf : Subsystem.GetShelves())
		{
			if (!Subsystem.IsShelfSelectable(Shelf))
			{
				continue;
			}

			for (const AActor* ProductActor : Subsystem.GetProductsOnShelf(Shelf))
			{
				const ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(ProductActor);
				if (Product == nullptr)
				{
					continue;
				}
				const FCSProductDataRow* ShelfRow = Product->GetProductRow().GetRow<FCSProductDataRow>(
					TEXT("HasCategoryStockAnywhere"));
				if (ShelfRow != nullptr && ShelfRow->Category == Category)
				{
					return true;
				}
			}
		}
		return false;
	}

	bool HasCategoryMatchAnywhere(
		const UCSCustomerRuntimeSubsystem& Subsystem,
		const TArray<TObjectPtr<AActor>>& UnvisitedSelectableShelves,
		const FCSProductDataRow& PlannedRow)
	{
		for (AActor* Shelf : UnvisitedSelectableShelves)
		{
			for (const AActor* ProductActor : Subsystem.GetProductsOnShelf(Shelf))
			{
				const ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(ProductActor);
				if (Product == nullptr)
				{
					continue;
				}
				const FCSProductDataRow* ShelfRow = Product->GetProductRow().GetRow<FCSProductDataRow>(
					TEXT("HasCategoryMatchAnywhere"));
				if (ShelfRow != nullptr && ShelfRow->Category == PlannedRow.Category)
				{
					return true;
				}
			}
		}
		return false;
	}
}

EStateTreeRunStatus FCSTask_PickNextShelf::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();
	UCSCustomerStateComponent* State = Data.Actor->GetCustomerState();
	if (Subsystem == nullptr || State == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// MoveToShelf 이동 실패로 재진입한 경우 처리 (ST_Customer: MoveToShelf의 Failed 전이가 Shopping을 다시 겨냥함)
	// 직전에 골랐던 진열대(Data.TargetShelf)가 아직 VisitedShelves에 없다는 건
	// Browse까지 가보지 못했다(= MarkVisited를 부를 기회가 없었다 = MoveToShelf가 실패했다)는 신호
	// 이동 성공/실패와 무관하게 시도한 진열대는 여기서 방문 처리
	// 이게 없으면 내비메시가 막힌 진열대가 계속 안 가본 곳으로 남아 재선택되고
	// 손님 하나가 같은 진열대에 수백 번 재시도하며 MaxAlive 슬롯을 점유해 신규 스폰을 막음
	if (Data.TargetShelf != nullptr && State->HasVisited(Data.TargetShelf) == false)
	{
		State->MarkVisited(Data.TargetShelf);
	}

	AActor* PickedShelf = nullptr;
	UCSPurchaseDecisionComponent* Decision = Data.Actor->FindComponentByClass<UCSPurchaseDecisionComponent>();

	// 아직 매장 어딘가(안 가본 진열대)에 같은 카테고리가 남아있는 계획 상품의 카테고리 집합
	// 정확 일치를 못 찾았을 때 아래 폴백에서 연관구매 대상과 함께 "끌림" 후보로 합류시키는 데 쓴다
	TSet<ECSProductCategory> RemainingPlannedCategories;

	// 계획 상품이 남아있으면 정확 일치부터 찾는다 - "이 상품을 사러 왔다"는 가장 강한 신호라
	// 연관구매·카테고리 선호 가중치보다 항상 우선한다(찾으면 아래 폴백 자체를 타지 않음)
	if (State->PlannedProductIds.Num() > 0)
	{
		const UDataTable* ProductMasterDataTable = Decision != nullptr ? Decision->ProductMasterDataTable : nullptr;

		// 안 가본 방문 후보만 미리 추려서 아래 스캔들이 같은 필터링을 반복하지 않게 함
		TArray<TObjectPtr<AActor>> UnvisitedShelves;
		for (const TObjectPtr<AActor>& Shelf : Subsystem->GetShelves())
		{
			if (Subsystem->IsShelfSelectable(Shelf) && !State->HasVisited(Shelf))
			{
				UnvisitedShelves.Add(Shelf);
			}
		}

		for (const TObjectPtr<AActor>& Shelf : UnvisitedShelves)
		{
			const ECSPlannedShelfMatch Match = EvaluateShelfForPlannedProducts(
				*Subsystem, *Shelf, State->PlannedProductIds, ProductMasterDataTable);
			if (Match == ECSPlannedShelfMatch::Exact)
			{
				PickedShelf = Shelf;
				break;
			}
		}

		// 안 가본 진열대 어디에도 같은 카테고리가 없는 계획 상품은 "카테고리 텅 빔"으로 조용히 정리
		// (시각 반응 없이 45% 확률로 포기 - 대체품조차 없어서 더 찾아다닐 이유가 없는 케이스)
		// 살아남은(=매장 어딘가에 카테고리가 있는) 계획 상품은 RemainingPlannedCategories에 모아뒀다가
		// 정확 일치가 안 나왔을 때 아래 폴백의 끌림 대상으로 넘긴다
		// 위에서 이번 라운드에 어느 진열대로 갈지와 무관하게, 남은 모든 계획 상품에 대해 독립적으로 검사한다
		if (Decision != nullptr && ProductMasterDataTable != nullptr)
		{
			for (int32 i = State->PlannedProductIds.Num() - 1; i >= 0; i--)
			{
				const FName PlannedId = State->PlannedProductIds[i];
				const FCSProductDataRow* PlannedRow = ProductMasterDataTable->FindRow<FCSProductDataRow>(
					PlannedId, TEXT("FCSTask_PickNextShelf::EnterState"));
				if (PlannedRow == nullptr)
				{
					continue;
				}

				if (HasCategoryMatchAnywhere(*Subsystem, UnvisitedShelves, *PlannedRow))
				{
					RemainingPlannedCategories.Add(PlannedRow->Category);
					continue;
				}

				// 찾기를 포기한다. 반응은 여기서 띄우지 않고 퇴장길에 한 번만 몰아서 낸다 -
				// 진열대마다 띄우면 계획 2개짜리 손님이 같은 대사를 두세 번 한다
				State->PlannedProductIds.RemoveAt(i);
				State->UnfulfilledPlannedProductIds.AddUnique(PlannedId);

				// 포기 굴림은 항상 일어나야 한다 - 조건부로 만들면
				// 진열 상태에 따라 굴림 횟수가 달라져 같은 시드 결과가 밀린다
				if (Decision->RollChance(0.45f))
				{
					State->bForceStopShopping = true;
				}
			}
		}
	}

	if (PickedShelf == nullptr)
	{
		// 정확 일치를 못 찾은 경우 - 연관구매 대상 + 남은 계획 카테고리 + 선호 카테고리를 하드 필터로 합친다
		// (장바구니가 비어있으면 연관구매 대상이 없어 사실상 선호 카테고리로 직행하게 됨)
		const TSet<ECSProductCategory> AffinityTargets = Subsystem->GetAffinityTargetCategories(State->SelectProducts);
		TSet<ECSProductCategory> PullTargets = AffinityTargets;
		PullTargets.Append(RemainingPlannedCategories);
		if (Decision != nullptr)
		{
			PullTargets.Append(Decision->GetPreferredCategories());
		}

		TMap<ECSProductCategory, float> AttractionWeights =
			Decision != nullptr ? Decision->GetCategoryAttractionWeights() : TMap<ECSProductCategory, float>();

		// 장바구니 때문에 끌리는 카테고리는 손님이 가장 좋아하는 카테고리만큼 끌리게 한다
		// 평소 선호도로 뽑으면 비선호인 숙취해소제는 술을 담아도 매대를 거의 안 찾아간다
		// 차단된 카테고리는 0이라 그대로 둔다
		float TopWeight = 0.0f;
		for (const TPair<ECSProductCategory, float>& Pair : AttractionWeights)
		{
			TopWeight = FMath::Max(TopWeight, Pair.Value);
		}
		for (const ECSProductCategory Category : AffinityTargets)
		{
			float* Weight = AttractionWeights.Find(Category);
			if (Weight != nullptr && *Weight > 0.0f)
			{
				*Weight = FMath::Max(*Weight, TopWeight);
			}
		}

		PickedShelf = Subsystem->PickRandomUnvisitedShelf(State->VisitedShelves, PullTargets, AttractionWeights, Data.Actor);
	}
	if (PickedShelf == nullptr)
	{
		// 더 볼 진열대가 없음 → 상태 실패 → 전이(On Failed)로 다음 단계 결정
		return EStateTreeRunStatus::Failed;
	}

	// 설 자리는 진열대의 CustomerShoppingPoint 마커에서 받아옵니다.
	FVector BrowseLocation;
	FRotator BrowseFacing;
	float BrowseRadius = 0.0f;
	if (!Subsystem->TryGetShelfBrowsePoint(PickedShelf, BrowseLocation, BrowseFacing, BrowseRadius))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BrowseZone] %s: CustomerShoppingPoint 주변에 NavMesh가 없어 갈 수 없습니다. "
				 "마커를 NavObstacleBox 바깥의 걸을 수 있는 바닥 위로 옮기세요."),
			*PickedShelf->GetName());
		State->MarkVisited(PickedShelf);
		return EStateTreeRunStatus::Failed;
	}

	Data.TargetShelf = PickedShelf;
	Data.TargetLocation = BrowseLocation;

	// Succeeded를 반환하지 않고 Running을 반환합니다.
	// 그래야 이 상태가 종료되지 않고 계속 유지되면서 자식 상태(MoveTo, Browse)들이
	// 이 태스크의 Output 데이터(TargetShelf, TargetLocation)를 바인딩해서 읽어갈 수 있음
	return EStateTreeRunStatus::Running;
}

// =====================================================================
// MoveToLocation
// =====================================================================

EStateTreeRunStatus FCSTask_MoveToLocation::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	AAIController* Controller = Data.AIController;
	if (Controller == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 청결도 신호(버리기 진행률 / 목격 판정)의 기준점. 재진입 사이에 남아있는 값을 매번 새로 잡아야 함
	if (const APawn* Pawn = Controller->GetPawn())
	{
		Data.MoveStartLocation = Pawn->GetActorLocation();
	}
	Data.WitnessCheckElapsedTime = 0.0f;

	// 진열대로 가는 이동이면 진열대가 정한 영역 반경 안에서 멈춤
	float StopRadius = Data.AcceptanceRadius;
	if (Data.TargetShelf != nullptr)
	{
		if (const UCSCustomerRuntimeSubsystem* Subsystem =
			Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>())
		{
			FVector ZoneCenter;
			FRotator ZoneFacing;
			float ZoneRadius = 0.0f;
			if (Subsystem->TryGetShelfBrowsePoint(Data.TargetShelf, ZoneCenter, ZoneFacing, ZoneRadius))
			{
				StopRadius = ZoneRadius;
			}
		}
	}

	const EPathFollowingRequestResult::Type Result = Controller->MoveToLocation(Data.TargetLocation, StopRadius, false);

	if (Result == EPathFollowingRequestResult::Failed)
	{
		// 경로가 없거나 NavMesh 밖일 때 등 실패 반환
		return EStateTreeRunStatus::Failed;
	}
	if (Result == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		// 이미 목표 위치에 있으면 즉시 성공
		return EStateTreeRunStatus::Succeeded;
	}
	// 이동이 시작되면 Tick에서 검사를 이어가기 위해 Running 반환
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FCSTask_MoveToLocation::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	AAIController* Controller = Data.AIController;
	if (Controller == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	APawn* Pawn = Controller->GetPawn();

	// 청결도 신호(버리는 손님 / 보는 손님)는 진열대로 가는 이동에서만 판정한다
	if (Data.TargetShelf != nullptr && Pawn != nullptr)
	{
		ACSCustomerCharacter* Customer = Cast<ACSCustomerCharacter>(Pawn);
		UCSCustomerStateComponent* State = IsValid(Customer) ? Customer->GetCustomerState() : nullptr;
		UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();

		if (State != nullptr && Subsystem != nullptr)
		{
			// 버리는 손님 : 몇 번째 진열대 이동인지는 VisitedShelves.Num()+1로 유추한다.
			// Browse가 첫 라운드 진입 시에만 MarkVisited를 부르므로, 지금(이동 중)은
			// 아직 이번 진열대가 방문 목록에 없다 - PickNextShelf::EnterState의 재진입 처리 주석 참고
			if (State->bWillDropTrash && (State->VisitedShelves.Num() + 1) == State->TrashDropTravelIndex)
			{
				const float TotalDist = FVector::Dist2D(Data.MoveStartLocation, Data.TargetLocation);
				if (TotalDist > KINDA_SMALL_NUMBER)
				{
					const float Progress = FVector::Dist2D(Data.MoveStartLocation, Pawn->GetActorLocation()) / TotalDist;
					if (Progress >= State->TrashDropProgress)
					{
						Subsystem->SpawnTrashNearCustomer(Customer);
						// 하루 1회 보장 - 이후 이동에서 조건이 다시 참이 되어도 재발동하지 않음
						State->bWillDropTrash = false;
					}
				}
			}

			// 보는 손님 : 1초 간격으로만 캐시된 쓰레기와의 거리를 확인해 판정 비용을 억제한다.
			// 굴림 자체를 하루 1회로 제한한다(bHasAttemptedTrashWitnessRoll)
			if (!State->bTrashWitnessedToday && !State->bHasAttemptedTrashWitnessRoll)
			{
				Data.WitnessCheckElapsedTime += DeltaTime;
				if (Data.WitnessCheckElapsedTime >= 1.0f)
				{
					Data.WitnessCheckElapsedTime = 0.0f;

					if (Subsystem->IsTrashNearby(Pawn->GetActorLocation(), Subsystem->TrashWitnessRadius))
					{
						// 오늘 처음 쓰레기를 발견한 순간 - 성공/실패와 무관하게 시도 자체를 여기서 소진한다
						State->bHasAttemptedTrashWitnessRoll = true;

						if (Subsystem->RollTrashWitnessed())
						{
							State->bTrashWitnessedToday = true;
							Subsystem->NotifyTrashWitnessed(Customer);
						}
					}
				}
			}
		}
	}

	// 단순 버전의 MoveToLocation이라 완료 콜백이 없으므로 매 틱 폴링하여 이동 종료를 판단합니다
	if (Controller->GetMoveStatus() == EPathFollowingStatus::Idle)
	{
		if (Pawn == nullptr)
		{
			return EStateTreeRunStatus::Failed;
		}

		// 진열대로 가는 이동이면 "그 진열대의 영역(마커 중심 · BrowseZoneRadius) 안에 멈췄는가"가
		// 유일한 성공 조건입니다. 거리로 판정하면 진열대마다 기준이 달라집니다.
		if (Data.TargetShelf != nullptr)
		{
			const UCSCustomerRuntimeSubsystem* Subsystem =
				Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();
			if (Subsystem == nullptr)
			{
				return EStateTreeRunStatus::Failed;
			}

			// 손님이 진열대의 구경 영역 안에 있는지 확인.
			if (Subsystem->IsCustomerInsideBrowseZone(Data.TargetShelf, Pawn))
			{
				// 영역 안에 들어왔다면 이동 성공으로 판정
				return EStateTreeRunStatus::Succeeded;
			}
			else
			{
				// 영역 밖에 있다면 이동 실패로 판정
				return EStateTreeRunStatus::Failed;
			}
		}

		// 계산대·출구처럼 영역이 없는 이동은 기존 거리 판정을 사용
		const float Dist2D = FVector::Dist2D(Pawn->GetActorLocation(), Data.TargetLocation);

		// 손님의 현재 위치와 목표 위치 사이의 평면 거리가 허용 반경 이내인지 확인
		if (Dist2D <= Data.AcceptanceRadius)
		{
			// 허용 반경 안으로 들어왔다면 이동 성공으로 판정
			return EStateTreeRunStatus::Succeeded;
		}
		else
		{
			// 허용 반경 밖에서 멈췄다면 이동 실패로 판정
			return EStateTreeRunStatus::Failed;
		}
	}

	return EStateTreeRunStatus::Running;
}

void FCSTask_MoveToLocation::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (Data.AIController != nullptr)
	{
		// 상태를 떠날 때 이동 명령이 남아있으면 다음 상태에서도 걸어갈 수 있으므로 반드시 끊어야 함
		Data.AIController->StopMovement();
	}
}

// =====================================================================
// BrowseShelf
// =====================================================================

EStateTreeRunStatus FCSTask_BrowseShelf::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	// 재진입할 때마다 타이머/세션 상태 리셋이 필요하므로 여기서 초기화
	// (인스턴스 데이터는 재진입 사이에 값이 남아있으므로 명시적으로 0으로 되돌려야 함)
	Data.ElapsedTime = 0.0f;
	Data.RoundCount = 0;
	Data.bBudgetCharged = false;
	Data.Phase = ECSCustomerBrowsePhase::WaitingBeforeThink;
	Data.bThinkingAttemptedForRound = false;
	Data.bPendingTakeProductAfterReaction = false;
	Data.PendingProductActor = nullptr;

	if (Data.Actor == nullptr || Data.TargetShelf == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}
	return EStateTreeRunStatus::Running;
}

// 평판 가격 불쾌가 감점을 시작하는 강도와 같다. 이보다 약한 반응은 띄울 이유가 없다
static constexpr float CS_MinPriceReactionSeverity = 0.4f;

// 느낌표는 플레이어가 고칠 수 있는 가격 부담과 물건 없음에만 띄운다
// 카테고리 차단은 도감에, 뉴스는 게시판에 이미 나와서 띄우면 소음만 늘어난다
static bool CS_ShouldShowReaction(const FCSPurchaseRejectionInfo& Info)
{
	switch (Info.Reason)
	{
	case ECSPurchaseRejectReason::PriceRejected:
		return Info.Severity >= CS_MinPriceReactionSeverity;
	case ECSPurchaseRejectReason::PlannedProductMissing:
	case ECSPurchaseRejectReason::PreferredCategoryMissing:
		return true;
	default:
		return false;
	}
}

// 거절 즉시표시(머리 위 아이콘) + LastRejectionInfo/LastRejectionTime 기록 +
// NotifyPurchaseRejected 방송을 한 번에 처리하는 공용 절차. 빈 진열대 분기와 정상 거절 분기가 동일하게 씀
static void CS_HandlePurchaseRejection(
	FCSTask_BrowseShelfInstanceData& Data,
	ACSCustomerCharacter& Customer,
	UCSCustomerStateComponent& State,
	UCSCustomerRuntimeSubsystem& Subsystem,
	UCSPurchaseDecisionComponent& Decision,
	const FCSPurchaseRejectionInfo& RejectionInfo,
	UWorld* World)
{
	const float DisplayDuration = Decision.GetInstantDisplayDuration(RejectionInfo.Reason);
	State.LastRejectionInfo = RejectionInfo;
	State.LastRejectionTime = World != nullptr ? World->GetTimeSeconds() : 0.0f;
	State.MarkReactedTo(RejectionInfo.ProductId);

	Customer.ShowHeadIcon(Decision.GetRejectionDisplayTier(RejectionInfo).Icon != nullptr, DisplayDuration);

	Subsystem.NotifyPurchaseRejected(&Customer, RejectionInfo);

	Data.Phase = ECSCustomerBrowsePhase::WaitingReaction;
	Data.bPendingTakeProductAfterReaction = false;
	Data.PendingProductActor = nullptr;
	Data.ElapsedTime = 0.0f;
}

// 구매는 결정됐지만 가격에 대한 반응이 있을 때
// 즉시 담지 않고 멈춰서 머리 위 아이콘 먼저 보여준 뒤 대기가 끝나면 그때 담음
static void CS_HandlePriceReactionThenBuy(
	FCSTask_BrowseShelfInstanceData& Data,
	ACSCustomerCharacter& Customer,
	UCSCustomerStateComponent& State,
	UCSPurchaseDecisionComponent& Decision,
	const FCSPurchaseRejectionInfo& ReactionInfo,
	AActor* ProductActor,
	UWorld* World)
{
	const float DisplayDuration = Decision.GetInstantDisplayDuration(ReactionInfo.Reason);
	State.LastRejectionInfo = ReactionInfo;
	State.LastRejectionTime = World != nullptr ? World->GetTimeSeconds() : 0.0f;
	State.MarkReactedTo(ReactionInfo.ProductId);

	Customer.ShowHeadIcon(Decision.GetRejectionDisplayTier(ReactionInfo).Icon != nullptr, DisplayDuration);

	// 가격 반응은 구매 성공 여부와 무관하게 평판에 영향을 준다 - 구매가 성사돼도 이 방송은 그대로 나가야 함
	if (UCSCustomerRuntimeSubsystem* Subsystem = World != nullptr ? World->GetSubsystem<UCSCustomerRuntimeSubsystem>() : nullptr)
	{
		Subsystem->NotifyPurchaseRejected(&Customer, ReactionInfo);
	}

	Data.Phase = ECSCustomerBrowsePhase::WaitingReaction;
	Data.bPendingTakeProductAfterReaction = true;
	Data.PendingProductActor = ProductActor;
	Data.ElapsedTime = 0.0f;
}

// 상품을 담은 직후(즉시 담았든, 가격 반응 대기 후에 담았든) 공통으로 수행하는 세션 지속 여부 판단
// 라운드상한 -> 목표달성 -> 예산바닥 -> 확률탈락 순으로 검사, 전부 통과하면 다음 라운드로 Running 반환
// 두 진입 경로(즉시 구매 / 대기 후 구매)가 이 판단을 중복 작성하지 않도록 뽑아냄
static EStateTreeRunStatus CS_EvaluateSessionContinuation(
	FCSTask_BrowseShelfInstanceData& Data,
	ACSCustomerCharacter& Customer,
	AActor& ShelfActor,
	UCSCustomerStateComponent& State,
	UCSCustomerRuntimeSubsystem& Subsystem,
	UCSPurchaseDecisionComponent& Decision)
{
	// 안전 상한 - 무한 루프 방지용 순수 방어 코드
	if (Data.RoundCount >= Data.MaxSessionRounds)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	// 목표 개수(PlannedBasketTarget)를 채웠으면 세션 종료
	const int32 ItemsInBasket = State.SelectProducts.Num();
	if (ItemsInBasket >= State.PlannedBasketTarget)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	// 소프트/하드 예산 중 하나라도 바닥나면 더 둘러봐도 살 수 없으므로 종료
	if (State.GetRemainingSoftBudget() <= 0 || State.GetRemainingHardWallet() <= 0)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	// 위 종료 조건에 안 걸리면 하나 더 담을 의욕이 있는지 확률적으로 판단
	// (FCSCond_WantsMoreItems에 있던 RollContinueShopping 판정이 여기로 옮겨옴)
	if (!Decision.RollContinueShopping(ItemsInBasket))
	{
		return EStateTreeRunStatus::Succeeded;
	}

	// 더 끌릴 게 없으면 머물지 않고 다음 진열대로 넘어간다 - 그래야 PickNextShelf의
	// 카테고리 선호·연관구매 가중치가 동선에 반영된다
	const TArray<FCSPurchaseCandidate> RemainingCandidates = Subsystem.BuildPurchaseCandidates(&ShelfActor);
	if (!Decision.HasWorthwhileCandidateOnShelf(RemainingCandidates))
	{
		return EStateTreeRunStatus::Succeeded;
	}

	// 계속 쇼핑 -> 다음 라운드까지 SessionInterval만큼 대기 (ElapsedTime은 호출부에서 이미 0으로 리셋됨)
	return EStateTreeRunStatus::Running;
}

// Pick 종료 뒤 보관해 둔 상품을 다시 검증해서 가져간 후 기존 세션 지속 판정을 그대로 실행합니다.
static EStateTreeRunStatus CS_CompletePendingProductTake(
	FCSTask_BrowseShelfInstanceData& Data,
	ACSCustomerCharacter& Customer,
	AActor* ShelfActor,
	UCSCustomerStateComponent& State,
	UCSCustomerRuntimeSubsystem& Subsystem,
	UCSPurchaseDecisionComponent& Decision)
{
	AActor* PendingProduct = Data.PendingProductActor;
	Data.PendingProductActor = nullptr;
	Data.bPendingTakeProductAfterReaction = false;
	Data.Phase = ECSCustomerBrowsePhase::WaitingBeforeThink;
	Data.bThinkingAttemptedForRound = false;
	Data.ElapsedTime = 0.0f;

	// Pick 재생 중 다른 손님이 먼저 가져갔을 수 있으므로 실제 획득 직전에 다시 확인합니다.
	if (!IsValid(PendingProduct))
	{
		return EStateTreeRunStatus::Succeeded;
	}

	// TakeProduct는 검증 실패 시 NAME_None, 실제 획득 완료 시 ProductId를 반환합니다.
	const FName TakenProductId =
		Subsystem.TakeProduct(ShelfActor, PendingProduct, &Customer);
	if (TakenProductId.IsNone())
	{
		return EStateTreeRunStatus::Succeeded;
	}

	Data.RoundCount++;
	return CS_EvaluateSessionContinuation(Data, Customer, *ShelfActor, State, Subsystem, Decision);
}

// 구매 확정 상품을 기억하고 Pick을 시작합니다. 애니메이션이 없으면 기존 즉시 획득으로 폴백합니다.
static EStateTreeRunStatus CS_BeginPickOrTake(
	FCSTask_BrowseShelfInstanceData& Data,
	ACSCustomerCharacter& Customer,
	AActor* ShelfActor,
	AActor* ProductActor,
	UCSCustomerStateComponent& State,
	UCSCustomerRuntimeSubsystem& Subsystem,
	UCSPurchaseDecisionComponent& Decision)
{
	Data.PendingProductActor = ProductActor;
	if (Customer.PlayPickAnimation())
	{
		Data.Phase = ECSCustomerBrowsePhase::Picking;
		Data.ElapsedTime = 0.0f;
		return EStateTreeRunStatus::Running;
	}

	// Thinking Hold 중 Pick이 누락되면 Transform을 먼저 원복하고 기존 즉시 획득으로 폴백합니다.
	Customer.ReleaseActionTransitionHold();
	return CS_CompletePendingProductTake(
		Data, Customer, ShelfActor, State, Subsystem, Decision);
}

EStateTreeRunStatus FCSTask_BrowseShelf::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Data.ElapsedTime += DeltaTime;

	ACSCustomerCharacter* Customer = Data.Actor;
	AActor* ShelfActor = Data.TargetShelf;
	if (!IsValid(Customer) || !IsValid(ShelfActor) || Context.GetWorld() == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	UCSCustomerStateComponent* State = Customer->GetCustomerState();
	UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();

	if (State == nullptr || Subsystem == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Decision 조회를 여기로 옮김
	UCSPurchaseDecisionComponent* Decision = Customer->FindComponentByClass<UCSPurchaseDecisionComponent>();
	if (Decision == nullptr)
	{
		// 이 손님에게 판단 컴포넌트가 붙어있지 않으면 세션 자체를 진행할 수 없으므로 여기서 종료
		Customer->ReleaseActionTransitionHold();
		return EStateTreeRunStatus::Succeeded;
	}

	// Thinking/Pick은 캐릭터가 재생 길이로 관리하므로 종료될 때까지 구매 판단/상품 획득을 진행하지 않습니다.
	if (Data.Phase == ECSCustomerBrowsePhase::Thinking)
	{
		if (Customer->IsActionAnimationPlaying())
		{
			return EStateTreeRunStatus::Running;
		}

		Data.Phase = ECSCustomerBrowsePhase::Evaluating;
		Data.ElapsedTime = 0.0f;
	}
	else if (Data.Phase == ECSCustomerBrowsePhase::Picking)
	{
		if (Customer->IsActionAnimationPlaying())
		{
			return EStateTreeRunStatus::Running;
		}

		return CS_CompletePendingProductTake(
			Data, *Customer, ShelfActor, *State, *Subsystem, *Decision);
	}

	// 거절/가격반응 표시가 떠 있는 동안 대기 - 손님이 멈춰 서는 시간(GetReactionHoldDuration)은
	// 아이콘/속마음이 떠 있는 시간(GetInstantDisplayDuration)보다 짧다. 반응 하나로 너무 오래 얼어있지 않게 함
	if (Data.Phase == ECSCustomerBrowsePhase::WaitingReaction)
	{
		if (Data.ElapsedTime < Decision->GetReactionHoldDuration(State->LastRejectionInfo.Reason))
		{
			return EStateTreeRunStatus::Running;
		}

		Data.ElapsedTime = 0.0f;

		// 대기가 실제 거절이 아니라 가격 반응은 있었지만 결국 사는 케이스였다면
		// 이제서야 상품을 담고 세션을 이어간다 (CS_HandlePriceReactionThenBuy가 걸어둔 플래그)
		if (Data.bPendingTakeProductAfterReaction)
		{
			Data.bPendingTakeProductAfterReaction = false;
			return CS_BeginPickOrTake(
				Data,
				*Customer,
				ShelfActor,
				Data.PendingProductActor,
				*State,
				*Subsystem,
				*Decision);
		}

		// 그 외에는 기존 그대로 - 실제 거절이었으므로 아무것도 안 담고 세션 종료
		return EStateTreeRunStatus::Succeeded;
	}

	// 첫 라운드(세션 진입)에 재고가 없으면 세션 자체를 시작하지 않고 GlanceTime만큼만 짧게 머물다 지나감
	// RoundCount == 0으로 제한하는 이유: 세션 도중(라운드 1 이상)에 다른 손님이 재고를 다 사가서 없어진 경우는
	// 여기로 다시 빠지지 않고, 아래 BuildPurchaseCandidates가 빈 배열을 만들어 DecidePurchase가 자연히
	// false(미구매)를 반환하며 세션이 끝나도록 둔다
	if (Data.Phase == ECSCustomerBrowsePhase::WaitingBeforeThink &&
		Data.RoundCount == 0 && !Subsystem->HasShelfStock(ShelfActor))
	{
		if (Data.ElapsedTime < Data.GlanceTime)
		{
			return EStateTreeRunStatus::Running;
		}

		State->MarkVisited(ShelfActor);
		// 진열대가 비어있어도 와서 확인하는 데는 시간이 들었다고 보고 최소 1만큼 차감 (기존 else 분기 그대로 유지)
		State->RemainingEvaluationBudget = FMath::Max(State->RemainingEvaluationBudget - 1, 0);

		// 재고 없음은 흔한 일이라 반응 연출 없이 조용히 세션을 끝낸다
		return EStateTreeRunStatus::Succeeded;
	}

	// 계획 상품 우선방문 결과 처리
	if (Data.Phase == ECSCustomerBrowsePhase::WaitingBeforeThink &&
		Data.RoundCount == 0 && !State->HasVisited(ShelfActor) && State->PlannedProductIds.Num() > 0)
	{
		if (Data.ElapsedTime < Data.GlanceTime)
		{
			return EStateTreeRunStatus::Running;
		}

		State->MarkVisited(ShelfActor);

		const TArray<FCSPurchaseCandidate> EarlyCandidates = Subsystem->BuildPurchaseCandidates(ShelfActor);

		// 1) 정확 일치: 반응 없이 조용히 "확인 완료" 처리. 실제로 사든 안 사든(대기줄만 확보) 아래
		// 정상 DecidePurchase 파이프라인에 맡기고, 계획으로서의 역할은 여기서 끝난다
		for (int32 i = State->PlannedProductIds.Num() - 1; i >= 0; i--)
		{
			const FName PlannedId = State->PlannedProductIds[i];
			const bool bExactMatch = EarlyCandidates.ContainsByPredicate([PlannedId](const FCSPurchaseCandidate& Candidate)
			{
				return Candidate.ProductRowHandle.RowName == PlannedId;
			});
			if (bExactMatch)
			{
				State->PlannedProductIds.RemoveAt(i);
			}
		}

		// 2) 카테고리만 일치(정확 일치 없음): PlannedProductMissing 반응 + 15% 포기 굴림
		// 한 진열대 방문당 반응은 최대 1회 - 첫 번째로 발견된 계획 상품만 처리하고 break
		for (int32 i = 0; i < State->PlannedProductIds.Num(); i++)
		{
			const FName PlannedId = State->PlannedProductIds[i];
			const FCSProductDataRow* PlannedRow = (Decision->ProductMasterDataTable != nullptr)
				? Decision->ProductMasterDataTable->FindRow<FCSProductDataRow>(PlannedId, TEXT("FCSTask_BrowseShelf::PlannedProductCheck"))
				: nullptr;
			if (PlannedRow == nullptr)
			{
				continue;
			}

			const bool bCategoryMatch = EarlyCandidates.ContainsByPredicate([PlannedRow](const FCSPurchaseCandidate& Candidate)
			{
				const FCSProductDataRow* CandidateRow = Candidate.ProductRowHandle.GetRow<FCSProductDataRow>(TEXT("FCSTask_BrowseShelf::PlannedProductCheck"));
				return CandidateRow != nullptr && CandidateRow->Category == PlannedRow->Category;
			});
			if (!bCategoryMatch)
			{
				continue;
			}

			// 이 진열대에 계획 상품이 없다고 해서 매장에 없는 게 아니다 -
			// 매장 어딘가에 실제로 남아 있으면 계획을 살려두고 반응도 띄우지 않는다.
			// 이미 지나친 진열대에 있는 경우까지 포함한다 - 갖다놓은 물건을 두고 "없다"고 하면
			// 플레이어에게는 거짓 신호다
			// 이 검사가 없으면 계획을 2개 가진 손님이 한쪽 진열대에 도착하는 순간
			// 나머지 하나를 잃는다(같은 카테고리인데 진열대가 갈려 있을 때)
			if (HasExactProductAnywhere(*Subsystem, PlannedId))
			{
				continue;
			}

			// 반응은 여기서 띄우지 않는다 - 퇴장길에 한 번만 몰아서 낸다.
			// 진열대마다 띄우면 같은 손님이 진열대와 문 앞에서 같은 대사를 반복한다
			State->PlannedProductIds.RemoveAt(i);
			State->UnfulfilledPlannedProductIds.AddUnique(PlannedId);

			// 대체품이 있는 케이스라 포기확률은 항상 15%(고정) - 지금 서 있는 이 진열대 자체가
			// 같은 카테고리 상품을 갖고 있으므로 대체품 유무를 다시 따질 필요가 없음
			if (Decision->RollChance(0.15f))
			{
				// 표시가 없어져 대기 상태로 넘길 이유가 사라졌다. 플래그만 세우면
				// 다음 WantsMoreItems 판정에서 세션이 정상적으로 끝난다
				State->bForceStopShopping = true;
			}

			// 한 진열대에서 여러 계획 상품을 한꺼번에 정리하지 않는다 -
			// 대체품을 실제로 담을지는 이후 정상 Thinking/BrowseTime → 평가 흐름에 맡긴다
			break;
		}
	}

	if (Data.Phase == ECSCustomerBrowsePhase::WaitingBeforeThink)
	{
		// 첫 라운드는 Thinking이 기존 BrowseTime을 대체합니다. 누락됐을 때만 BrowseTime을 기다립니다.
		if (Data.RoundCount == 0 && !Data.bThinkingAttemptedForRound)
		{
			Data.bThinkingAttemptedForRound = true;
			if (Customer->PlayThinkingAnimation())
			{
				Data.Phase = ECSCustomerBrowsePhase::Thinking;
				Data.ElapsedTime = 0.0f;
				return EStateTreeRunStatus::Running;
			}

			Data.ElapsedTime = 0.0f;
		}

		const float RequiredWait = (Data.RoundCount == 0) ? Data.BrowseTime : Data.SessionInterval;
		if (Data.ElapsedTime < RequiredWait)
		{
			return EStateTreeRunStatus::Running;
		}

		// 추가 라운드는 기존 SessionInterval이 지난 뒤 Thinking을 다시 한 번 재생합니다.
		if (Data.RoundCount > 0 && !Data.bThinkingAttemptedForRound)
		{
			Data.bThinkingAttemptedForRound = true;
			if (Customer->PlayThinkingAnimation())
			{
				Data.Phase = ECSCustomerBrowsePhase::Thinking;
				Data.ElapsedTime = 0.0f;
				return EStateTreeRunStatus::Running;
			}
		}

		Data.Phase = ECSCustomerBrowsePhase::Evaluating;
		Data.ElapsedTime = 0.0f;
	}

	// 사든 안 사든 방문한 건 사실이므로 첫 라운드에서 한 번만 기록
	// (재방문은 하지 않는다는 원칙 - HasUnvisitedShelf가 이 기록으로 게이트 역할을 함)
	if (Data.RoundCount == 0)
	{
		State->MarkVisited(ShelfActor);
	}

	// 진열대 위 상품들을 판단용 후보 목록으로 변환
	// 캐싱하지 않고 매 라운드 재조회 - 그 사이 다른 손님이 먼저 집어간 상품도 자연스럽게 반영됨
	const TArray<FCSPurchaseCandidate> Candidates = Subsystem->BuildPurchaseCandidates(ShelfActor);

	// 실제로 비교 대상에 오르는 개수는 상위 TopCandidateCount개로 잘리므로(ScoreAndSelectTopCandidates)
	// 봤다고 칠 개수도 진열대 상품 총개수가 아니라 그 상한을 넘지 않도록 함
	if (!Data.bBudgetCharged)
	{
		const int32 EvaluatedCount = FMath::Min(Candidates.Num(), Decision->GetEffectiveTopCandidateCount());
		State->RemainingEvaluationBudget = FMath::Max(State->RemainingEvaluationBudget - EvaluatedCount, 0);
		Data.bBudgetCharged = true;
	}

	// 후보 중 하나를 확률적으로 선택하거나 다 별로면 미구매(false) 반환
	FCSPurchaseCandidate Selected;
	FCSPurchaseRejectionInfo RejectionInfo;
	const bool bPicked = Decision->DecidePurchase(Candidates, Selected, RejectionInfo);

	if (!bPicked)
	{
		// 띄우지 않는 사유는 조용히 미구매 처리
		// 이미 반응한 상품도 같이 묶는다 - 표시만 막고 대기를 남기면 아이콘 없이 멈춰 선다
		if (!CS_ShouldShowReaction(RejectionInfo) ||
			State->HasReactedTo(RejectionInfo.ProductId))
		{
			Customer->ReleaseActionTransitionHold();
			return EStateTreeRunStatus::Succeeded;
		}

		// 그 외 사유는 거절 연출 후 대기상태 진입. Hold를 여기서 풀면 포즈 스냅이 아이콘 등장과
		// 겹쳐 보이므로, ExitState에서 자연히 풀리게 유지한다
		CS_HandlePurchaseRejection(Data, *Customer, *State, *Subsystem, *Decision, RejectionInfo, Context.GetWorld());
		return EStateTreeRunStatus::Running;
	}

	// 가격 반응(Reason=PriceRejected, 무반응이면 None)이 있으면 즉시 담지 않고 먼저 멈춰서
	// 보여준 뒤(CS_HandlePurchaseRejection과 동일한 대기), 대기가 끝나면 그때 담는다
	// (위 WaitingReaction 단계의 bPendingTakeProductAfterReaction 처리가 이어받음)
	// 이미 반응한 상품이면 멈추지 않고 바로 담는다
	if (CS_ShouldShowReaction(RejectionInfo) &&
		!State->HasReactedTo(RejectionInfo.ProductId))
	{
		// Hold를 미리 풀지 않는다 - Pick 시작 시 PlayPickAnimation이 덮어쓰고, 아니면 ExitState가 정리한다
		CS_HandlePriceReactionThenBuy(Data, *Customer, *State, *Decision, RejectionInfo, Selected.ProductActor, Context.GetWorld());
		return EStateTreeRunStatus::Running;
	}

	// 무반응 구매도 Pick 종료 후에만 상품을 가져갑니다. Pick 누락 시에는 즉시 획득으로 폴백합니다.
	return CS_BeginPickOrTake(
		Data,
		*Customer,
		ShelfActor,
		Selected.ProductActor,
		*State,
		*Subsystem,
		*Decision);
}

void FCSTask_BrowseShelf::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (IsValid(Data.Actor))
	{
		// 강제 퇴장/제압 등 상위 전이는 진행 중인 상품 행동을 즉시 중단할 수 있어야 합니다.
		Data.Actor->CancelActionAnimation();
	}

	Data.ElapsedTime = 0.0f;
	Data.Phase = ECSCustomerBrowsePhase::WaitingBeforeThink;
	Data.bThinkingAttemptedForRound = false;
	Data.bPendingTakeProductAfterReaction = false;
	Data.PendingProductActor = nullptr;
}

// =====================================================================
// WaitInQueue
// =====================================================================

// 대기줄 슬롯으로 이동시킵니다
void FCSTask_WaitInQueue::MoveToQueueSlot(
	AAIController& Controller,
	const UCSCustomerRuntimeSubsystem& Subsystem,
	const int32 Index)
{
	// 계산대에게 직접 "몇 거리에서 멈춰야 손님끼리 안 겹치는지" 확인
	const float CounterStopRadius = Subsystem.GetQueueSlotStopRadius();

	// 0 이하는 정상 값이 아니라서 방어 코드
	if (CounterStopRadius <= 0.0f)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[대기줄] 계산대가 정지 반경을 주지 않았습니다(%.1f). ")
			TEXT("ICSCustomerCounterInterface::GetQueueSlotStopRadius 구현이 필요합니다. 슬롯=%d | 손님=%s"),
			CounterStopRadius, Index, *GetNameSafe(Controller.GetPawn()));
	}

	FVector SlotCenter;
	float SlotRadius = 0.0f;
	if (Subsystem.TryGetQueueSlotZone(Index, SlotCenter, SlotRadius) && SlotRadius > 0.0f)
	{
		// 계산대가 이미 판정 반경 이하로 잘라서 주지만, 불변식을 어기는 구현체가 새로 생겨도
		// 위의 정지 버그가 돌아오지 않도록 호출부에서도 한 번 더 막습니다.
		// 정지 반경을 못 받았으면 판정 반경을 사용
		float AcceptanceRadius = SlotRadius;
		if (CounterStopRadius > 0.0f)
		{
			AcceptanceRadius = FMath::Min(CounterStopRadius, SlotRadius);
		}

		Controller.MoveToLocation(SlotCenter, AcceptanceRadius, false);
		return;
	}

	// 영역 정보를 주지 않는 계산대는 기존 동작을 유지합니다.
	// 정지 반경이 없으면 엔진 기본값을 넘겨 손님 NavAgent 반지름에서 유도되게 합니다.
	float FinalStopRadius = UPathFollowingComponent::DefaultAcceptanceRadius;
	if (CounterStopRadius > 0.0f)
	{
		FinalStopRadius = CounterStopRadius;
	}

	Controller.MoveToLocation(Subsystem.GetQueueSlotLocation(Index), FinalStopRadius);
}

// 순번을 받기 전 단계입니다. 줄 꼬리의 집결 지점으로 보냅니다.
void FCSTask_WaitInQueue::MoveToQueueEntry(AAIController& Controller,const UCSCustomerRuntimeSubsystem& Subsystem)
{
	const float CounterStopRadius = Subsystem.GetQueueSlotStopRadius();

	FVector EntryCenter;
	float EntryRadius = 0.0f;
	if (Subsystem.TryGetQueueEntryZone(EntryCenter, EntryRadius) && EntryRadius > 0.0f)
	{
		// 정지 반경이 집결 지점 판정 반경을 넘지 않도록 제한
		// 계산대 정지 반경이 없으면 집결 지점 반경을 사용
		float AcceptanceRadius = EntryRadius;
		if (CounterStopRadius > 0.0f)
		{
			AcceptanceRadius = FMath::Min(CounterStopRadius, EntryRadius);
		}

		Controller.MoveToLocation(EntryCenter, AcceptanceRadius, false);
		return;
	}

	// 집결 지점 영역이 없으면 슬롯 좌표를 이용해 이동
	// 계산대 정지 반경이 없으면 엔진 기본값을 사용
	float FinalStopRadius = UPathFollowingComponent::DefaultAcceptanceRadius;
	if (CounterStopRadius > 0.0f)
	{
		FinalStopRadius = CounterStopRadius;
	}

	Controller.MoveToLocation(
		Subsystem.GetQueueSlotLocation(Subsystem.GetCommittedQueueLength()), FinalStopRadius);
}

EStateTreeRunStatus FCSTask_WaitInQueue::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr || Data.AIController == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();
	if (Subsystem == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 순번이 확정된 인원만으로 정원이 찼을 때만 여기서 바로 포기합니다.
	// 줄이 다 찬 것을 보고 아예 서지 않고 나가는 손님입니다.
	// 줄에 자리가 남아 있으면 걸어오는 인원이 몇 명이든 일단 집결 지점으로 출발하고,
	// 도착했을 때 자리를 못 잡은 손님은 ResolvePendingQueueCommits에서 거부됩니다.
	// 아직 줄에 들어가지 않았으므로 AbandonCheckout 내부의 LeaveQueue는 무동작입니다.
	if (!Subsystem->BeginQueueApproach(Data.Actor))
	{
		// 등록에 실패하면 계산을 포기해 Checkout 상태 재진입을 방지
		// 아직 대기하지 않았으므로 대기 시간은 0초로 전달
		Subsystem->AbandonCheckout(Data.Actor, 0.0f);
		return EStateTreeRunStatus::Failed;
	}

	// 집결 지점 도착 후 순번을 확정하도록 미확정 상태로 초기화
	Data.CachedQueueIndex = -1;
	// 집결 지점 변경을 확인하기 위해 현재 확정 인원 수를 보관
	Data.CachedEntryTargetCount = Subsystem->GetCommittedQueueLength();
	// 상태 재진입 시 이전 대기 시간과 접근 시간을 초기화
	Data.ElapsedWaitTime = 0.0f;
	Data.ElapsedApproachTime = 0.0f;
	// 순번 확정 전 집결 지점으로 이동
	MoveToQueueEntry(*Data.AIController, *Subsystem);

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FCSTask_WaitInQueue::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	UCSCustomerStateComponent* State = nullptr;
	if (Data.Actor != nullptr)
	{
		State = Data.Actor->GetCustomerState();
	}

	UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();

	if (State == nullptr || Subsystem == nullptr || Data.AIController == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 계산 완료 → 성공 (계산대의 CompleteFrontCheckout이 플래그 킴)
	if (State->bCheckoutDone)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	// 순번 확정 전 집결 지점 접근을 처리
	const int32 QueueIndex = Subsystem->GetQueueIndex(Data.Actor);
	if (QueueIndex < 0)
	{
		// 대기열과 접근 목록에 모두 없으면 계산을 포기해 무한 대기를 방지
		if (!Subsystem->IsApproachingQueue(Data.Actor))
		{
			Subsystem->AbandonCheckout(Data.Actor, 0.0f);
			return EStateTreeRunStatus::Failed;
		}

		// 집결 지점 접근 시간을 누적하고 제한 시간 초과 시 계산을 포기
		Data.ElapsedApproachTime += DeltaTime;
		if (Data.MaxApproachSeconds > 0.0f &&
			Data.ElapsedApproachTime >= Data.MaxApproachSeconds)
		{
			Subsystem->AbandonCheckout(Data.Actor, Data.ElapsedWaitTime);
			return EStateTreeRunStatus::Failed;
		}

		// 확정 인원 수가 바뀌면 새 집결 지점으로 이동을 다시 요청
		const int32 CommittedCount = Subsystem->GetCommittedQueueLength();
		if (CommittedCount != Data.CachedEntryTargetCount)
		{
			Data.CachedEntryTargetCount = CommittedCount;
			MoveToQueueEntry(*Data.AIController, *Subsystem);
		}

		// 집결 지점에 도착한 손님을 순번 확정 후보로 등록
		// 후보 간 거리 비교와 순번 확정은 서브시스템 틱에서 일괄 처리
		if (Subsystem->IsCustomerInsideQueueEntryZone(Data.Actor))
		{
			Subsystem->RequestQueueCommit(Data.Actor);
		}

		return EStateTreeRunStatus::Running;
	}

	// 순번 확정 후에는 확정된 순서를 유지하며 슬롯 이동을 처리
	// 순번을 처음 받거나 앞사람 이탈로 순번이 당겨지면 이동 목표를 변경
	if (QueueIndex != Data.CachedQueueIndex)
	{
		Data.CachedQueueIndex = QueueIndex;
		Data.ElapsedApproachTime = 0.0f;
		MoveToQueueSlot(*Data.AIController, *Subsystem, QueueIndex);
	}

	// 자기 자리에 도착했는지가 인내심과 접근 타임아웃을 가르는 기준입니다.
	const bool bArrivedAtSlot = Subsystem->IsCustomerAtQueueSlot(Data.Actor);

	if (!bArrivedAtSlot)
	{
		// 아직 걸어오는 중입니다. 인내심은 흐르지 않습니다.
		// 매장 끝에서 계산대까지 걸어오는 시간은 플레이어 책임이 아니기 때문입니다.
		Data.ElapsedApproachTime += DeltaTime;

		// 다만 인내심이 안 흐른다는 이유로 못 가는 손님이 줄 맨 앞에 영구히 박히면
		// 줄 전체가 정지하므로, 접근 자체에 별도 상한을 둡니다.
		if (Data.MaxApproachSeconds > 0.0f &&
			Data.ElapsedApproachTime >= Data.MaxApproachSeconds)
		{
			// 경로 실패로 못 온 손님을 "오래 기다려서 화난 손님"으로 집계하지 않도록
			// 방송에는 실제로 줄에서 기다린 시간(0초)을 넘깁니다.
			Subsystem->AbandonCheckout(Data.Actor, Data.ElapsedWaitTime);
			return EStateTreeRunStatus::Failed;
		}
	}
	else if (State->bIsBeingServed == false)
	{
		// 인내심 판정 (시간 오버되면 포기하고 Leave)
		// 응대 중인 손님은 제외합니다. 서비스를 받는 중에 "기다리다 지쳐" 나가면 안 됩니다.
		Data.ElapsedWaitTime += DeltaTime;

		float Patience = Data.PatienceOverride;
		if (Patience <= 0.0f)
		{
			// 태스크에서 따로 지정하지 않았으면 Subsystem이 주는 값을 쓴다.
			// 소비심리 파트가 붙으면 그 함수 내부만 바뀌므로 여기는 그대로 둔다.
			Patience = Subsystem->GetPatienceSeconds(Data.Actor);
		}

		if (Patience > 0.0f && Data.ElapsedWaitTime >= Patience)
		{
			// 대기열 이탈, 상품 반환, 방송까지 Subsystem이 한 번에 처리한다.
			Subsystem->AbandonCheckout(Data.Actor, Data.ElapsedWaitTime);

			// 실패로 종료 → Checkout 상태의 On Failed 전이가 Leave로 보낸다.
			return EStateTreeRunStatus::Failed;
		}
	}

	// 앞사람 이탈에 따른 슬롯 이동은 순번 변경 처리에서 수행

	// 슬롯에 도착하면 계산대가 지정한 방향으로 부드럽게 회전
	// 회전 완료 여부는 계산 가능 판정에서 제외
	FRotator TargetFacing;
	if (bArrivedAtSlot &&
		Subsystem->TryGetQueueSlotFacing(QueueIndex, TargetFacing))
	{
		const FRotator CurrentRotation = Data.Actor->GetActorRotation();
		Data.Actor->SetActorRotation(FRotator(
			CurrentRotation.Pitch,
			FMath::RInterpTo(CurrentRotation, TargetFacing, DeltaTime, Data.QueueFacingInterpSpeed).Yaw,
			CurrentRotation.Roll));
	}

	return EStateTreeRunStatus::Running;
}

void FCSTask_WaitInQueue::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	// 계산 없이 상태를 떠나는 경우(중도 이탈) 대기열 정리 - 안전장치
	UCSCustomerStateComponent* State = nullptr;
	if (Data.Actor != nullptr)
	{
		State = Data.Actor->GetCustomerState();
	}

	if (State != nullptr && State->bCheckoutDone == false)
	{
		UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();
		if (Subsystem != nullptr)
		{
			// 상태를 떠나는 손님을 확정된 대기열에서 제거
			Subsystem->LeaveQueue(Data.Actor);

			// 순번 확정 전 이탈한 손님을 접근 목록에서 제거해 정원을 반환
			Subsystem->CancelQueueApproach(Data.Actor);
		}
	}

	// 강제 퇴장·제압처럼 계산대를 거치지 않고 상태를 떠나는 경로에서도 응대 플래그가 남지 않게 합니다.
	if (State != nullptr)
	{
		State->bIsBeingServed = false;
	}

	if (Data.AIController != nullptr)
	{
		Data.AIController->StopMovement();
	}
}

// =====================================================================
// LeaveAndDespawn
// =====================================================================

EStateTreeRunStatus FCSTask_LeaveAndDespawn::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr || Data.AIController == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	UCSCustomerStateComponent* State = Data.Actor->GetCustomerState();
	if (State == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();

	// 퇴장 상태에 들어오는 시점에 장바구니를 숨긴다. 액터 자체는 손님 파괴 때 함께 정리된다.
	// 결제 완료(ShowShoppingBag)와 계산 포기(AbandonCheckout)는 이미 숨긴 뒤 들어오므로 중복 호출이지만 무해하다.
	// 훔친 물건을 든 채 도주하는 연출은 유지해야 하므로 절도 실행자만 예외로 둔다.
	if (Subsystem == nullptr || !Subsystem->IsEscapingShoplifter(Data.Actor))
	{
		Data.Actor->SetBasketDisplayVisible(false);
	}

	// 퇴장 시 속마음 반응 둘 다 매장에 들어와 본 손님 전용이다 - 입장 안 하고 지나치기만 한
	// 행인도 이 태스크로 들어오는데(ShouldEnterStore 실패 시 바로 여기로 옴), 가드가 없으면
	// 매장 안을 구경도 안 한 행인이 "계획한 걸 못 샀다"/"선호 카테고리가 없다"고 말하는
	// 거짓 신호가 나간다(실측됨 - 매장에 들어오지 않은 손님이 대사를 띄움)
	if (State->HasEnteredStore())
	{
		// 계획 상품 미발견 반응은 전부 이 자리 하나로 모았다.
		// 진열대마다 띄우면 한 손님이 같은 대사를 여러 번 하고, 어떤 손님은 진열대에서
		// 어떤 손님은 문 앞에서 말해 규칙이 없어 보인다 - 퇴장 시점 1회로 고정한다
		// 걸어 나가는 동안 마법봉으로 맞혀야 문장이 보이므로, 퇴장 이동을 시작하기 전에 띄운다
		if (UCSPurchaseDecisionComponent* Decision = Data.Actor->FindComponentByClass<UCSPurchaseDecisionComponent>();
			Decision != nullptr && Subsystem != nullptr && !State->bPlannedMissingReported)
		{
			// 찾다가 포기한 것이 먼저다. 그 뒤가 아예 만나보지도 못한 계획 상품
			TArray<FName> Unfulfilled = State->UnfulfilledPlannedProductIds;
			Unfulfilled.Append(State->PlannedProductIds);

			for (const FName PlannedId : Unfulfilled)
			{
				// 진열은 돼 있는데 이 손님이 그 진열대까지 못 간 것뿐이면 아무 말도 안 한다.
				// 갖다놓은 물건을 두고 없다고 하면 플레이어에게 거짓 신호가 된다
				if (HasExactProductAnywhere(*Subsystem, PlannedId))
				{
					continue;
				}

				FCSPurchaseRejectionInfo PlannedMissingInfo;
				PlannedMissingInfo.Reason = ECSPurchaseRejectReason::PlannedProductMissing;
				PlannedMissingInfo.ProductId = PlannedId;
				PlannedMissingInfo.Severity = CS_PlannedMissingSeverity(*Subsystem, *Decision, PlannedId);

				State->LastRejectionInfo = PlannedMissingInfo;
				State->LastRejectionTime = Context.GetWorld()->GetTimeSeconds();
				Data.Actor->ShowHeadIcon(Decision->GetRejectionDisplayTier(PlannedMissingInfo).Icon != nullptr,
					Decision->GetInstantDisplayDuration(PlannedMissingInfo.Reason));
				Subsystem->NotifyPurchaseRejected(Data.Actor, PlannedMissingInfo);

				// 이 태스크는 영업 종료 강제 퇴장 등으로 다시 들어올 수 있다.
				// 플래그를 안 세우면 같은 손님이 같은 상품으로 두 번 반응한다(실측됨)
				State->bPlannedMissingReported = true;
				State->PlannedProductIds.Empty();
				State->UnfulfilledPlannedProductIds.Empty();

				// 나가면서 여러 개를 연달아 띄우면 마지막 것만 보이므로 하나만
				break;
			}
		}

		// 계획구매(뉴스/기념일/날씨) 관련 반응이 이번에도, 이전 호출에서도 안 뜬 "나머지" 손님 전용 채널.
		// 이벤트 없는 날은 위 블록이 항상 비어서 퇴장 연출이 심심해지므로, 종족 선호 카테고리가
		// 매장에 아예 없을 때 30%(PreferredCategoryMissingChance) 확률로 아쉬움을 대신 표현한다
		if (UCSPurchaseDecisionComponent* Decision = Data.Actor->FindComponentByClass<UCSPurchaseDecisionComponent>();
			Decision != nullptr && Subsystem != nullptr &&
			!State->bPlannedMissingReported && !State->bPreferredCategoryMissingReported)
		{
			ECSProductCategory MissingCategory = ECSProductCategory::Beverage;
			const bool bFoundMissingCategory = Decision->RollPreferredCategoryMissing(
				[Subsystem](ECSProductCategory Category) { return HasCategoryStockAnywhere(*Subsystem, Category); },
				MissingCategory);

			// 굴림 자체는 성공/실패 무관하게 1회로 제한 - 안 그러면 강제 퇴장 재진입마다 다시 30%를 굴리게 된다
			State->bPreferredCategoryMissingReported = true;

			if (bFoundMissingCategory)
			{
				FCSPurchaseRejectionInfo MissingInfo;
				MissingInfo.Reason = ECSPurchaseRejectReason::PreferredCategoryMissing;
				MissingInfo.BlockedCategory = MissingCategory;
				MissingInfo.Severity = 1.0f;

				State->LastRejectionInfo = MissingInfo;
				State->LastRejectionTime = Context.GetWorld()->GetTimeSeconds();
				Data.Actor->ShowHeadIcon(Decision->GetRejectionDisplayTier(MissingInfo).Icon != nullptr,
					Decision->GetInstantDisplayDuration(MissingInfo.Reason));
			}
		}
	}

	const EPathFollowingRequestResult::Type Result =
		Data.AIController->MoveToLocation(State->ExitLocation, Data.AcceptanceRadius);

	if (Result == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		// 빈손 퇴장률 집계용 : 파괴 직전에 구매 완료 여부를 방송
		if (Subsystem != nullptr)
		{
			Subsystem->NotifyCustomerDespawned(Data.Actor);
		}

		Data.Actor->Destroy();
		return EStateTreeRunStatus::Succeeded;
	}
	if (Result == EPathFollowingRequestResult::Failed)
	{
		// 경로를 못 찾아도 남겨두면 유령 손님이 되므로 그냥 제거
		if (Subsystem != nullptr)
		{
			Subsystem->NotifyCustomerDespawned(Data.Actor);
		}
		Data.Actor->Destroy();
		return EStateTreeRunStatus::Succeeded;
	}
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FCSTask_LeaveAndDespawn::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr || Data.AIController == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (Data.AIController->GetMoveStatus() == EPathFollowingStatus::Idle)
	{
		// 빈손 퇴장률 집계용 : 파괴 직전에 구매 완료 여부를 방송
		if (UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>())
		{
			Subsystem->NotifyCustomerDespawned(Data.Actor);
		}

		// 이동이 완료되면 도달 여부와 무관하게 무조건 삭제
		Data.Actor->Destroy();
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

// =====================================================================
// ReturnProducts
// =====================================================================

EStateTreeRunStatus FCSTask_ReturnProducts::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr || Data.AIController == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();
	if (Subsystem == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 빈자리 여부와 무관하게 진열대 하나를 선택
	Data.TargetShelf = Subsystem->PickRandomShelf();

	// 예외처리) 레벨에 진열대가 하나도 없으면 갈 곳이 없다. 그 자리에 떨구고 끝낸다.
	if (Data.TargetShelf == nullptr)
	{
		Subsystem->DropCustomerItems(Data.Actor);
		return EStateTreeRunStatus::Succeeded;
	}

	// 설 자리는 진열대의 CustomerShoppingPoint 마커에서 받아옵니다.
	FVector TargetLocation;
	FRotator TargetFacing;
	float TargetRadius = 0.0f;
	if (!Subsystem->TryGetShelfBrowsePoint(Data.TargetShelf, TargetLocation, TargetFacing, TargetRadius))
	{
		// 예외처리) 붙잡아두면 손님이 상품을 든 채 소멸해 재고가 증발하므로 그 자리에 떨굽니다.
		Subsystem->DropCustomerItems(Data.Actor);
		return EStateTreeRunStatus::Succeeded;
	}

	// 여기는 TargetShelf를 이미 알고 있으므로 진열대가 정한 영역 반경을 그대로 씁니다.
	const EPathFollowingRequestResult::Type Result =
		Data.AIController->MoveToLocation(TargetLocation, TargetRadius, false);

	if (Result == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		Subsystem->ReturnCustomerItemsToShelf(Data.Actor, Data.TargetShelf);
		return EStateTreeRunStatus::Succeeded;
	}
	if (Result == EPathFollowingRequestResult::Failed)
	{
		// 예외처리) 경로를 못 찾으면 붙잡아두지 말고 그 자리에 떨어뜨림
		Subsystem->DropCustomerItems(Data.Actor);
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FCSTask_ReturnProducts::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr || Data.AIController == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();
	if (Subsystem == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 이동이 끝날 때까지 대기
	if (Data.AIController->GetMoveStatus() != EPathFollowingStatus::Idle)
	{
		return EStateTreeRunStatus::Running;
	}

	// 길을 막혀서 정확히 도착하지 못하고 멈춰버리는 경우에 일단 되돌리기 기능을 시도는 함
	Subsystem->ReturnCustomerItemsToShelf(Data.Actor, Data.TargetShelf);

	return EStateTreeRunStatus::Succeeded;
}

void FCSTask_ReturnProducts::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.AIController != nullptr)
	{
		Data.AIController->StopMovement();
	}

	// 어떤 경로로든 상품이 남은 채 이 상태를 떠나면 그 자리에 떨군다.
	// 남겨두면 손님이 상품을 든 채로 소멸해 재고가 증발. (나중에 손님유형따라 든 채로 가는것도 컨텐츠 가능)
	if (Data.Actor == nullptr)
	{
		return;
	}

	const UCSCustomerStateComponent* State = Data.Actor->GetCustomerState();
	if (State != nullptr && State->HasSelectedItems())
	{
		UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();
		if (Subsystem != nullptr)
		{
			Subsystem->DropCustomerItems(Data.Actor);
		}
	}
}

EStateTreeRunStatus FCSTask_Shoplift::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();
	if (Subsystem == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// bVillainActionDone 표시 + 경고 방송을 서브시스템이 처리
	Subsystem->CommitShoplift(Data.Actor);

	// 도주 연출. 이 값은 상태를 떠나도 유지되므로 Leave까지 빠른 걸음이 이어집니다.
	if (UCharacterMovementComponent* Movement = Data.Actor->GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = Data.FleeWalkSpeed;
	}

	// 이동은 Leave 상태가 담당하므로 여기서는 즉시 성공을 반환
	return EStateTreeRunStatus::Succeeded;
}

// =====================================================================
// PrepareForcedLeave
// =====================================================================

EStateTreeRunStatus FCSTask_PrepareForcedLeave::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	UCSCustomerRuntimeSubsystem* Subsystem =
		Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();

	if (Subsystem != nullptr)
	{
		Subsystem->PrepareForcedLeave(Data.Actor);
	}

	// 서브시스템이 없어도 성공으로 넘김
	UE_LOG(LogTemp, Log, TEXT("[영업종료퇴장] %s"), *Data.Actor->GetName());

	return EStateTreeRunStatus::Succeeded;
}

// =====================================================================
// SubduedDespawn
// =====================================================================

EStateTreeRunStatus FCSTask_SubduedDespawn::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 이전 상태의 ExitState가 이미 StopMovement를 불렀지만,
	// 어느 상태에서 들어왔는지에 의존하지 않도록 여기서도 한 번 확실히 끊는다.
	if (Data.AIController != nullptr)
	{
		Data.AIController->StopMovement();
	}

	// Shoplift가 올려둔 도주 속도가 남아 있어도 밀려나지 않게 이동 자체를 막는다.
	if (UCharacterMovementComponent* Movement = Data.Actor->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	// 제압된 손님은 그 자리에서 소멸을 기다리므로 장바구니도 함께 숨긴다.
	// 제압 시점에 상품 목록이 이미 비워지므로 절도 연출을 지킬 이유가 없다.
	Data.Actor->SetBasketDisplayVisible(false);

	Data.ElapsedTime = 0.0f;

	UE_LOG(LogTemp, Log, TEXT("[제압] %s 소멸 대기 시작"), *Data.Actor->GetName());

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FCSTask_SubduedDespawn::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	UCSCustomerRuntimeSubsystem* Subsystem =
		Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();

	// 파라미터가 0이면 서브시스템의 공용 값을 쓴다. 서브시스템까지 없으면 최후 폴백.
	float DespawnDelay = Data.DespawnDelayOverride;
	if (DespawnDelay <= 0.0f)
	{
		DespawnDelay = (Subsystem != nullptr) ? Subsystem->SubduedDespawnDelay : 3.0f;
	}

	Data.ElapsedTime += DeltaTime;
	if (Data.ElapsedTime < DespawnDelay)
	{
		return EStateTreeRunStatus::Running;
	}

	// TODO(제압 연출): 랙돌/몬타주 작업 시 주석 해제. 상세는 VillainSubdue_Design.md 4-6절
	// 연출 정리 자리. 랙돌이 들어오더라도 소멸 시점은 아래 그대로 유지된다.
	//Data.Actor->EndSubduedReaction();

	// 통계 방송. 제압 시점에 상품 목록이 비워졌으므로
	// 이 안의 ReportShopliftEscaped는 손실을 방송하지 않고 조용히 빠져나간다.
	if (Subsystem != nullptr)
	{
		Subsystem->NotifyCustomerDespawned(Data.Actor);
	}

	// Destroyed()가 AIController와 장바구니 액터, 봉투까지 정리한다. (표시는 EnterState에서 이미 껐다)
	Data.Actor->Destroy();

	return EStateTreeRunStatus::Succeeded;
}

// =====================================================================
// ApproachStorefront
// =====================================================================

EStateTreeRunStatus FCSTask_ApproachStorefront::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr || Data.AIController == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	UCSCustomerStateComponent* State = Data.Actor->GetCustomerState();
	if (State == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// [ForceLeave 연동 가드]
	// 이미 강제 퇴장이 확정된 손님은 가게 앞으로 되돌아가면 안 된다.
	//
	// 이 가드가 필요한 경로: Leave 상태가 Failed를 반환하면 Root로 돌아가는데,
	// 그때 ForceLeave는 bForcedToLeave 때문에 false가 되고
	// Approach는 HasEnteredStore가 false라 true가 되어 여기로 들어온다.
	// 그러면 마감 시각에 손님이 가게 앞으로 걸어가게 됨
	if (State->IsForcedToLeave())
	{
		return EStateTreeRunStatus::Failed;
	}

	// 판정 지점이 없을 때 조용히 입장시키지 않는다.
	// 조용히 통과시키면 개폐 판정이 통째로 사라지는데 로그가 없어 원인 추적이 불가능하다.
	// (스폰 초기화 순서 버그가 실제로 이 폴백에 가려 오래 발견되지 않았다.)
	if (State->StorefrontLocation.IsNearlyZero())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[입장판정] %s StorefrontLocation이 비어 있음 - 레벨에 StorefrontPoint를 배치했는지, "
				 "스포너가 StateTree 시작 전에 값을 주입하는지 확인 필요"),
			*Data.Actor->GetName());

		return EStateTreeRunStatus::Failed;
	}

	// 반환값을 확인한다. 경로 요청이 실패하면 손님은 제자리에서 판정만 반복해
	// "가게 앞까지 못 간다"로 보이는데, 조용히 넘기면 원인 추적이 불가능하다.
	// (목표 좌표를 NavMesh에 투영하지 못하면 여기서 Failed가 나온다)
	const EPathFollowingRequestResult::Type MoveResult =
		Data.AIController->MoveToLocation(State->StorefrontLocation, Data.AcceptanceRadius);

	if (MoveResult == EPathFollowingRequestResult::Failed)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[입장판정] %s 가게 앞으로 가는 경로 요청 실패 - 목표 %s. "
				 "판정 지점이 NavMesh 위에 있는지, 목표 Z가 지면 높이인지 확인 필요"),
			*Data.Actor->GetName(), *State->StorefrontLocation.ToCompactString());

		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FCSTask_ApproachStorefront::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr || Data.AIController == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 아직 걸어가는 중이면 대기
	if (Data.AIController->GetMoveStatus() != EPathFollowingStatus::Idle)
	{
		return EStateTreeRunStatus::Running;
	}

	UCSCustomerRuntimeSubsystem* Subsystem =
		Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();
	UCSCustomerStateComponent* State = Data.Actor->GetCustomerState();
	if (Subsystem == nullptr || State == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 가게 앞에 도착했으니 한 번 판정
	if (Subsystem->ShouldEnterStore(Data.Actor) == false)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 진입 확정
	//  - 통계에서 행인과 구분되고
	//  - 매장 안 손님이 다시 가게 앞으로 나가지 않는다.
	State->MarkEnteredStore();

	return EStateTreeRunStatus::Succeeded;
}

void FCSTask_ApproachStorefront::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	// 다음 상태가 새 이동 명령을 내리기 전에 진행 중인 경로를 정리한
	// ForceLeave가 이 상태를 중간에 끊을 때도 이 함수가 먼저 실행
	// PrepareForcedLeave 태스크는 StopMovement를 따로 부르지 않아도 된다.
	if (Data.AIController != nullptr)
	{
		Data.AIController->StopMovement();
	}
}
