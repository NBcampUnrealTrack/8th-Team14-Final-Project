// 날씨 디버그 표시와 콘솔 커맨드를 모아둔 임시 파일입니다.
// 코어 로직(CSWeatherSubsystem.cpp)은 이 파일에 의존하지 않으므로 통째로 지워도 컴파일됩니다.
#include "Systems/Weather/CSWeatherSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "UObject/Class.h"

// 디버그 메시지 키의 시작값입니다. DayOffset을 더해 한 날짜당 한 줄을 차지합니다.
// 다른 시스템의 디버그 메시지와 겹치지 않을 값으로 잡습니다.
static constexpr uint64 WeatherDebugMessageKey = 0x57454154ULL;   // 'WEAT'

// ---------------------------------------------------------------------------
// 화면 출력
// ---------------------------------------------------------------------------

void UCSWeatherSubsystem::DebugPrintForecast(float DisplayDuration)
{
	if (GEngine == nullptr)
	{
		return;
	}

	// 공개 범위만큼 채워져서 나옵니다. 이 함수는 개수를 세지 않습니다.
	const TArray<FCSWeatherForecastEntry> Entries = GetForecastEntries();

	for (const FCSWeatherForecastEntry& Entry : Entries)
	{
		// 키를 DayOffset마다 다르게 줘야 줄이 서로를 덮어쓰지 않습니다.
		// 같은 키를 재사용하면 매번 같은 줄이 갱신되어 마지막 하루만 보입니다.
		GEngine->AddOnScreenDebugMessage(
			WeatherDebugMessageKey + Entry.DayOffset,
			DisplayDuration,
			GetDebugColor(Entry),
			MakeDebugLine(Entry));
	}
}

FString UCSWeatherSubsystem::MakeDebugLine(const FCSWeatherForecastEntry& Entry) const
{
	// DayOffset으로 문구를 만들므로 공개 일수가 늘어나도 이 함수는 그대로입니다.
	FString DayLabel;
	switch (Entry.DayOffset)
	{
	case 0:  DayLabel = TEXT("오늘  ");  break;
	case 1:  DayLabel = TEXT("내일  ");  break;
	case 2:  DayLabel = TEXT("모레  ");  break;
	default: DayLabel = FString::Printf(TEXT("%d일 뒤"), Entry.DayOffset); break;
	}

	return FString::Printf(
		TEXT("[날씨] %s  %2d일차  %s"),
		*DayLabel,
		Entry.BusinessDay,
		*Entry.DisplayName.ToString());
}

FColor UCSWeatherSubsystem::GetDebugColor(const FCSWeatherForecastEntry& Entry) const
{
	// 오늘만 눈에 띄게 표시합니다.
	return Entry.DayOffset == 0 ? FColor::Yellow : FColor::White;
}

// ---------------------------------------------------------------------------
// 강제 조작
// ---------------------------------------------------------------------------

void UCSWeatherSubsystem::DebugSetTodayWeather(ECSWeatherType NewWeather)
{
	if (NewWeather == ECSWeatherType::None || NewWeather == ECSWeatherType::Max)
	{
		return;
	}

	// 화면 출력의 "오늘" 줄과 실제 날씨가 어긋나지 않도록 확정 배열도 함께 덮어씁니다.
	EnsureDecidedThrough(CurrentBusinessDay);
	const int32 Index = CurrentBusinessDay - 1;
	if (DecidedWeather.IsValidIndex(Index))
	{
		DecidedWeather[Index] = NewWeather;
	}

	const ECSWeatherType Previous = CurrentWeather;
	CurrentWeather = NewWeather;

	if (CurrentWeather != Previous)
	{
		// 영업 중 오늘 날씨가 바뀌는 SetForecastWeather와 같은 방식으로 색감을 서서히 적용
		// cs.WeatherSet으로 날씨의 돌 전환 연출을 확인할 때 사용
		ApplyWeatherEffect(CurrentWeather, true);
	}

	OnWeatherChanged.Broadcast(CurrentWeather, Previous);
	OnWeatherForecastUpdated.Broadcast();

	// 강제 지정도 오늘 날씨가 정해지는 지점입니다.
	// ApplyBusinessDay와 같은 형식으로 남겨야 로그만 보고 "이 런의 날씨가 무엇이었는지"를 셀 수 있습니다.
	UE_LOG(LogTemp, Log,
		TEXT("[날씨수요] 오늘 날씨=%s 적용 규칙 %d건 (%d일차, 강제 지정)"),
		*StaticEnum<ECSWeatherType>()->GetNameStringByValue(static_cast<int64>(CurrentWeather)),
		GetActiveDemandRuleCount(), CurrentBusinessDay);

	DebugPrintForecast();
}

void UCSWeatherSubsystem::DebugAdvanceDayPreview()
{
	// 날씨 쪽 일차만 앞으로 밉니다. GameState의 CurrentBusinessDay는 그대로이므로
	// 실제 하루가 넘어가면 OnBusinessDayStarted가 다시 맞춰줍니다.
	ApplyBusinessDay(CurrentBusinessDay + 1);
}

// ---------------------------------------------------------------------------
// 콘솔 커맨드
// ---------------------------------------------------------------------------

namespace
{
	UCSWeatherSubsystem* FindWeatherSubsystem(UWorld* World)
	{
		return World != nullptr ? World->GetSubsystem<UCSWeatherSubsystem>() : nullptr;
	}
}

static FAutoConsoleCommandWithWorld CmdWeatherShow(
	TEXT("cs.WeatherShow"),
	TEXT("오늘과 공개 예보를 화면에 출력한다. 줄 수는 PublicForecastDays를 따른다."),
	FConsoleCommandWithWorldDelegate::CreateStatic(
		[](UWorld* World)
		{
			if (UCSWeatherSubsystem* Subsystem = FindWeatherSubsystem(World))
			{
				Subsystem->DebugPrintForecast();
			}
		}));

static FAutoConsoleCommandWithWorldAndArgs CmdWeatherSet(
	TEXT("cs.WeatherSet"),
	TEXT("cs.WeatherSet <Clear|Rain|Snow|Cloudy|Heat|Cold> - 오늘 날씨를 강제로 변경한다."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Weather] 날씨 이름을 인자로 넣어야 합니다."));
				return;
			}

			UCSWeatherSubsystem* Subsystem = FindWeatherSubsystem(World);
			if (Subsystem == nullptr)
			{
				return;
			}

			const UEnum* EnumPtr = StaticEnum<ECSWeatherType>();//만약 여기에 오류떠도 문제없음, IDE 표시만 그럴 뿐 컴파일은 정상! (디버그용이라 신경 안써도 됩니다!)
			const int64 Value = EnumPtr != nullptr
				? EnumPtr->GetValueByNameString(Args[0])
				: static_cast<int64>(INDEX_NONE);
			if (Value == INDEX_NONE)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Weather] 알 수 없는 날씨 이름: %s"), *Args[0]);
				return;
			}

			Subsystem->DebugSetTodayWeather(static_cast<ECSWeatherType>(Value));
		}));

static FAutoConsoleCommandWithWorld CmdWeatherNext(
	TEXT("cs.WeatherNext"),
	TEXT("날씨 쪽 일차만 하루 앞으로 밀어 전환 연출을 미리 본다. GameState의 일차는 바뀌지 않는다."),
	FConsoleCommandWithWorldDelegate::CreateStatic(
		[](UWorld* World)
		{
			if (UCSWeatherSubsystem* Subsystem = FindWeatherSubsystem(World))
			{
				Subsystem->DebugAdvanceDayPreview();
			}
		}));

