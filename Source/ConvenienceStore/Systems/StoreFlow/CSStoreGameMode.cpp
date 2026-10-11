#include "Systems/StoreFlow/CSStoreGameMode.h"

#include "Data/CSAudioConfigDataAsset.h"
#include "Data/CSFreshnessSettingsDataAsset.h"
#include "Data/CSStoreEconomyDataAsset.h"
#include "Data/CSStoreProgressionDataAsset.h"
#include "Data/CSWeatherSettingsDataAsset.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Systems/Audio/CSAudioSubsystem.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "Systems/SaveLoad/CSSaveSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "TimerManager.h"

ACSStoreGameMode::ACSStoreGameMode()
{
	GameStateClass = ACSStoreGameState::StaticClass();
	PlayerControllerClass = ACSStorePlayerController::StaticClass();
}

void ACSStoreGameMode::BeginPlay()
{
	Super::BeginPlay();
	StoreGameState = GetGameState<ACSStoreGameState>();
	if (!IsValid(StoreGameState))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StoreGameMode] ACSStoreGameState를 찾을 수 없어 상점 설정을 초기화하지 못했습니다."));
	}
	else
	{
		if (!IsValid(StoreProgressionDataAsset))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[StoreGameMode] StoreProgressionDataAsset이 지정되지 않아 기본 성장 상태만 유지합니다."));
		}
		else if (!StoreGameState->InitializeStoreProgression(StoreProgressionDataAsset))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[StoreGameMode] StoreProgressionDataAsset으로 성장 상태를 초기화하지 못했습니다."));
		}

		if (!IsValid(FreshnessSettingsDataAsset))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[StoreGameMode] FreshnessSettingsDataAsset이 지정되지 않아 신선도 감소를 적용하지 않습니다."));
		}
		else if (!StoreGameState->InitializeFreshnessSettings(FreshnessSettingsDataAsset))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[StoreGameMode] FreshnessSettingsDataAsset으로 신선도 설정을 초기화하지 못했습니다."));
		}

		if (!IsValid(StoreEconomyDataAsset))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[StoreGameMode] StoreEconomyDataAsset이 지정되지 않아 임대료를 처리하지 않습니다."));
		}
		else if (!StoreGameState->InitializeStoreEconomy(StoreEconomyDataAsset))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[StoreGameMode] StoreEconomyDataAsset 설정이 유효하지 않아 임대료를 처리하지 않습니다."));
		}
	}

	if (!IsValid(StoreAudioConfigDataAsset))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StoreGameMode] StoreAudioConfigDataAsset이 지정되지 않아 공용 사운드 시스템을 초기화하지 않습니다."));
	}
	else if (UGameInstance* GameInstance = GetGameInstance(); IsValid(GameInstance))
	{
		if (UCSAudioSubsystem* AudioSubsystem =
			GameInstance->GetSubsystem<UCSAudioSubsystem>(); IsValid(AudioSubsystem))
		{
			if (!AudioSubsystem->InitializeAudio(StoreAudioConfigDataAsset))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[StoreGameMode] StoreAudioConfigDataAsset으로 공용 사운드 시스템을 초기화하지 못했습니다."));
			}
			else if (const ACSStorePlayerController* StoreController =
				Cast<ACSStorePlayerController>(GetWorld()->GetFirstPlayerController());
				StoreController && StoreController->ShouldDeferStoreBGMForOpening())
			{
				// 새 게임 프롤로그 음성이 끝날 때까지 기본 BGM은 재생하지 않습니다.
				AudioSubsystem->StopBGM();
			}
			else if (!AudioSubsystem->PlayBGM())
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[StoreGameMode] StoreBGM이 지정되지 않았거나 현재 월드에서 재생할 수 없습니다."));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[StoreGameMode] UCSAudioSubsystem을 찾을 수 없습니다."));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StoreGameMode] GameInstance를 찾을 수 없어 공용 사운드 시스템을 초기화하지 못했습니다."));
	}

	if (bStartFlowOnBeginPlay)
	{
		StartStoreFlow();
	}

	// 불러오기로 열린 월드는 준비 단계 확정 후 다음 틱에 세이브를 적용
	// 월드 서브시스템의 OnWorldBeginPlay가 GameMode BeginPlay 이후에 실행되므로 데이터 캐시 생성 이후로 지연
	if (UGameInstance* OwningGameInstance = GetGameInstance())
	{
		if (const UCSSaveSubsystem* SaveSubsystem = OwningGameInstance->GetSubsystem<UCSSaveSubsystem>();
			SaveSubsystem != nullptr && SaveSubsystem->HasPendingLoad())
		{
			GetWorldTimerManager().SetTimerForNextTick(this, &ACSStoreGameMode::ApplyPendingSaveLoad);
		}
	}
}

void ACSStoreGameMode::ApplyPendingSaveLoad()
{
	UWorld* World = GetWorld();
	UGameInstance* OwningGameInstance = GetGameInstance();
	if (World == nullptr || OwningGameInstance == nullptr)
	{
		return;
	}

	UCSSaveSubsystem* SaveSubsystem = OwningGameInstance->GetSubsystem<UCSSaveSubsystem>();
	if (SaveSubsystem == nullptr || !SaveSubsystem->ApplyPendingLoad(*World))
	{
		return;
	}

	// 영업 종료 시점 세이브는 자유 시간으로 복귀
	// 그 외 단계(2단계의 새 게임 초기 저장 등)는 StartStoreFlow()가 확정한 준비 단계 아침을 그대로 사용
	if (IsValid(StoreGameState) && StoreGameState->RestoredSavePhase == ECStorePhase::ClosingFreeRoam)
	{
		RestoreClosingCheckpoint();
	}
}

void ACSStoreGameMode::RestoreClosingCheckpoint()
{
	// 불러온 상태를 다시 저장하지 않도록 차단
	bSkipClosingAutoSave = true;
	bClosingAutoSaveDone = true;

	// 마감 시각은 UpdateGameClock()에서 넣으므로 직접 설정
	StoreGameState->SetCurrentGameMinutes(AutomaticClosingTimeMinutes);

	// 마감 경고 재표시 방지
	bClosingWarningFired = true;

	// 기존 마감 처리 재사용
	// 임대료·인건비는 복원된 처리일(Last*ProcessedBusinessDay)이 같아 건너뜀
	// "영업 종료" 안내는 직전 단계가 Open일 때만 표시되므로 다시 뜨지 않음
	BeginClosingCheckpoint();

	UE_LOG(LogTemp, Log,
		TEXT("[CSSave] 영업 종료 시점 세이브를 불러와 %d일차 자유 시간으로 복귀했습니다."),
		StoreGameState->CurrentBusinessDay);
}

void ACSStoreGameMode::BeginClosingSaveWait()
{
	ClearClosingSaveTimers();

	if (bSkipClosingAutoSave || bClosingAutoSaveDone)
	{
		return;
	}

	// 강제 퇴장은 손님 StateTree 틱에서 순차 진행되고 배달 주문 마감 실패도 다음 틱에 처리되므로
	// 같은 틱에 바로 확인하지 않고 짧은 간격으로 반복 확인
	FTimerManager& TimerManager = GetWorldTimerManager();
	TimerManager.SetTimer(
		ClosingSaveCheckTimerHandle,
		this,
		&ACSStoreGameMode::TryClosingAutoSave,
		ClosingSaveCheckIntervalSeconds,
		true);

	TimerManager.SetTimer(
		ClosingSaveTimeoutTimerHandle,
		this,
		&ACSStoreGameMode::HandleClosingSaveTimeout,
		ClosingSaveMaxWaitSeconds,
		false);
}

void ACSStoreGameMode::TryClosingAutoSave()
{
	if (bSkipClosingAutoSave || bClosingAutoSaveDone)
	{
		ClearClosingSaveTimers();
		return;
	}

	const UCSCustomerRuntimeSubsystem* CustomerRuntimeSubsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>() : nullptr;
	if (CustomerRuntimeSubsystem != nullptr && !CustomerRuntimeSubsystem->IsReadyForClosingSave())
	{
		return;
	}

	ExecuteClosingAutoSave();
}

void ACSStoreGameMode::HandleClosingSaveTimeout()
{
	if (bSkipClosingAutoSave || bClosingAutoSaveDone)
	{
		ClearClosingSaveTimers();
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[CSSave] 영업 종료 후 %.1f초 안에 손님 상품 정리가 끝나지 않아 현재 상태로 자동 저장합니다."),
		ClosingSaveMaxWaitSeconds);
	ExecuteClosingAutoSave();
}

void ACSStoreGameMode::ExecuteClosingAutoSave()
{
	ClearClosingSaveTimers();

	// 자유 시간을 벗어났거나(정산 시작 등) 불러오기로 레벨이 전환되는 중이면 저장하지 않음
	if (!IsValid(StoreGameState) ||
		StoreGameState->CurrentPhase != ECStorePhase::ClosingFreeRoam ||
		StoreGameState->bIsDailySummaryPending)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CSSave] 자유 시간이 아니어서 영업 종료 자동 저장을 건너뜁니다."));
		bClosingAutoSaveDone = true;
		return;
	}

	UGameInstance* OwningGameInstance = GetGameInstance();
	UCSSaveSubsystem* SaveSubsystem =
		OwningGameInstance ? OwningGameInstance->GetSubsystem<UCSSaveSubsystem>() : nullptr;
	if (SaveSubsystem == nullptr || SaveSubsystem->HasPendingLoad())
	{
		return;
	}

	bClosingAutoSaveDone = true;
	SaveSubsystem->SaveToAutoSlot();
}

void ACSStoreGameMode::ClearClosingSaveTimers()
{
	GetWorldTimerManager().ClearTimer(ClosingSaveCheckTimerHandle);
	GetWorldTimerManager().ClearTimer(ClosingSaveTimeoutTimerHandle);
}

void ACSStoreGameMode::StartStoreFlow()
{
	if (StoreGameState == nullptr)
	{
		StoreGameState = GetGameState<ACSStoreGameState>();
	}

	if (StoreGameState == nullptr)
	{
		return;
	}

	FiredScheduleEventRows.Empty();
	bClosingWarningFired = false;

	// 새 영업일은 영업 종료 자동 저장을 다시 받음
	ClearClosingSaveTimers();
	bClosingAutoSaveDone = false;
	bSkipClosingAutoSave = false;

	StoreGameState->SetEndOfDayTask(ECEndOfDayTask::None);
	StoreGameState->CompleteDailySummary();
	StoreGameState->SetClosingTimeSettings(
		AutomaticClosingTimeMinutes,
		ClosingClockCountdownMinutes);
	StoreGameState->SetGameClock(
		InitialGameTimeMinutes,
		GameMinutesPerRealSecond,
		false);

	StoreGameState->SetStorePhase(MakePhaseData(
		ECStorePhase::Preparation, NSLOCTEXT("Store", "Preparation", "준비")));
	// 영업 시간대가 되어도 입간판 확인 전까지는 고객을 받지 않습니다.
	StoreGameState->bIsStoreOpen = false;
	StoreGameState->OnStoreOpenStateChanged.Broadcast(false);
	GetWorldTimerManager().ClearTimer(GameClockTimerHandle);
	GetWorldTimerManager().SetTimer(
		GameClockTimerHandle,
		this,
		&ACSStoreGameMode::UpdateGameClock,
		0.1f,
		true);
}

bool ACSStoreGameMode::RequestBusinessOpenState(bool bShouldOpen)
{
	if (!IsValid(StoreGameState) || !StoreGameState->CanChangeStoreOpenState())
	{
		return false;
	}

	if (StoreGameState->CurrentPhase == ECStorePhase::Preparation)
	{
		if (!bShouldOpen)
		{
			return false;
		}

		StoreGameState->SetGameClock(BusinessStartTimeMinutes, GameMinutesPerRealSecond, true);
		StoreGameState->SetStorePhase(MakePhaseData(
			ECStorePhase::Open, NSLOCTEXT("Store", "Business", "영업")));
		CheckScheduleEvents(BusinessStartTimeMinutes - 0.01f, BusinessStartTimeMinutes);
	}

	return StoreGameState->SetStoreOpen(bShouldOpen);
}

void ACSStoreGameMode::CompleteDailySummary()
{
	if (!IsValid(StoreGameState) || !StoreGameState->bIsDailySummaryPending)
	{
		return;
	}

	StoreGameState->CompleteDailySummary();
	StartNextBusinessDay();
}

bool ACSStoreGameMode::RequestClosingDailySummary()
{
	if (StoreGameState == nullptr)
	{
		StoreGameState = GetGameState<ACSStoreGameState>();
	}

	if (!IsValid(StoreGameState) ||
		StoreGameState->CurrentPhase != ECStorePhase::ClosingFreeRoam ||
		StoreGameState->bIsDailySummaryPending)
	{
		return false;
	}

	// 정산 화면을 띄우기 전에 남아 있는 손님을 전부 정리합니다.
	// DespawnAllCustomers가 절도범과 일반 손님을 두 갈래로 나눠 처리하므로
	// 이 호출 하나로 둘 다 마감 종 시점에 확정됩니다.
	//   절도범   : 상품을 유지한 채 파괴 → 손실이 당일 장부로 확정
	//   그 외    : 상품을 낙하시켜 재고로 회수한 뒤 파괴
	//
	// RequestDailySummary가 요약 위젯을 생성하므로 반드시 그 앞에서 불러야
	// 위젯 첫 프레임부터 올바른 금액이 보입니다. 뒤에 두면 플레이어 눈앞에서 금액이 튀어오릅니다.
	if (UCSCustomerRuntimeSubsystem* CustomerRuntimeSubsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>() : nullptr)
	{
		CustomerRuntimeSubsystem->DespawnAllCustomers();
	}

	// 자동 저장 대기 중에 잠자기를 확인한 경우
	// 손님 정리로 상품 낙하·도난 손실이 확정된 지금, 평판 정산 전에 저장을 마침
	if (!bSkipClosingAutoSave && !bClosingAutoSaveDone)
	{
		ExecuteClosingAutoSave();
	}

	StoreGameState->RequestDailySummary();
	return true;
}

void ACSStoreGameMode::StartPhase(ECStorePhase NewPhase)
{
	if (StoreGameState == nullptr)
	{
		StoreGameState = GetGameState<ACSStoreGameState>();
	}

	if (StoreGameState == nullptr)
	{
		return;
	}

	FCSStorePhaseData FinishedPhaseData;
	const FCSStorePhaseData* PhaseData = FindPhaseData(NewPhase);
	if (PhaseData == nullptr && NewPhase == ECStorePhase::Finished)
	{
		FinishedPhaseData.Phase = ECStorePhase::Finished;
		FinishedPhaseData.DisplayName = NSLOCTEXT(
			"Store", "FinishedPhase", "Finished");
		PhaseData = &FinishedPhaseData;
	}

	if (PhaseData == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Store phase could not start. Assign DT_StorePhase to StorePhaseDataTable."));
		return;
	}

	StoreGameState->SetStorePhase(*PhaseData);
}

void ACSStoreGameMode::UpdateGameClock()
{
	if (StoreGameState == nullptr || !StoreGameState->bIsGameClockRunning)
	{
		return;
	}

	const float PreviousGameMinutes = StoreGameState->CurrentGameMinutes;
	const float NewGameMinutes = PreviousGameMinutes +
		StoreGameState->GameMinutesPerRealSecond * 0.1f;
	const float ClosingWarningTime =
		AutomaticClosingTimeMinutes - ClosingWarningLeadMinutes;
	if (!bClosingWarningFired && ClosingWarningLeadMinutes > 0.0f &&
		PreviousGameMinutes < ClosingWarningTime &&
		NewGameMinutes >= ClosingWarningTime)
	{
		bClosingWarningFired = true;
		StoreGameState->OnClosingWarningRequested.Broadcast(
			FMath::RoundToInt(ClosingWarningLeadMinutes));
	}

	if (NewGameMinutes >= AutomaticClosingTimeMinutes)
	{
		StoreGameState->SetCurrentGameMinutes(AutomaticClosingTimeMinutes);
		BeginClosingCheckpoint();
		return;
	}

	StoreGameState->SetCurrentGameMinutes(NewGameMinutes);
	CheckScheduleEvents(PreviousGameMinutes, NewGameMinutes);
}

void ACSStoreGameMode::UpdateStorePhaseForCurrentTime()
{
	const FCSStorePhaseData* PhaseData = FindPhaseDataAtTime(
		StoreGameState->CurrentGameMinutes);

	if (PhaseData != nullptr &&
		(StoreGameState->CurrentPhase != PhaseData->Phase ||
			StoreGameState->CurrentPhaseDisplayName.IsEmpty()))
	{
		StoreGameState->SetStorePhase(*PhaseData);
	}
}

void ACSStoreGameMode::CheckScheduleEvents(
	float PreviousGameMinutes,
	float NewGameMinutes)
{
	if (StoreScheduleEventDataTable == nullptr || StoreGameState == nullptr)
	{
		return;
	}

	for (const FName& RowName : StoreScheduleEventDataTable->GetRowNames())
	{
		if (FiredScheduleEventRows.Contains(RowName))
		{
			continue;
		}

		const FCSStoreScheduleEventData* Row =
			StoreScheduleEventDataTable->FindRow<FCSStoreScheduleEventData>(
				RowName, TEXT("CheckStoreSchedule"));

		if (Row != nullptr && PreviousGameMinutes < Row->TriggerTimeMinutes &&
			NewGameMinutes >= Row->TriggerTimeMinutes)
		{
			FiredScheduleEventRows.Add(RowName);
			StoreGameState->OnScheduleEventFired.Broadcast(RowName, Row->Message);
		}
	}
}

void ACSStoreGameMode::BeginClosingCheckpoint()
{
	GetWorldTimerManager().ClearTimer(GameClockTimerHandle);
	// 시계 정지 상태를 HUD에도 알리기 위해 setter로 변경합니다.
	// StoreGameState->bIsGameClockRunning = false;
	StoreGameState->SetGameClockRunning(false);
	// 새벽 02:00에는 입간판 상태와 무관하게 영업을 강제 종료합니다.
	StoreGameState->bIsStoreOpen = false;
	StoreGameState->OnStoreOpenStateChanged.Broadcast(false);

	// 마감 자유 활동에 들어가기 전에 오늘 임대료를 실제 지출로 확정합니다.
	StoreGameState->ProcessDailyRent();
	StoreGameState->ProcessDailyEmployeeWages();

	// 통계는 자동으로 열지 않고 플레이어가 마감종을 확인할 때 요청합니다.
	StoreGameState->SetStorePhase(MakePhaseData(
		ECStorePhase::ClosingFreeRoam,
		NSLOCTEXT("Store", "ClosingFreeRoam", "마감 활동")));

	// 손님이 들고 있던 상품을 모두 떨어뜨린 뒤 자동 저장
	// 불러오기로 복귀한 경우 bSkipClosingAutoSave로 건너뜀
	BeginClosingSaveWait();
}

void ACSStoreGameMode::CompleteCurrentEndOfDayTask()
{
	if (StoreGameState == nullptr ||
		StoreGameState->CurrentEndOfDayTask == ECEndOfDayTask::None)
	{
		return;
	}

	const ECEndOfDayTask NextTask = GetNextEndOfDayTask(
		StoreGameState->CurrentEndOfDayTask);
	StoreGameState->SetEndOfDayTask(NextTask);

	if (NextTask == ECEndOfDayTask::None)
	{
		// 마감 업무가 모두 끝나면 다음 날 02:00 AM 상태로 이동합니다.
		StoreGameState->SetCurrentGameMinutes(120.0f);

		FCSStorePhaseData NextDayPhase;
		NextDayPhase.Phase = ECStorePhase::Finished;
		NextDayPhase.DisplayName = NSLOCTEXT("Store", "NextDay", "Next Day");
		StoreGameState->SetStorePhase(NextDayPhase);
	}
}

void ACSStoreGameMode::StartNextBusinessDay()
{
	if (StoreGameState != nullptr)
	{
		// 다음 날이 시작되기 전에 전날의 판매 요약값을 초기화합니다.
		StoreGameState->BeginNextBusinessDay();
	}

	// 자동 저장은 영업 종료 직후(BeginClosingCheckpoint)에만 수행
	StartStoreFlow();
}

ECEndOfDayTask ACSStoreGameMode::GetNextEndOfDayTask(ECEndOfDayTask Task) const
{
	switch (Task)
	{
	case ECEndOfDayTask::OrderReview:
		return ECEndOfDayTask::Settlement;
	case ECEndOfDayTask::Settlement:
		return ECEndOfDayTask::SalesReport;
	case ECEndOfDayTask::SalesReport:
		return ECEndOfDayTask::ReputationUpdate;
	case ECEndOfDayTask::ReputationUpdate:
	default:
		return ECEndOfDayTask::None;
	}
}

FCSStorePhaseData ACSStoreGameMode::MakePhaseData(
	ECStorePhase Phase, const FText& DisplayName) const
{
	FCSStorePhaseData PhaseData;
	PhaseData.Phase = Phase;
	PhaseData.DisplayName = DisplayName;
	PhaseData.bCanRestock = Phase != ECStorePhase::Open;
	PhaseData.bCanCheckout = Phase == ECStorePhase::Open;
	PhaseData.bCanSpawnCustomers = Phase == ECStorePhase::Open;
	return PhaseData;
}

const FCSStorePhaseData* ACSStoreGameMode::FindPhaseData(ECStorePhase Phase) const
{
	if (StorePhaseDataTable == nullptr)
	{
		return nullptr;
	}

	for (const FName& RowName : StorePhaseDataTable->GetRowNames())
	{
		if (const FCSStorePhaseData* Row = StorePhaseDataTable->FindRow<FCSStorePhaseData>(
			RowName, TEXT("FindStorePhase"));
			Row != nullptr && Row->Phase == Phase)
		{
			return Row;
		}
	}

	return nullptr;
}

const FCSStorePhaseData* ACSStoreGameMode::FindPhaseDataAtTime(float GameMinutes) const
{
	if (StorePhaseDataTable == nullptr)
	{
		return nullptr;
	}

	for (const FName& RowName : StorePhaseDataTable->GetRowNames())
	{
		const FCSStorePhaseData* Row = StorePhaseDataTable->FindRow<FCSStorePhaseData>(
			RowName, TEXT("FindStorePhaseAtTime"));

		if (Row != nullptr && GameMinutes >= Row->StartTimeMinutes &&
			GameMinutes < Row->EndTimeMinutes)
		{
			return Row;
		}
	}

	return nullptr;
}

bool ACSStoreGameMode::IsTutorialEnabled() const
{
	return bTutorialEnabled;
}

UDataTable* ACSStoreGameMode::GetTutorialStepDataTable() const
{
	return TutorialStepDataTable;
}

TSubclassOf<UCSTutorialBannerWidgetBase> ACSStoreGameMode::GetTutorialBannerWidgetClass() const
{
	return TutorialBannerWidgetClass;
}

TSubclassOf<UCSTutorialDialogueWidgetBase> ACSStoreGameMode::GetTutorialDialogueWidgetClass() const
{
	return TutorialDialogueWidgetClass;
}

TSubclassOf<UCSTutorialObjectiveWidgetBase> ACSStoreGameMode::GetTutorialObjectiveWidgetClass() const
{
	return TutorialObjectiveWidgetClass;
}

TSubclassOf<AActor> ACSStoreGameMode::GetTutorialShelfFacilityClass() const
{
	return TutorialShelfFacilityClass;
}

UCSWeatherSettingsDataAsset* ACSStoreGameMode::GetWeatherSettings() const
{
	return WeatherSettingsDataAsset;
}
