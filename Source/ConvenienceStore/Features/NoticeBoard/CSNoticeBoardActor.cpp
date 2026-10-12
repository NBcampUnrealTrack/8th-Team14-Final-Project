#include "Features/NoticeBoard/CSNoticeBoardActor.h"

#include "Blueprint/UserWidget.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Features/NoticeBoard/UI/CSNoticeBoardWidget.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Systems/NoticeEvents/CSNoticeEventSubsystem.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"

ACSNoticeBoardActor::ACSNoticeBoardActor()
{
	PrimaryActorTick.bCanEverTick = false;

	BoardMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BoardMesh"));
	SetRootComponent(BoardMesh);
	BoardMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	DisplayWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("DisplayWidgetComponent"));
	DisplayWidgetComponent->SetupAttachment(BoardMesh);
	DisplayWidgetComponent->SetWidgetSpace(EWidgetSpace::World);
	DisplayWidgetComponent->SetDrawAtDesiredSize(false);
	DisplayWidgetComponent->SetDrawSize(FVector2D(800.0f, 450.0f));
	DisplayWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DisplayWidgetComponent->SetTwoSided(true);

	InteractionCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionCollision"));
	InteractionCollision->SetupAttachment(BoardMesh);
	InteractionCollision->SetBoxExtent(FVector(120.0f, 60.0f, 140.0f));
	InteractionCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void ACSNoticeBoardActor::Interact_Implementation(AActor* Interactor)
{
	if (!IsValid(NoticeBoardWidgetClass))
	{
		return;
	}

	APlayerController* PlayerController = nullptr;
	if (const APawn* InteractingPawn = Cast<APawn>(Interactor))
	{
		PlayerController = Cast<APlayerController>(InteractingPawn->GetController());
	}
	if (!IsValid(PlayerController))
	{
		return;
	}

	if (IsValid(ActiveNoticeBoardWidget))
	{
		CompleteCloseNoticeBoardUI(ActiveNoticeBoardWidget);
	}

	ActiveNoticeBoardWidget = CreateWidget<UCSNoticeBoardWidget>(
		PlayerController,
		NoticeBoardWidgetClass);
	if (!IsValid(ActiveNoticeBoardWidget))
	{
		return;
	}

	ActiveNoticeBoardWidget->InitializeNoticeBoard(this);
	ActiveNoticeBoardWidget->AddToViewport();
	if (ACSStorePlayerController* StorePlayerController = Cast<ACSStorePlayerController>(PlayerController))
	{
		StorePlayerController->SetGameplayUIInputLock(ActiveNoticeBoardWidget, true);
	}
}

FText ACSNoticeBoardActor::GetInteractionText_Implementation() const
{
	return NSLOCTEXT("NoticeBoard", "OpenNoticeBoard", "게시판 확인");
}

void ACSNoticeBoardActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(ActiveNoticeBoardWidget))
	{
		CompleteCloseNoticeBoardUI(ActiveNoticeBoardWidget);
	}
	Super::EndPlay(EndPlayReason);
}

void ACSNoticeBoardActor::CloseNoticeBoardUI(UCSNoticeBoardWidget* RequestingWidget)
{
	if (!IsValid(RequestingWidget) || RequestingWidget != ActiveNoticeBoardWidget)
	{
		return;
	}

	RequestingWidget->CloseNoticeBoardWindow();
}

void ACSNoticeBoardActor::CompleteCloseNoticeBoardUI(UCSNoticeBoardWidget* RequestingWidget)
{
	if (!IsValid(RequestingWidget) || RequestingWidget != ActiveNoticeBoardWidget)
	{
		return;
	}

	if (ACSStorePlayerController* StorePlayerController =
		Cast<ACSStorePlayerController>(RequestingWidget->GetOwningPlayer()))
	{
		StorePlayerController->SetGameplayUIInputLock(RequestingWidget, false);
	}
	ActiveNoticeBoardWidget = nullptr;
	// NativeDestruct 또는 레벨 정리 중에는 이미 부모에서 제거된 상태일 수 있습니다.
	if (RequestingWidget->IsInViewport())
	{
		RequestingWidget->RemoveFromParent();
	}
}

bool ACSNoticeBoardActor::IsNoticeBoardOpen() const
{
	return IsValid(ActiveNoticeBoardWidget);
}

bool ACSNoticeBoardActor::HasTodayNews() const
{
	const UWorld* World = GetWorld();
	const UCSNoticeEventSubsystem* NoticeEventSubsystem =
		World ? World->GetSubsystem<UCSNoticeEventSubsystem>() : nullptr;
	return IsValid(NoticeEventSubsystem) && NoticeEventSubsystem->HasTodayNews();
}

bool ACSNoticeBoardActor::GetTodayNews(FCSNewsEventDataRow& OutNews) const
{
	OutNews = FCSNewsEventDataRow();
	const UWorld* World = GetWorld();
	const UCSNoticeEventSubsystem* NoticeEventSubsystem =
		World ? World->GetSubsystem<UCSNoticeEventSubsystem>() : nullptr;
	return IsValid(NoticeEventSubsystem) && NoticeEventSubsystem->GetTodayNews(OutNews);
}

bool ACSNoticeBoardActor::HasTodayHoliday() const
{
	const UWorld* World = GetWorld();
	const UCSNoticeEventSubsystem* NoticeEventSubsystem =
		World ? World->GetSubsystem<UCSNoticeEventSubsystem>() : nullptr;
	return IsValid(NoticeEventSubsystem) && NoticeEventSubsystem->HasTodayHoliday();
}

bool ACSNoticeBoardActor::GetTodayHoliday(FCSHolidayEventDataRow& OutHoliday) const
{
	OutHoliday = FCSHolidayEventDataRow();
	const UWorld* World = GetWorld();
	const UCSNoticeEventSubsystem* NoticeEventSubsystem =
		World ? World->GetSubsystem<UCSNoticeEventSubsystem>() : nullptr;
	return IsValid(NoticeEventSubsystem) && NoticeEventSubsystem->GetTodayHoliday(OutHoliday);
}

void ACSNoticeBoardActor::GetTodayHolidays(TArray<FCSHolidayEventDataRow>& OutHolidays) const
{
	OutHolidays.Reset();
	const UWorld* World = GetWorld();
	const UCSNoticeEventSubsystem* NoticeEventSubsystem =
		World ? World->GetSubsystem<UCSNoticeEventSubsystem>() : nullptr;
	if (IsValid(NoticeEventSubsystem))
	{
		NoticeEventSubsystem->GetTodayHolidays(OutHolidays);
	}
}
