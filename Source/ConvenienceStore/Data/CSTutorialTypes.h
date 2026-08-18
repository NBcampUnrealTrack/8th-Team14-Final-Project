#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CSTutorialTypes.generated.h"

class UUserWidget;

// 지금이 하루 흐름 중 어디에 해당하는지
UENUM(BlueprintType)
enum class ECSTutorialPart : uint8
{
	PreOpen,     // 1부: 영업 전 (청소 ~ 입간판)
	DuringOpen,  // 2부: 영업 중 (손님 응대)
	PostClose    // 3부: 영업 후 (마감 업무)
};

// 이 스텝의 안내를 화면 하단 대화창으로 띄울지
// 기존 상호작용 프롬프트(조준점 아래)에 맡기고 대화창은 띄우지 않을지
UENUM(BlueprintType)
enum class ECSTutorialPromptType : uint8
{
	None,      // 대화창 없음 마커와 우측 상단 목표로만 안내
	Dialogue   // 화면 하단 대화창 사용
};

// 튜토리얼 스텝 이름표
// 진행 순서는 이 목록의 순서가 아니라 DT_TutorialStep의 행 순서로 정해짐
// 건축 관련 4~5단계도 실제 건축 성공 이벤트를 통해 진행됨
UENUM(BlueprintType)
enum class ECSTutorialStepId : uint8
{
	MoveToStore,          // 1: 입구로 이동
	CleanTrash,           // 2: 쓰레기 청소
	DeliveryArrived,      // 3: 배송 알림 (건축 진입 안내)
	StockShelves,         // 6: 진열
	ApproachCrystalBall,  // 7: 수정구슬 접근
	PlaceProductOrder,    // 8: 상품 발주
	SwitchToFacilityTab,  // 9: 시설 발주 탭 전환 안내
	FacilityOrderInfo,    // 10: 시설 발주 설명
	CloseOrderUI,         // 11: 발주창 닫기
	ApproachLedger,       // 12: 장부 접근
	SetProductPrice,      // 13: 가격 설정
	CloseLedgerUI,        // 14: 장부 닫기
	OpenSign,             // 15: 입간판으로 영업 시작

	// 기존 직렬화 값을 보존하기 위해 신규 ID는 끝에 추가하며, 실제 진행 순서는 DataTable 행을 따름
	EnterBuildMode,       // 4: 건축 모드 진입
	PlaceFacility         // 5: 시설 배치
};

// DT_TutorialStep의 한 행
// 스텝 하나가 화면에 어떻게 보이고 언제 다음 스텝으로 넘어가는지를 데이터로 정의
// 문구나 순서를 바꿀 때 코드 재컴파일 없이 이 테이블만 고치면 되도록
USTRUCT(BlueprintType)
struct FCSTutorialStepRow : public FTableRowBase
{
	GENERATED_BODY()

	// 이 행이 어느 스텝인지 구분하는 이름표
	// 서브시스템이 스텝별 완료 조건을 분기할 때 이 값으로 판단
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	ECSTutorialStepId StepId = ECSTutorialStepId::MoveToStore;

	// 1부/2부/3부 중 어디 소속인지
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	ECSTutorialPart Part = ECSTutorialPart::PreOpen;

	// 대화창을 쓸지 기존 조준 프롬프트에 맡길지
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	ECSTutorialPromptType PromptType = ECSTutorialPromptType::Dialogue;

	// 화면 중앙에 잠깐 떴다가 사라지는 배너 문구 비워두면 배너 자체를 생략
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	FText BannerText;

	// 화면 하단 대화창 문구 PromptType이 Dialogue일 때만 사용
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	FText PromptText;

	// 우측 상단 목표 텍스트
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	FText ObjectiveText;

	// 목표 수치 0이면 진행바(0/3 같은 표시) 자체를 숨김
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial", meta = (ClampMin = "0"))
	int32 ObjectiveGoal = 0;

	// 이 스텝에서 마커를 띄울 액터를 찾는 데 쓰는 태그
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	FName HighlightMarkerTag;

	// true면 완료 조건을 채워야만 다음으로 넘어감 (관문형).
	// false면 조건을 못 채워도 AutoAdvanceSeconds 후 자동으로 넘어감 (안내형).
	// 2부(영업 중) 스텝은 손님을 놓칠 수 있으므로 반드시 false 두기
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	bool bBlocking = true;

	// bBlocking이 false일 때 몇 초 후 자동으로 다음 스텝으로 넘어갈지
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial", meta = (ClampMin = "0.0"))
	float AutoAdvanceSeconds = 0.0f;

	// 이 스텝이 특정 UI 창이 열렸는지로 완료를 판단할 때 비교할 위젯 클래스
	// 예: 발주창이 열리면 7단계 완료, 장부창이 열리면 12단계 완료
	// 필요할 때만 실제로 로드되도록 소프트 참조로
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	TSoftClassPtr<UUserWidget> ExpectedWidgetClass;

	// ExpectedWidgetClass로 완료를 판단할 때 "열릴 때" 완료로 볼지 "닫힐 때" 완료로 볼지
	// false(기본값) = 위젯이 열릴 때 완료
	// true = 위젯이 닫힐 때 완료
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	bool bCompleteOnWidgetClosed = false;
};
