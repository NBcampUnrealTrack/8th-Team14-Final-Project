#include "Systems/Artifact/CSArtifactSubsystem.h"
#include "Data/CSArtifactVisualRegistry.h"
#include "Engine/GameInstance.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "Features/Cart/CSCartActor.h"
#include "Features/Artifact/CSFairyHouse.h"
#include "Features/Employees/CSStoreEmployeeCharacter.h"
#include "Features/Customers/CSCustomerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Systems/Journal/CSJournalSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "Systems/Weather/CSWeatherSubsystem.h"
#include "Data/CSWeatherTypes.h"
#include "Systems/SaveLoad/CSSaveGame.h"
#include "Features/Facilities/CSSaveIdComponent.h"

UCSArtifactSubsystem::UCSArtifactSubsystem()
{
	// A reflected hard reference keeps the registry reachable for cooking as well as runtime.
	static ConstructorHelpers::FObjectFinderOptional<UCSArtifactVisualRegistry> Registry(
		TEXT("/Game/ConvenienceStore/Data/DataAsset/Artifact_Visual/DA_ArtifactVisualRegistry.DA_ArtifactVisualRegistry"));
	VisualRegistry = Registry.Get();
}

UCSArtifactVisualRegistry* UCSArtifactSubsystem::ResolveVisualRegistry(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	const UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
	const UCSArtifactSubsystem* Owner = Instance ? Instance->GetSubsystem<UCSArtifactSubsystem>() : GetDefault<UCSArtifactSubsystem>();
	return Owner ? Owner->VisualRegistry.Get() : nullptr;
}

void UCSArtifactSubsystem::CaptureSaveData(UCSSaveGame& OutSave) const
{
	OutSave.Artifacts.Counts = Counts;
	OutSave.Artifacts.AppliedStacks = AppliedStacks;
	OutSave.Artifacts.NextAvailableDays = NextAvailableDays;
	OutSave.Artifacts.bFairyContractSigned = bFairyContractSigned;
}

void UCSArtifactSubsystem::RestoreSaveData(const UCSSaveGame& InSave)
{
	Counts = InSave.Artifacts.Counts;
	AppliedStacks = InSave.Artifacts.AppliedStacks;
	NextAvailableDays = InSave.Artifacts.NextAvailableDays;
	bFairyContractSigned = InSave.Artifacts.bFairyContractSigned;
	bUsingArtifact = false;
	// 저장된 계약은 등장 연출을 반복하지 않고 즉시 청소 기능을 복원합니다.
	if (UWorld* World = GetGameInstance()->GetWorld())
	{
		for (TActorIterator<ACSFairyHouse> It(World); It; ++It)
		{
			It->FinishIntroduction();
			It->RefreshContractState();
		}
		for (TActorIterator<ACSPlayerCharacter> It(World); It; ++It) It->RefreshArtifactMoveSpeed();
		for (TActorIterator<ACSStoreEmployeeCharacter> It(World); It; ++It) It->RefreshArtifactMoveSpeed();
		for (TActorIterator<ACSCustomerCharacter> It(World); It; ++It) It->RefreshArtifactMoveSpeed();
	}
	OnArtifactsChanged.Broadcast();
}

bool UCSArtifactSubsystem::HasFairyContract() const
{
	for (const auto& Pair : Counts)
	{
		FCSJournalArtifactDataRow Row;
		if (Pair.Value > 0 && GetDefinition(Pair.Key, Row) &&
			(Row.Effect == ECSArtifactEffectType::FairyContract || Row.Effect == ECSArtifactEffectType::FairyHouse)) return true;
	}
	return false;
}

void UCSArtifactSubsystem::RegisterTable(UDataTable* InTable)
{
	if (IsValid(InTable) && InTable->GetRowStruct() &&
		InTable->GetRowStruct()->IsChildOf(FCSJournalArtifactDataRow::StaticStruct()))
	{
		ArtifactTables.AddUnique(InTable);
	}
}

bool UCSArtifactSubsystem::GetDefinition(FName Id, FCSJournalArtifactDataRow& OutRow) const
{
	if (Id.IsNone()) return false;
	// 도감과 동일하게 나중에 등록된 테이블의 같은 ID를 우선합니다.
	for (int32 Index = ArtifactTables.Num() - 1; Index >= 0; --Index)
	{
		const UDataTable* Table = ArtifactTables[Index];
		if (!IsValid(Table)) continue;
		if (const auto* Row = Table->FindRow<FCSJournalArtifactDataRow>(Id, TEXT("Artifact definition"), false))
		{
			OutRow = *Row;
			// 기존 집 아티팩트의 행 식별자는 유지하여 뽑기와 수집 기록의 연결을 보존합니다.
			if (OutRow.Effect == ECSArtifactEffectType::FairyHouse)
			{
				OutRow.Effect = ECSArtifactEffectType::FairyContract;
				OutRow.DisplayName = NSLOCTEXT("Artifact", "FairyContract", "요정 고용 계약서");
				OutRow.Description = NSLOCTEXT("Artifact", "FairyContractDescription", "수첩에서 사용하면 미화의 요정과 영구 계약합니다. 계약 이후 중복 획득한 계약서는 500골드로 교환됩니다.");
			}
			return true;
		}
	}
	return false;
}

int32 UCSArtifactSubsystem::GetCount(FName Id) const { return Counts.FindRef(Id); }
int32 UCSArtifactSubsystem::GetAppliedStacks(FName Id) const { return AppliedStacks.FindRef(Id); }
int32 UCSArtifactSubsystem::GetNextAvailableDay(FName Id) const { return NextAvailableDays.FindRef(Id); }

bool UCSArtifactSubsystem::IsFairyHousePlaced(const UObject* WorldContextObject) const
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World) return false;
	// 게임 인스턴스에 설치 여부를 저장하지 않고 현재 월드의 실제 설치 액터를 확인합니다.
	for (TActorIterator<ACSFairyHouse> It(World); It; ++It)
	{
		if (IsValid(*It)) return true;
	}
	return false;
}

float UCSArtifactSubsystem::GetSpeedMultiplier(ECSArtifactEffectType Effect) const
{
	float Bonus = 0.0f;
	for (const auto& Pair : AppliedStacks)
	{
		FCSJournalArtifactDataRow Definition;
		if (GetDefinition(Pair.Key, Definition) && Definition.Effect == Effect)
		{
			Bonus += FMath::Max(0.0f, Definition.EffectValue) * Pair.Value;
		}
	}
	return 1.0f + Bonus;
}

bool UCSArtifactSubsystem::Acquire(FName Id, int32 Quantity)
{
	FCSJournalArtifactDataRow Definition;
	if (Quantity <= 0 || !GetDefinition(Id, Definition) || Counts.FindRef(Id) > MAX_int32 - Quantity) return false;
	if (Definition.Effect == ECSArtifactEffectType::FairyContract && bFairyContractSigned)
	{
		// 계약 완료 후 중복 보상은 보유량을 늘리지 않고 골드로 교환합니다.
		ACSStoreGameState* State = GetGameInstance()->GetWorld() ? GetGameInstance()->GetWorld()->GetGameState<ACSStoreGameState>() : nullptr;
		if (!State || Quantity > MAX_int32 / 500 || !State->AddFunds(Quantity * 500)) return false;
		if (ACSStorePlayerController* Controller = Cast<ACSStorePlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
			Controller->ShowGameplayToast(NSLOCTEXT("Artifact", "DuplicateContract", "중복 계약서 교환"),
				FText::Format(NSLOCTEXT("Artifact", "DuplicateContractGold", "이미 계약한 요정의 계약서를 {0}골드로 교환했습니다."), FText::AsNumber(Quantity * 500)));
		OnArtifactsChanged.Broadcast();
		return true;
	}
	Counts.FindOrAdd(Id) += Quantity;
	if (UCSJournalSubsystem* Journal = GetGameInstance()->GetSubsystem<UCSJournalSubsystem>())
	{
		Journal->CollectArtifact(Id);
	}
	OnArtifactsChanged.Broadcast();
	return true;
}

bool UCSArtifactSubsystem::GetStatUseAmounts(FName Id, ACSPlayerCharacter* Player, int32& Requested, int32& Applied) const
{
	Requested = Applied = 0;
	FCSJournalArtifactDataRow Definition;
	const ACSStoreGameState* State = IsValid(Player) && Player->GetWorld() ? Player->GetWorld()->GetGameState<ACSStoreGameState>() : nullptr;
	if (!State || !GetDefinition(Id, Definition) ||
		(Definition.Effect != ECSArtifactEffectType::Reputation && Definition.Effect != ECSArtifactEffectType::Experience)) return false;
	Requested = FMath::Max(0, FMath::RoundToInt(Definition.EffectValue));
	Applied = FMath::Min(Requested, Definition.Effect == ECSArtifactEffectType::Reputation
		? State->GetRemainingReputation() : State->GetRemainingStoreExperience());
	return true;
}

bool UCSArtifactSubsystem::TryUse(FName Id, ACSPlayerCharacter* Player, int32 ConfirmedAppliedAmount)
{
	FCSJournalArtifactDataRow Definition;
	if (bUsingArtifact || !IsValid(Player) || !GetDefinition(Id, Definition) || GetCount(Id) < 1) return false;
	// 생성 중 블루프린트에서 사용 함수를 다시 호출해도 중복 설치나 중복 차감하지 않습니다.
	TGuardValue<bool> UseGuard(bUsingArtifact, true);
	UWorld* World = Player->GetWorld();
	ACSStoreGameState* State = World ? World->GetGameState<ACSStoreGameState>() : nullptr;
	if (!State) return false;
	const int32 Day = State->CurrentBusinessDay;
	if (Day < GetNextAvailableDay(Id)) return false;
	int32 Requested = 0;
	int32 Applied = 0;
	if (GetStatUseAmounts(Id, Player, Requested, Applied))
	{
		if (Applied <= 0 || (Applied < Requested && ConfirmedAppliedAmount != Applied))
		{
			if (ACSStorePlayerController* Controller = Cast<ACSStorePlayerController>(Player->GetController()))
			{
				const FText Message = Applied > 0
					? NSLOCTEXT("Artifact", "ConfirmOverflow", "효과가 일부만 적용됩니다. 아티팩트 도감에서 적용량을 확인한 후 사용해 주세요.")
					: Definition.Effect == ECSArtifactEffectType::Reputation
						? NSLOCTEXT("Artifact", "ReputationFull", "평판이 최대치여서 사용할 수 없습니다.")
						: NSLOCTEXT("Artifact", "ExperienceFull", "상점이 최고 레벨에 도달하여 사용할 수 없습니다.");
				Controller->ShowGameplayToast(FText::Format(NSLOCTEXT("Artifact", "UseUnavailable", "{0} 사용 불가"), Definition.DisplayName), Message);
			}
			return false;
		}
	}
	bool bSucceeded = false;
	bool bConsume = true;
	switch (Definition.Effect)
	{
	case ECSArtifactEffectType::Gold: bSucceeded = State->AddFunds(FMath::RoundToInt(Definition.EffectValue)); break;
	case ECSArtifactEffectType::Reputation: bSucceeded = State->AddReputation(FMath::RoundToInt(Definition.EffectValue), TEXT("아티팩트 사용")); break;
	case ECSArtifactEffectType::Experience: bSucceeded = State->AddStoreExperience(FMath::RoundToInt(Definition.EffectValue)); break;
	case ECSArtifactEffectType::PlayerSpeed:
	case ECSArtifactEffectType::EmployeeSpeed:
	case ECSArtifactEffectType::CustomerSpeed:
		bSucceeded = Definition.EffectValue > 0.0f && AppliedStacks.FindRef(Id) < MAX_int32;
		if (bSucceeded)
		{
			++AppliedStacks.FindOrAdd(Id);
			if (Definition.Effect == ECSArtifactEffectType::PlayerSpeed) Player->RefreshArtifactMoveSpeed();
			else if (Definition.Effect == ECSArtifactEffectType::EmployeeSpeed)
			{
				for (TActorIterator<ACSStoreEmployeeCharacter> It(World); It; ++It) It->RefreshArtifactMoveSpeed();
			}
			else
			{
				for (TActorIterator<ACSCustomerCharacter> It(World); It; ++It) It->RefreshArtifactMoveSpeed();
			}
		}
		break;
	case ECSArtifactEffectType::SummonCart:
	{
		UClass* ActorClass = Definition.SpawnActorClass.LoadSynchronous();
		if (ActorClass && ActorClass->IsChildOf(ACSCartActor::StaticClass()))
		{
			const ACSCartActor* CartDefaults = Cast<ACSCartActor>(ActorClass->GetDefaultObject());
			const float GroundOffset = CartDefaults ? CartDefaults->GetSpawnGroundOffset() : 5.0f;
			const FBoxSphereBounds CartBounds = CartDefaults
				? CartDefaults->GetSpawnMeshBounds()
				: FBoxSphereBounds(FVector::ZeroVector, FVector::ZeroVector, 0.0f);
			const FVector Forward = Player->GetActorForwardVector().GetSafeNormal2D();
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
			const FVector Directions[] = {
				Forward, (Forward + Right * 0.75f).GetSafeNormal(),
				(Forward - Right * 0.75f).GetSafeNormal(), Right, -Right, -Forward
			};
			FCollisionQueryParams QueryParams;
			QueryParams.AddIgnoredActor(Player);
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
			const FRotator Rotation(0.0f, Player->GetActorRotation().Yaw, 0.0f);
			// 고정된 한 지점 대신 주변 바닥을 확인하고 빈 자리에서만 수레를 생성합니다.
			for (const float Distance : {250.0f, 350.0f, 450.0f})
			{
				for (const FVector& Direction : Directions)
				{
					const FVector Candidate = Player->GetActorLocation() + Direction * Distance;
					FHitResult GroundHit;
					if (!World->LineTraceSingleByChannel(GroundHit,
						Candidate + FVector(0.0f, 0.0f, 150.0f),
						Candidate - FVector(0.0f, 0.0f, 400.0f), ECC_Visibility, QueryParams) ||
						GroundHit.ImpactNormal.Z < 0.6f)
					{
						continue;
					}
					const FVector Location = GroundHit.ImpactPoint + FVector::UpVector * GroundOffset;
					if (CartBounds.BoxExtent.GetMax() > 1.0f &&
						World->OverlapBlockingTestByChannel(
							Location + Rotation.RotateVector(CartBounds.Origin), Rotation.Quaternion(),
							ECC_WorldDynamic, FCollisionShape::MakeBox(CartBounds.BoxExtent), QueryParams))
					{
						continue;
					}
					if (ACSCartActor* Cart = World->SpawnActor<ACSCartActor>(ActorClass, Location, Rotation, Params); IsValid(Cart))
					{
						// [백무송]
						// 세이브 시 레벨 배치 수레와 구분하도록 런타임 식별자를 발급
						UCSSaveIdComponent::FindOrAddComponent(Cart);
						bSucceeded = true;
						break;
					}
				}
				if (bSucceeded) break;
			}
			if (!bSucceeded)
			{
				if (ACSStorePlayerController* Controller = Cast<ACSStorePlayerController>(Player->GetController()))
				{
					Controller->ShowGameplayToast(
						NSLOCTEXT("Artifact", "CartSummonFailed", "수레 소환 실패"),
						NSLOCTEXT("Artifact", "CartNeedsSpace", "주변에 수레를 놓을 공간이 없습니다."));
				}
			}
		}
		break;
	}
	case ECSArtifactEffectType::ChangeWeather:
	{
		bConsume = false;
		if (UCSWeatherSubsystem* Weather = World->GetSubsystem<UCSWeatherSubsystem>())
		{
			const int32 FixedClearDays = Weather->GetFixedClearBusinessDays();
			if (FixedClearDays > 0 && Weather->GetCurrentBusinessDay() <= FixedClearDays)
			{
				if (ACSStorePlayerController* Controller = Cast<ACSStorePlayerController>(Player->GetController()))
				{
					const FText Message = FixedClearDays == 1
						? NSLOCTEXT("Artifact", "WeatherStoneFirstDay", "첫 영업일에는 맑은 날씨가 유지됩니다. 2일차부터 사용할 수 있습니다.")
						: FText::Format(NSLOCTEXT("Artifact", "WeatherStoneFixedClear", "{0}일차까지는 맑은 날씨가 유지됩니다. {1}일차부터 사용할 수 있습니다."),
							FText::AsNumber(FixedClearDays), FText::AsNumber(FixedClearDays + 1));
					Controller->ShowGameplayToast(NSLOCTEXT("Artifact", "WeatherStoneUnavailable", "날씨의 돌 사용 불가"), Message);
				}
				// 고정 맑음 기간에는 효과를 실행하지 않고 수량과 쿨타임을 유지합니다.
				return false;
			}
			const ECSWeatherType Candidates[] = {ECSWeatherType::Clear, ECSWeatherType::Rain, ECSWeatherType::Snow};
			for (int32 Offset = 0; Offset < UE_ARRAY_COUNT(Candidates); ++Offset)
			{
				const ECSWeatherType Candidate = Candidates[(FMath::RandRange(0, 2) + Offset) % 3];
				if (Candidate != Weather->GetCurrentWeather() && Weather->SetForecastWeather(0, Candidate))
				{
					bSucceeded = true;
					break;
				}
			}
		}
		break;
	}
	case ECSArtifactEffectType::FairyHouse:
	case ECSArtifactEffectType::FairyContract:
	{
		FText Failure;
		ACSFairyHouse* Target = nullptr;
		int32 HouseCount = 0;
		for (TActorIterator<ACSFairyHouse> It(World); It; ++It)
		{
			if (IsValid(*It)) { Target = *It; ++HouseCount; }
		}
		if (bFairyContractSigned)
		{
			Failure = NSLOCTEXT("Artifact", "FairyAlreadyContracted", "미화의 요정과 이미 계약했습니다.");
		}
		else if (HouseCount != 1)
		{
			Failure = NSLOCTEXT("Artifact", "FairyHouseMissing", "레벨에 요정의 집을 하나 배치해 주세요. 계약서는 유지됩니다.");
		}
		else
		{
			bSucceeded = Target->BeginContractIntroduction(Cast<ACSStorePlayerController>(Player->GetController()));
			if (bSucceeded) bFairyContractSigned = true;
			else Failure = NSLOCTEXT("Artifact", "FairyIntroductionFailed", "요정 등장 연출을 시작할 수 없습니다. 계약서는 유지됩니다.");
		}
		if (!bSucceeded)
			if (ACSStorePlayerController* Controller = Cast<ACSStorePlayerController>(Player->GetController()))
				Controller->ShowGameplayToast(NSLOCTEXT("Artifact", "FairyContractUse", "요정 계약"), Failure);
		break;
	}
	case ECSArtifactEffectType::None:
	default: return false;
	}
	if (!bSucceeded) return false;
	if (bConsume) --Counts.FindOrAdd(Id);
	if (Definition.Effect == ECSArtifactEffectType::FairyContract && bFairyContractSigned)
	{
		// 계약 전에 여러 장을 모으셨다면 사용한 한 장을 제외한 나머지도 교환합니다.
		const int32 DuplicateCount = GetCount(Id);
		if (DuplicateCount > 0 && DuplicateCount <= MAX_int32 / 500 && State->AddFunds(DuplicateCount * 500))
			Counts.Add(Id, 0);
	}
	if (Definition.CooldownDays > 0) NextAvailableDays.Add(Id, Day + Definition.CooldownDays);
	OnArtifactsChanged.Broadcast();
	return true;
}

void UCSArtifactSubsystem::ResetArtifacts()
{
	Counts.Reset();
	AppliedStacks.Reset();
	NextAvailableDays.Reset();
	bFairyContractSigned = false;
	if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		for (TActorIterator<ACSPlayerCharacter> It(World); It; ++It) It->RefreshArtifactMoveSpeed();
		for (TActorIterator<ACSStoreEmployeeCharacter> It(World); It; ++It) It->RefreshArtifactMoveSpeed();
		for (TActorIterator<ACSCustomerCharacter> It(World); It; ++It) It->RefreshArtifactMoveSpeed();
	}
	OnArtifactsChanged.Broadcast();
}
