#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CSCraftRumorSubsystem.generated.h"

class ACSStoreGameState;
class UCSWeatherSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnCraftRumorsChanged);

// 게시판 소문 탭에 보여 줄 소문 3개를 고른다
// 하루 9개를 전용 시드로 섞어 두고 영업 시간을 셋으로 나눠 3개씩 꺼낸다. 손님 RNG 스트림은 쓰지 않는다
UCLASS()
class CONVENIENCESTORE_API UCSCraftRumorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 RumorsPerDay = 9;
	static constexpr int32 TimeSlotsPerDay = 3;

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	// 날씨 변형이 있으면 첫 자리에 둔다. 제조 서브시스템이 준비 전이면 빈 배열
	TArray<FText> GetCurrentRumors();

	// cs.Seed가 0 이상이면 그 값, 아니면 처음 부를 때 한 번 정한다
	int32 GetRumorBaseSeed();
	// 불러온 시드로 바꾼다. cs.Seed가 고정이면 테스트 재현이 우선이라 무시한다
	void SetRumorBaseSeed(int32 NewSeed);

	UPROPERTY(BlueprintAssignable, Category = "Crafting|Rumor")
	FCSOnCraftRumorsChanged OnCraftRumorsChanged;

private:
	UFUNCTION()
	void HandleGameTimeChanged(float NewGameMinutes);

	UFUNCTION()
	void HandleBusinessDayChanged(int32 NewBusinessDay);

	UFUNCTION()
	void HandleWeatherForecastUpdated();

	int32 GetBusinessDay() const;
	int32 GetTimeSlot() const;
	// 내일 날씨로 수요가 가장 많이 오르는 기존 제조 상품의 소문. 없으면 None
	FName FindWeatherVariant(FText& OutText);
	const TArray<FText>& GetDailyRumors(int32 BusinessDay);
	// (영업일, 시간대, 날씨 변형)이 바뀌었을 때만 방송한다
	void CheckRumorStateChanged();

	TWeakObjectPtr<ACSStoreGameState> GameState;
	TWeakObjectPtr<UCSWeatherSubsystem> Weather;

	bool bHasBaseSeed = false;
	int32 BaseSeed = 0;

	int32 CachedDay = INDEX_NONE;
	TArray<FText> CachedDailyRumors;

	bool bHasLastState = false;
	int32 LastDay = INDEX_NONE;
	int32 LastTimeSlot = INDEX_NONE;
	FName LastVariant;
	bool bWarnedSmallPool = false;
};
