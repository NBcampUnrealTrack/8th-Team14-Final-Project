#include "Data/CSCustomerTrafficDataAsset.h"

// UEnum::GetDisplayValueAsText로 등급 이름을 로그에 남기기 위해 필요합니다.
#include "UObject/Class.h"


UCSCustomerTrafficDataAsset::UCSCustomerTrafficDataAsset()
{
	// 등급별 확정 수치입니다. 두 값이 뜻하는 것은 이렇습니다.
	//   SpawnIntervalSeconds : 몇 초에 한 명씩 만들지
	//   MaxAliveCustomers    : 그렇게 만든 손님이 맵에 최대 몇 명까지 남아 있을 수 있는지
	GradeRows = {
		{ ECSReputationGrade::VeryBad,   60.0f,  3 },
		{ ECSReputationGrade::Bad,       40.0f,  4 },
		{ ECSReputationGrade::Normal,    27.0f,  5 },
		{ ECSReputationGrade::Good,      20.0f,  7 },
		{ ECSReputationGrade::Excellent, 15.0f, 10 }
	};
}

const FCSReputationTrafficRow* UCSCustomerTrafficDataAsset::FindRowForGrade(ECSReputationGrade Grade) const
{
	const FCSReputationTrafficRow* Best = nullptr;

	// 데이터 에셋에 등록된 모든 등급별 설정을 순회
	for (const FCSReputationTrafficRow& Row : GradeRows)
	{
		// 해당 행의 등급이 'None'이라면 의미 없는 데이터이므로 통과
		if (Row.Grade == ECSReputationGrade::None)
		{
			continue;
		}

		// 현재 검사 중인 행의 등급이 찾고자 하는 등급보다 높다면 통과
		if (static_cast<uint8>(Row.Grade) > static_cast<uint8>(Grade))
		{
			continue;
		}

		// 아직 Best값을 찾지 못했거나 검사 중인 행의 등급이 기존에 임시로 저장해둔 Best의 등급보다 크다면 Best를 갱신
		if (Best == nullptr || static_cast<uint8>(Row.Grade) > static_cast<uint8>(Best->Grade))
		{
			Best = &Row;
		}
	}

	return Best;
}

float UCSCustomerTrafficDataAsset::GetSpawnIntervalSeconds(ECSReputationGrade Grade,bool bIsPeakActive) const
{
	// FindRowForGrade 이용해 현재 평판 등급에 맞는 설정값을 가져옴
	const FCSReputationTrafficRow* Row = FindRowForGrade(Grade);

	// 오류 메시지 및 방어코드
	if (Row == nullptr)
	{
		UE_LOG(LogTemp, Error,TEXT("[%s] 평판 등급 %s에 해당하는 행이 없어 손님 생성 간격을 계산할 수 없어서 1.0f로 반환 하겠습니다."),*GetName(), *UEnum::GetDisplayValueAsText(Grade).ToString());
		return 1.0f;
	}

	// 데이터 에셋에 설정된 기본 생성 간격을 가져오되, 실수로 0이나 음수를 넣었을 경우를 대비해 최소 0.1초로 제한
	float Interval = FMath::Max(Row->SpawnIntervalSeconds, 0.1f);

	// 피크타임에는 간격을 나눠 손님이 더 자주 오게 합니다.
	if (bIsPeakActive)
	{
		Interval /= FMath::Max(PeakMultiplier, 1.0f);
	}

	// 배율을 크게 올려도 간격이 0에 수렴하지 않도록 제한
	return FMath::Max(Interval, 0.1f);
}

int32 UCSCustomerTrafficDataAsset::GetMaxAliveCustomers(ECSReputationGrade Grade,bool bIsPeakActive) const
{
	const FCSReputationTrafficRow* Row = FindRowForGrade(Grade);

	// 오류 방어코드
	if (Row == nullptr)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[%s] 평판 등급 %s에 해당하는 행이 없어 동시 손님 수 상한을 계산할 수 없습니다."),
			*GetName(), *UEnum::GetDisplayValueAsText(Grade).ToString());
		return 1;
	}

	// 데이터에 설정된 최대 인원을 가져옵니다 (최소 1명 보장)
	int32 MaxAlive = FMath::Max(Row->MaxAliveCustomers, 1);

	// 최대 인원 상한선도 피크 배율만큼 곱함
	if (bIsPeakActive)
	{
		MaxAlive = FMath::RoundToInt(MaxAlive * FMath::Max(PeakMultiplier, 1.0f));
	}

	// 최종 산출된 값이 상한선(MaximumAliveCustomers) 사이에 있도록 제한
	return FMath::Clamp(MaxAlive, 1, FMath::Max(MaximumAliveCustomers, 1));
}

bool UCSCustomerTrafficDataAsset::ValidateTrafficData(FString& OutReason) const
{
	OutReason.Reset(); // 이전 에러 메시지 초기화

	// 행 자체가 하나도 등록 안 되어 있으면 실패 처리
	if (GradeRows.IsEmpty())
	{
		OutReason = TEXT("GradeRows가 비어 있습니다.");
		return false;
	}

	TArray<FString> Problems; // 발견된 모든 문제점들을 모아둘 배열
	TSet<ECSReputationGrade> SeenGrades; // 중복 등급 등록을 확인하기 위한 세트

	// 등록된 모든 행을 순회
	for (const FCSReputationTrafficRow& Row : GradeRows)
	{
		const FString GradeName = UEnum::GetDisplayValueAsText(Row.Grade).ToString();

		// 2. 등급을 None으로 설정해 둔 실수가 있는지 체크
		if (Row.Grade == ECSReputationGrade::None)
		{
			Problems.Add(TEXT("None 등급 행은 사용할 수 없습니다."));
			continue;
		}

		// 똑같은 등급을 두 번 넣었는지 체크
		bool bAlreadySeen = false;
		SeenGrades.Add(Row.Grade, &bAlreadySeen);
		if (bAlreadySeen)
		{
			Problems.Add(FString::Printf(TEXT("%s 등급 행이 중복입니다."), *GradeName));
		}

		// 스폰 간격을 0.1초 미만으로 비정상적으로 짧게 설정했는지 체크
		if (Row.SpawnIntervalSeconds < 0.1f)
		{
			Problems.Add(FString::Printf(
				TEXT("%s 등급의 생성 간격(%.2f초)이 너무 짧습니다."),
				*GradeName, Row.SpawnIntervalSeconds));
		}
		// 최대 인원을 0명 이하로 설정했는지 체크
		if (Row.MaxAliveCustomers < 1)
		{
			Problems.Add(FString::Printf(
				TEXT("%s 등급의 동시 인원(%d명)이 1명보다 적습니다."),
				*GradeName, Row.MaxAliveCustomers));
		}
	}

	// VeryBad부터 Excellent까지 5개 등급이 다 존재하는지 체크
	const ECSReputationGrade RequiredGrades[] = {
		ECSReputationGrade::VeryBad,
		ECSReputationGrade::Bad,
		ECSReputationGrade::Normal,
		ECSReputationGrade::Good,
		ECSReputationGrade::Excellent
	};

	for (const ECSReputationGrade RequiredGrade : RequiredGrades)
	{
		// 필수 등급이 빠져있다면 경고 추가
		if (!SeenGrades.Contains(RequiredGrade))
		{
			Problems.Add(FString::Printf(
				TEXT("%s 등급 행이 없습니다(더 낮은 등급 값으로 대체됩니다)."),
				*UEnum::GetDisplayValueAsText(RequiredGrade).ToString()));
		}
	}

	// 문제점이 없으면 true 반환
	if (Problems.IsEmpty())
	{
		return true;
	}

	// 문제점이 있다면 배열에 모아둔 문자열들을 " / " 로 이어서 경고 메시지로 만든 뒤 false 반환
	OutReason = FString::Join(Problems, TEXT(" / "));
	return false;
}
