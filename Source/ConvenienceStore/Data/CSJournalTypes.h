#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/CSDeliveryOrderTypes.h"
#include "CSJournalTypes.generated.h"

class UCSCustomerArchetypeDataAsset;

// 수첩의 종족 페이지를 구분하며 고블린부터 엘프까지의 페이지 순서에 대응합니다.
UENUM(BlueprintType)
enum class ECSJournalRace : uint8
{
	Goblin,
	Orc,
	Human,
	Wizard,
	Dwarf,
	Elf
};

// 건물 상호작용으로 해금되는 종족 정보의 제목과 본문을 정의합니다.
USTRUCT(BlueprintType)
struct FCSJournalEntry
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName EntryId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) ECSJournalRace Race = ECSJournalRace::Goblin;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Title;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (MultiLine = true)) FText Body;
};

// 아티팩트 도감의 아이콘과 설명을 정의합니다. 배열 순서대로 도감 칸을 구성합니다.
USTRUCT(BlueprintType)
struct FCSJournalArtifact
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName ArtifactId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (MultiLine = true)) FText Description;
	// 미획득 실루엣을 정확하게 표시하려면 배경이 투명한 아이콘을 지정해 주셔야 합니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UTexture2D> Icon;
};

// 종족 정보와 아티팩트 목록을 에디터에서 지정할 수 있는 수첩 카탈로그입니다.
UCLASS(BlueprintType)
class CONVENIENCESTORE_API UCSJournalCatalog : public UDataAsset
{
	GENERATED_BODY()
  public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FCSJournalEntry> RaceEntries;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FCSJournalArtifact> Artifacts;
	// RaceEntries가 비어 있을 때 기본 설명을 만들기 위한 실제 에셋 참조입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fallback")
	TMap<ECSJournalRace, TObjectPtr<UCSCustomerArchetypeDataAsset>> FallbackArchetypes;
};

// 발생한 날짜의 제목과 본문을 보관하므로 날짜가 지나도 당시 기록을 확인하실 수 있습니다.
USTRUCT(BlueprintType)
struct FCSJournalEvent
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FName EventId;
	UPROPERTY(BlueprintReadOnly) int32 BusinessDay = 1;
	UPROPERTY(BlueprintReadOnly) FText Title;
	UPROPERTY(BlueprintReadOnly) FText Body;
	UPROPERTY(BlueprintReadOnly) bool bRead = false;
};

// 수락한 주문의 상태와 수락일·종료일을 보관합니다. 고유 기록 ID로 과거 기록을 구분합니다.
USTRUCT(BlueprintType)
struct FCSJournalDelivery
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FGuid RecordId;
	UPROPERTY(BlueprintReadOnly) FName LevelId;
	UPROPERTY(BlueprintReadOnly) int32 AcceptedDay = 1;
	UPROPERTY(BlueprintReadOnly) int32 FinishedDay = 0;
	UPROPERTY(BlueprintReadOnly) FCSDeliveryOrderRuntime Order;
};
