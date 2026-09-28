#include "Systems/Artifact/CSArtifactSubsystem.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "Features/Cart/CSCartActor.h"
#include "Features/Employees/CSStoreEmployeeCharacter.h"
#include "Features/Customers/CSCustomerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Systems/Journal/CSJournalSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "Systems/Weather/CSWeatherSubsystem.h"
#include "Data/CSWeatherTypes.h"

void UCSArtifactSubsystem::RegisterTable(UDataTable* InTable)
{
	if (IsValid(InTable) && InTable->GetRowStruct() &&
		InTable->GetRowStruct()->IsChildOf(FCSJournalArtifactDataRow::StaticStruct()))
	{
		ArtifactTable = InTable;
	}
}

bool UCSArtifactSubsystem::GetDefinition(FName Id, FCSJournalArtifactDataRow& OutRow) const
{
	if (!ArtifactTable || Id.IsNone()) return false;
	if (const auto* Row = ArtifactTable->FindRow<FCSJournalArtifactDataRow>(Id, TEXT("Artifact definition")))
	{
		OutRow = *Row;
		return true;
	}
	return false;
}

int32 UCSArtifactSubsystem::GetCount(FName Id) const { return Counts.FindRef(Id); }
int32 UCSArtifactSubsystem::GetAppliedStacks(FName Id) const { return AppliedStacks.FindRef(Id); }
int32 UCSArtifactSubsystem::GetNextAvailableDay(FName Id) const { return NextAvailableDays.FindRef(Id); }

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
	Counts.FindOrAdd(Id) += Quantity;
	if (UCSJournalSubsystem* Journal = GetGameInstance()->GetSubsystem<UCSJournalSubsystem>())
	{
		Journal->CollectArtifact(Id);
	}
	OnArtifactsChanged.Broadcast();
	return true;
}

bool UCSArtifactSubsystem::TryUse(FName Id, ACSPlayerCharacter* Player)
{
	FCSJournalArtifactDataRow Definition;
	if (!IsValid(Player) || !GetDefinition(Id, Definition) || GetCount(Id) < 1) return false;
	UWorld* World = Player->GetWorld();
	ACSStoreGameState* State = World ? World->GetGameState<ACSStoreGameState>() : nullptr;
	if (!State) return false;
	const int32 Day = State->CurrentBusinessDay;
	if (Day < GetNextAvailableDay(Id)) return false;
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
					if (IsValid(World->SpawnActor<ACSCartActor>(ActorClass, Location, Rotation, Params)))
					{
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
	// 요정의 집 설치와 소환은 담당 기능이 연결된 뒤 사용 가능합니다.
	case ECSArtifactEffectType::FairyHouse:
	case ECSArtifactEffectType::None:
	default: return false;
	}
	if (!bSucceeded) return false;
	if (bConsume) --Counts.FindOrAdd(Id);
	if (Definition.CooldownDays > 0) NextAvailableDays.Add(Id, Day + Definition.CooldownDays);
	OnArtifactsChanged.Broadcast();
	return true;
}

void UCSArtifactSubsystem::ResetArtifacts()
{
	Counts.Reset();
	AppliedStacks.Reset();
	NextAvailableDays.Reset();
	if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		for (TActorIterator<ACSPlayerCharacter> It(World); It; ++It) It->RefreshArtifactMoveSpeed();
		for (TActorIterator<ACSStoreEmployeeCharacter> It(World); It; ++It) It->RefreshArtifactMoveSpeed();
		for (TActorIterator<ACSCustomerCharacter> It(World); It; ++It) It->RefreshArtifactMoveSpeed();
	}
	OnArtifactsChanged.Broadcast();
}
