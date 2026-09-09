#include "Systems/Weather/CSWeatherSubsystem.h"

#include "Data/CSWeatherSettingsDataAsset.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Features/Weather/CSWeatherEffectActor.h"
#include "Systems/StoreFlow/CSStoreGameMode.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "UObject/Class.h"

void UCSWeatherSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	ACSStoreGameMode* GameMode = InWorld.GetAuthGameMode<ACSStoreGameMode>();
	if (!IsValid(GameMode))
	{
		return;
	}

	Settings = GameMode->GetWeatherSettings();
	if (!IsValid(Settings) || !IsValid(Settings->WeatherTable))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Weather] WeatherSettingsDataAsset이 지정되지 않아 날씨를 항상 맑음으로 유지합니다."));
		return;
	}

	StoreGameState = InWorld.GetGameState<ACSStoreGameState>();
	if (!IsValid(StoreGameState))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Weather] ACSStoreGameState를 찾을 수 없어 날씨를 초기화하지 못했습니다."));
		return;
	}

	// DT_Weather 테이블을 캐싱해서 미리 읽어둡니다
	BuildWeatherRowCache();

	// 마감종 → 정산 확인으로 하루가 넘어가는 지점입니다.
	// 이 델리게이트 하나만 구독하면 되므로 StoreFlow 쪽 코드는 수정하지 않습니다.
	StoreGameState->OnBusinessDayStarted.AddDynamic(
		this, &UCSWeatherSubsystem::HandleBusinessDayStarted);

	// 1일차는 OnBusinessDayStarted가 방송되지 않으므로(++ 이후에만 방송) 여기서 직접 적용
	ApplyBusinessDay(StoreGameState->CurrentBusinessDay);
}

void UCSWeatherSubsystem::Deinitialize()
{
	if (IsValid(StoreGameState))
	{
		StoreGameState->OnBusinessDayStarted.RemoveDynamic(
			this, &UCSWeatherSubsystem::HandleBusinessDayStarted);
	}

	if (IsValid(ActiveEffectActor))
	{
		ActiveEffectActor->Destroy();
		ActiveEffectActor = nullptr;
	}

	Super::Deinitialize();
}

// ---------------------------------------------------------------------------
// 하루 전환
// ---------------------------------------------------------------------------

void UCSWeatherSubsystem::HandleBusinessDayStarted(int32 NewBusinessDay)
{
	ApplyBusinessDay(NewBusinessDay);
}

void UCSWeatherSubsystem::ApplyBusinessDay(int32 BusinessDay)
{
	CurrentBusinessDay = FMath::Max(1, BusinessDay);

	// 공개 범위까지 미리 확정합니다.
	// 표시할 때 굴리면 출력할 때마다 예보가 바뀝니다.
	EnsureDecidedThrough(CurrentBusinessDay + GetPublicForecastDays());

	const ECSWeatherType Previous = CurrentWeather;
	CurrentWeather = GetDecidedWeather(CurrentBusinessDay);

	// 오늘 날씨가 어제랑 다를 때만 파티클 이펙트를 갈아 끼웁니다.
	// 비가 이틀 연속 오면 안 끊기게 하려는 의도입니다
	if (CurrentWeather != Previous)
	{
		ApplyWeatherEffect(CurrentWeather);
	}

	// 이펙트는 같으면 재스폰하지 않지만 델리게이트는 항상 방송합니다.
	// 소비 측이 "새 날이 시작됐다 + 날씨는 이거다"를 한 번에 받아야 하기 때문입니다.
	OnWeatherChanged.Broadcast(CurrentWeather, Previous);
	OnWeatherForecastUpdated.Broadcast();

#if CS_WEATHER_DEBUG
	// UI가 없으므로 하루 시작 시 화면에 한 번 띄웁니다.
	DebugPrintForecast();
#endif
}

// ---------------------------------------------------------------------------
// 확정 배열
// ---------------------------------------------------------------------------

void UCSWeatherSubsystem::EnsureDecidedThrough(int32 BusinessDay)
{
	// BusinessDay가 0 이하이면 아래 while 조건이 처음부터 거짓이라 그대로 빠져나갑니다.
	const int32 TargetCount = BusinessDay;
	const bool bCanRoll = IsValid(Settings) && IsValid(Settings->WeatherTable);

	// 이미 채워진 구간은 다시 굴리지 않습니다. (append-only)
	while (DecidedWeather.Num() < TargetCount)
	{
		const int32 TargetDay = DecidedWeather.Num() + 1;
		DecidedWeather.Add(bCanRoll ? RollWeather(TargetDay) : ECSWeatherType::Clear);
	}
}

ECSWeatherType UCSWeatherSubsystem::GetDecidedWeather(int32 BusinessDay) const
{
	const int32 Index = BusinessDay - 1;
	return DecidedWeather.IsValidIndex(Index) ? DecidedWeather[Index] : ECSWeatherType::Clear;
}

// ---------------------------------------------------------------------------
// 확률 합성과 추첨
// ---------------------------------------------------------------------------

void UCSWeatherSubsystem::BuildDayWeights(int32 BusinessDay,TArray<ECSWeatherType>& OutTypes,TArray<float>& OutWeights) const
{
	OutTypes.Reset();
	OutWeights.Reset();

	if (!IsValid(Settings))
	{
		return;   // 호출한 쪽이 빈 결과를 맑음으로 처리합니다.
	}

	// 열거형 값 순서로 순회합니다. TMap의 내부 순서에 결과가 의존하면 결정론이 깨집니다.
	float Total = 0.0f;
	for (int32 TypeIndex = static_cast<int32>(ECSWeatherType::Clear);
		TypeIndex < static_cast<int32>(ECSWeatherType::Max);
		++TypeIndex)
	{
		const ECSWeatherType Type = static_cast<ECSWeatherType>(TypeIndex);

		// 확률표에서 값을 가져옴
		float Weight = Settings->BaseWeights.GetWeight(Type);
		if (Weight <= 0.0f)
		{
			continue;
		}

		// 다른 시스템(계절 등)이 개입한 배율이 있다면 찾아서 곱해줍니다
		for (const TPair<FName, FCSWeatherWeightModifier>& Pair : WeightModifiers)
		{
			if (!Pair.Value.IsActiveOn(BusinessDay))
			{
				continue;
			}
			if (const float* Mul = Pair.Value.Multipliers.Find(Type))
			{
				Weight *= FMath::Max(0.0f, *Mul);
			}
		}

		if (Weight <= 0.0f)
		{
			continue;   // 배율 0으로 제외된 종류입니다.
		}


		if (FindWeatherRow(Type) == nullptr)
		{
			continue;
		}

		OutTypes.Add(Type);
		OutWeights.Add(Weight);
		Total += Weight;
	}

	//  정규화
	if (Total > 0.0f)
	{
		for (float& Weight : OutWeights)
		{
			Weight /= Total;
		}
	}
	else
	{
		OutTypes.Reset();
		OutWeights.Reset();
	}
}

ECSWeatherType UCSWeatherSubsystem::RollWeather(int32 BusinessDay) const
{
	if (!IsValid(Settings) || BusinessDay <= Settings->FixedClearBusinessDays)
	{
		return ECSWeatherType::Clear;
	}

	TArray<ECSWeatherType> Types;
	TArray<float> Chances;
	BuildDayWeights(BusinessDay, Types, Chances);

	if (Types.Num() == 0)
	{
		return ECSWeatherType::Clear;   // 안전장치입니다. 데이터가 비어도 멈추지 않습니다.
	}

	// 한 번 굴린 날짜는 DecidedWeather에 그대로 남으므로 여기서는 매번 새로 굴립니다.
	float Pick = FMath::FRand();   // 합이 1.0이므로 0..1을 그대로 씁니다
	for (int32 Index = 0; Index < Types.Num(); ++Index)
	{
		Pick -= Chances[Index];
		if (Pick <= 0.0f)
		{
			return Types[Index];
		}
	}
	return Types.Last();   // 부동소수 오차용 마무리
}

// ---------------------------------------------------------------------------
// 확률 배율
// ---------------------------------------------------------------------------

void UCSWeatherSubsystem::AddWeightModifier(const FCSWeatherWeightModifier& Modifier)
{
	if (Modifier.SourceId.IsNone())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Weather] SourceId가 비어 있어 확률 배율을 등록하지 않았습니다. "
			     "해제할 수 없는 배율이 남으면 확률이 왜 그런지 추적할 수 없습니다."));
		return;
	}

	WeightModifiers.Add(Modifier.SourceId, Modifier);
	InvalidateUnpublishedTail();
}

void UCSWeatherSubsystem::RemoveWeightModifier(FName SourceId)
{
	if (WeightModifiers.Remove(SourceId) > 0)
	{
		InvalidateUnpublishedTail();
	}
}

void UCSWeatherSubsystem::InvalidateUnpublishedTail()
{
	// 공개 예보로 이미 보여준 범위는 남깁니다. 그 날짜를 다시 굴리면 예보가 틀려집니다.
	const int32 KeepCount = CurrentBusinessDay + GetPublicForecastDays();
	if (DecidedWeather.Num() <= KeepCount)
	{
		return;
	}

	DecidedWeather.SetNum(KeepCount);
	EnsureDecidedThrough(KeepCount);

	// 오늘 날씨는 바뀌지 않았으므로 OnWeatherChanged는 방송하지 않습니다.
	// 이펙트를 다시 스폰하면 내리던 비가 한 번 끊깁니다.
	OnWeatherForecastUpdated.Broadcast();
}

// ---------------------------------------------------------------------------
// 조회
// ---------------------------------------------------------------------------

int32 UCSWeatherSubsystem::GetPublicForecastDays() const
{
	if (IsValid(Settings))
	{
		// Settings 데이터 에셋이 유효하게 잘 지정되어 있다면, 데이터에 적힌 일수를 반환
		return Settings->PublicForecastDays;
	}
	else
	{
		// Settings가 비어있거나 찾을 수 없다면, 기본값인 1을 반환합니다.
		return 1;
	}
}

TArray<FCSWeatherForecastEntry> UCSWeatherSubsystem::GetForecastEntries()
{
	const int32 Days = GetPublicForecastDays();
	EnsureDecidedThrough(CurrentBusinessDay + Days);

	TArray<FCSWeatherForecastEntry> Entries;
	Entries.Reserve(Days + 1);
	for (int32 Offset = 0; Offset <= Days; ++Offset)
	{
		Entries.Add(MakeForecastEntry(Offset));
	}
	return Entries;
}

FCSWeatherForecastEntry UCSWeatherSubsystem::GetCurrentWeatherEntry()
{
	EnsureDecidedThrough(CurrentBusinessDay);
	return MakeForecastEntry(0);
}

FCSWeatherForecastEntry UCSWeatherSubsystem::MakeForecastEntry(int32 DayOffset) const
{
	FCSWeatherForecastEntry Entry;
	Entry.DayOffset    = DayOffset;
	Entry.BusinessDay  = CurrentBusinessDay + DayOffset;
	Entry.WeatherType  = GetDecidedWeather(Entry.BusinessDay);

	// 이름이 비면 표시 측에 빈칸이 그대로 나갑니다.
	// 대체 이름을 끼워 넣지 않는 대신 BuildWeatherRowCache가 시작 시점에 경고로 잡습니다.
	if (const FCSWeatherRow* Row = FindWeatherRow(Entry.WeatherType))
	{
		Entry.DisplayName = Row->DisplayName;
	}
	return Entry;
}

ECSWeatherType UCSWeatherSubsystem::GetForecastWeather(int32 DayOffset)
{
	// 공개 범위를 넘는 조회는 막습니다. 소비 측이 실수로 미래를 들여다보지 않게 합니다.
	if (DayOffset < 0 || DayOffset > GetPublicForecastDays())
	{
		return ECSWeatherType::None;
	}

	EnsureDecidedThrough(CurrentBusinessDay + DayOffset);
	return GetDecidedWeather(CurrentBusinessDay + DayOffset);
}

// ---------------------------------------------------------------------------
// 행 캐시
// ---------------------------------------------------------------------------

void UCSWeatherSubsystem::BuildWeatherRowCache()
{
	WeatherRowCache.Reset();

	if (!IsValid(Settings) || !IsValid(Settings->WeatherTable))
	{
		return;
	}

	const UEnum* EnumPtr = StaticEnum<ECSWeatherType>();//만약 여기에 오류떠도 문제없음, IDE 표시만 그럴 뿐 컴파일은 정상!
	const TArray<FName> RowNames = Settings->WeatherTable->GetRowNames();

	for (const FName& RowName : RowNames)
	{
		const FCSWeatherRow* Row =
			Settings->WeatherTable->FindRow<FCSWeatherRow>(RowName, TEXT("BuildCache"), false);
		if (Row == nullptr)
		{
			continue;
		}

		// 행 이름 규칙이 어긋난 데이터를 시작 시점에 잡습니다.
		// 동작은 WeatherType 필드를 신뢰하므로 멈추지는 않고 경고만 남깁니다.
		const int64 Value = EnumPtr != nullptr
			? EnumPtr->GetValueByNameString(RowName.ToString())
			: static_cast<int64>(INDEX_NONE);
		if (Value == INDEX_NONE || static_cast<ECSWeatherType>(Value) != Row->WeatherType)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Weather] DT_Weather의 행 이름 %s 이 WeatherType과 일치하지 않습니다."),
				*RowName.ToString());
		}

		// 이름이 비면 게시판과 HUD에 빈칸이 나갑니다.
		// 대체 이름으로 덮지 않고 데이터를 채우도록 여기서 알립니다.
		if (Row->DisplayName.IsEmpty())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Weather] DT_Weather의 행 %s 에 DisplayName이 비어 있습니다. "
				     "게시판에 빈칸으로 표시됩니다."),
				*RowName.ToString());
		}

		WeatherRowCache.Add(Row->WeatherType, *Row);
	}
}

const FCSWeatherRow* UCSWeatherSubsystem::FindWeatherRow(ECSWeatherType Type) const
{
	return WeatherRowCache.Find(Type);
}

// ---------------------------------------------------------------------------
// 이펙트 교체
// ---------------------------------------------------------------------------

void UCSWeatherSubsystem::ApplyWeatherEffect(ECSWeatherType NewWeather)
{
	if (IsValid(ActiveEffectActor))
	{
		ActiveEffectActor->StopEffect();   // 내부에서 지연 후 Destroy
		ActiveEffectActor = nullptr;
	}

	const FCSWeatherRow* Row = FindWeatherRow(NewWeather);
	if (Row == nullptr || Row->EffectActorClass.IsNull())
	{
		return;   // 맑음처럼 이펙트가 없는 날씨입니다. 정상 경로입니다.
	}

	UClass* EffectClass = Row->EffectActorClass.LoadSynchronous();
	if (EffectClass == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Weather] %s의 EffectActorClass를 로드하지 못했습니다."),
			*Row->DisplayName.ToString());
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr || !IsValid(Settings))
	{
		return;
	}

	const FTransform SpawnTransform(FRotator::ZeroRotator, Settings->EffectSpawnOffset);
	ActiveEffectActor = World->SpawnActor<ACSWeatherEffectActor>(EffectClass, SpawnTransform);
	if (IsValid(ActiveEffectActor))
	{
		ActiveEffectActor->StartEffect();
	}
}
