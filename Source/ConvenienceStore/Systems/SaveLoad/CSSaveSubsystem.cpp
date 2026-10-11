#include "Systems/SaveLoad/CSSaveSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/SoftObjectPath.h"

#include "Systems/Journal/CSJournalSubsystem.h"
#include "Systems/Audio/CSAudioSubsystem.h"
#include "Systems/SaveLoad/CSSaveGame.h"
#include "Systems/SaveLoad/CSSaveParticipant.h"

void UCSSaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PendingLoadData = nullptr;
	BasePlayTimeSeconds = 0.0f;
	ActiveSlotName = NAME_None;
}

void UCSSaveSubsystem::Deinitialize()
{
	PendingLoadData = nullptr;
	Super::Deinitialize();
}

void UCSSaveSubsystem::CollectParticipants(
	UWorld& InWorld,
	TArray<UObject*>& OutParticipants) const
{
	OutParticipants.Reset();

	// 인터페이스를 구현한 객체만 참가자로 추가
	auto AddIfParticipant = [&OutParticipants](UObject* Candidate)
	{
		if (IsValid(Candidate) && Cast<ICSSaveParticipant>(Candidate) != nullptr)
		{
			OutParticipants.Add(Candidate);
		}
	};

	AddIfParticipant(InWorld.GetGameState());

	// UE 5.8에서 GetSubsystemArray가 제거되어 ForEachSubsystem으로 월드 서브시스템을 순회
	InWorld.ForEachSubsystem<UWorldSubsystem>(
		[&AddIfParticipant](UWorldSubsystem* WorldSubsystem)
		{
			AddIfParticipant(WorldSubsystem);
		});

	// 레벨 전환 후에도 유지되는 수첩 등 게임 인스턴스 서브시스템을 수집
	if (UGameInstance* OwningGameInstance = GetGameInstance())
	{
		OwningGameInstance->ForEachSubsystem<UGameInstanceSubsystem>(
			[&AddIfParticipant](UGameInstanceSubsystem* GameInstanceSubsystem)
			{
				AddIfParticipant(GameInstanceSubsystem);
			});
	}

	// 복원 우선순위가 낮은 참가자부터 처리하도록 정렬
	OutParticipants.Sort(
		[](const UObject& Left, const UObject& Right)
		{
			const ICSSaveParticipant* LeftParticipant = Cast<ICSSaveParticipant>(&Left);
			const ICSSaveParticipant* RightParticipant = Cast<ICSSaveParticipant>(&Right);
			// 인터페이스를 조회하지 못하면 서브시스템 단계(5)의 기본 우선순위를 사용
			int32 LeftPriority = 5;
			if (LeftParticipant != nullptr)
			{
				LeftPriority = LeftParticipant->GetSaveRestorePriority();
			}

			int32 RightPriority = 5;
			if (RightParticipant != nullptr)
			{
				RightPriority = RightParticipant->GetSaveRestorePriority();
			}

			return LeftPriority < RightPriority;
		});
}

void UCSSaveSubsystem::FillHeader(
	UWorld& InWorld,
	UCSSaveGame& InOutSave,
	FName SlotName) const
{
	FCSSaveHeader& Header = InOutSave.Header;
	Header.SaveVersion = CS_SAVE_VERSION_CURRENT;
	Header.SlotName = SlotName;
	Header.bIsAutoSave = SlotName == GetAutoSaveSlotName();
	Header.SavedAtUtc = FDateTime::UtcNow();
	Header.PlayTimeSeconds = GetTotalPlayTimeSeconds();

	// 월드 오브젝트 경로를 기록해 TSoftObjectPtr<UWorld>로 그대로 열 수 있도록 설정
	Header.LevelPath = FSoftObjectPath(&InWorld);

	// GameState 레코드 캡처가 끝난 뒤이므로 요약값을 그대로 복사
	Header.BusinessDay = InOutSave.StoreState.CurrentBusinessDay;
	Header.Funds = InOutSave.StoreState.CurrentFunds;
	Header.StoreLevel = InOutSave.StoreState.CurrentStoreLevel;
}

bool UCSSaveSubsystem::SaveToSlot(FName SlotName)
{
	if (SlotName.IsNone())
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CSSave] 월드를 찾을 수 없어 저장을 중단합니다."));
		return false;
	}

	UCSSaveGame* SaveGame = NewObject<UCSSaveGame>(this, UCSSaveGame::StaticClass());
	if (!IsValid(SaveGame))
	{
		return false;
	}

	SaveGame->SaveVersion = CS_SAVE_VERSION_CURRENT;

	// 각 참가자가 자기 블록을 기록
	TArray<UObject*> Participants;
	CollectParticipants(*World, Participants);
	for (UObject* Participant : Participants)
	{
		if (const ICSSaveParticipant* SaveParticipant = Cast<ICSSaveParticipant>(Participant))
		{
			SaveParticipant->CaptureSaveData(*SaveGame);
		}
	}

	FillHeader(*World, *SaveGame, SlotName);

	// 쓰기 전에 기존 슬롯을 백업해 쓰기 중 손상에 대비
	BackupSlot(SlotName);

	const bool bSaved = UGameplayStatics::SaveGameToSlot(SaveGame, ToSlotString(SlotName), 0);
	if (!bSaved)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CSSave] 슬롯 저장에 실패했습니다: %s"), *SlotName.ToString());
		return false;
	}

	UpdateMetaHeader(SaveGame->Header);
	OnSaveCompleted.Broadcast(SlotName);

	UE_LOG(LogTemp, Log,
		TEXT("[CSSave] 저장 완료: %s (%d일차, 자금 %d)"),
		*SlotName.ToString(),
		SaveGame->StoreState.CurrentBusinessDay,
		SaveGame->StoreState.CurrentFunds);
	return true;
}

bool UCSSaveSubsystem::SaveToAutoSlot()
{
	// 타이틀에서 선택한 슬롯이 있으면 해당 슬롯에 저장
	if (HasActiveSlot())
	{
		return SaveToSlot(ActiveSlotName);
	}

	// 타이틀을 거치지 않은 실행은 자동 세이브 슬롯에 저장
	return SaveToSlot(GetAutoSaveSlotName());
}

bool UCSSaveSubsystem::StartNewGame(FName SlotName, TSoftObjectPtr<UWorld> GameLevel)
{
	UWorld* World = GetWorld();
	if (SlotName.IsNone() || World == nullptr || GameLevel.IsNull())
	{
		return false;
	}

	// 이전 슬롯에서 이어받은 세이브와 플레이 시간을 초기화
	// 같은 세션에서 다른 슬롯을 진행한 뒤 새 게임을 시작하는 경우를 처리
	PendingLoadData = nullptr;
	BasePlayTimeSeconds = 0.0f;
	ActiveSlotName = SlotName;

	// 레벨 전환 후에도 유지되는 수첩과 아티팩트를 초기화
	// ResetJournal에서 아티팩트 초기화까지 함께 처리
	if (UGameInstance* OwningGameInstance = GetGameInstance())
	{
		if (UCSAudioSubsystem* Audio = OwningGameInstance->GetSubsystem<UCSAudioSubsystem>())
		{
			Audio->ResetBGMSelection();
		}
		if (UCSJournalSubsystem* Journal = OwningGameInstance->GetSubsystem<UCSJournalSubsystem>())
		{
			Journal->ResetJournal();
		}
	}

	UGameplayStatics::OpenLevelBySoftObjectPtr(World, GameLevel);
	return true;
}

bool UCSSaveSubsystem::LoadFromSlot(FName SlotName)
{
	if (SlotName.IsNone())
	{
		return false;
	}

	UCSSaveGame* LoadedSave = LoadSaveGameWithFallback(SlotName);
	if (!IsValid(LoadedSave))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CSSave] 슬롯을 읽을 수 없습니다: %s"), *SlotName.ToString());
		OnLoadFailed.Broadcast(SlotName);
		return false;
	}

	// 마이그레이션 없이 버전이 다른 세이브의 로드를 거부
	if (!LoadedSave->IsVersionSupported())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CSSave] 지원하지 않는 세이브 버전입니다. 슬롯 %s, 버전 %d (현재 %d)"),
			*SlotName.ToString(),
			LoadedSave->SaveVersion,
			CS_SAVE_VERSION_CURRENT);
		OnLoadFailed.Broadcast(SlotName);
		return false;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		OnLoadFailed.Broadcast(SlotName);
		return false;
	}

	PendingLoadData = LoadedSave;
	BasePlayTimeSeconds = LoadedSave->Header.PlayTimeSeconds;
	// 이후 자동 저장이 불러온 슬롯에 기록되도록 현재 슬롯으로 설정
	ActiveSlotName = SlotName;

	// 헤더에 기록된 레벨을 열고 경로가 없으면 현재 레벨을 다시 열기
	const FSoftObjectPath& LevelPath = LoadedSave->Header.LevelPath;
	if (LevelPath.IsValid())
	{
		UGameplayStatics::OpenLevelBySoftObjectPtr(World, TSoftObjectPtr<UWorld>(LevelPath));
	}
	else
	{
		UGameplayStatics::OpenLevel(World, FName(*World->GetName()));
	}

	return true;
}

bool UCSSaveSubsystem::ApplyPendingLoad(UWorld& InWorld)
{
	if (!IsValid(PendingLoadData))
	{
		return false;
	}

	UCSSaveGame* SaveToApply = PendingLoadData;

	TArray<UObject*> Participants;
	CollectParticipants(InWorld, Participants);
	for (UObject* Participant : Participants)
	{
		if (ICSSaveParticipant* SaveParticipant = Cast<ICSSaveParticipant>(Participant))
		{
			SaveParticipant->RestoreSaveData(*SaveToApply);
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("[CSSave] 복원 완료: %d일차, 자금 %d, 참가자 %d개"),
		SaveToApply->StoreState.CurrentBusinessDay,
		SaveToApply->StoreState.CurrentFunds,
		Participants.Num());

	// 모든 참가자 복원이 끝난 뒤 대기 상태를 해제
	PendingLoadData = nullptr;
	return true;
}

TArray<FCSSaveHeader> UCSSaveSubsystem::GetAllSlotHeaders() const
{
	TArray<FCSSaveHeader> Result;

	// 전체 세이브 대신 경량 메타 슬롯만 조회
	if (const UCSSaveMetaGame* Meta = LoadOrCreateMeta())
	{
		Result = Meta->Headers;
	}

	// 최근 저장한 슬롯이 앞에 오도록 정렬
	Result.Sort(
		[](const FCSSaveHeader& Left, const FCSSaveHeader& Right)
		{
			return Left.SavedAtUtc > Right.SavedAtUtc;
		});
	return Result;
}

bool UCSSaveSubsystem::GetSlotHeader(FName SlotName, FCSSaveHeader& OutHeader) const
{
	if (const UCSSaveMetaGame* Meta = LoadOrCreateMeta())
	{
		if (const FCSSaveHeader* Found = Meta->FindHeader(SlotName))
		{
			OutHeader = *Found;
			return true;
		}
	}

	return false;
}

bool UCSSaveSubsystem::DoesSlotExist(FName SlotName) const
{
	return UGameplayStatics::DoesSaveGameExist(ToSlotString(SlotName), 0);
}

bool UCSSaveSubsystem::DeleteSlot(FName SlotName)
{
	const bool bDeleted = UGameplayStatics::DeleteGameInSlot(ToSlotString(SlotName), 0);
	UGameplayStatics::DeleteGameInSlot(MakeBackupSlotString(SlotName), 0);
	RemoveMetaHeader(SlotName);

	// 현재 슬롯을 삭제하면 지정을 해제해 이후 자동 저장을 자동 세이브 슬롯에 기록
	if (SlotName == ActiveSlotName)
	{
		ActiveSlotName = NAME_None;
	}

	return bDeleted;
}

float UCSSaveSubsystem::GetTotalPlayTimeSeconds() const
{
	const UWorld* World = GetWorld();
	float WorldSeconds = 0.0f;
	if (World != nullptr)
	{
		WorldSeconds = World->GetTimeSeconds();
	}

	return BasePlayTimeSeconds + WorldSeconds;
}

void UCSSaveSubsystem::BackupSlot(FName SlotName) const
{
	const FString SlotString = ToSlotString(SlotName);
	if (!UGameplayStatics::DoesSaveGameExist(SlotString, 0))
	{
		return;
	}

	// 재직렬화 없이 바이트 그대로 복사해 원본을 유지
	TArray<uint8> RawData;
	if (!UGameplayStatics::LoadDataFromSlot(RawData, SlotString, 0) || RawData.Num() <= 0)
	{
		return;
	}

	UGameplayStatics::SaveDataToSlot(RawData, MakeBackupSlotString(SlotName), 0);
}

UCSSaveGame* UCSSaveSubsystem::LoadSaveGameWithFallback(FName SlotName) const
{
	const FString SlotString = ToSlotString(SlotName);
	if (UGameplayStatics::DoesSaveGameExist(SlotString, 0))
	{
		if (UCSSaveGame* Loaded = Cast<UCSSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotString, 0)))
		{
			return Loaded;
		}

		UE_LOG(LogTemp, Warning, TEXT("[CSSave] 본 슬롯이 손상되어 백업으로 폴백합니다: %s"), *SlotString);
	}

	// 본 슬롯을 읽지 못하면 백업 슬롯을 조회
	const FString BackupSlotString = MakeBackupSlotString(SlotName);
	if (UGameplayStatics::DoesSaveGameExist(BackupSlotString, 0))
	{
		return Cast<UCSSaveGame>(UGameplayStatics::LoadGameFromSlot(BackupSlotString, 0));
	}

	return nullptr;
}

UCSSaveMetaGame* UCSSaveSubsystem::LoadOrCreateMeta() const
{
	const FString MetaSlotString = GetMetaSlotName().ToString();
	if (UGameplayStatics::DoesSaveGameExist(MetaSlotString, 0))
	{
		if (UCSSaveMetaGame* Loaded = Cast<UCSSaveMetaGame>(UGameplayStatics::LoadGameFromSlot(MetaSlotString, 0)))
		{
			return Loaded;
		}
	}

	return NewObject<UCSSaveMetaGame>(const_cast<UCSSaveSubsystem*>(this), UCSSaveMetaGame::StaticClass());
}

void UCSSaveSubsystem::UpdateMetaHeader(const FCSSaveHeader& InHeader) const
{
	UCSSaveMetaGame* Meta = LoadOrCreateMeta();
	if (!IsValid(Meta))
	{
		return;
	}

	Meta->UpsertHeader(InHeader);
	UGameplayStatics::SaveGameToSlot(Meta, GetMetaSlotName().ToString(), 0);
}

void UCSSaveSubsystem::RemoveMetaHeader(FName SlotName) const
{
	UCSSaveMetaGame* Meta = LoadOrCreateMeta();
	if (!IsValid(Meta))
	{
		return;
	}

	Meta->RemoveHeader(SlotName);
	UGameplayStatics::SaveGameToSlot(Meta, GetMetaSlotName().ToString(), 0);
}
