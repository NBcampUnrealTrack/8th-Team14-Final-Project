#pragma once

#include "CoreMinimal.h"
#include "Data/CSJournalTypes.h"
#include "CSJournalViewTypes.generated.h"

// 위젯 스위처의 인덱스와 동일한 순서로 수첩 구역을 구분합니다.
UENUM(BlueprintType)
enum class ECSJournalPage : uint8
{
	Map,
	Goblin,
	Orc,
	Human,
	Wizard,
	Dwarf,
	Elf,
	Calendar,
	Delivery,
	Artifacts
};

// 영업일을 한 달 28일, 한 해 12개월 기준의 게임 날짜로 표현합니다.
USTRUCT(BlueprintType)
struct FCSJournalDateView
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 Year = 1;
	UPROPERTY(BlueprintReadOnly) int32 Month = 1;
	UPROPERTY(BlueprintReadOnly) int32 Day = 1;
	UPROPERTY(BlueprintReadOnly) int32 MonthIndex = 0;
};

// UI 담당자가 날짜 칸의 강조, 비활성화와 읽지 않은 기록 표시를 구성하실 수 있습니다.
USTRUCT(BlueprintType)
struct FCSJournalCalendarDayView
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 BusinessDay = 1;
	UPROPERTY(BlueprintReadOnly) int32 DayOfMonth = 1;
	UPROPERTY(BlueprintReadOnly) int32 EventCount = 0;
	UPROPERTY(BlueprintReadOnly) bool bToday = false;
	UPROPERTY(BlueprintReadOnly) bool bSelected = false;
	UPROPERTY(BlueprintReadOnly) bool bAvailable = false;
	UPROPERTY(BlueprintReadOnly) bool bUnread = false;
};

// 잠긴 항목의 본문은 비워서 전달하며 UI에서는 해금 여부에 맞는 디자인을 적용합니다.
USTRUCT(BlueprintType)
struct FCSJournalRaceEntryView
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FName EntryId;
	UPROPERTY(BlueprintReadOnly) ECSJournalRace Race = ECSJournalRace::Goblin;
	UPROPERTY(BlueprintReadOnly) FText Title;
	UPROPERTY(BlueprintReadOnly) FText Body;
	UPROPERTY(BlueprintReadOnly) bool bUnlocked = false;
};

// 미획득 상태에도 실루엣용 아이콘을 전달하며 이름과 설명은 숨깁니다.
USTRUCT(BlueprintType)
struct FCSJournalArtifactView
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FName ArtifactId;
	UPROPERTY(BlueprintReadOnly) FText Name;
	UPROPERTY(BlueprintReadOnly) FText Description;
	UPROPERTY(BlueprintReadOnly) TObjectPtr<UTexture2D> Icon;
	UPROPERTY(BlueprintReadOnly) bool bCollected = false;
};

// 지도 표식 위치는 이미지의 왼쪽 위가 0,0이고 오른쪽 아래가 1,1인 정규화 좌표입니다.
USTRUCT(BlueprintType)
struct FCSJournalMapMarkerView
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FText BuildingName;
	UPROPERTY(BlueprintReadOnly) ECSJournalRace Race = ECSJournalRace::Human;
	UPROPERTY(BlueprintReadOnly) FVector2D Position = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly) bool bImplemented = true;
	UPROPERTY(BlueprintReadOnly) bool bInBounds = true;
};

// 지도 이미지가 없으면 UI에서 메시의 평면 바운드를 사각형으로 표시하실 수 있습니다.
USTRUCT(BlueprintType)
struct FCSJournalMapFootprintView
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FVector2D Position = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FVector2D Size = FVector2D::ZeroVector;
};

// 화면을 직접 그리지 않고 WBP에서 지도와 플레이어 화살표를 배치할 데이터만 제공합니다.
USTRUCT(BlueprintType)
struct FCSJournalMapView
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) TObjectPtr<UTexture2D> MapTexture;
	UPROPERTY(BlueprintReadOnly) FVector2D MapSize = FVector2D(20000, 20000);
	UPROPERTY(BlueprintReadOnly) TArray<FCSJournalMapMarkerView> Markers;
	UPROPERTY(BlueprintReadOnly) TArray<FCSJournalMapFootprintView> Footprints;
	UPROPERTY(BlueprintReadOnly) bool bHasPlayer = false;
	UPROPERTY(BlueprintReadOnly) bool bPlayerInBounds = false;
	UPROPERTY(BlueprintReadOnly) FVector2D PlayerPosition = FVector2D::ZeroVector;
	// 오른쪽을 바라보는 원본 화살표 이미지에 적용할 UMG 회전각을 제공합니다.
	UPROPERTY(BlueprintReadOnly) float PlayerRotationDegrees = 0;
};
