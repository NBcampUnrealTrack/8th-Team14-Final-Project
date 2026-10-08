#include "Systems/CustomerFlow/CSPurchaseVerificationSubsystem.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "Features/Customers/CSCustomerCharacter.h"
#include "Features/Customers/CSCustomerStateComponent.h"
#include "Features/Customers/CSPurchaseDecisionComponent.h"
#include "Data/CSCustomerArchetypeDataAsset.h"
#include "Data/CSProductTypes.h"

void UCSPurchaseVerificationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 같은 월드에 있는 CSCustomerRuntimeSubsystem을 찾아서 그 서브시스템이
	// 계산 완료 시점마다 울리는 방송(OnCheckoutCompleted)에 이 함수를 등록
	if (UCSCustomerRuntimeSubsystem* RuntimeSubsystem = InWorld.GetSubsystem<UCSCustomerRuntimeSubsystem>())
	{
		RuntimeSubsystem->OnCheckoutCompleted.AddDynamic(this, &UCSPurchaseVerificationSubsystem::HandleCheckoutCompleted);
		RuntimeSubsystem->OnCustomerDespawned.AddDynamic(this, &UCSPurchaseVerificationSubsystem::HandleCustomerDespawned);
	}
}

void UCSPurchaseVerificationSubsystem::Deinitialize()
{
	// 월드가 종료되기 직전에 구독을 명시적으로 해제
	if (UWorld* World = GetWorld())
	{
		if (UCSCustomerRuntimeSubsystem* RuntimeSubsystem = World->GetSubsystem<UCSCustomerRuntimeSubsystem>())
		{
			RuntimeSubsystem->OnCheckoutCompleted.RemoveDynamic(this, &UCSPurchaseVerificationSubsystem::HandleCheckoutCompleted);
			RuntimeSubsystem->OnCustomerDespawned.RemoveDynamic(this, &UCSPurchaseVerificationSubsystem::HandleCustomerDespawned);
		}
	}

	Super::Deinitialize();
}

void UCSPurchaseVerificationSubsystem::HandleCheckoutCompleted(ACSCustomerCharacter* Customer)
{
	if (Customer == nullptr)
	{
		return;
	}

	const UCSCustomerStateComponent* State = Customer->GetCustomerState();
	if (State == nullptr)
	{
		return;
	}

	// 방송이 울리는 시점(CompleteFrontCheckout 내부)엔 아직 장바구니를 비우지 않은 상태이므로
	// 여기서 SelectProducts 배열 길이를 그대로 이 손님이 담은 개수로 읽을 수 있음
	const int32 ItemCount = State->SelectProducts.Num();

	// 누적 집계 갱신
	TotalCheckouts++;
	TotalItemsPurchased += ItemCount;
	if (ItemCount == 1)
	{
		SingleItemCheckouts++;
	}

	// 같은 배열을 순회해 카테고리별로도 집계 - 짝이 없는 카테고리가 연관구매에 밀려 덜 팔려도
	// 합계만으로는 드러나지 않아서 필요
	// 술 없이 숙취해소제만 산 손님처럼 합계로는 안 보이는 조합을 로그에서 세려고 손님별 목록도 남긴다
	TArray<FString> BoughtCategories;
	for (const FCSSelectProduct& Selected : State->SelectProducts)
	{
		const FCSProductDataRow* Row = Selected.ProductRow.GetRow<FCSProductDataRow>(
			TEXT("UCSPurchaseVerificationSubsystem::HandleCheckoutCompleted"));
		if (Row == nullptr)
		{
			++UnknownCategoryPurchases;
			BoughtCategories.Add(TEXT("Unknown"));
			continue;
		}
		CategoryPurchaseCounts.FindOrAdd(Row->Category)++;
		BoughtCategories.Add(StaticEnum<ECSProductCategory>()->GetNameStringByValue(static_cast<int64>(Row->Category)));
	}

	const UCSPurchaseDecisionComponent* Decision = Customer->FindComponentByClass<UCSPurchaseDecisionComponent>();
	const FString RaceName = Decision != nullptr && Decision->Archetype != nullptr
		? Decision->Archetype->DisplayName.ToString() : TEXT("?");

	// 개별 손님 로그는 Log 레벨로 아래 요약과 구분되게 표시
	UE_LOG(LogTemp, Log, TEXT("[구매검증] %s 계산 완료 | 담은 개수=%d | 종족=%s | 카테고리=%s"),
		*Customer->GetName(), ItemCount, *RaceName, *FString::Join(BoughtCategories, TEXT(",")));

	// LogIntervalCount명마다 한 번씩 누적 요약을 찍음
	if (TotalCheckouts % LogIntervalCount == 0)
	{
		LogSummary();
	}
}

void UCSPurchaseVerificationSubsystem::HandleCustomerDespawned(ACSCustomerCharacter* Customer, bool bCheckoutDone)
{
	const UCSCustomerStateComponent* State = IsValid(Customer) ? Customer->GetCustomerState() : nullptr;
	// 마감으로 끊긴 손님은 결제도 빈손도 아니다. 평판 집계와 같은 기준으로 뺀다
	if (IsValid(State) && State->IsForcedToLeave())
	{
		return;
	}

	++TotalCustomersLeft;

	if (bCheckoutDone)
	{
		return;
	}

	if (IsValid(State) && State->bCheckoutAbandoned)
	{
		++CheckoutAbandonedDepartures;
	}
	else
	{
		++EmptyHandedDepartures;
	}
}

void UCSPurchaseVerificationSubsystem::LogSummary() const
{
	if (TotalCheckouts == 0)
	{
		return; // 0으로 나누기 방지
	}

	// 평균 장바구니 개수 = 지금까지 팔린 총 개수 / 지금까지 계산한 손님 수
	const float AverageBasketSize = static_cast<float>(TotalItemsPurchased) / static_cast<float>(TotalCheckouts);

	// 1개만 산 비율(%) = 1개만 산 손님 수 / 전체 손님 수 × 100
	const float SingleItemRatio = static_cast<float>(SingleItemCheckouts) / static_cast<float>(TotalCheckouts) * 100.0f;

	// 빈손 퇴장률 = 빈손으로 나간 손님 / 매장을 떠난 손님 총원 × 100
	const float EmptyHandedRatio = (TotalCustomersLeft > 0)
	? (static_cast<float>(EmptyHandedDepartures) / static_cast<float>(TotalCustomersLeft) * 100.0f)
	: 0.0f;

	const float CheckoutAbandonedRatio = (TotalCustomersLeft > 0)
	? (static_cast<float>(CheckoutAbandonedDepartures) / static_cast<float>(TotalCustomersLeft) * 100.0f)
	: 0.0f;

	// 요약 로그는 Warning 레벨로
	UE_LOG(LogTemp, Warning,
	TEXT("[구매검증 요약] 누적 %d명 | 평균 장바구니=%.2f개 (목표 2.5~3.0) | 1개만 구매 비율=%.1f%% (목표 37~50%%) | 빈손 퇴장률=%.1f%% (목표 10~15%%) | 계산포기율=%.1f%%"),
	TotalCheckouts, AverageBasketSize, SingleItemRatio, EmptyHandedRatio, CheckoutAbandonedRatio);

	// 카테고리별 판매 개수 - 하드코딩 나열 대신 enum 전체를 리플렉션으로 순회한다
	if (const UEnum* CategoryEnum = StaticEnum<ECSProductCategory>())
	{
		FString CategoryLine = TEXT("[구매검증 요약] 카테고리별 판매");

		// NumEnums()는 자동 생성되는 _MAX 항목을 포함하므로 하나 뺀 개수만 순회한다
		const int32 NumRealCategories = CategoryEnum->NumEnums() - 1;
		for (int32 Index = 0; Index < NumRealCategories; Index++)
		{
			const int64 Value = CategoryEnum->GetValueByIndex(Index);
			const int32* Count = CategoryPurchaseCounts.Find(static_cast<ECSProductCategory>(Value));
			CategoryLine += FString::Printf(TEXT(" | %s=%d"),
				*CategoryEnum->GetNameStringByValue(Value), Count != nullptr ? *Count : 0);
		}

		if (UnknownCategoryPurchases > 0)
		{
			CategoryLine += FString::Printf(TEXT(" | 카테고리불명=%d"), UnknownCategoryPurchases);
		}

		UE_LOG(LogTemp, Warning, TEXT("%s"), *CategoryLine);
	}
}
