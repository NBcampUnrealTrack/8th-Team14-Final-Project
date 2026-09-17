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

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Data/CSPurchaseDecisionTypes.h"
#include "Data/CSVillainTypes.h"
#include "Math/RandomStream.h"
#include "CSCustomerRuntimeSubsystem.generated.h"

class ACSCustomerCharacter;
class ACSShelfActor;
class ACSProductPickupActor;
class ACSTrashActor;
class ACSStoreInteriorActor;
class UCSInnerThoughtWidget;
class UDataTable;
struct FCSSelectProduct;
struct FCSCategoryAffinityRow;

// 계산 완료 시 방송. 손님의 SelectProducts(상품 행 정보 목록)로 매출을 집계하면 됨
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSOnCheckoutCompleted, ACSCustomerCharacter*, Customer);
// 손님이 진열대에서 상품을 집었을 때 방송. ProductId는 DT_ProductMaster의 RowName임.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCSOnItemTaken, ACSCustomerCharacter*, Customer, FName, ProductId);
// 계산을 포기하고 떠날 때 방송. WaitedSeconds는 줄에서 기다린 시간. 만족도 하락, 평판, 매출 손실 계산은 구독하는 쪽에서 처리
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCSOnCheckoutAbandoned,ACSCustomerCharacter*, Customer,float, WaitedSeconds);
// 손님이 매장을 완전히 떠날 때(FCSTask_LeaveAndDespawn에서 파괴 직전) 방송
// bCheckoutDone이 false면 결제를 한 번도 완료하지 못하고 빈손으로 나간 것 : 빈손 퇴장률 집계에 사용
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCSOnCustomerDespawned, ACSCustomerCharacter*, Customer, bool, bCheckoutDone);
// 절도 시작 방송(디버그용 테스트). AtRiskGold는 들고 나가려는 상품의 판매가 합계.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCSOnShopliftStarted,ACSCustomerCharacter*, Customer, int32, AtRiskGold);
// 빌런 도주 성공 방송, 매출/평판 집계는 이것을 구독하세요.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCSOnShopliftEscaped,ACSCustomerCharacter*, Customer, int32, LostGold);
// 빌런 제압 성공 방송. RecoveredGold는 되찾은 상품의 판매가 합계.
// 평판 상승 / 통계 / 업적은 이것을 구독하세요.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCSOnVillainSubdued,ACSCustomerCharacter*, Customer, int32, RecoveredGold);
// 영업시간 종료로 강제 퇴장된 손님 방송, 통계 파트가 자발적 빈손 퇴장 / 계산 포기와 구분하기 위한 별도 채널
DECLARE_MULTICAST_DELEGATE_OneParam(FCSOnCustomerForcedToLeave, ACSCustomerCharacter* /*Customer*/);
// 손님이 구매를 거절했을 때 방송. 거절 연출(디버그 텍스트/속마음 위젯)과
// 애니메이션 파트의 향후 구독을 위한 채널. Info는 사유/강도 등 거절 상세 정보
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCSOnPurchaseRejected, ACSCustomerCharacter*, Customer, FCSPurchaseRejectionInfo, Info);
// 손님이 다른 손님이 흘린 쓰레기를 목격해 평판 반영 대상이 됐을 때 방송
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSOnTrashWitnessed, ACSCustomerCharacter*, Customer);

UCLASS()
class CONVENIENCESTORE_API UCSCustomerRuntimeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	UCSCustomerRuntimeSubsystem();

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// =====================================================================
	// 진열대
	// =====================================================================

	//레벨에 있는 진열대를 갱신하는 함수, 레벨 전체를 확인하여 진열대를 다시 수집.
	UFUNCTION(BlueprintCallable, Category = "Store|Shelf")
	void RefreshShelves();

	// 설치가 완료된 진열대를 손님 방문 후보에 중복 없이 등록합니다.
	void RegisterShelf(ACSShelfActor* Shelf);

	// 이동 또는 제거되는 진열대를 손님 방문 후보에서 안전하게 해제합니다.
	void UnregisterShelf(ACSShelfActor* Shelf);

	// 등록된 진열대 목록 Get 함수, 소비심리 파트가 목표를 정할 때 이 목록 쓰일 가능성 높음
	const TArray<TObjectPtr<AActor>>& GetShelves() const;

	// 손님이 진열대 앞에 설 자리와 도착 판정 영역을 진열대에서 받아옴
	bool TryGetShelfBrowsePoint(const AActor* ShelfActor,FVector& OutLocation,FRotator& OutFacing,float& OutRadius) const;

	// 손님이 지금 이 진열대의 도착 판정 영역 안에 있는지 확인합니다.
	bool IsCustomerInsideBrowseZone(const AActor* ShelfActor, const AActor* Customer) const;

	// 손님 방문 후보가 될 수 있는 진열대인지 한 번에 판정합니다.
	// (유효 + 시설 인터페이스 구현 + 설치 완료)
	// 진열대를 고르는 세 곳이 같은 조건을 쓰도록 모음
	bool IsShelfSelectable(const AActor* ShelfActor) const;

	// 계획이 없거나 계획에 맞는 진열대를 찾지 못했을 때의 폴백. 안 가본 선택 가능 진열대 중 하나를
	// 랜덤 반환(없으면 nullptr). 아래 오버로드에 빈 짝 대상 집합을 넘겨 위임한다
	// 방문 기록은 손님 개인 데이터라 UCSCustomerStateComponent가 가지고 있기 때문에 서브시스템은 손님 목록이 없으므로 호출할 때 같이 넘겨줘야 한다
	AActor* PickRandomUnvisitedShelf(const TArray<TObjectPtr<AActor>>& Visited) const;

	// 위와 같지만 AffinityTargetCategories(장바구니 기준 짝 대상 카테고리 - GetAffinityTargetCategories 참고)가
	// 있으면 "안 가본 선택 가능 진열대" 목록 안에서만 그 짝 진열대를 우선 고른다
	// 거리/가격/강도/차단은 보지 않는다 - 짝 진열대 유무만 본다
	// CategoryAttractionWeights가 있으면 위에서 정해진 후보 목록 안에서 균등이 아니라 그 가중치로 뽑는다
	// 짝 진열대가 "어느 진열대들이 후보가 될지"를 정하고, 선호 가중치는 "그 후보들 중 어디가 더 끌리는지"를 정하는 두 단계 구조
	AActor* PickRandomUnvisitedShelf(
		const TArray<TObjectPtr<AActor>>& Visited,
		const TSet<ECSProductCategory>& AffinityTargetCategories,
		const TMap<ECSProductCategory, float>& CategoryAttractionWeights,
		const ACSCustomerCharacter* Customer = nullptr) const;

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

	// 이 매장이 한 번이라도 진열했던 상품인지. 계획 상품을 못 찾은 손님의 대사를
	// "재고를 채워라"와 "레벨을 올려라" 중 어느 쪽으로 할지 가르는 데 쓴다
	// 지금 품절이어도 true - 팔던 물건을 두고 "안 판다"고 하면 플레이어에게 반대 신호가 된다
	bool WasProductEverStocked(FName ProductRowName) const;

	// 지정한 상품을 집는 함수
	// 소비심리 파트가 고른 상품을 이 함수로 넘기면 됨.
	FName TakeProduct(AActor* ShelfActor, AActor* ProductActor, ACSCustomerCharacter* Customer);

	// 진열대 위 상품들을 판단용 후보 목록으로 변환
	// GetProductsOnShelf()가 주는 액터 목록을 판단 컴포넌트가 바로 쓸 수 있는
	// FCSPurchaseCandidate(도감 참조 + 액터) 형태로 번역하는 역할
	UFUNCTION(BlueprintCallable, Category = "Store|Shelf")
	TArray<FCSPurchaseCandidate> BuildPurchaseCandidates(const AActor* ShelfActor) const;

	// 손님이 구매를 거절했을 때 호출. OnPurchaseRejected를 직접 Broadcast하지 않고
	// 이 래퍼를 거치게 해 캡슐화
	UFUNCTION(BlueprintCallable, Category = "Store|Shelf")
	void NotifyPurchaseRejected(ACSCustomerCharacter* Customer, const FCSPurchaseRejectionInfo& Info);

	// =====================================================================
	// 카테고리 연관구매
	// =====================================================================
	// 공유 데이터(DT_CategoryAffinity 로드 결과)는 서브시스템이 갖고, 개인 데이터(장바구니)는 인수로 받는다

	// 손님의 장바구니(SelectProducts)를 카테고리별 개수로 집계한다. 행을 못 찾은 원소는 건너뜀
	TMap<ECSProductCategory, int32> CountCategoriesInBasket(const TArray<FCSSelectProduct>& SelectProducts) const;

	// TargetCategory를 끌어올리는 유효 쌍 중, 장바구니에 실제로 있는 앵커의 Strength를
	// (앵커 개수만큼 중복 포함해) 내림차순으로 모아 반환 - 점수 보너스(W_Affinity) 계산용
	// 연관구매가 비활성이거나 유효 쌍이 없으면 빈 배열을 반환한다
	TArray<float> GetAffinityStrengthsForTarget(ECSProductCategory TargetCategory, const TArray<FCSSelectProduct>& SelectProducts, FString* OutAnchorSummary = nullptr) const;

	// 장바구니에 앵커가 1개 이상 있는 쌍의 TargetCategory를 전부 모은 집합
	// 연관구매가 비활성이면 빈 집합을 반환한다
	TSet<ECSProductCategory> GetAffinityTargetCategories(const TArray<FCSSelectProduct>& SelectProducts) const;

	// =====================================================================
	// 청결도 (버리는 손님 / 보는 손님)
	// =====================================================================

	// 손님이 흘린 쓰레기의 액터 클래스. 생성자에서 BP_Trash로 기본값을 채운다
	UPROPERTY(EditAnywhere, Category = "Store|Customer|Trash")
	TSubclassOf<AActor> TrashActorClass;

	// 목격 판정 반경
	UPROPERTY(EditAnywhere, Category = "Store|Customer|Trash", meta = (ClampMin = "0.0"))
	float TrashWitnessRadius = 600.0f;

	// 캐시된 쓰레기가 반경 안에 있을 때 실제로 목격 처리할 확률
	UPROPERTY(EditAnywhere, Category = "Store|Customer|Trash", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TrashWitnessChance = 0.3f;

	// Customer 발밑 근처에 TrashActorClass(폴백 시 ACSTrashActor)를 스폰하고 캐시에 등록한다
	// 캐시 등록까지 이 함수 하나로 끝내 호출부(StateTree 태스크)가 캐시 구조를 몰라도 되게 한다
	// Location이 아니라 Customer를 받는 이유: 캡슐 중심이 아니라 바닥 높이를 직접 계산해야 하고,
	// 스폰 직후 ACSTrashActor::PrepareForCustomerDrop()(물리·충돌·내비메시 정리)도 이 안에서 처리하기 때문
	void SpawnTrashNearCustomer(ACSCustomerCharacter* Customer);

	// 캐시된 쓰레기 중 Location 반경 Radius 안에 하나라도 살아 있는 게 있는지 확인한다
	// TActorIterator로 매 틱 월드를 훑지 않고 캐시 배열만 검사하는 것이 이 함수의 성능 전제
	bool IsTrashNearby(const FVector& Location, float Radius) const;

	// 목격 판정 확률(TrashWitnessChance)을 전용 스트림으로 굴린다
	bool RollTrashWitnessed() const;

	// 손님이 다른 손님의 쓰레기를 목격했을 때 호출. OnTrashWitnessed를 직접 Broadcast하지 않고
	// 이 래퍼를 거치게 해 캡슐화(NotifyPurchaseRejected와 동일한 이유)
	void NotifyTrashWitnessed(ACSCustomerCharacter* Customer);

	UPROPERTY(BlueprintAssignable, Category = "Store")
	FCSOnTrashWitnessed OnTrashWitnessed; // 쓰레기 목격 순간 방송. 청결도 평판 반영은 구독하는 쪽(평판 서브시스템) 담당

	// =====================================================================
	// 속마음 위젯
	// =====================================================================

	// 화면 하단 대화창 스타일 속마음 위젯을 Duration초간 표시
	void ShowInnerThought(TSubclassOf<UCSInnerThoughtWidget> WidgetClass, const FText& InText, float Duration);

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

	// 계산대의 순번에 해당하는 도착 판정 영역 (중심 + 반경).
	// 손님 AI는 이 중심으로 이동하고 이 반경 안에서 멈춥니다.
	bool TryGetQueueSlotZone(const int32 Index, FVector& OutCenter, float& OutRadius) const;

	// 계산대의 순번 자리로 이동할 때 AI가 멈춰도 되는 반경
	float GetQueueSlotStopRadius() const;

	// 진열대 선택/반환 굴림용 스트림. 여러 함수가 const라서 mutable 필요
	mutable FRandomStream ShelfRandomStream;

	// 이번 세션에 한 번이라도 진열대에 올라온 적 있는 상품(GetProductsOnShelf가 채운다)
	// 수집 지점이 const 함수라 mutable. 하루 단위로 비우지 않는 이유는
	// 어제 팔던 물건을 오늘 아침 재고 채우기 전에 "안 판다"고 말하게 되기 때문이다
	mutable TSet<FName> ProductsEverStocked;

	//지금 줄 맨 앞 손님 (없으면 nullptr)
	UFUNCTION(BlueprintCallable, Category = "Store|Checkout")
	ACSCustomerCharacter* GetFrontCustomer() const;

	// 맨 앞 손님이 0번 자리에 실제로 도착했을 때만 반환
	// 계산대의 결제 관련 호출부는 GetFrontCustomer가 아니라 이 함수를 써야 함
	// GetFrontCustomer는 "줄 순번상 맨 앞"이고, 이 함수는 "지금 계산 가능"입니다.
	UFUNCTION(BlueprintCallable, Category = "Store|Checkout")
	ACSCustomerCharacter* GetCheckoutReadyCustomer() const;

	// 이 손님이 자기 순번 자리에 도착했는지 확인, 인내심을 "자리에 도착한 뒤부터" 흐르게 하는 데 사용
	UFUNCTION(BlueprintPure, Category = "Store|Checkout")
	bool IsCustomerAtQueueSlot(const ACSCustomerCharacter* Customer) const;

	// 순번 자리에 선 손님이 바라볼 방향입니다. 계산대가 방향을 지정하지 않으면 false입니다.
	bool TryGetQueueSlotFacing(const int32 Index, FRotator& OutFacing) const;

	// 지금 이 계산대에 실제로 설 수 있는 사람 수. 계산대가 없거나 정원 개념이 없으면 0
	UFUNCTION(BlueprintPure, Category = "Store|Checkout")
	int32 GetEffectiveMaxQueueLength() const;

	// 대기줄이 정원까지 찼는지. 정원을 구할 수 없으면 예전처럼 무제한 설정
	UFUNCTION(BlueprintPure, Category = "Store|Checkout")
	bool IsQueueFull() const;

	// 맨 앞 손님의 계산을 완료 처리. 플레이어 파트가 계산 상호작용에서 호출.
	UFUNCTION(BlueprintCallable, Category = "Store|Checkout")
	void CompleteFrontCheckout();

	// =====================================================================
	// 인내심 / 계산 포기
	// =====================================================================

	// 폴백값
	UPROPERTY(EditAnywhere, Category = "Store|Customer", meta = (ClampMin = "0.0"))
	float DefaultPatienceSeconds = 30.0f;

	// 가장 침착한 손님(Impulsiveness=0)의 인내심 상한(초)
	UPROPERTY(EditAnywhere, Category = "Store|Customer", meta = (ClampMin = "0.0"))
	float MaxPatienceSeconds = 45.0f;

	// 가장 충동적인 손님(Impulsiveness=1)의 인내심 하한(초)
	UPROPERTY(EditAnywhere, Category = "Store|Customer", meta = (ClampMin = "0.0"))
	float MinPatienceSeconds = 15.0f;

	// 이 손님이 계산대에서 참을 수 있는 시간.
	// Archetype->Impulsiveness를 MaxPatienceSeconds~MinPatienceSeconds로 매핑해서 반환한다
	// Archetype이 없으면 DefaultPatienceSeconds로 폴백
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
	// Archetype->Impulsiveness를 MaxReturnToShelfChance~MinReturnToShelfChance로 매핑해서 반환한다
	float GetReturnToShelfChance(const ACSCustomerCharacter* Customer) const;

	// 손님이 매장을 완전히 떠날 때 호출
	// 구매 완료 여부를 읽어 OnCustomerDespawned로 방송한다 : 빈손 퇴장률 집계용
	void NotifyCustomerDespawned(ACSCustomerCharacter* Customer);

	// 폴백값
	UPROPERTY(EditAnywhere, Category = "Store|Customer", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultReturnToShelfChance = 0.5f;

	// 가장 침착한 손님(Impulsiveness=0)이 상품을 되돌려놓으러 갈 확률 상한
	UPROPERTY(EditAnywhere, Category = "Store|Customer", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxReturnToShelfChance = 0.8f;

	// 가장 충동적인 손님(Impulsiveness=1)이 상품을 되돌려놓으러 갈 확률 하한
	UPROPERTY(EditAnywhere, Category = "Store|Customer", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinReturnToShelfChance = 0.2f;

	// 레벨의 진열대 중 하나를 랜덤으로 반환. 진열대가 하나도 없으면 nullptr.
	// [임시] 동선을 어떻게 될지 확정되면 교체 예정.
	AActor* PickRandomShelf() const;

	// 손님이 들고 있던 상품을 진열대 슬롯에 되돌려놓는다, 슬롯이 부족하면 남은 상품은 진열대 앞에 떨어뜨린다.
	// 처리 후 손님의 구매 목록은 비워진다. 반환값 = 슬롯에 실제로 들어간 개수
	int32 ReturnCustomerItemsToShelf(ACSCustomerCharacter* Customer, AActor* ShelfActor);

	// =====================================================================
	// 영업시간 종료 전원 퇴장
	// =====================================================================

	// 영업시간이 끝났는가 확인.
	// 입간판 폐점과 24:00 마감이 같은 델리게이트/같은 플래그를 쓰기 때문에,그것만 보면 입간판을 돌리는 순간 매장 손님이 전부 물건을 던지고 나감
	// 입간판 폐점은 "신규 손님을 안 받는다"일 뿐,매장 안 손님은 쇼핑과 계산을 마치고 정상 퇴장
	UFUNCTION(BlueprintPure, Category = "Customer|Flow")
	bool IsBusinessHoursOver() const;

	// 이 손님을 즉시 내보내야 하는가.
	bool ShouldForceLeaveStore(const ACSCustomerCharacter* Customer) const;

	// 강제 퇴장 직전 정리. 줄 이탈 + 미결제 상품 낙하.
	// 판정과 분리한 이유: 조건은 매 틱 불리고 정리는  한 번만 불려야 함
	void PrepareForcedLeave(ACSCustomerCharacter* Customer);

	// 월드에 남은 손님을 전부 정리하고 파괴합니다.
	// 절도범은 상품을 유지한 채 파괴해 손실을 당일 장부로 확정하고,
	// 그 외 손님은 상품을 낙하시켜 재고로 되돌린 뒤 파괴합니다.
	//
	// 호출부는 마감 종(ACSStoreGameMode::RequestClosingDailySummary) 하나뿐입니다.
	// 반드시 ACSStoreGameState::BeginNextBusinessDay의 일일값 리셋보다 먼저 불러야
	// 절도 손실이 당일 장부에 남습니다. 리셋 뒤에 부르면 다음 날로 넘어갑니다.
	void DespawnAllCustomers();

	// 절도를 실행하고 아직 잡히지 않은 도주 중인 손님인지 판정합니다.
	// 마감 확정과 영업일 전환이 같은 기준을 쓰도록 한 곳에 모아둡니다.
	bool IsEscapingShoplifter(const ACSCustomerCharacter* Customer) const;

	FCSOnCustomerForcedToLeave OnCustomerForcedToLeave;

	// =====================================================================
	// 거리 통행 / 입장 판정
	// =====================================================================

	// 계획 상품이 있는 손님의 입장 확률 범위 (매장 상태 0일 때 Min, 1일 때 Max로 선형 보간)
	// 계획을 세우고 온 손님이라 매장 상태에 거의 흔들리지 않게 좁은 구간으로 잡음
	UPROPERTY(EditAnywhere, Category = "Store|Customer|Entry", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultPlannedEntryChanceMin = 0.90f;

	UPROPERTY(EditAnywhere, Category = "Store|Customer|Entry", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultPlannedEntryChanceMax = 0.98f;

	// 계획 없이 온 손님의 입장 확률 범위. 매장 상태에 크게 좌우되도록 넓은 구간으로 잡음
	// 하한을 낮게 잡은 이유: 매장이 비어 있을 때 가게 앞 거리가 눈에 띄게 한산해지도록 하기 위한 게임적 허용
	UPROPERTY(EditAnywhere, Category = "Store|Customer|Entry", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultUnplannedEntryChanceMin = 0.05f;

	UPROPERTY(EditAnywhere, Category = "Store|Customer|Entry", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultUnplannedEntryChanceMax = 0.35f;

	// 쓰레기가 이 개수 이상이면 청결도를 0으로 취급
	// 하루 동안 안 치우면 쌓이는 추정량(입장 손님 약 22명 × 투척 확률 0.30)에 맞춘 값 - 쓰레기 하나를 주울 때마다 체감되는 차이를 주려는 것
	UPROPERTY(EditAnywhere, Category = "Store|Customer|Entry", meta = (ClampMin = "1"))
	int32 DefaultTrashCountForZeroCleanliness = 6;

	// 손님이 매장에 들어갈지 판정
	// "문이 열려 있는가"라는 매장 상태만 확인, "들어가고 싶은가"라는 개인 심리는 UCSPurchaseDecisionComponent가 판단
	bool ShouldEnterStore(const ACSCustomerCharacter* Customer) const;

	// =====================================================================
	// 빌런
	// =====================================================================

	// 손님이 계산하지 않고 상품을 든 채 도주하는 로직
	// 빌런 행동을 했는지 체크용 + 빌런이 발생해서 처리해야할 함수를 구독해야하는 부분
	void CommitShoplift(ACSCustomerCharacter* Customer);

	// 절도범이 사라지기 직전에 호출. 손실이 일어났다고 알리는 부분
	// NotifyCustomerDespawned 안에서 불리므로 태스크가 직접 부를 일은 없습니다.
	void ReportShopliftEscaped(ACSCustomerCharacter* Customer);

	// 도망치는 빌런(손님)이 들고 있는 상품들의 목록을 정산용 리포트 포맷에 맞게 변환하여 추출
	UFUNCTION(BlueprintPure, Category = "Customer|Shoplift")
	TArray<FCSStolenProductRecord> GetStolenProductRecords(const ACSCustomerCharacter* Customer) const;

	// 빌런 제압 -------

	// 제압이 성립하는 손님인지 판정. 피격 시점에 한 번만 쓰는 판정
	bool CanSubdueCustomer(const ACSCustomerCharacter* Customer) const;

	// 지팡이 피격이 들어오는 함수. 제압이 성립했으면 true를 반환
	bool HandleMagicHitOnCustomer(ACSCustomerCharacter* Customer,const FVector& ImpactDirectionconst,float KnockbackStrength);

	// 제압된 손님 남아 있는 시간. 이 시간이 지나면 파괴
	UPROPERTY(EditAnywhere, Category = "Store|Villain", meta = (ClampMin = "0.0"))
	float SubduedDespawnDelay = 3.0f;

	// 아직 절도를 저지르지 않은 빌런도 때려서 잡을 수 있게 할지.
	// 기본값이 false인 이유: 실행 전 빌런은 겉보기에 일반 손님과 구분되지 않으므로, 허용하면 아무나 때리게 됨
	UPROPERTY(EditAnywhere, Category = "Store|Villain")
	bool bAllowSubdueBeforeShoplift = false;

	// =====================================================================
	// 이벤트
	// =====================================================================

	UPROPERTY(BlueprintAssignable, Category = "Store")
	FCSOnCheckoutCompleted OnCheckoutCompleted;//계산 완료 순간 방송, 구매 목록 전체를 읽을 수 있다. 매출 집계용

	UPROPERTY(BlueprintAssignable, Category = "Store")
	FCSOnItemTaken OnItemTaken; //손님이 상품을 집는 순간마다 방송, 실시간 판매 통계/재고 추적용

	UPROPERTY(BlueprintAssignable, Category = "Store")
	FCSOnCheckoutAbandoned OnCheckoutAbandoned; // 계산 포기 순간 방송. 만족도/평판/통계 파트 구독용

	UPROPERTY(BlueprintAssignable, Category = "Store")
	FCSOnCustomerDespawned OnCustomerDespawned; // 손님 퇴장(파괴) 순간 방송. 빈손 퇴장률 등 통계용

	UPROPERTY(BlueprintAssignable, Category = "Store")
	FCSOnShopliftStarted OnShopliftStarted; // 절도 시작. 경고 연출용

	UPROPERTY(BlueprintAssignable, Category = "Store")
	FCSOnShopliftEscaped OnShopliftEscaped; // 도주 성공. 손실/평판 집계용

	UPROPERTY(BlueprintAssignable, Category = "Store")
	FCSOnVillainSubdued OnVillainSubdued; // 제압 성공. 회수액/평판 집계용

	UPROPERTY(BlueprintAssignable, Category = "Store")
	FCSOnPurchaseRejected OnPurchaseRejected; // 구매 거절 순간 방송 거절 연출/애니메이션 파트 구독용

private:
	UPROPERTY()
	TArray<TObjectPtr<AActor>> Shelves; // 레벨에 있는 모든 진열대 목록

	UPROPERTY()
	TObjectPtr<AActor> Counter; // 등록된 계산대

	UPROPERTY()
	TArray<TObjectPtr<ACSCustomerCharacter>> Queue; //계산 대기줄. 배열 순서가 줄 순서
	//Queue를 사용하지 않는 이유 배열의 인덱스가 곧 줄 순번이라서 인덱스를 추적가능하게 해야해서

	// ShowInnerThought()가 재사용하는 캐싱된 화면 하단 대화창 위젯 인스턴스
	UPROPERTY()
	TObjectPtr<UCSInnerThoughtWidget> CachedInnerThoughtWidget;

	// 손님이 흘린 쓰레기 캐시. IsTrashNearby가 TActorIterator 없이 반경 검사를 하기 위한 목록
	// 유효하지 않은 항목(플레이어가 주워서 파괴됨 등)은 검사 시점에 걸러내므로 mutable
	mutable TArray<TWeakObjectPtr<ACSTrashActor>> DroppedTrashCache;

	// 레벨에 배치된 매장 실내 영역. OnWorldBeginPlay에서 한 번만 찾아 캐싱한다(레벨 로드 뒤 옮겨 다니지 않는 정적 마커라서 매 호출마다 다시 찾을 필요가 없음)
	// 없으면(레벨에 배치가 안 된 경우) nullptr로 남고 CountTrashInWorld가 월드 전체를 세는 것으로 폴백한다
	TWeakObjectPtr<ACSStoreInteriorActor> CachedStoreInteriorActor;

	// 목격 판정 30% 확률 굴림 전용 스트림. RollTrashWitnessed가 const라서 mutable 필요
	mutable FRandomStream TrashWitnessRollStream;

	// 위 위젯을 Duration 뒤 숨기기 위한 타이머
	FTimerHandle InnerThoughtHideTimerHandle;

	// 타이머 콜백 - 위젯을 숨김(뷰포트에서 제거하지 않고 Collapsed 처리만 - 다음 호출 때 재사용하기 위함)
	void HideInnerThought();

	// 등록된 진열대 전체의 충진율(점유 슬롯 ÷ 전체 슬롯)을 계산
	// const 함수라 무효 항목을 RemoveAll로 정리할 수 없어 건너뛰기만 한다
	// Out 인수는 로그에서 전체 슬롯 0(등록 누락 의심)을 바로 보여주기 위한 것
	float CalculateShelfFillRatio(int32& OutShelfCount, int32& OutFilledSlots, int32& OutTotalSlots) const;

	// GetProductsOnShelf와 달리 IsSellable() 필터 없이 Attach된 상품 액터 수만 센다
	// 충진율은 "겉보기 점유 여부"만 봐야 한다 - 유통기한이 지나 판매 불가여도 매장 밖에서는 구분되지 않는다
	int32 CountAttachedProducts(const AActor* ShelfActor) const;

	// 월드에 남아있는 쓰레기 액터 수를 센다
	// DroppedTrashCache는 손님이 흘린 것만 등록되고 레벨에 사전 배치된 쓰레기는 빠지므로,
	// 충진율과 같은 기준("눈에 보이는 것")에 맞추려고 매 판정마다 월드를 직접 순회한다
	int32 CountTrashInWorld() const;

	// 카테고리 연관구매 -------
	// DT_ProductMaster와 같은 폴더의 고정 경로. UWorldSubsystem은 디테일 패널이 없어
	// 에디터에서 할당할 수 없으므로 CSNoticeEventSubsystem과 같은 방식으로 코드에 경로를 박아둔다
	UPROPERTY()
	TSoftObjectPtr<UDataTable> CategoryAffinityDataTableAsset = TSoftObjectPtr<UDataTable>(
		FSoftObjectPath(TEXT("/Game/ConvenienceStore/Data/DT_CategoryAffinity.DT_CategoryAffinity")));

	// 검증을 통과한 쌍만 남긴 배열
	TArray<FCSCategoryAffinityRow> ValidatedAffinityRows;

	// DT_CategoryAffinity를 로드·검증해 ValidatedAffinityRows를 채운다
	void LoadAndValidateCategoryAffinityTable();
};
