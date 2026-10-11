#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Data/CSSaveTypes.h"
// 제조 저장 블록의 구조체 정의를 조회
#include "Data/CSCraftTypes.h"
#include "CSSaveGame.generated.h"

// 슬롯 하나의 전체 세이브 데이터를 담는 컨테이너
// 각 참가자가 자기 블록만 기록하고 읽음
UCLASS()
class CONVENIENCESTORE_API UCSSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	// 저장 시점의 세이브 포맷 버전
	UPROPERTY()
	int32 SaveVersion = CS_SAVE_VERSION_CURRENT;

	// 슬롯 목록 표시용 요약 정보
	UPROPERTY()
	FCSSaveHeader Header;

	// GameState

	// 영업일·자금·레벨·평판 등 영업 진행 상태
	// ACSStoreGameState가 기록하고 가장 먼저 복원
	UPROPERTY()
	FCSStoreStateRecord StoreState;

	// 서브시스템

	// 잠자기 때 평판 계산에 쓰는 그날 손님 통계
	// UCSStoreReputationSubsystem이 기록하고 복원
	UPROPERTY()
	FCSReputationDailySaveRecord ReputationDaily;

	// 상품별 판매가 오버라이드
	// UCSStorePricingSubsystem이 기록하고 복원
	UPROPERTY()
	FCSPricingSaveRecord Pricing;

	// 직원 고용 계약 목록
	// UCSEmployeeHiringSubsystem이 기록하고 복원
	UPROPERTY()
	FCSEmployeeHiringSaveRecord EmployeeHiring;

	// 배송 대기 중인 상품·시설 발주와 배송 스폰 순번
	// UCSOrderSubsystem이 기록하고 복원
	UPROPERTY()
	FCSOrderSaveRecord Order;

	// 진행 중인 배달 주문과 주문 생성 일정
	// UCSDeliveryOrderSubsystem이 기록하고 복원
	UPROPERTY()
	FCSDeliveryOrderSaveRecord DeliveryOrder;

	// 수첩의 해금 정보·이벤트·배달 기록
	// 게임 인스턴스 서브시스템인 UCSJournalSubsystem이 기록하고 복원
	UPROPERTY()
	FCSJournalSaveRecord Journal;

	// 기존 세이브에는 필드가 없으므로 NAME_None으로 읽고 StoreBGM을 사용합니다.
	UPROPERTY()
	FName SelectedBGMId = NAME_None;
	UPROPERTY()
	FCSArtifactSaveRecord Artifacts;

	// 해금 안내의 확인 상태와 미확인 목록을 저장하며, 구버전에는 현재 레벨을 기준으로 적용합니다.
	UPROPERTY() int32 UnlockGuideObservedLevel = INDEX_NONE;
	UPROPERTY() TArray<FName> UnlockGuideSeen;
	UPROPERTY() TArray<FName> UnlockGuidePending;
	UPROPERTY() bool bUnlockGuideDeferred = false;
	// 대기 중인 레벨업은 순서대로 재생하며, 완료된 스토리는 중복 재생하지 않습니다.
	UPROPERTY() TArray<int32> PendingLevelStories;
	UPROPERTY() TArray<int32> CompletedLevelStories;

	// 확정 날씨와 기간제 날씨 확률
	// UCSWeatherSubsystem이 기록하고 복원
	UPROPERTY()
	FCSWeatherSaveRecord Weather;

	// 오늘의 뉴스와 기념일
	// UCSNoticeEventSubsystem이 기록하고 복원
	UPROPERTY()
	FCSNoticeEventSaveRecord NoticeEvent;

	// 제조 도감 기록·솥 재료·결과 슬롯 물건·게시판 소문 시드
	// UCSCraftingSubsystem이 기록하고 월드 액터 복원 후에 복원
	UPROPERTY()
	FCSCraftSaveData Craft;

	// 월드 액터

	// 진열대·상품·상자·시설·퀵바 상태
	// UCSWorldStateSaveHandler가 기록하고 서브시스템 복원 후에 복원
	UPROPERTY()
	FCSWorldSaveRecord World;

	// 엔딩 재발동 방지용 진행 상태
	// UCSWorldStateSaveHandler가 엔딩 액터에서 기록하고 복원
	UPROPERTY()
	FCSEndingSaveRecord Ending;

	// 현재 빌드가 읽을 수 있는 버전인지 확인
	bool IsVersionSupported() const
	{
		return SaveVersion == CS_SAVE_VERSION_CURRENT;
	}
};

// 슬롯별 헤더만 모아 두는 경량 메타 세이브
// 슬롯 목록 표시 시 전체 레코드 역직렬화를 방지
UCLASS()
class CONVENIENCESTORE_API UCSSaveMetaGame : public USaveGame
{
	GENERATED_BODY()

public:
	// 슬롯별 헤더 목록
	UPROPERTY()
	TArray<FCSSaveHeader> Headers;

	// 같은 슬롯 이름의 헤더를 교체하고 없으면 추가
	void UpsertHeader(const FCSSaveHeader& InHeader)
	{
		for (FCSSaveHeader& Existing : Headers)
		{
			if (Existing.SlotName == InHeader.SlotName)
			{
				Existing = InHeader;
				return;
			}
		}

		Headers.Add(InHeader);
	}

	// 슬롯 이름으로 헤더를 조회하고 없으면 nullptr을 반환
	const FCSSaveHeader* FindHeader(FName InSlotName) const
	{
		return Headers.FindByPredicate(
			[InSlotName](const FCSSaveHeader& Candidate)
			{
				return Candidate.SlotName == InSlotName;
			});
	}

	// 슬롯 이름이 같은 헤더를 제거
	void RemoveHeader(FName InSlotName)
	{
		Headers.RemoveAll(
			[InSlotName](const FCSSaveHeader& Candidate)
			{
				return Candidate.SlotName == InSlotName;
			});
	}
};
