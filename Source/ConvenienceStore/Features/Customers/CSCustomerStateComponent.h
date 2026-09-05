// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/CSProductTypes.h"
#include "Data/CSVillainTypes.h"
#include "Data/CSPurchaseDecisionTypes.h"
#include "Engine/DataTable.h"
#include "Components/ActorComponent.h"
#include "CSCustomerStateComponent.generated.h"

// 손님이 고른 상품 하나의 정보.
// ProductId(RowName)만으로는 상품 액터를 다시 만들 수 없어서
// (어느 데이터 테이블인지, 어떤 BP였는지를 알 수 없음)
// 집는 시점에 원본 정보를 기록
USTRUCT(BlueprintType)
struct FCSSelectProduct
{
	GENERATED_BODY()

	// 상품의 데이터 테이블 행 (DataTable + RowName 쌍).
	// ProductRow.RowName이 ProductId다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	FDataTableRowHandle ProductRow;

	// 집었던 상품 액터의 클래스.
	// 같은 BP로 생성해야 DataAsset 기반 외형이 동일하게 적용된다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	TSubclassOf<AActor> ProductClass;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	FCSProductRuntimeState ProductRuntimeState;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	int32 SalePriceAtPickup = 0;
};

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class CONVENIENCESTORE_API UCSCustomerStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// 손님이 고른 상품 목록. 계산대 주문 생성과 포기 시 반환에 모두 사용된다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	TArray<FCSSelectProduct> SelectProducts;

	// 이번 방문에서 이미 구경한 진열대 액터 목록 (또 다시 안가게 막을려고)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	TArray<TObjectPtr<AActor>> VisitedShelves;

	// 계산이 완료됬는지 알려주는 상태 변수
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	bool bCheckoutDone = false;

	// 이번 방문에서 계산을 포기했는지, 포기한 손님이 다시 줄을 서지 않도록 막는 용도 (진열대 반환 기능에서 조건으로 사용)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	bool bCheckoutAbandoned = false;

	// 손님이 나갈 때 걸어갈 목표 좌표
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	FVector ExitLocation = FVector::ZeroVector;

	// 가게 앞 판정 지점의 좌표. 스포너가 스폰 시점에 채워줍니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	FVector StorefrontLocation = FVector::ZeroVector;

	// 영업시간 종료로 강제 퇴장이 확정된 손님인지.
	// 이 플래그가 필요한 이유는 두 가지
	//  1) 정리 작업(줄 이탈 / 상품 낙하)을 딱 한 번만 실행하기 위해서
	//     Root의 On Tick 전이는 매 틱 평가되므로, 없으면 상품이 매 틱 떨어짐
	//  2) 통계 파트가 "살 게 없어 빈손으로 나간 손님"과
	//     "영업이 끝나서 못 산 손님"을 구분할 근거.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	bool bForcedToLeave = false;

	UFUNCTION(BlueprintPure, Category = "Customer")
	bool IsForcedToLeave() const;

	// 강제 퇴장 확정 시 호출.
	void MarkForcedToLeave();

	// 이 손님이 매장 안에 들어온 적이 있는지.
	//
	// 두 가지 용도가 있습니다.
	//  1) 통계 : 그냥 지나간 행인을 구매 통계에서 제외
	//  2) 입장 : 이미 들어온 손님이 다시 가게 앞으로 나가지 않게 함
	// 강제 퇴장 대상인가와는 무관
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	bool bHasEnteredStore = false;

	UFUNCTION(BlueprintPure, Category = "Customer")
	bool HasEnteredStore() const;

	// 가게 앞 판정을 통과해 입장이 확정될 때 호출합니다.
	void MarkEnteredStore();

	// 빌런 -------

	// 스폰 시점에 확정된 이 손님의 빌런 행동. None이면 일반 손님.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Villain")
	ECSVillainAction PlannedVillainAction = ECSVillainAction::None;

	// 계획한 빌런 행동을 했는지 확인
	// 이 플래그가 없으면 Root를 재선택할 때마다 빌런 상태가 계속 참이라
	// 손님이 그 상태에 갇힘
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Villain")
	bool bVillainActionDone = false;

	// 빌런 행동을 배정하는 유일한 통로. 배정 규칙을 한 곳에만 두기 위함입니다.
	// 반환값이 false면 규칙에 걸려 배정되지 않은 것입니다.
	bool AssignVillainAction(ECSVillainAction NewAction);

	// 이 손님이 빌런인가 (행동 종류 무관)
	UFUNCTION(BlueprintPure, Category = "Customer|Villain")
	bool IsVillain() const;

	// 아직 저지르지 않은 빌런 행동이 남아 있는가. StateTree 조건이 읽는 값
	UFUNCTION(BlueprintPure, Category = "Customer|Villain")
	bool HasPendingVillainAction() const;

	// 빌런 행동을 실행한 뒤 호출. 안 부르면 트리가 그 상태에 갇힙니다
	void MarkVillainActionDone();

	// 절도 손실을 이미 장부에 올렸는지 확인하는 변수.
	// 손실 확정 지점이 정문 퇴장 / 마감 종 / 영업일 전환 세 곳으로 늘어났으므로
	// 이 플래그가 없으면 같은 손님의 손실이 두 번 집계될 수 있다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Villain")
	bool bShopliftLossReported = false;

	// 절도 손실 집계 시 호출. 이번 호출이 첫 집계면 true, 이미 올렸으면 false.
	// MarkSubdued와 같은 방식으로 호출자가 중복을 걸러낼 근거를 돌려준다.
	bool MarkShopliftLossReported();

	// 제압된 빌런인지 확인하는 변수
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Villain")
	bool bIsSubdued = false;

	UFUNCTION(BlueprintPure, Category = "Customer|Villain")
	bool IsSubdued() const;

	// 제압 확정 시 호출. 이번 호출이 실제 전환이면 true, 이미 제압된 상태였으면 false.
	// 중복을 걸러낼 근거가 필요
	bool MarkSubdued();

	// 예산 -------

	// 이번 방문에서 쓸 생각이었던 돈 (소프트 예산)
	// 실제 값을 뽑는 계산은 CSPurchaseDecisionComponent가 담당
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Budget")
	int32 SoftBudgetGold = 0;

	// 물리적으로 낼 수 있는 최대 금액 (하드 지갑) 이걸 넘으면 무조건 구매 불가
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Budget")
	int32 HardWalletLimitGold = 0;

	// 지금까지 실제로 쓴 금액 상품을 집을 때마다 늘어남
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Budget")
	int32 SpentGold = 0;

	// false면 아직 소프트/하드 예산이 안 정해진 상태
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Budget")
	bool bBudgetInitialized = false;

	// 체류 중 살펴볼 수 있는 남은 상품 개수
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Browsing")
	int32 RemainingEvaluationBudget = 8;

	// 스폰 시점에 이번 방문에서 목표로 삼은 장바구니 개수
	// CSPurchaseDecisionComponent::RollPlannedBasketTarget()으로 계산된 개인 상태값이라
	// Archetype/Fallback 이중화 대상이 아니고, 이 필드 자체가 결과값이라 여기 그대로 저장함
	// 진열대 개수와 무관하게 손님이 몇 개를 살지 정하기 위한 값 (장바구니가 진열대 개수에 종속되던 문제 해소)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Browsing")
	int32 PlannedBasketTarget = 1;

	// 상품을 실제로 구매(집기)했을 때 지출을 기록
	void RecordSpending(int32 Amount);

	// 남은 소프트 예산 (0 밑으로는 안 내려감)
	int32 GetRemainingSoftBudget() const;

	// 남은 하드 지갑 한도 (0 밑으로는 안 내려감)
	int32 GetRemainingHardWallet() const;

	// 고른 상품을 기록한다. 상품 액터를 파괴하기 전에 호출할 것.
	void AddSelectProduct(
		const FDataTableRowHandle& InProductRow,
		TSubclassOf<AActor> InProductClass,
		const FCSProductRuntimeState& InProductRuntimeState,
		int32 InSalePriceAtPickup);
	bool HasSelectedItems() const;

	// 고른 상품 목록을 비운다. 상품을 월드에 반환하거나 계산이 끝난 뒤 호출
	void ClearSelectItems();

	// 고른 상품의 ID(RowName) 목록. 계산대 주문 생성 등 Getter용
	UFUNCTION(BlueprintPure, Category = "Customer")
	TArray<FName> GetSelectProductIds() const;

	void MarkVisited(AActor* ShelfActor);
	bool HasVisited(const AActor* ShelfActor) const;

	// 거절 연출 / 가격 반응 공용

	// 가장 최근 거절/가격반응 사유. 즉시표시(디버그 텍스트)와 마법봉 속마음 트리거가 둘 다 이 값을 읽음
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Rejection")
	FCSPurchaseRejectionInfo LastRejectionInfo;

	// 위 정보가 기록된 월드 시각. 마법봉 트리거가 GetInstantDisplayDuration(Reason) 이내인지 판정하는 기준
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Rejection")
	float LastRejectionTime = 0.0f;

	// 이 손님의 가격 불쾌가 하루 평판 집계(DailyPriceUpsetSum)에 이미 반영됐는지
	// 손님 한 명이 여러 번 가격에 반응해도 평판 감점은 방문당 1회로 제한하기 위한 플래그
	// 손님 액터가 매일 밤 파괴되고 새로 스폰되므로 별도 리셋 없이 기본값(false)으로 되돌아감
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Rejection")
	bool bPriceUpsetCounted = false;

	// 청결도 -------

	// 스폰 시점에 pre-roll로 확정된, 이 손님이 오늘 진열대 이동 중 통로에 쓰레기를 버릴지 여부
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Trash")
	bool bWillDropTrash = false;

	// 몇 번째 진열대 이동에서 버릴지(1부터 시작). pre-roll로 스폰 시점에 확정
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Trash")
	int32 TrashDropTravelIndex = 0;

	// 그 이동에서 출발 지점 대비 몇 %쯤 걸었을 때 버릴지(0~1). pre-roll로 스폰 시점에 확정
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Trash")
	float TrashDropProgress = 0.0f;

	// 오늘 다른 손님이 흘린 쓰레기를 목격해 평판 집계(DailyTrashWitnessed)에 이미 반영됐는지
	// 손님 한 명이 여러 번 목격해도 반영은 방문당 1회로 제한하기 위한 플래그
	// 손님 액터가 매일 밤 파괴되고 새로 스폰되므로 별도 리셋 없이 기본값(false)으로 되돌아감
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Trash")
	bool bTrashWitnessedToday = false;

	// 오늘 목격 확률 굴림을 이미 한 번 시도했는지(성공/실패 무관). 굴림 자체를 하루 1회로 제한하는 용도
	// 이게 없으면 진열대를 여러 번 오가며 같은 쓰레기 옆을 지날 때마다 다시 굴려서
	// "30% 확률"이 하루 동안 여러 번 누적 시도되어 사실상 30%보다 훨씬 높아진다
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Trash")
	bool bHasAttemptedTrashWitnessRoll = false;

};
