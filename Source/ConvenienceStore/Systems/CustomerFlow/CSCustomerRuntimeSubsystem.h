// 손님의 매장 내 흐름(진열대 이동 → 상품 집기 → 계산 대기열)을 관리
//
//   [add] [한길] 편의점 핵심 시스템 구조 및 일과 흐름 구현, 플레이어 캐릭터 생성 (#1) 기준으로 만든 시스템을 그대로 사용한다.
//   - 진열대   = ACSShelfActor (Features/Shelves)       ← 레벨에서 자동 수집
//   - 상품     = ACSProductPickupActor (Features/Items) ← 진열대에 Attach된 것 = 재고
//   - 슬롯 해제 = ICSShelfPlaceable::RemovePlacedItem    ← 손님이 상품을 집을 때 호출
//
// 다른 파트와의 접점:
//   - 플레이어 파트    → 계산 상호작용에서 CompleteFrontCheckout() 호출
//                        + 실제 계산대에 ICSCustomerCounterInterface 구현 후 RegisterCounter()
//   - 편의점/통계 파트 → OnCheckoutCompleted / OnItemTaken 델리게이트 구독
//
// TODO(한길님 요청 파트):
//   1. 계산대 액터에 ICSCustomerCounterInterface 상속
//      (Features/Customers/Interface/CSCustomerCounterInterface.h)
//      - 구현할 함수는 GetQueueSlotLocation(int32 Index) 하나뿐
//      - Index번째 손님이 설 월드 좌표 반환. 줄의 시작점/방향/간격은 계산대가 자유롭게 결정
//   2. 계산대 BeginPlay에서 Subsystem->RegisterCounter(this) 호출
//      (이거 없으면 GetQueueSlotLocation이 ZeroVector 반환 → 손님이 원점으로 감)
//   3. 플레이어 계산 상호작용에서 Subsystem->CompleteFrontCheckout() 호출
//      (호출하면 줄 전진 + 손님 퇴장 + OnCheckoutCompleted 방송까지 자동)
//   4. 진열대 슬롯 높이(상/중/하) 제공 방식 확정 필요
//
// TODO(소비심리 파트 연동 시):
//   1. "어디 갈지"는 현재 PickRandomUnvisitedShelf(랜덤)로 임시 구현되어 있음 → 교체 예정
//   [완료] 2. "뭘 살지"도 현재 태스크에서 확률로 처리 중 → TakeProduct()에 결정된 상품을 넘기는 방식으로 교체
//   3. 진열 높이(상/중/하) 등 상세 진열 정보 제공은 요청 확정 후 추가 예정
//   [완료] 4. 판단 결과("어느 진열대로 가라", "이 상품 사라")를 손님 AI가 어떻게 전달받을지 미정 — 컴포넌트 함수 호출 / 인터페이스 중 협의 필요

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Data/CSPurchaseDecisionTypes.h"
#include "Math/RandomStream.h"
#include "CSCustomerRuntimeSubsystem.generated.h"

class ACSCustomerCharacter;
class ACSShelfActor;
class ACSProductPickupActor;
struct FCSSelectProduct;

// 계산 완료 시 방송. 손님의 SelectProducts(상품 행 정보 목록)로 매출을 집계하면 됨
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSOnCheckoutCompleted, ACSCustomerCharacter*, Customer);
// 손님이 진열대에서 상품을 집었을 때 방송. ProductId는 DT_ProductMaster의 RowName임.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCSOnItemTaken, ACSCustomerCharacter*, Customer, FName, ProductId);
// 계산을 포기하고 떠날 때 방송. WaitedSeconds는 줄에서 기다린 시간. 만족도 하락, 평판, 매출 손실 계산은 구독하는 쪽에서 처리
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCSOnCheckoutAbandoned,ACSCustomerCharacter*, Customer,float, WaitedSeconds);
UCLASS()
class CONVENIENCESTORE_API UCSCustomerRuntimeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// =====================================================================
	// 진열대
	// =====================================================================

	//레벨에 있는 진열대를 갱신하는 함수, 레벨 전체를 확인하여 진열대를 다시 수집.
	UFUNCTION(BlueprintCallable, Category = "Store|Shelf")
	void RefreshShelves();

	// 등록된 진열대 목록 Get 함수, 소비심리 파트가 목표를 정할 때 이 목록 쓰일 가능성 높음
	const TArray<TObjectPtr<AActor>>& GetShelves() const;

	// 손님이 진열대 앞에 서는 좌표. (현재 값을 줘서 직접 서 있는 위치를 값으로 제공)
	// [중요] EQS나 진열대에서 서 있는 위치를 알려줄 수 있으면 [BrowseDistance 삭제 가능]
	FVector GetShelfBrowseLocation(const AActor* ShelfActor, const float BrowseDistance) const;

	// [임시 테스트용] 아직 안 가본 진열대 중 하나를 랜덤 반환. 없으면 nullptr
	// 방문 기록은 손님 개인 데이터라 UCSCustomerStateComponent가 가지고 있기 때문에 서브시스템은 손님 목록이 없으므로 호출할 때 같이 넘겨줘야 한다
	// 어디갈지를 정하는 파트이기 때문에 ["백록담"]님 파트가 구현되면 교체 예정
	AActor* PickRandomUnvisitedShelf(const TArray<TObjectPtr<AActor>>& Visited) const;

	// [임시/테스트용] 안 가본 진열대가 남아있는가 판정 로직
	bool HasUnvisitedShelf(const TArray<TObjectPtr<AActor>>& Visited) const;


	// =====================================================================
	// 재고 / 집기
	// =====================================================================

	//진열대에 있는 상품을 전부 가져와 반환
	UFUNCTION(BlueprintCallable, Category = "Store|Shelf")
	TArray<AActor*> GetProductsOnShelf(const AActor* ShelfActor) const;

	// 진열대에 진열된 상품이 1개 이상인지 확인
	UFUNCTION(BlueprintCallable, Category = "Store|Shelf")
	bool HasShelfStock(const AActor* ShelfActor) const;

	// 지정한 상품을 집는 함수
	// 소비심리 파트가 고른 상품을 이 함수로 넘기면 됨.
	FName TakeProduct(AActor* ShelfActor, AActor* ProductActor, ACSCustomerCharacter* Customer);

	// 진열대 위 상품들을 판단용 후보 목록으로 변환
	// GetProductsOnShelf()가 주는 액터 목록을 판단 컴포넌트가 바로 쓸 수 있는
	// FCSPurchaseCandidate(도감 참조 + 액터) 형태로 번역하는 역할
	UFUNCTION(BlueprintCallable, Category = "Store|Shelf")
	TArray<FCSPurchaseCandidate> BuildPurchaseCandidates(const AActor* ShelfActor) const;

	// =====================================================================
	// 계산대 / 대기열
	// =====================================================================

	// Features/Customers/Interface/CSCustomerCounterInterface의 인터페이스를 무조건 상속받고 구현해야한다라는 안전장치
	void RegisterCounter(AActor* CounterActor);

	// 등록된 계산대가 제거될 때 Subsystem에 남은 참조를 정리합니다.
	void UnregisterCounter(AActor* CounterActor);

	// 대기열에 등록
	// 이미 줄에 있으면 기존 순번 반환, 줄에 없으면 맨 뒤에 추가하고 새 순번 반환
	int32 JoinQueue(ACSCustomerCharacter* Customer);

	// 줄에서 제거
	// 계산 없이 상태를 떠날 때의 안전장치. 나중에 인내심 타임아웃(포기) 기능이 생기면 그때도 이 함수가 쓰인다.
	void LeaveQueue(ACSCustomerCharacter* Customer);

	// 현재 순번 반환, 없으면 -1.
	int32 GetQueueIndex(const ACSCustomerCharacter* Customer) const;

	// 순번에 해당하는 월드 위치 (계산대가 알려주는 좌표)
	FVector GetQueueSlotLocation(const int32 Index) const;

	// 진열대 선택/반환 굴림용 스트림. 여러 함수가 const라서 mutable 필요
	mutable FRandomStream ShelfRandomStream;

	//지금 줄 맨 앞 손님 (없으면 nullptr)
	UFUNCTION(BlueprintCallable, Category = "Store|Checkout")
	ACSCustomerCharacter* GetFrontCustomer() const;

	// 맨 앞 손님의 계산을 완료 처리. 플레이어 파트가 계산 상호작용에서 호출.
	UFUNCTION(BlueprintCallable, Category = "Store|Checkout")
	void CompleteFrontCheckout();

	// =====================================================================
	// 인내심 / 계산 포기
	// =====================================================================

	// [임시] 인내심 시간. 소비심리 로직이 붙기 전까지 모든 손님이 이 값을 사용
	UPROPERTY(EditAnywhere, Category = "Store|Customer", meta = (ClampMin = "0.0"))
	float DefaultPatienceSeconds = 30.0f;

	// 이 손님이 계산대에서 참을 수 있는 시간.
	// [임시] 지금은 기본값을 그대로 반환한다.
	// ["백록담"]님 파트 연동 시 이 함수 내부만 교체하면 되고 호출하는 태스크는 수정 불필요
	float GetPatienceSeconds(const ACSCustomerCharacter* Customer) const;

	// 계산 포기 처리. 대기열 이탈 → 상품 반환 → 방송 순서로 진행한다.
	void AbandonCheckout(ACSCustomerCharacter* Customer, const float WaitedSeconds);

	// 손님이 받아에 생성 할 상품 갯수
	int32 DropCustomerItems(ACSCustomerCharacter* Customer);

	// 기록해둔 원본 정보로 상품 액터를 다시 생성, 실패 시 nullptr.
	ACSProductPickupActor* RespawnSelectProduct(const FCSSelectProduct& Selected, const FTransform& SpawnTransform, const bool bEnableDropPhysics = true);

	// 상품 떨어트리는 열의 최대 높이
	UPROPERTY(EditAnywhere, Category = "Store|Customer|Drop", meta = (ClampMin = "1.0"))
	float DropMaxStackHeight = 80.0f;

	// 위로 쌓을 때 상품 사이 너비. 상품 높이에 이 값을 더해 다음 높이를 정함
	UPROPERTY(EditAnywhere, Category = "Store|Customer|Drop", meta = (ClampMin = "0.0"))
	float DropStackGap = 3.0f;

	// 열 사이 간격. 열 기준에 있는 상품 중 지름이 가장 긴 값에 값을 더해 열 간격을 정함
	UPROPERTY(EditAnywhere, Category = "Store|Customer|Drop", meta = (ClampMin = "0.0"))
	float DropColumnGap = 8.0f;

	// 최소 열 간격
	UPROPERTY(EditAnywhere, Category = "Store|Customer|Drop", meta = (ClampMin = "1.0"))
	float DropMinColumnSpacing = 25.0f;

	// 정렬된 배치를 하지 않게 위해 오차 변수 값
	UPROPERTY(EditAnywhere, Category = "Store|Customer|Drop", meta = (ClampMin = "0.0"))
	float DropJitterRadius = 3.0f;

	// 이 손님이 계산을 포기했을 때 진열대에 되돌려놓으러 갈 확률 (0~1)
	// [임시] ["백록담"]님 파트 연동 시 이 함수 내부만 교체
	float GetReturnToShelfChance(const ACSCustomerCharacter* Customer) const;

	// [임시] 되돌려놓기 확률 기본값. 소비심리 로직이 붙기 전까지  이 값을 사용, 두 행동이 다 보여야 검증이 되므로 0.5로 설정
	UPROPERTY(EditAnywhere, Category = "Store|Customer", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultReturnToShelfChance = 0.5f;

	// 레벨의 진열대 중 하나를 랜덤으로 반환. 진열대가 하나도 없으면 nullptr.
	// [임시] 동선을 어떻게 될지 확정되면 교체 예정.
	AActor* PickRandomShelf() const;

	// 손님이 들고 있던 상품을 진열대 슬롯에 되돌려놓는다, 슬롯이 부족하면 남은 상품은 진열대 앞에 떨어뜨린다.
	// 처리 후 손님의 구매 목록은 비워진다. 반환값 = 슬롯에 실제로 들어간 개수
	int32 ReturnCustomerItemsToShelf(ACSCustomerCharacter* Customer, AActor* ShelfActor);


	// =====================================================================
	// 이벤트
	// =====================================================================

	UPROPERTY(BlueprintAssignable, Category = "Store")
	FCSOnCheckoutCompleted OnCheckoutCompleted;//계산 완료 순간 방송, 구매 목록 전체를 읽을 수 있다. 매출 집계용

	UPROPERTY(BlueprintAssignable, Category = "Store")
	FCSOnItemTaken OnItemTaken; //손님이 상품을 집는 순간마다 방송, 실시간 판매 통계/재고 추적용

	UPROPERTY(BlueprintAssignable, Category = "Store")
	FCSOnCheckoutAbandoned OnCheckoutAbandoned; // 계산 포기 순간 방송. 만족도/평판/통계 파트 구독용

private:
	UPROPERTY()
	TArray<TObjectPtr<AActor>> Shelves; // 레벨에 있는 모든 진열대 목록

	UPROPERTY()
	TObjectPtr<AActor> Counter; // 등록된 계산대

	UPROPERTY()
	TArray<TObjectPtr<ACSCustomerCharacter>> Queue; //계산 대기줄. 배열 순서가 줄 순서
	//Queue를 사용하지 않는 이유 배열의 인덱스가 곧 줄 순번이라서 인덱스를 추적가능하게 해야해서
};
