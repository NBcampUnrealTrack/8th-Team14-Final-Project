#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CSTutorialTypes.generated.h"

class UUserWidget;
class UCSTutorialChoiceWidget;

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
	PlaceFacility,        // 5: 시설 배치

	// 3부(마감 후) 신설. 침대와 상호작용해 잠자기 확인 UI(WBP_ClosingBellConfirm)를 열면 완료 처리됨
	// 이름은 ApproachClosingBell이지만 실제 액터는 침대(클래스명 ACSStoreClosingBellActor는 유지)
	ApproachClosingBell,

	// 2부(영업 중) 원포인트 팁 4종. StepRows(1부/3부 관문형 진행)에는 안 들어가고
	// TipRows에 따로 담겨 각자의 트리거로만 1회 표시됨
	TipDiary,
	TipNoticeBoard,
	TipInnerThought,
	TipSettlement,

	// 3부 제조 튜토리얼. 판매가 설정과 잠자기 사이에 들어간다
	CraftMoveToHouse,
	CraftChoice,
	CraftAddIngredients,
	CraftBrew,
	CraftRecipeBook,
	CraftWrapUp,

	// 진열 뒤 상자 하나를 더 받아 창고에 넣는다. 창고 쓰는 법을 처음 알려 주는 스텝
	StoreCrate,
	// 2부 팁. 튜토리얼 날 진열대가 비어 갈 때 창고와 즉시 배송을 알려 준다
	TipRestock
};

// 대화창 힌트 줄 하나 (키 라벨 + 설명). 이동/건축 회전처럼 키가 여러 개라
// PromptHintKey/PromptHintText 한 줄로 못 담는 스텝에서 PromptHintList로 여러 줄 표시할 때 씀
USTRUCT(BlueprintType)
struct FCSTutorialHintEntry
{
	GENERATED_BODY()

	// 표시할 키 라벨
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	FText HintKey;

	// 그 키에 대한 설명
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	FText HintText;
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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial", meta = (MultiLine = "true"))
	FText BannerText;

	// 화면 하단 대화창 문구 PromptType이 Dialogue일 때만 사용
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial", meta = (MultiLine = "true"))
	FText PromptText;

	// 대화창 하단 보조 힌트 줄
	// 비워두면 힌트 줄 자체를 표시하지 않음
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial", meta = (MultiLine = "true"))
	FText PromptHintText;

	// 힌트 줄 앞에 아이콘으로 띄울 키 라벨
	// 비워두면 키 아이콘 없이 PromptHintText만 표시
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	FText PromptHintKey;

	// 키가 여러 개라 한 줄 힌트로 못 담는 스텝(이동 WASD/달리기, 건축 회전 등)에서 쓰는 여러 줄 힌트
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	TArray<FCSTutorialHintEntry> PromptHintList;

	// 우측 상단 목표 텍스트
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	FText ObjectiveText;

	// 목표 수치 0이면 진행바(0/3 같은 표시) 자체를 숨김
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial", meta = (ClampMin = "0"))
	int32 ObjectiveGoal = 0;

	// 이 스텝에서 마커를 띄울 액터를 찾는 데 쓰는 태그
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	FName HighlightMarkerTag;

	// 3D 마커 위젯(WBP_TutorialMarker)에 표시할 이름표
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	FText MarkerLabelText;

	// true면 완료 조건을 채워야만 다음으로 넘어감 (관문형).
	// false면 조건을 못 채워도 AutoAdvanceSeconds 후 자동으로 넘어감 (안내형).
	// 2부(영업 중) 스텝은 손님을 놓칠 수 있으므로 반드시 false 두기
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	bool bBlocking = true;

	// bBlocking이 false일 때 몇 초 후 자동으로 다음 스텝으로 넘어갈지
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial", meta = (ClampMin = "0.0"))
	float AutoAdvanceSeconds = 0.0f;

	// 이 스텝이 특정 UI 창이 열렸는지로 완료를 판단할 때 비교할 위젯 클래스
	// 필요할 때만 실제로 로드되도록 소프트 참조로
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	TSoftClassPtr<UUserWidget> ExpectedWidgetClass;

	// ExpectedWidgetClass로 완료를 판단할 때 "열릴 때" 완료로 볼지 "닫힐 때" 완료로 볼지
	// false(기본값) = 위젯이 열릴 때 완료
	// true = 위젯이 닫힐 때 완료
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	bool bCompleteOnWidgetClosed = false;

	// 채워져 있으면 이 스텝은 예/아니요 선택 창을 띄운다. 질문은 PromptText를 쓴다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial|Choice")
	TSoftClassPtr<UCSTutorialChoiceWidget> ChoiceWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial|Choice")
	FText ChoiceYesText;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial|Choice")
	FText ChoiceNoText;

	// 3부에서 아직 잠자기 스텝이 아닐 때 침대가 띄우는 문구. 비우면 코드 기본 문구
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial", meta = (MultiLine = "true"))
	FText SleepBlockedText;
};
