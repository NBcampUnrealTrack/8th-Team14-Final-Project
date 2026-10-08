#include "Features/Journal/CSJournalWorldActors.h"
#include "Data/CSJournalSettings.h"
#include "Systems/Journal/CSJournalSubsystem.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"

namespace
{
FText GetJournalEntryTitle(const UCSJournalSubsystem* Journal, FName EntryId)
{
	if (Journal != nullptr && Journal->Catalog != nullptr)
	{
		for (const FCSJournalEntry& Entry : Journal->Catalog->RaceEntries)
		{
			if (Entry.EntryId == EntryId)
			{
				return Entry.Title;
			}
		}
	}
	return FText::FromName(EntryId);
}

void ShowDiscoveryToast(ACSStorePlayerController* Controller, const TArray<FText>& UnlockedTitles)
{
	if (Controller == nullptr || UnlockedTitles.IsEmpty())
	{
		return;
	}

	if (UnlockedTitles.Num() == 1)
	{
		Controller->ShowGameplayToast(
			NSLOCTEXT("Journal", "DiscoveryToastTitle", "새 기록 추가"),
			FText::Format(NSLOCTEXT("Journal", "DiscoveryToastSingle",
									"{0} 기록이 추가되었습니다.\nTab 키를 눌러 수첩에서 확인하세요."),
						  UnlockedTitles[0]));
		return;
	}

	Controller->ShowGameplayToast(
		NSLOCTEXT("Journal", "DiscoveryToastTitle", "새 기록 추가"),
		FText::Format(NSLOCTEXT("Journal", "DiscoveryToastMultiple",
								"새 기록 {0}개가 추가되었습니다.\nTab 키를 눌러 수첩에서 확인하세요."),
					  FText::AsNumber(UnlockedTitles.Num())));
}
} // namespace

// 로컬 플레이어의 유효한 상호작용에서 지정된 정보를 해금하고 새 기록만 한 번에 알립니다.
void ACSJournalDiscoveryPoint::Interact_Implementation(AActor* Interactor)
{
	const APawn* Pawn = Cast<APawn>(Interactor);
	if (!Pawn || !Pawn->IsLocallyControlled() || !GetWorld()->GetGameInstance())
		return;
	auto* Journal = GetWorld()->GetGameInstance()->GetSubsystem<UCSJournalSubsystem>();
	TArray<FText> UnlockedTitles;
	for (FName Id : EntryIds)
	{
		if (Journal->UnlockEntry(Id))
		{
			UnlockedTitles.Add(GetJournalEntryTitle(Journal, Id));
		}
	}

	ShowDiscoveryToast(Cast<ACSStorePlayerController>(Pawn->GetController()), UnlockedTitles);
}

bool ACSJournalDiscoveryPoint::IsDiscoveryComplete() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const UGameInstance* GameInstance = World->GetGameInstance())
		{
			const UCSJournalSubsystem* Journal = GameInstance->GetSubsystem<UCSJournalSubsystem>();
			if (Journal != nullptr && !EntryIds.IsEmpty())
			{
				for (const FName EntryId : EntryIds)
				{
					if (!Journal->IsEntryUnlocked(EntryId))
					{
						return false;
					}
				}
				return true;
			}
		}
	}

	return false;
}

TArray<FString> ACSJournalDiscoveryPoint::GetEntryIdOptions() const
{
	TArray<FString> Options;
	if (const auto* Catalog = GetDefault<UCSJournalSettings>()->Catalog.LoadSynchronous())
		for (const auto& Entry : Catalog->RaceEntries)
			if (!Entry.EntryId.IsNone())
				Options.Add(Entry.EntryId.ToString());
	return Options;
}
// 발견 지점의 조준 판정과 임시 외형을 구성합니다. 외형은 블루프린트 자식에서 변경하실 수 있습니다.
ACSJournalDiscoveryPoint::ACSJournalDiscoveryPoint()
{
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	InteractionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBox"));
	InteractionBox->SetupAttachment(SceneRoot);
	InteractionBox->SetBoxExtent(FVector(35, 35, 65));
	InteractionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	DiscoveryMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DiscoveryMesh"));
	DiscoveryMesh->SetupAttachment(SceneRoot);
	DiscoveryMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DiscoveryMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	DiscoveryMesh->SetRelativeScale3D(FVector(.12, .6, .9));
	InteractionText = FText::FromString(TEXT("기록 조사하기"));
}
FText ACSJournalDiscoveryPoint::GetInteractionText_Implementation() const
{
	if (IsDiscoveryComplete())
	{
		return NSLOCTEXT("Journal", "DiscoveryCompletePrompt", "이미 기록을 완료했습니다.");
	}
	return InteractionText;
}
ACSJournalMapMarker::ACSJournalMapMarker()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}
ACSJournalMapSettings::ACSJournalMapSettings()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

// 알려진 건물 메시를 찾아 기본 지도 표식을 구성하며 직접 배치한 표식을 우선합니다.
void CSConfigureJournalWorld(UWorld* World)
{
	struct FBuilding
	{
		FVector Center;
		FString Name;
		ECSJournalRace Race;
	};
	TArray<FBuilding> Buildings;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->IsA<ACSJournalMapMarker>() || It->IsA<ACSJournalDiscoveryPoint>())
			continue;
		TInlineComponentArray<UStaticMeshComponent*> Meshes;
		It->GetComponents(Meshes);
		for (auto* Mesh : Meshes)
		{
			if (!Mesh->GetStaticMesh() || !Mesh->IsVisible())
				continue;
			FString Asset = Mesh->GetStaticMesh()->GetName();
			FBuilding B;
			if (Asset == TEXT("GoblinSchool"))
			{
				B.Name = TEXT("고블린 학교");
				B.Race = ECSJournalRace::Goblin;
			}
			else if (Asset == TEXT("ElfTraining"))
			{
				B.Name = TEXT("엘프 훈련장");
				B.Race = ECSJournalRace::Elf;
			}
			else if (Asset == TEXT("MagicSchool"))
			{
				B.Name = TEXT("마법 학교");
				B.Race = ECSJournalRace::Wizard;
			}
			else if (Asset == TEXT("Recruitment"))
			{
				B.Name = TEXT("고용소");
				B.Race = ECSJournalRace::Human;
			}
			else if (Asset == TEXT("potion_shop"))
			{
				B.Name = TEXT("물약 상점");
				B.Race = ECSJournalRace::Wizard;
			}
			else if (Asset == TEXT("AstroShop"))
			{
				B.Name = TEXT("AstroShop");
				B.Race = ECSJournalRace::Human;
			}
			else
				continue;
			B.Center = Mesh->Bounds.Origin;
			if (!Buildings.ContainsByPredicate([&](const FBuilding& Other)
											   { return FVector::DistSquared2D(Other.Center, B.Center) < 10000; }))
				Buildings.Add(B);
		}
	}
	for (const auto& B : Buildings)
	{
		bool Authored = false;
		for (TActorIterator<ACSJournalMapMarker> It(World); It; ++It)
			if (FVector::DistSquared2D(It->GetActorLocation(), B.Center) < 250000)
			{
				Authored = true;
				break;
			}
		// 직접 배치한 표식이 있으면 해당 건물의 자동 표식 생성을 생략합니다.
		if (Authored)
			continue;
		if (auto* Marker = World->SpawnActor<ACSJournalMapMarker>(B.Center, FRotator::ZeroRotator))
		{
			Marker->BuildingName = FText::FromString(B.Name);
			Marker->Race = B.Race;
		}
	}
}
