#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Data/CSJournalTypes.h"
#include "CSJournalSettings.generated.h"

// 프로젝트 전체에서 하나만 사용하는 도감 카탈로그를 지정합니다.
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "도감 기록지"))
class CONVENIENCESTORE_API UCSJournalSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 프로젝트 설정의 도감 기록지 항목에서 공용 카탈로그를 한 번만 지정해 주세요.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Catalog") TSoftObjectPtr<UCSJournalCatalog> Catalog;
};
