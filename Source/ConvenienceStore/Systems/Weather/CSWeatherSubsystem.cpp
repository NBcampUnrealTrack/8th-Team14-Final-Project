#include "Systems/Weather/CSWeatherSubsystem.h"

#include "Data/CSProductTypes.h"
#include "Data/CSWeatherSettingsDataAsset.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Features/Weather/CSWeatherEffectActor.h"
#include "Features/Weather/CSWeatherIndoorArea.h"
#include "Features/Weather/CSWeatherZoneActor.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraSystem.h"
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

	// 수요 규칙도 날씨별로 나눠 미리 담아둡니다
	BuildWeatherDemandRuleCache();

	// 확률표는 사본으로 들고 갑니다. 원본 데이터 에셋을 런타임에 고치면
	// 로드된 인스턴스가 변조되어 PIE를 껐다 켜기 전까지 값이 남습니다.
	RuntimeWeights = Settings->BaseWeights;

	// 마감종 → 정산 확인으로 하루가 넘어가는 지점입니다.
	// 이 델리게이트 하나만 구독하면 되므로 StoreFlow 쪽 코드는 수정하지 않습니다.
	StoreGameState->OnBusinessDayStarted.AddDynamic(
		this, &UCSWeatherSubsystem::HandleBusinessDayStarted);

	// 확률표·규칙표가 모두 갖춰진 뒤에 찍어야 실제로 쓰일 값이 나옵니다
	LogWeatherStartupSummary();

	// 1일차는 OnBusinessDayStarted가 방송되지 않으므로(++ 이후에만 방송) 여기서 직접 적용
	ApplyBusinessDay(StoreGameState->CurrentBusinessDay);
}

void UCSWeatherSubsystem::Deinitialize()
{
	Zones.Reset();
	IndoorAreas.Reset();
	WeatherDemandRuleCache.Reset();
	ActiveZoneSystem = nullptr;

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

	// 오늘 날씨가 정해지는 지점입니다. 판매 로그를 볼 때 "그날이 무슨 날씨였는지"를
	// 로그 파일 안에서 바로 맞춰볼 수 있어야 해서 남깁니다.
	UE_LOG(LogTemp, Log,
		TEXT("[날씨수요] 오늘 날씨=%s 적용 규칙 %d건 (%d일차)"),
		*StaticEnum<ECSWeatherType>()->GetNameStringByValue(static_cast<int64>(CurrentWeather)),
		GetActiveDemandRuleCount(), CurrentBusinessDay);

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
		if (bCanRoll)
		{
			DecidedWeather.Add(RollWeather(TargetDay));
		}
		else
		{
			DecidedWeather.Add(ECSWeatherType::Clear);
		}
	}
}

ECSWeatherType UCSWeatherSubsystem::GetDecidedWeather(int32 BusinessDay) const
{
	const int32 Index = BusinessDay - 1;
	if (DecidedWeather.IsValidIndex(Index))
	{
		return DecidedWeather[Index];
	}

	return ECSWeatherType::Clear;
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

	// 그날의 유효 확률표를 먼저 만듭니다. 영구층 + 기간제 레이어가 여기서 합쳐집니다.
	const FCSWeatherWeightSet Effective = BuildEffectiveWeights(BusinessDay);

	// 열거형 값 순서로 순회합니다. TMap의 내부 순서에 결과가 의존하면 결정론이 깨집니다.
	float Total = 0.0f;
	for (int32 TypeIndex = static_cast<int32>(ECSWeatherType::Clear);
		TypeIndex < static_cast<int32>(ECSWeatherType::Max);
		++TypeIndex)
	{
		const ECSWeatherType Type = static_cast<ECSWeatherType>(TypeIndex);

		const float Weight = Effective.GetWeight(Type);
		if (Weight <= 0.0f)
		{
			continue;
		}

		// 행이 없으면 표시 이름도 이펙트도 찾지 못하므로 후보에서 뺍니다.
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
// 유효 확률표 (영구층 + 기간제 레이어)
// ---------------------------------------------------------------------------

FCSWeatherWeightSet UCSWeatherSubsystem::BuildEffectiveWeights(int32 BusinessDay) const
{
	// 기간제 계산이 영구층에 남지 않도록 사본을 생성
	FCSWeatherWeightSet Effective = RuntimeWeights;

	// 지정한 일차에 활성인 레이어만 수집
	TArray<FCSWeatherChanceLayer> Actives;
	for (const FCSWeatherChanceLayer& Layer : ChanceLayers)
	{
		if (Layer.IsActiveOn(BusinessDay))
		{
			Actives.Add(Layer);
		}
	}
	// 활성 레이어가 없으면 영구층 사본을 그대로 반환
	if (Actives.Num() == 0)
	{
		return Effective;
	}

	// 적용 순서에 따라 결과가 달라질 수 있으므로 SourceId 사전순으로 정렬
	Actives.Sort([](const FCSWeatherChanceLayer& A, const FCSWeatherChanceLayer& B)
	{
		return A.SourceId.LexicalLess(B.SourceId);
	});

	for (const FCSWeatherChanceLayer& Layer : Actives)
	{
		// 앞선 레이어까지 반영된 표에 현재 레이어의 연산을 적용
		ApplyOpTo(Effective, Layer.WeatherType, Layer.Op, Layer.Value, Layer.Mode);
	}

	return Effective;
}

void UCSWeatherSubsystem::AddChanceLayer(const FCSWeatherChanceLayer& Layer)
{
	// 교체와 제거에 사용할 식별자가 없으면 등록을 거부
	if (Layer.SourceId.IsNone())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Weather] AddChanceLayer: SourceId가 비어 있어 등록하지 않았습니다. "
			     "해제할 수 없는 지정이 남으면 확률이 왜 그런지 추적할 수 없습니다."));
		return;
	}

	// 날씨 데이터 행이 없으면 실제 추첨 후보에 포함되지 않으므로 등록을 거부
	if (FindWeatherRow(Layer.WeatherType) == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Weather] AddChanceLayer: DT_Weather에 행이 없는 날씨입니다. "
			     "행을 먼저 추가해야 확률이 의미를 갖습니다."));
		return;
	}

	// Set은 목표 확률이므로 1을 넘는 값을 거부
	// 음수 값은 실제 연산을 적용할 때 0으로 보정
	if (Layer.Op == ECSChanceOpType::Set && Layer.Value > 1.0f)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Weather] AddChanceLayer: Set의 Value는 0~1이어야 합니다. (%.2f)"),
			Layer.Value);
		return;
	}

	// 같은 SourceId의 기존 레이어를 제거한 뒤 새 설정을 등록
	RemoveChanceLayer(Layer.SourceId);
	ChanceLayers.Add(Layer);
}

// SourceId가 일치하는 기간제 레이어를 모두 제거
void UCSWeatherSubsystem::RemoveChanceLayer(FName SourceId)
{
	// 전달받은 SourceId와 일치하는 항목만 제거
	ChanceLayers.RemoveAll([SourceId](const FCSWeatherChanceLayer& Layer)
	{
		return Layer.SourceId == SourceId;
	});
}

float UCSWeatherSubsystem::GetEffectiveChance(ECSWeatherType Type, int32 BusinessDay) const
{
	return GetChanceIn(BuildEffectiveWeights(BusinessDay), Type);
}

// ---------------------------------------------------------------------------
// 확률 조정 - 영구
// ---------------------------------------------------------------------------

float UCSWeatherSubsystem::GetWeatherChance(ECSWeatherType Type) const
{
	return GetChanceIn(RuntimeWeights, Type);
}

void UCSWeatherSubsystem::ApplyWeatherChance(ECSWeatherType Type, ECSChanceOpType Op, float Value,ECSChanceRebalanceMode Mode)
{
	// 실제 날씨로 사용할 수 없는 열거형 값은 제외
	if (Type == ECSWeatherType::None || Type == ECSWeatherType::Max)
	{
		return;
	}

	// 날씨 데이터 행이 없으면 실제 추첨 후보에 포함되지 않으므로 변경을 거부
	if (FindWeatherRow(Type) == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Weather] ApplyWeatherChance: DT_Weather에 행이 없는 날씨입니다. "
			     "행을 먼저 추가해야 확률이 의미를 갖습니다."));
		return;
	}

	// Set은 목표 확률이므로 1을 넘는 값을 거부
	// Multiply와 Weight는 배수이므로 1보다 큰 값도 허용
	if (Op == ECSChanceOpType::Set && Value > 1.0f)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Weather] ApplyWeatherChance: Set의 Value는 0~1이어야 합니다. (%.2f)"), Value);
		return;
	}

	// 확률이 0인 날씨는 곱셈으로 올릴 수 없으므로 Set을 사용하도록 변경을 거부
	if (Op != ECSChanceOpType::Set && GetWeatherChance(Type) <= 0.0f)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Weather] ApplyWeatherChance: 확률이 0이라 곱셈으로 올릴 수 없습니다. "
			     "Set으로 값을 직접 지정하세요."));
		return;
	}

	// 영구층 원본에 연산을 적용
	ApplyOpTo(RuntimeWeights, Type, Op, Value, Mode);
}

void UCSWeatherSubsystem::SetRuntimeWeights(const FCSWeatherWeightSet& NewWeights)
{
	RuntimeWeights = NewWeights;
	NormalizeSet(RuntimeWeights);
}

// ---------------------------------------------------------------------------
// 확률표 연산 (영구층과 임시 계산본 양쪽에서 쓰는 순수 함수들)
// ---------------------------------------------------------------------------

void UCSWeatherSubsystem::ApplyOpTo(FCSWeatherWeightSet& Weights, ECSWeatherType Type,ECSChanceOpType Op, float Value, ECSChanceRebalanceMode Mode)
{
	switch (Op)
	{
	case ECSChanceOpType::Set:
		// 목표 확률을 직접 지정하며, 확률이 0이던 날씨도 조정 가능
		ApplyChanceTo(Weights, Type, Value, Mode);
		break;

	case ECSChanceOpType::Multiply:
		// 현재 확률에 배수를 곱한 값을 목표 확률로 전달
		// 목표 확률은 ApplyChanceTo에서 0~1로 제한
		ApplyChanceTo(Weights, Type, GetChanceIn(Weights, Type) * Value, Mode);
		break;

	case ECSChanceOpType::Weight:
		// 대상 가중치에 배수를 곱한 뒤 전체 합이 1이 되도록 정규화
		// 대상 값도 함께 나누므로 최종 확률이 정확히 Value배가 되지는 않음
		// 음수 배수는 0으로 보정하며, Mode는 사용하지 않음
		Weights.Weights.Add(Type, Weights.GetWeight(Type) * FMath::Max(0.0f, Value));
		NormalizeSet(Weights);
		break;

	default:
		break;
	}
}

float UCSWeatherSubsystem::GetChanceIn(const FCSWeatherWeightSet& Weights, ECSWeatherType Type)
{
	// 음수 가중치는 0으로 취급해 전체 합을 계산
	float Total = 0.0f;
	for (const TPair<ECSWeatherType, float>& Pair : Weights.Weights)
	{
		Total += FMath::Max(0.0f, Pair.Value);
	}
	// 전체 합이 있을 때만 대상 가중치를 확률로 환산
	if (Total > 0.0f)
	{
		return Weights.GetWeight(Type) / Total;
	}

	return 0.0f;
}

void UCSWeatherSubsystem::ApplyChanceTo(FCSWeatherWeightSet& Weights, ECSWeatherType Type,float NewChance, ECSChanceRebalanceMode Mode)
{
	// 목표 확률을 0~1 범위로 제한
	NewChance = FMath::Clamp(NewChance, 0.0f, 1.0f);

	// 목표 확률이 100%이면 다른 날씨가 제외됨을 경고
	if (NewChance >= 1.0f)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Weather] 확률이 100%%에 도달해 다른 날씨가 모두 제외됩니다."));
	}

	// 저장된 가중치를 확률로 비교할 수 있도록 정규화
	NormalizeSet(Weights);

	// 기존 확률과 목표 확률의 차이를 계산
	const float OldChance = Weights.GetWeight(Type);
	const float Delta = NewChance - OldChance;

	// 차이가 거의 없으면 추가 조정 없이 종료
	if (FMath::IsNearlyZero(Delta))
	{
		return;
	}

	// 대상 날씨에 목표 확률을 먼저 설정
	Weights.Weights.Add(Type, NewChance);

	if (Mode == ECSChanceRebalanceMode::Proportional)
	{
		// 다른 날씨 사이의 기존 비율을 유지하며 재분배
		RebalanceProportionalIn(Weights, Type, NewChance);
	}
	else
	{
		// 변경된 양을 다른 날씨에 균등하게 분배하거나 회수
		RebalanceEvenIn(Weights, Type, Delta);
	}
}

void UCSWeatherSubsystem::NormalizeSet(FCSWeatherWeightSet& Weights)
{
	float Total = 0.0f;
	for (TPair<ECSWeatherType, float>& Pair : Weights.Weights)
	{
		// 음수를 제거한 값으로 전체 합을 계산
		Pair.Value = FMath::Max(0.0f, Pair.Value);
		Total += Pair.Value;
	}

	// 정규화할 가중치가 없으면 종료
	if (Total <= 0.0f)
	{
		return;
	}

	// 각 가중치를 전체 합으로 나누어 확률로 변환
	for (TPair<ECSWeatherType, float>& Pair : Weights.Weights)
	{
		Pair.Value /= Total;
	}
}

void UCSWeatherSubsystem::RebalanceProportionalIn(FCSWeatherWeightSet& Weights,ECSWeatherType Type, float NewChance)
{
	// 대상 날씨를 제외한 가중치 합을 계산
	float OthersTotal = 0.0f;
	for (const TPair<ECSWeatherType, float>& Pair : Weights.Weights)
	{
		if (Pair.Key != Type)
		{
			OthersTotal += Pair.Value;
		}
	}

	// 나머지 합이 거의 0이면 비례 분배가 어려우므로 대상 값을 1로 설정
	if (OthersTotal <= KINDA_SMALL_NUMBER)
	{
		Weights.Weights.Add(Type, 1.0f);
		return;
	}

	// 나머지 합이 1 - NewChance가 되도록 공통 배율을 계산
	const float Scale = (1.0f - NewChance) / OthersTotal;
	for (TPair<ECSWeatherType, float>& Pair : Weights.Weights)
	{
		if (Pair.Key != Type)
		{
			Pair.Value *= Scale;
		}
	}
}

void UCSWeatherSubsystem::RebalanceEvenIn(FCSWeatherWeightSet& Weights,ECSWeatherType Type, float Delta)
{
	// 대상 확률을 낮췄다면 남는 몫을 다른 날씨에 분배
	if (Delta < 0.0f)
	{
		// 표에 있는 다른 날씨의 수를 계산하며, 가중치가 0인 항목도 포함
		int32 OtherCount = 0;
		for (const TPair<ECSWeatherType, float>& Pair : Weights.Weights)
		{
			if (Pair.Key != Type)
			{
				++OtherCount;
			}
		}
		// 분배할 다른 날씨가 없으면 종료
		if (OtherCount == 0)
		{
			return;
		}

		// 남는 몫을 다른 날씨에 같은 양으로 분배
		const float Share = -Delta / OtherCount;
		for (TPair<ECSWeatherType, float>& Pair : Weights.Weights)
		{
			if (Pair.Key != Type)
			{
				Pair.Value += Share;
			}
		}
		return;
	}
	// 변경량이 없으면 종료
	if (Delta == 0.0f)
	{
		return;
	}

	// 대상 날씨를 제외한 회수 후보를 수집
	TArray<TPair<ECSWeatherType, float>> Others;
	for (const TPair<ECSWeatherType, float>& Pair : Weights.Weights)
	{
		if (Pair.Key != Type)
		{
			Others.Add(Pair);
		}
	}

	// 값이 작은 날씨부터 처리하도록 정렬
	Others.Sort([](
		const TPair<ECSWeatherType, float>& A,
		const TPair<ECSWeatherType, float>& B)
	{
		return A.Value < B.Value;
	});

	// 대상 확률이 증가한 만큼 다른 날씨에서 회수
	float Remaining = Delta;

	// 각 날씨를 한 번씩 처리하며 필요한 확률을 회수
	for (int32 Index = 0; Index < Others.Num(); ++Index)
	{
		// 남은 회수량을 아직 처리하지 않은 날씨 수로 나눔
		const int32 RemainingCount = Others.Num() - Index;
		const float Share = Remaining / RemainingCount;

		// 현재 날씨가 가진 값까지만 회수해 음수를 방지
		const float Take = FMath::Min(Share, Others[Index].Value);
		Weights.Weights.Add(
			Others[Index].Key,
			Others[Index].Value - Take);

		// 부족한 몫은 뒤에 남은 날씨들이 나눠 부담
		Remaining = FMath::Max(0.0f, Remaining - Take);
	}
}

// ---------------------------------------------------------------------------
// 이벤트 개입 (예보 확정 변경)
// ---------------------------------------------------------------------------

bool UCSWeatherSubsystem::SetForecastWeather(int32 DayOffset, ECSWeatherType NewWeather)
{
	// 과거 날짜와 공개 예보 범위 밖의 날짜는 변경을 거부
	if (DayOffset < 0 || DayOffset > GetPublicForecastDays())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Weather] SetForecastWeather: DayOffset %d 은 공개 예보 범위(0~%d) 밖입니다."),
			DayOffset, GetPublicForecastDays());
		return false;
	}

	// 실제 날씨로 사용할 수 없는 열거형 값은 거부
	if (NewWeather == ECSWeatherType::None || NewWeather == ECSWeatherType::Max)
	{
		return false;
	}

	// 표시 이름과 이펙트 등을 조회할 날씨 데이터 행이 없으면 거부
	if (FindWeatherRow(NewWeather) == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Weather] SetForecastWeather: DT_Weather에 행이 없는 날씨라 변경하지 않았습니다."));
		return false;
	}

	// 오늘 기준 오프셋을 실제 영업 일차로 변환
	const int32 TargetDay = CurrentBusinessDay + DayOffset;

	// 초반 고정 맑음 구간은 이벤트로도 변경하지 못하도록 제한
	if (IsValid(Settings) && TargetDay <= Settings->FixedClearBusinessDays)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Weather] SetForecastWeather: %d일차는 고정 맑음 구간이라 변경하지 않았습니다."),
			TargetDay);
		return false;
	}

	// 대상 날짜까지 확정 배열을 채운 뒤 접근 가능한 인덱스인지 확인
	EnsureDecidedThrough(TargetDay);

	const int32 Index = TargetDay - 1;
	if (!DecidedWeather.IsValidIndex(Index))
	{
		return false;
	}

	// 이미 같은 날씨면 이벤트를 전달하지 않고 성공 처리
	if (DecidedWeather[Index] == NewWeather)
	{
		return true;
	}

	// 저장된 확정 날씨를 요청한 날씨로 교체
	DecidedWeather[Index] = NewWeather;

	// 오늘을 변경한 경우 현재 날씨와 이펙트도 갱신
	// 미래 예보만 변경한 경우 현재 날씨는 유지
	if (DayOffset == 0)
	{
		const ECSWeatherType Previous = CurrentWeather;
		CurrentWeather = NewWeather;
		ApplyWeatherEffect(CurrentWeather);
		OnWeatherChanged.Broadcast(CurrentWeather, Previous);
	}

	// 변경된 예보를 표시하도록 구독 중인 대상에 갱신 이벤트를 전달
	OnWeatherForecastUpdated.Broadcast();
	return true;
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
		int64 Value = static_cast<int64>(INDEX_NONE);

		if (EnumPtr != nullptr)
		{
			Value = EnumPtr->GetValueByNameString(RowName.ToString());
		}

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
// 날씨별 상품 수요
// ---------------------------------------------------------------------------

void UCSWeatherSubsystem::BuildWeatherDemandRuleCache()
{
	WeatherDemandRuleCache.Reset();

	if (!IsValid(Settings) || !IsValid(Settings->WeatherDemandRuleTable))
	{
		// 경고는 LogWeatherStartupSummary가 한 번에 냅니다. 여기서 또 내면 같은 말이 두 줄 찍힙니다.
		return;
	}

	TArray<FCSWeatherDemandRuleRow*> Rules;
	Settings->WeatherDemandRuleTable->GetAllRows(
		TEXT("UCSWeatherSubsystem::BuildWeatherDemandRuleCache"), Rules);

	for (const FCSWeatherDemandRuleRow* Rule : Rules)
	{
		if (Rule == nullptr)
		{
			continue;
		}

		// bEnabled는 여기서 거르지 않고 조회 시점에 봅니다.
		// 미리 걸러내면 "표에는 행이 있는데 시작 로그의 행 수에는 안 잡힌다"가 되어
		// 규칙을 꺼둔 것과 행을 빠뜨린 것을 로그만 보고는 구분할 수 없습니다.
		WeatherDemandRuleCache.FindOrAdd(Rule->WeatherType).Add(*Rule);
	}
}

int32 UCSWeatherSubsystem::GetActiveDemandRuleCount() const
{
	const TArray<FCSWeatherDemandRuleRow>* Rules = WeatherDemandRuleCache.Find(CurrentWeather);
	return Rules != nullptr ? Rules->Num() : 0;
}

void UCSWeatherSubsystem::LogWeatherStartupSummary() const
{
	const UEnum* WeatherEnum = StaticEnum<ECSWeatherType>();

	// DT_Weather에 행이 있고 가중치가 양수인 날씨만 실제로 뜹니다.
	// 둘 중 하나만 갖춰지면 조용히 후보에서 빠지므로 둘을 같은 줄에 붙여 찍습니다.
	FString CandidateSummary;
	for (int32 Index = static_cast<int32>(ECSWeatherType::None) + 1;
		Index < static_cast<int32>(ECSWeatherType::Max); ++Index)
	{
		const ECSWeatherType Type = static_cast<ECSWeatherType>(Index);
		if (FindWeatherRow(Type) == nullptr)
		{
			continue;
		}

		const TArray<FCSWeatherDemandRuleRow>* Rules = WeatherDemandRuleCache.Find(Type);
		CandidateSummary += FString::Printf(
			TEXT("%s%s(가중치 %.2f, 규칙 %d행)"),
			CandidateSummary.IsEmpty() ? TEXT("") : TEXT(", "),
			*WeatherEnum->GetNameStringByValue(static_cast<int64>(Type)),
			RuntimeWeights.GetWeight(Type),
			Rules != nullptr ? Rules->Num() : 0);
	}

	UE_LOG(LogTemp, Log, TEXT("[날씨수요] 시작 - 날씨 후보: %s"),
		CandidateSummary.IsEmpty() ? TEXT("없음") : *CandidateSummary);

	if (!IsValid(Settings) || !IsValid(Settings->WeatherDemandRuleTable))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[날씨수요] WeatherDemandRuleTable이 연결되지 않아 날씨 수요 보정이 전부 0입니다. "
			     "DA_WeatherSetting에 DT_WeatherDemandRule을 지정해야 합니다."));
	}
}

float UCSWeatherSubsystem::GetProductWeatherDemandModifier(const FDataTableRowHandle& ProductRow) const
{
	if (!IsValid(Settings) ||
		!IsValid(Settings->WeatherDemandRuleTable) ||
		!IsValid(ProductRow.DataTable) ||
		ProductRow.RowName.IsNone())
	{
		return 0.0f;
	}

	// 오늘 날씨는 반드시 여기서 읽습니다. 캐싱하면 cs.WeatherSet으로 강제한 뒤에도
	// 이전 날씨 기준으로 계산됩니다(강제 커맨드는 CurrentWeather만 덮어씁니다).
	// GetCurrentWeatherEntry/GetForecastWeather처럼 EnsureDecidedThrough를 타는 함수를 부르면
	// 채점 한 번마다 날씨 추첨이 앞당겨질 수 있으므로 인라인 게터만 씁니다.
	const ECSWeatherType Weather = GetCurrentWeather();
	if (Weather == ECSWeatherType::None)
	{
		return 0.0f;
	}

	const TArray<FCSWeatherDemandRuleRow>* Rules = WeatherDemandRuleCache.Find(Weather);
	if (Rules == nullptr)
	{
		return 0.0f;
	}

	const FCSProductDataRow* ProductData = ProductRow.GetRow<FCSProductDataRow>(
		TEXT("UCSWeatherSubsystem::GetProductWeatherDemandModifier"));
	if (ProductData == nullptr)
	{
		return 0.0f;
	}

	FName ProductCategoryName = NAME_None;
	if (const UEnum* ProductCategoryEnum = StaticEnum<ECSProductCategory>())
	{
		ProductCategoryName = FName(*ProductCategoryEnum->GetNameStringByValue(
			static_cast<int64>(ProductData->Category)));
	}

	// 같은 날씨에 여러 규칙이 걸리면 전부 더합니다. 상품 규칙과 카테고리 규칙이 겹쳐도 마찬가지입니다.
	float TotalModifier = 0.0f;
	for (const FCSWeatherDemandRuleRow& Rule : *Rules)
	{
		if (!Rule.bEnabled)
		{
			continue;
		}

		const bool bTargetMatches =
			(Rule.TargetType == ECSWeatherDemandTargetType::Product &&
				ProductRow.RowName == Rule.TargetId) ||
			(Rule.TargetType == ECSWeatherDemandTargetType::Category &&
				!ProductCategoryName.IsNone() &&
				ProductCategoryName == Rule.TargetId);
		if (bTargetMatches)
		{
			TotalModifier += Rule.DemandModifier;
		}
	}

	return TotalModifier;
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

	// 맑음처럼 이펙트가 없는 날씨도 문 연출 영역은 꺼야 하므로 두 단계를 따로 처리합니다.
	const FCSWeatherRow* Row = FindWeatherRow(NewWeather);
	SpawnEffectActor(Row);
	ApplyZoneSystem(Row);

	// 새 이펙트에 현재 보호 상태를 즉시 적용
	// 판정 결과가 이전과 같아도 표시 여부를 다시 설정
	UpdateShelterState(true);
}

void UCSWeatherSubsystem::SpawnEffectActor(const FCSWeatherRow* Row)
{
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

void UCSWeatherSubsystem::ApplyZoneSystem(const FCSWeatherRow* Row)
{
	ActiveZoneSystem = nullptr;

	// 날씨 행에 영역 에셋이 지정되어 있으면 동기 로드
	if (Row != nullptr && !Row->ZoneNiagaraSystem.IsNull())
	{
		ActiveZoneSystem = Row->ZoneNiagaraSystem.LoadSynchronous();
		if (ActiveZoneSystem == nullptr)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Weather] %s의 ZoneNiagaraSystem을 로드하지 못했습니다."),
				*Row->DisplayName.ToString());
		}
	}

	// 유효한 모든 영역에 에셋을 전달
	// nullptr을 전달받은 영역은 현재 파티클을 비활성화
	for (const TWeakObjectPtr<ACSWeatherZoneActor>& Zone : Zones)
	{
		if (Zone.IsValid())
		{
			Zone->ApplyZoneSystem(ActiveZoneSystem);
		}
	}
}

// ---------------------------------------------------------------------------
// 실내·문 앞 연출 영역
// ---------------------------------------------------------------------------

void UCSWeatherSubsystem::RegisterZone(ACSWeatherZoneActor* Zone)
{
	if (!IsValid(Zone))
	{
		return;
	}
	// 같은 영역이 중복 등록되지 않도록 약한 참조로 추가
	Zones.AddUnique(TWeakObjectPtr<ACSWeatherZoneActor>(Zone));

	// 등록 시점의 날씨 에셋과 보호 상태를 즉시 전달
	Zone->ApplyZoneSystem(ActiveZoneSystem);
	Zone->SetZoneVisible(bSheltered);

	// 플레이어가 이미 박스 안에 있는 경우를 반영하도록 재판정
	UpdateShelterState(false);
}

void UCSWeatherSubsystem::UnregisterZone(ACSWeatherZoneActor* Zone)
{
	Zones.RemoveAll([Zone](const TWeakObjectPtr<ACSWeatherZoneActor>& Item)
	{
		return !Item.IsValid() || Item.Get() == Zone;
	});
}

void UCSWeatherSubsystem::RegisterIndoorArea(ACSWeatherIndoorArea* Area)
{
	if (!IsValid(Area))
	{
		return;
	}
	// 같은 영역이 중복 등록되지 않도록 약한 참조로 추가
	IndoorAreas.AddUnique(TWeakObjectPtr<ACSWeatherIndoorArea>(Area));

	// 플레이어가 이미 실내에 있는 경우를 반영하도록 재판정
	UpdateShelterState(false);
}

void UCSWeatherSubsystem::UnregisterIndoorArea(ACSWeatherIndoorArea* Area)
{
	IndoorAreas.RemoveAll([Area](const TWeakObjectPtr<ACSWeatherIndoorArea>& Item)
	{
		return !Item.IsValid() || Item.Get() == Area;
	});
}

void UCSWeatherSubsystem::NotifyShelterOverlapChanged(AActor* OtherActor)
{
	// 유효하지 않은 액터와 0번 플레이어 폰이 아닌 액터는 제외
	if (!IsValid(OtherActor) || OtherActor != UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
	{
		return;
	}

	UpdateShelterState(false);
}

void UCSWeatherSubsystem::UpdateShelterState(bool bForceApply)
{
	// 플레이어 폰이 없으면 이전 판정 결과를 유지
	bool bNewSheltered = bSheltered;

	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (IsValid(PlayerPawn))
	{
		bNewSheltered = EvaluateSheltered(PlayerPawn);
	}
	// 상태가 같고 강제 적용도 아니면 추가 처리 없이 종료
	if (bNewSheltered == bSheltered && !bForceApply)
	{
		return;
	}
	// 새 판정 결과를 저장하고 연출 표시를 갱신
	bSheltered = bNewSheltered;
	ApplyShelterState();
}

bool UCSWeatherSubsystem::EvaluateSheltered(const AActor* PlayerPawn) const
{
	// 등록된 문 앞 판정 박스의 현재 오버랩 상태를 확인
	for (const TWeakObjectPtr<ACSWeatherZoneActor>& Zone : Zones)
	{
		if (Zone.IsValid() && Zone->IsActorInJudgeBox(PlayerPawn))
		{
			return true;
		}
	}
	// 등록된 실내 박스의 현재 오버랩 상태를 확인
	for (const TWeakObjectPtr<ACSWeatherIndoorArea>& Area : IndoorAreas)
	{
		if (Area.IsValid() && Area->IsActorInside(PlayerPawn))
		{
			return true;
		}
	}
	// 어느 판정 영역에도 겹쳐 있지 않으면 실외로 처리
	return false;
}

void UCSWeatherSubsystem::ApplyShelterState()
{
	if (IsValid(ActiveEffectActor))
	{
		// 실내면 플레이어 주위 파티클을 숨기고 실외이면 표시
		ActiveEffectActor->SetLocalEffectActive(!bSheltered);
	}

	// 플레이어가 실내 또는 문 앞 판정 박스 안에 있으면
	// 정문과 뒷마당 등 등록된 모든 문 앞 영역의 비·눈을 표시
	for (const TWeakObjectPtr<ACSWeatherZoneActor>& Zone : Zones)
	{
		if (Zone.IsValid())
		{
			Zone->SetZoneVisible(bSheltered);
		}
	}
}
