#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Data/CSCheckoutTypes.h"
#include "Data/CSProductTypes.h"
#include "Data/CSStorePhaseTypes.h"
#include "Data/CSWeatherTypes.h"
#include "Data/CSEmployeeHiringTypes.h"
#include "Data/CSDeliveryOrderTypes.h"
#include "Data/CSGachaDataTypes.h"
#include "Features/Interaction/CSShelfPlaceable.h"
#include "CSSaveTypes.generated.h"

// 세이브 포맷 버전
// 마이그레이션 없이 버전 불일치를 로드 거부로 처리
// 2: 퀵바 슬롯별 레코드, 포장 상자·창고 보관·바닥 물리 상태 추가
// 3: 월드 쓰레기 레코드 추가
// 4: 영업 종료 시점 저장, 저장 당시 단계·당일 집계·평판 당일 통계 추가
// 5: 아티팩트·제조 블록, 가챠 봉인석·아티팩트 픽업·소환 수레 레코드, 쓰레기 표시 이름 추가
#define CS_SAVE_VERSION_CURRENT 5

// DataTable 행을 세이브에 담는 공용 표현
// RowName을 기준으로 저장하고 테이블 경로는 복원용 힌트로 함께 보관
USTRUCT()
struct CONVENIENCESTORE_API FCSSaveRowRef
{
	GENERATED_BODY()

	// 복원 기준이 되는 행 이름
	UPROPERTY()
	FName RowName;

	// 저장 시점에 행이 속해 있던 테이블 경로
	UPROPERTY()
	FSoftObjectPath TablePath;

	// 행 이름이 비어 있지 않은지 확인
	bool IsValidRef() const
	{
		return !RowName.IsNone();
	}

	// 런타임 행 핸들을 세이브 표현으로 변환
	static FCSSaveRowRef FromHandle(const FDataTableRowHandle& InHandle)
	{
		FCSSaveRowRef Result;
		Result.RowName = InHandle.RowName;
		if (InHandle.DataTable != nullptr)
		{
			Result.TablePath = FSoftObjectPath(InHandle.DataTable.Get());
		}

		return Result;
	}

	// 세이브 표현을 런타임 행 핸들로 변환
	// 테이블 경로를 해석하지 못하면 FallbackTable을 사용
	FDataTableRowHandle ToHandle(const UDataTable* FallbackTable = nullptr) const
	{
		FDataTableRowHandle Result;
		if (!IsValidRef())
		{
			return Result;
		}

		const UDataTable* ResolvedTable = nullptr;
		if (TablePath.IsValid())
		{
			ResolvedTable = Cast<UDataTable>(TablePath.TryLoad());
		}

		if (ResolvedTable == nullptr)
		{
			ResolvedTable = FallbackTable;
		}

		if (ResolvedTable == nullptr)
		{
			return Result;
		}

		Result.DataTable = ResolvedTable;
		Result.RowName = RowName;
		return Result;
	}
};

// -----------------------------------------------------------------------------
// 헤더
// -----------------------------------------------------------------------------

// 슬롯 목록 UI가 읽는 경량 요약 정보
// 위젯 함수 시그니처에 노출되므로 BlueprintType으로 설정
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSSaveHeader
{
	GENERATED_BODY()

	// 저장 시점의 세이브 포맷 버전
	// 0이면 비어 있는 헤더로 판단
	UPROPERTY()
	int32 SaveVersion = 0;

	// 로드 시 다시 열 레벨 경로
	// 튜토리얼 맵과 본 게임 맵의 GameMode가 달라 반드시 기록
	UPROPERTY()
	FSoftObjectPath LevelPath;

	// 헤더가 가리키는 슬롯 이름
	UPROPERTY()
	FName SlotName;

	// 저장 시점의 영업일
	UPROPERTY()
	int32 BusinessDay = 1;

	// 저장 시점의 보유 자금
	UPROPERTY()
	int32 Funds = 0;

	// 저장 시점의 상점 레벨
	UPROPERTY()
	int32 StoreLevel = 1;

	// 저장 시점까지 누적된 플레이 시간(초)
	UPROPERTY()
	float PlayTimeSeconds = 0.0f;

	// 저장 시각(UTC)
	UPROPERTY()
	FDateTime SavedAtUtc = FDateTime();

	// 자동 세이브 슬롯 여부
	UPROPERTY()
	bool bIsAutoSave = false;

	// 실제 저장 데이터가 있는 헤더인지 확인
	bool IsValidHeader() const
	{
		return SaveVersion > 0;
	}
};

// -----------------------------------------------------------------------------
// GameState
// -----------------------------------------------------------------------------

// 영업 진행 상태 레코드
// 영업 종료 직후에 저장하므로 그날 집계를 함께 기록해 불러온 뒤 정산이 같은 결과를 내도록 유지
// 시계·영업 여부 등 나머지 단계 상태는 불러온 뒤 GameMode가 SavedPhase 기준으로 다시 확정
USTRUCT()
struct CONVENIENCESTORE_API FCSStoreStateRecord
{
	GENERATED_BODY()

	// 저장 당시 단계
	// ClosingFreeRoam이면 불러온 뒤 자유 시간으로 복귀하고 그 외에는 준비 단계 아침으로 시작
	UPROPERTY()
	ECStorePhase SavedPhase = ECStorePhase::Preparation;

	// 저장 시점의 영업일
	UPROPERTY()
	int32 CurrentBusinessDay = 1;

	// 보유 자금 골드
	UPROPERTY()
	int32 CurrentFunds = 0;

	// 현재 가게 레벨에서 누적한 경험치
	UPROPERTY()
	int32 CurrentStoreExperience = 0;

	// 가게 레벨
	UPROPERTY()
	int32 CurrentStoreLevel = 1;

	// 평판 점수
	UPROPERTY()
	int32 CurrentReputationScore = 6000;

	// 평판 점수에 대응하는 평판 등급
	UPROPERTY()
	ECSReputationGrade CurrentReputationGrade = ECSReputationGrade::Normal;

	// 마법진 강화 단계
	// 하루 배달 주문 생성 한도로 사용
	UPROPERTY()
	int32 MagicCircleLevel = 1;

	// 확인을 마친 직전 정산의 최종 순이익
	UPROPERTY()
	int32 PreviousDailyNetProfitGold = 0;

	// 오늘의 평판 변화를 정산에 표시하기 위한 하루 시작 평판 점수
	UPROPERTY()
	int32 DailyStartingReputationScore = 6000;

	// 임대료를 마지막으로 처리한 영업일
	// 복원하지 않으면 로드 직후 임대료를 이중 청구
	UPROPERTY()
	int32 LastRentProcessedBusinessDay = INDEX_NONE;

	// 인건비를 마지막으로 처리한 영업일
	// 복원하지 않으면 로드 직후 인건비를 이중 청구
	UPROPERTY()
	int32 LastPayrollProcessedBusinessDay = INDEX_NONE;

	// 당일 집계
	// 필드 의미는 ACSStoreGameState의 같은 이름 멤버와 동일

	// 오늘 영업을 한 번이라도 시작했는지
	UPROPERTY()
	bool bHasStoreOpenedToday = false;

	// 오늘 결제 완료된 거래의 총매출
	UPROPERTY()
	int32 DailySalesGold = 0;

	// 오늘 판매된 상품의 총매입가
	UPROPERTY()
	int32 DailyCostGold = 0;

	// 오늘 총매출에서 총매입가를 뺀 이익
	UPROPERTY()
	int32 DailyProfitGold = 0;

	// 오늘 결제 완료된 상품의 총수량
	UPROPERTY()
	int32 DailySoldQuantity = 0;

	// 오늘 완료한 손님 결제 횟수
	UPROPERTY()
	int32 CompletedCheckoutCount = 0;

	// 오늘 낱개 상품을 폐기하며 잃은 매입가 합계
	UPROPERTY()
	int32 DailyWasteLossGold = 0;

	// 오늘 마감 처리에서 실제로 납부한 임대료
	UPROPERTY()
	int32 DailyRentGold = 0;

	// 오늘 보유 자금에서 실제로 차감된 모든 지출의 합계
	UPROPERTY()
	int32 DailyExpenseGold = 0;

	// 오늘 마법진 강화에 쓴 지출
	UPROPERTY()
	int32 DailyMagicCircleUpgradeExpenseGold = 0;

	// 오늘 직원 고용에 쓴 지출
	UPROPERTY()
	int32 DailyEmployeeHireExpenseGold = 0;

	// 오늘 마감 처리에서 지급한 직원 인건비
	UPROPERTY()
	int32 DailyEmployeeWageGold = 0;

	// 오늘 일반 상품 발주에 쓴 지출
	UPROPERTY()
	int32 DailyProductOrderExpenseGold = 0;

	// 오늘 일반 시설 발주에 쓴 지출
	UPROPERTY()
	int32 DailyFacilityOrderExpenseGold = 0;

	// 오늘 빠른 상품 발주에 쓴 지출
	UPROPERTY()
	int32 DailyQuickProductOrderExpenseGold = 0;

	// 오늘 빠른 시설 발주에 쓴 지출
	UPROPERTY()
	int32 DailyQuickFacilityOrderExpenseGold = 0;

	// 도주에 성공한 절도 손님이 훔친 상품의 판매가 합계
	UPROPERTY()
	int32 DailyShopliftLossGold = 0;

	// 상품 분석에 표시할 오늘 폐기된 상품별 수량
	UPROPERTY()
	TMap<FName, int32> DailyDisposedProductQuantities;

	// 폐기 후 상품 액터가 사라져도 분석 화면에 표시할 상품명
	UPROPERTY()
	TMap<FName, FText> DailyDisposedProductNames;

	// 상품 분석에 표시할 오늘 도난당한 상품별 수량
	UPROPERTY()
	TMap<FName, int32> DailyStolenProductQuantities;

	// 도주 손님이 파괴된 뒤에도 분석 화면에 표시할 도난 상품명
	UPROPERTY()
	TMap<FName, FText> DailyStolenProductNames;

	// 정산과 판매 보고서가 읽는 그날 결제 거래 목록
	UPROPERTY()
	TArray<FCSCheckoutTransaction> DailyCheckoutTransactions;

	// 영업일 안에서 다음 거래에 사용할 순번
	UPROPERTY()
	int32 NextTransactionId = 1;
};

// 잠자기 때 평판 계산에 쓰는 그날 손님 통계
// UCSStoreReputationSubsystem의 Daily* 멤버와 1:1
USTRUCT()
struct CONVENIENCESTORE_API FCSReputationDailySaveRecord
{
	GENERATED_BODY()

	// 매장에 실제로 들어온 손님 수 (지나간 행인·영업 종료 강제 퇴장 손님 제외)
	// 만족도 계산의 분모
	UPROPERTY()
	int32 DailyEnteredCustomers = 0;

	// 정상 결제를 완료한 횟수
	UPROPERTY()
	int32 DailyNormalCheckouts = 0;

	// 계산 포기 횟수
	UPROPERTY()
	int32 DailyCheckoutAbandoned = 0;

	// 빈손 퇴장 횟수
	UPROPERTY()
	int32 DailyEmptyHandedDepartures = 0;

	// 누적된 가격 불쾌 가중치 합 (손님당 최대 1.0, 1일 1회 제한)
	UPROPERTY()
	float DailyPriceUpsetSum = 0.0f;

	// 다른 손님의 쓰레기를 목격해 평판에 반영된 횟수 (손님당 1일 1회 제한)
	UPROPERTY()
	int32 DailyTrashWitnessed = 0;
};

// -----------------------------------------------------------------------------
// 서브시스템
// -----------------------------------------------------------------------------

// 상품별 판매가 오버라이드 레코드
USTRUCT()
struct CONVENIENCESTORE_API FCSPricingSaveRecord
{
	GENERATED_BODY()

	// 상품 행 이름별로 플레이어가 지정한 판매가
	// 목록에 없는 상품은 기본 판매가를 사용
	UPROPERTY()
	TMap<FName, int32> SalePriceOverrides;
};

// 고용 계약 한 건의 레코드
// 직원 액터 참조는 복원 시 다시 연결하므로 제외
USTRUCT()
struct CONVENIENCESTORE_API FCSEmployeeContractRecord
{
	GENERATED_BODY()

	// 계약한 고용 슬롯
	UPROPERTY()
	ECSEmployeeHireSlot Slot = ECSEmployeeHireSlot::FirstRestock;

	// 고용 시 지불한 비용
	UPROPERTY()
	int32 HireCost = 0;

	// 영업일마다 지급할 일급
	UPROPERTY()
	int32 DailyWage = 0;

	// 직원이 근무를 시작하는 영업일
	UPROPERTY()
	int32 StartBusinessDay = 1;
};

// 고용 계약 전체 레코드
USTRUCT()
struct CONVENIENCESTORE_API FCSEmployeeHiringSaveRecord
{
	GENERATED_BODY()

	// 체결된 고용 계약 목록
	UPROPERTY()
	TArray<FCSEmployeeContractRecord> Contracts;
};

// 발주 한 줄의 레코드
USTRUCT()
struct CONVENIENCESTORE_API FCSOrderLineRecord
{
	GENERATED_BODY()

	// 발주한 상품 또는 시설의 데이터 행
	UPROPERTY()
	FCSSaveRowRef Row;

	// 상품 발주는 상자 수, 시설 발주는 수량으로 사용
	UPROPERTY()
	int32 Quantity = 0;
};

// 배송 대기 중인 발주 한 건의 레코드
USTRUCT()
struct CONVENIENCESTORE_API FCSPendingOrderRecord
{
	GENERATED_BODY()

	// 한 번에 발주한 품목 목록
	UPROPERTY()
	TArray<FCSOrderLineRecord> OrderLines;

	// 배송이 도착할 영업일
	UPROPERTY()
	int32 DeliveryBusinessDay = 0;
};

// 발주 서브시스템 레코드
USTRUCT()
struct CONVENIENCESTORE_API FCSOrderSaveRecord
{
	GENERATED_BODY()

	// 배송 대기 중인 상품 발주 목록
	UPROPERTY()
	TArray<FCSPendingOrderRecord> PendingProductOrders;

	// 배송 대기 중인 시설 발주 목록
	UPROPERTY()
	TArray<FCSPendingOrderRecord> PendingFacilityOrders;

	// 배송 스폰 순번을 마지막으로 초기화한 영업일
	// 같은 영업일의 배송 위치 중복 배정을 방지
	UPROPERTY()
	int32 DeliverySpawnIndexBusinessDay = INDEX_NONE;

	// 다음 상품 배송 상자에 배정할 스폰 위치 순번
	UPROPERTY()
	int32 NextProductDeliverySpawnIndex = 0;

	// 다음 시설 배송에 배정할 스폰 위치 순번
	UPROPERTY()
	int32 NextFacilityDeliverySpawnIndex = 0;
};

// 배달 주문 상품 한 줄의 레코드
USTRUCT()
struct CONVENIENCESTORE_API FCSDeliveryOrderLineRecord
{
	GENERATED_BODY()

	// 주문한 상품의 데이터 행
	UPROPERTY()
	FCSSaveRowRef ProductRow;

	// 주문한 상품 수량
	UPROPERTY()
	int32 Quantity = 1;
};

// 배달 주문 한 건의 레코드
USTRUCT()
struct CONVENIENCESTORE_API FCSDeliveryOrderRecord
{
	GENERATED_BODY()

	// 주문 식별 번호
	UPROPERTY()
	int32 OrderId = INDEX_NONE;

	// 주문 진행 상태
	UPROPERTY()
	ECSDeliveryOrderStatus Status = ECSDeliveryOrderStatus::None;

	// 배달에 필요한 상품 목록
	UPROPERTY()
	TArray<FCSDeliveryOrderLineRecord> RequiredProducts;

	// 수신처 식별자
	UPROPERTY()
	FName RecipientId;

	// 화면에 표시할 수신처 이름
	UPROPERTY()
	FText RecipientDisplayName;

	// 화면에 표시할 수신처 지역 이름
	UPROPERTY()
	FText RecipientRegionName;

	// 배달 완료 시 지급할 골드
	UPROPERTY()
	int32 RewardGold = 0;

	// 배달 완료 시 지급할 평판
	UPROPERTY()
	int32 ReputationReward = 1;

	// 주문을 수락할 수 있는 마감 시각
	// 자정부터 경과한 게임 분 단위
	UPROPERTY()
	float AcceptanceDeadlineMinutes = 0.0f;

	// 수락한 주문의 배달 완료 마감 시각
	// 자정부터 경과한 게임 분 단위
	UPROPERTY()
	float DeliveryDeadlineMinutes = 0.0f;
};

// 배달 주문 서브시스템 레코드
USTRUCT()
struct CONVENIENCESTORE_API FCSDeliveryOrderSaveRecord
{
	GENERATED_BODY()

	// 진행 중인 배달 주문 목록
	UPROPERTY()
	TArray<FCSDeliveryOrderRecord> DeliveryOrders;

	// 최근 종료된 배달 주문 결과 목록
	// 최신 결과가 앞에 오며 최대 개수를 넘으면 오래된 결과를 제거
	UPROPERTY()
	TArray<FCSDeliveryOrderRecord> RecentDeliveryResults;

	// 수락한 주문에 예약된 상품 행 이름별 수량
	UPROPERTY()
	TMap<FName, int32> ReservedQuantities;

	// 다음 주문에 발급할 식별 번호
	UPROPERTY()
	int32 NextOrderId = 1;

	// 주문 생성을 마지막으로 초기화한 영업일
	// 복원하지 않으면 로드 직후 그날 주문을 새로 생성
	UPROPERTY()
	int32 LastObservedBusinessDay = INDEX_NONE;

	// 오늘 생성한 주문 수
	UPROPERTY()
	int32 GeneratedOrderCountToday = 0;

	// 다음 배달 주문이 들어올 예정 시간
	// -1은 아직 예약하지 않은 상태를 의미
	UPROPERTY()
	float NextOrderGenerationMinutes = -1.0f;

	// 다음 주문 시각을 예약할 때 사용한 하루 주문 한도
	// 한도가 변경되면 남은 시간에 맞춰 다음 주문 시각을 재배치
	UPROPERTY()
	int32 LastScheduledDailyOrderLimit = INDEX_NONE;

	// 현재 배달을 진행 중인 주문 번호
	// INDEX_NONE은 진행 중인 배달이 없는 상태를 의미
	UPROPERTY()
	int32 ActiveDispatchOrderId = INDEX_NONE;
};

// 수첩 배달 기록 한 건의 레코드
USTRUCT()
struct CONVENIENCESTORE_API FCSJournalDeliveryRecord
{
	GENERATED_BODY()

	// 배달 기록 고유 식별자
	UPROPERTY()
	FGuid RecordId;

	// 주문이 발생한 월드 구분자
	// 월드마다 주문 번호가 다시 시작되므로 주문 번호와 함께 기록을 식별
	UPROPERTY()
	FName LevelId;

	// 주문을 수락한 영업일
	UPROPERTY()
	int32 AcceptedDay = 1;

	// 주문이 종료된 영업일
	// 0은 아직 진행 중인 주문을 의미
	UPROPERTY()
	int32 FinishedDay = 0;

	// 마지막으로 반영한 주문 상태
	UPROPERTY()
	FCSDeliveryOrderRecord Order;
};

// 수첩 이벤트 기록 한 건의 레코드
USTRUCT()
struct CONVENIENCESTORE_API FCSJournalEventRecord
{
	GENERATED_BODY()

	// 이벤트 식별자
	UPROPERTY()
	FName EventId;

	// 이벤트가 발생한 영업일
	UPROPERTY()
	int32 BusinessDay = 1;

	// 수첩에 표시할 이벤트 제목
	UPROPERTY()
	FText Title;

	// 수첩에 표시할 이벤트 본문
	UPROPERTY()
	FText Body;

	// 플레이어의 열람 여부
	UPROPERTY()
	bool bRead = false;
};

// 계약 해금과 아티팩트 보유 상태를 함께 복원합니다.
USTRUCT()
struct CONVENIENCESTORE_API FCSArtifactSaveRecord
{
	GENERATED_BODY()
	UPROPERTY() TMap<FName, int32> Counts;
	UPROPERTY() TMap<FName, int32> AppliedStacks;
	UPROPERTY() TMap<FName, int32> NextAvailableDays;
	UPROPERTY() bool bFairyContractSigned = false;
};

// 수첩 서브시스템 레코드
USTRUCT()
struct CONVENIENCESTORE_API FCSJournalSaveRecord
{
	GENERATED_BODY()

	// 해금한 종족 정보 식별자 목록
	UPROPERTY()
	TArray<FName> UnlockedEntries;

	// 수집한 유물 식별자 목록
	UPROPERTY()
	TArray<FName> CollectedArtifacts;

	// 기록된 이벤트 목록
	UPROPERTY()
	TArray<FCSJournalEventRecord> Events;

	// 기록된 배달 목록
	UPROPERTY()
	TArray<FCSJournalDeliveryRecord> Deliveries;

	// 현재까지 도달한 가장 늦은 영업일
	// 달력에서 열람할 수 있는 마지막 달을 결정
	UPROPERTY()
	int32 LatestDay = 1;
};

// 날씨 서브시스템 레코드
USTRUCT()
struct CONVENIENCESTORE_API FCSWeatherSaveRecord
{
	GENERATED_BODY()

	// 영업일 인덱스별 확정 날씨
	// EnsureDecidedThrough()가 이미 결정된 날을 다시 추첨하지 않도록 유지
	UPROPERTY()
	TArray<ECSWeatherType> DecidedWeather;

	// 오늘의 날씨
	UPROPERTY()
	ECSWeatherType CurrentWeather = ECSWeatherType::Clear;

	// 기간을 지정해 적용 중인 날씨 확률 목록
	// 적용 기간이 지난 항목은 확률 계산에서 제외
	UPROPERTY()
	TArray<FCSWeatherChanceLayer> ChanceLayers;

	// 날씨 서브시스템이 마지막으로 반영한 영업일
	UPROPERTY()
	int32 CurrentBusinessDay = 1;
};

// 뉴스·기념일 서브시스템 레코드
USTRUCT()
struct CONVENIENCESTORE_API FCSNoticeEventSaveRecord
{
	GENERATED_BODY()

	// 오늘의 뉴스 데이터 행 이름
	// None은 오늘 뉴스가 없는 상태를 의미
	UPROPERTY()
	FName CurrentNewsRowName;

	// 오늘 적용 중인 기념일 데이터 행 이름 목록
	UPROPERTY()
	TArray<FName> ActiveHolidayRowNames;

	// 뉴스를 마지막으로 결정한 영업일
	// 복원하지 않으면 로드 직후 뉴스를 재추첨
	UPROPERTY()
	int32 LastProcessedBusinessDay = INDEX_NONE;
};

// -----------------------------------------------------------------------------
// 월드 액터
// -----------------------------------------------------------------------------

// 상품 런타임 상태 레코드
// 신선도 감소 기준 시각이 월드 시간에 묶여 있어 경과분을 반영한 현재 신선도로 저장
USTRUCT()
struct CONVENIENCESTORE_API FCSProductStateRecord
{
	GENERATED_BODY()

	// 신선도 초기화 여부
	UPROPERTY()
	bool bFreshnessInitialized = false;

	// 저장 시점까지의 감소분을 반영한 신선도
	UPROPERTY()
	int32 Freshness = 0;

	// 신선도 감소 진행 여부
	// 복원 시 감소 기준 시각을 현재 월드 시간으로 다시 설정
	UPROPERTY()
	bool bFreshnessDecayActive = false;

	// 유통기한 초기화 여부
	UPROPERTY()
	bool bExpirationInitialized = false;

	// 영업일 기준 유통기한
	// 월드 시간과 무관하므로 그대로 저장
	UPROPERTY()
	int32 ExpirationBusinessDay = 0;
};

// 진열대 슬롯 한 칸의 상품 레코드
USTRUCT()
struct CONVENIENCESTORE_API FCSShelfSlotRecord
{
	GENERATED_BODY()

	// 진열대 내 슬롯 번호
	UPROPERTY()
	int32 SlotIndex = INDEX_NONE;

	// 슬롯 상품을 다시 생성할 액터 클래스
	UPROPERTY()
	FSoftClassPath ActorClass;

	// 슬롯에 놓인 상품의 데이터 행
	UPROPERTY()
	FCSSaveRowRef ProductRow;

	// 슬롯에 놓인 상품의 신선도·유통기한 상태
	UPROPERTY()
	FCSProductStateRecord ProductState;
};

// 진열대 단별 지정 품목 레코드
USTRUCT()
struct CONVENIENCESTORE_API FCSShelfLevelConfigRecord
{
	GENERATED_BODY()

	// 품목을 지정한 진열대 단
	UPROPERTY()
	ECSShelfSlotLevel SlotLevel = ECSShelfSlotLevel::Middle;

	// 해당 단에 지정한 상품의 데이터 행
	UPROPERTY()
	FCSSaveRowRef ProductRow;

	// 플레이어가 UI에서 직접 지정한 단인지 여부
	// 직접 지정한 단은 다른 단과 같은 품목을 허용
	UPROPERTY()
	bool bIsPlayerConfigured = false;
};

// 진열대 한 개의 레코드
USTRUCT()
struct CONVENIENCESTORE_API FCSShelfRecord
{
	GENERATED_BODY()

	// 레벨 배치 진열대를 다시 찾을 때 사용하는 식별자
	UPROPERTY()
	FGuid SaveId;

	// 런타임 스폰 진열대를 다시 생성할 클래스
	UPROPERTY()
	FSoftClassPath ActorClass;

	// 진열대의 월드 트랜스폼
	UPROPERTY()
	FTransform Transform = FTransform::Identity;

	// 게임 중 새로 생성한 진열대인지 여부
	// true면 다시 스폰하고 false면 레벨 배치 진열대를 찾아 복원
	UPROPERTY()
	bool bWasSpawnedAtRuntime = false;

	// 설치 완료 여부
	UPROPERTY()
	bool bIsInstalled = true;

	// 단별 지정 품목 목록
	UPROPERTY()
	TArray<FCSShelfLevelConfigRecord> LevelConfigurations;

	// 상품이 올라가 있는 슬롯만 기록
	UPROPERTY()
	TArray<FCSShelfSlotRecord> OccupiedSlots;
};

// 바닥 또는 창고에 놓인 낱개 상품 레코드
USTRUCT()
struct CONVENIENCESTORE_API FCSLooseProductRecord
{
	GENERATED_BODY()

	// 상품을 다시 생성할 액터 클래스
	UPROPERTY()
	FSoftClassPath ActorClass;

	// 상품의 데이터 행
	UPROPERTY()
	FCSSaveRowRef ProductRow;

	// 상품의 월드 트랜스폼
	UPROPERTY()
	FTransform Transform = FTransform::Identity;

	// 상품의 신선도·유통기한 상태
	UPROPERTY()
	FCSProductStateRecord ProductState;

	// 상품이 보관된 진열대 종류
	// 상품 데이터의 진열대 종류와 같을 때만 신선도 관리를 적용
	UPROPERTY()
	ECSProductShelfType StorageShelfType = ECSProductShelfType::UtilityShelf;

	// 보관 중이던 창고 액터 이름
	// 비어 있으면 월드 바닥에 놓인 상품으로 처리
	UPROPERTY()
	FName StorageActorName;

	// 바닥 물리 상태로 놓여 있던 상품 여부
	// 복원 후 물리를 다시 켜서 바닥에 안착
	UPROPERTY()
	bool bWorldDropPhysics = false;
};

// 배송 상자 한 개의 레코드
USTRUCT()
struct CONVENIENCESTORE_API FCSCrateRecord
{
	GENERATED_BODY()

	// 상자를 다시 생성할 액터 클래스
	UPROPERTY()
	FSoftClassPath ActorClass;

	// 상자에 담긴 상품의 데이터 행
	UPROPERTY()
	FCSSaveRowRef ProductRow;

	// 상자에 남은 상품 수량
	UPROPERTY()
	int32 RemainingQuantity = 0;

	// 상자의 월드 트랜스폼
	UPROPERTY()
	FTransform Transform = FTransform::Identity;

	// 상자에 담긴 상품의 신선도·유통기한 상태
	UPROPERTY()
	FCSProductStateRecord ProductState;

	// 창고 입고를 기다리는 발주 상자 여부
	UPROPERTY()
	bool bAwaitingStockroomIntake = false;

	// 낱개 상품을 담아 만든 포장 상자 여부
	UPROPERTY()
	bool bPackingCrate = false;

	// 포장 상자에 담긴 낱개 상품별 상태
	UPROPERTY()
	TArray<FCSProductStateRecord> PackedProductStates;

	// 포장 상자가 상품을 꺼낼 때 생성할 상품 액터 클래스
	UPROPERTY()
	FSoftClassPath ProductActorClass;

	// 보관 중이던 창고 액터 이름
	// 비어 있으면 월드 바닥에 놓인 상자로 처리
	UPROPERTY()
	FName StorageActorName;

	// 바닥 물리 상태로 놓여 있던 상자 여부
	// 복원 후 물리를 다시 켜서 바닥에 안착
	UPROPERTY()
	bool bWorldDropPhysics = false;

	// [백무송]
	// 실려 있던 소환 수레의 식별자
	// 비어 있거나 수레를 찾지 못하면 바닥 상자로 복원
	UPROPERTY()
	FGuid CartSaveId;
};

// 설치형 시설 한 개의 레코드
USTRUCT()
struct CONVENIENCESTORE_API FCSFacilityRecord
{
	GENERATED_BODY()

	// 레벨 배치 시설을 다시 찾을 때 사용하는 식별자
	UPROPERTY()
	FGuid SaveId;

	// 시설 데이터 식별자
	UPROPERTY()
	FName FacilityId;

	// 런타임 스폰 시설을 다시 생성할 클래스
	UPROPERTY()
	FSoftClassPath ActorClass;

	// 시설의 월드 트랜스폼
	UPROPERTY()
	FTransform Transform = FTransform::Identity;

	// 게임 중 새로 생성한 시설인지 여부
	// true면 다시 스폰하고 false면 레벨 배치 시설을 찾아 복원
	UPROPERTY()
	bool bWasSpawnedAtRuntime = false;

	// 설치 완료 여부
	UPROPERTY()
	bool bIsInstalled = true;
};

// 월드에 놓인 쓰레기 한 개의 레코드
// 레벨 배치 쓰레기와 손님이 흘린 쓰레기를 구분 없이 기록
USTRUCT()
struct CONVENIENCESTORE_API FCSTrashRecord
{
	GENERATED_BODY()

	// 쓰레기를 다시 생성할 액터 클래스
	UPROPERTY()
	FSoftClassPath ActorClass;

	// 쓰레기 메시
	// 레벨 인스턴스마다 메시를 바꿔 배치할 수 있어 함께 기록
	UPROPERTY()
	FSoftObjectPath StaticMesh;

	// 상호작용 안내와 퀵바에 표시할 쓰레기 이름
	// 제조 실패물처럼 생성 후 이름을 바꾼 쓰레기가 기본 이름으로 돌아가지 않도록 기록
	UPROPERTY()
	FText DisplayName;

	// 쓰레기의 월드 트랜스폼
	UPROPERTY()
	FTransform Transform = FTransform::Identity;

	// 루트 기준 메시의 상대 트랜스폼
	UPROPERTY()
	FTransform MeshRelativeTransform = FTransform::Identity;

	// 바닥 물리 상태로 놓여 있던 쓰레기 여부
	// 복원 후 물리를 다시 켜서 바닥에 안착
	UPROPERTY()
	bool bWorldDropPhysics = false;

	// Pawn 충돌을 무시하던 쓰레기 여부
	// 손님이 흘린 쓰레기가 통로를 막지 않도록 복원 후에도 유지
	UPROPERTY()
	bool bIgnorePawnCollision = false;
};

// 월드에 놓인 아티팩트 픽업 한 개의 레코드
// 레벨 배치 픽업과 가챠 보상 픽업을 구분 없이 기록
USTRUCT()
struct CONVENIENCESTORE_API FCSArtifactPickupRecord
{
	GENERATED_BODY()

	// 픽업을 다시 생성할 액터 클래스
	UPROPERTY()
	FSoftClassPath ActorClass;

	// 아티팩트 데이터 행
	UPROPERTY()
	FCSSaveRowRef ArtifactRow;

	// 획득 시 지급할 수량
	UPROPERTY()
	int32 Quantity = 1;

	// 픽업 액터의 월드 트랜스폼
	// 물리로 떨어진 픽업은 메시 위치를 액터 위치로 기록
	UPROPERTY()
	FTransform Transform = FTransform::Identity;

	// 표시 메시의 월드 트랜스폼
	// 행 기본 메시 배치를 다시 적용한 뒤에도 저장 시점 위치·회전·크기를 유지
	UPROPERTY()
	FTransform MeshWorldTransform = FTransform::Identity;

	// 표시 메시가 물리 시뮬레이션 중이던 픽업 여부
	// 가챠에서 배출된 픽업의 충돌·물리 상태를 복원 후에도 유지
	UPROPERTY()
	bool bSimulatingPhysics = false;
};

// 개봉하지 않은 가챠 봉인석 한 개의 레코드
// 보상 행 전체 대신 개봉 시 보상 생성에 필요한 값만 기록
USTRUCT()
struct CONVENIENCESTORE_API FCSGachaSealRecord
{
	GENERATED_BODY()

	// 봉인석을 다시 생성할 액터 클래스
	UPROPERTY()
	FSoftClassPath ActorClass;

	// 봉인석의 월드 트랜스폼
	// 루트인 표시 메시가 물리로 움직인 위치까지 포함
	UPROPERTY()
	FTransform Transform = FTransform::Identity;

	// 바닥 물리 상태로 놓여 있던 봉인석 여부
	// 복원 후 물리를 다시 켜서 바닥에 안착
	UPROPERTY()
	bool bWorldDropPhysics = false;

	// 보상 종류
	UPROPERTY()
	ECSGachaRewardType RewardType = ECSGachaRewardType::Product;

	// 개봉 알림과 경고 로그에 사용하는 보상 식별자
	UPROPERTY()
	FName RewardId;

	// 보상 표시 이름
	UPROPERTY()
	FText DisplayName;

	// 상품 보상의 데이터 행
	UPROPERTY()
	FCSSaveRowRef ProductRow;

	// 아티팩트 보상의 데이터 행
	UPROPERTY()
	FCSSaveRowRef ArtifactRow;

	// 보상 행이 지정한 상품 액터 클래스
	// 비어 있으면 ProductPickupClass로 상품을 생성
	UPROPERTY()
	FSoftClassPath RewardActorClass;

	// 보상 수량
	UPROPERTY()
	int32 Quantity = 1;

	// 상품 보상을 생성할 기본 상품 액터 클래스
	UPROPERTY()
	FSoftClassPath ProductPickupClass;

	// 아티팩트 보상을 생성할 픽업 액터 클래스
	UPROPERTY()
	FSoftClassPath ArtifactPickupClass;
};

// 아티팩트로 소환한 수레 한 개의 레코드
// 레벨 배치 수레는 레벨이 다시 생성하므로 기록하지 않음
USTRUCT()
struct CONVENIENCESTORE_API FCSSummonedCartRecord
{
	GENERATED_BODY()

	// 다시 생성한 수레가 이어받을 식별자
	UPROPERTY()
	FGuid SaveId;

	// 수레를 다시 생성할 액터 클래스
	UPROPERTY()
	FSoftClassPath ActorClass;

	// 수레의 월드 트랜스폼
	// 복원 시 위치·회전만 사용하고 크기는 클래스 기본값을 사용
	UPROPERTY()
	FTransform Transform = FTransform::Identity;
};

// 퀵바 슬롯 한 칸의 레코드
// 도구는 클래스 경로, 상자·쓰레기·봉인석은 각 레코드로 저장
USTRUCT()
struct CONVENIENCESTORE_API FCSQuickbarSlotRecord
{
	GENERATED_BODY()

	// 보관 중인 도구 클래스
	// 도구가 아니면 비어 있음
	UPROPERTY()
	FSoftClassPath ToolClass;

	// 배송 상자 보관 여부
	UPROPERTY()
	bool bHasCrate = false;

	// 보관 중인 배송 상자 상태
	UPROPERTY()
	FCSCrateRecord Crate;

	// 쓰레기 보관 여부
	// 이전 세이브는 false로 읽혀 월드 쓰레기 레코드의 바닥 복원을 유지
	UPROPERTY()
	bool bHasTrash = false;

	// 보관 중인 쓰레기 상태
	// 표시 이름과 메시를 함께 기록해 퀵바 복원 후에도 유지
	UPROPERTY()
	FCSTrashRecord Trash;

	// [백무송]
	// 가챠 봉인석 보관 여부
	// 이전 세이브는 false로 읽혀 클래스만 기록된 봉인석과 구분
	UPROPERTY()
	bool bHasGachaSeal = false;

	// [백무송]
	// 보관 중인 가챠 봉인석의 보상 정보
	// 퀵바에 다시 보관하므로 위치와 바닥 물리 상태는 사용하지 않음
	UPROPERTY()
	FCSGachaSealRecord GachaSeal;
};

// 퀵바 전체 레코드
USTRUCT()
struct CONVENIENCESTORE_API FCSQuickbarRecord
{
	GENERATED_BODY()

	// 슬롯 순서대로 기록하며 빈 칸은 비어 있는 레코드로 유지
	// 퀵바의 쓰레기는 월드 쓰레기 레코드에서 제외하고 이 레코드에 기록
	UPROPERTY()
	TArray<FCSQuickbarSlotRecord> Slots;

	// 선택 중이던 슬롯 번호
	UPROPERTY()
	int32 SelectedSlotIndex = 0;
};

// 월드 액터 전체 레코드
USTRUCT()
struct CONVENIENCESTORE_API FCSWorldSaveRecord
{
	GENERATED_BODY()

	// 진열대 레코드 목록
	UPROPERTY()
	TArray<FCSShelfRecord> Shelves;

	// 진열대 밖에 놓인 낱개 상품 레코드 목록
	UPROPERTY()
	TArray<FCSLooseProductRecord> LooseProducts;

	// 월드와 창고에 놓인 배송 상자 레코드 목록
	UPROPERTY()
	TArray<FCSCrateRecord> Crates;

	// 진열대를 제외한 설치형 시설 레코드 목록
	UPROPERTY()
	TArray<FCSFacilityRecord> Facilities;

	// 월드에 놓인 쓰레기 레코드 목록
	UPROPERTY()
	TArray<FCSTrashRecord> Trash;

	// 월드에 놓인 미개봉 가챠 봉인석 레코드 목록
	// 퀵바에 보관하거나 손에 든 봉인석은 제외
	UPROPERTY()
	TArray<FCSGachaSealRecord> GachaSeals;

	// 월드에 놓인 아티팩트 픽업 레코드 목록
	UPROPERTY()
	TArray<FCSArtifactPickupRecord> ArtifactPickups;

	// 아티팩트로 소환한 수레 레코드 목록
	// 레벨 배치 수레는 제외
	UPROPERTY()
	TArray<FCSSummonedCartRecord> SummonedCarts;

	// 플레이어 퀵바 레코드
	UPROPERTY()
	FCSQuickbarRecord Quickbar;
};

// -----------------------------------------------------------------------------
// 엔딩
// -----------------------------------------------------------------------------

// 엔딩 진행 상태 레코드
USTRUCT()
struct CONVENIENCESTORE_API FCSEndingSaveRecord
{
	GENERATED_BODY()

	// 다음에 구매할 엔딩 아이템 순번
	UPROPERTY()
	int32 NextPurchaseIndex = 0;

	// 전시대에 제출한 엔딩 아이템 식별자 목록
	UPROPERTY()
	TArray<FName> SubmittedItemIds;

	// 엔딩 시작 여부
	// 자금 복원 후 엔딩 조건이 다시 충족되어 재발동하는 상황을 방지
	UPROPERTY()
	bool bEndingStarted = false;
};
