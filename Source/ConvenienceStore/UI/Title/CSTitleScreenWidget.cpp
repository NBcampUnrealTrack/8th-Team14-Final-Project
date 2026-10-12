#include "UI/Title/CSTitleScreenWidget.h"

#include "Components/Button.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Systems/Audio/CSAudioSubsystem.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#include "UI/Title/CSTitleSaveMenuWidget.h"

UCSTitleScreenWidget::UCSTitleScreenWidget(const FObjectInitializer& Initializer) : Super(Initializer)
{
	static ConstructorHelpers::FObjectFinder<UCSAudioConfigDataAsset> Config(
		TEXT("/Game/ConvenienceStore/Audio/DA_AudioConfig.DA_AudioConfig"));
	TitleAudioConfig = Config.Object;
}

void UCSTitleScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	BTN_Start->OnClicked.AddUniqueDynamic(this, &UCSTitleScreenWidget::HandleStartClicked);
	BTN_Quit->OnClicked.AddUniqueDynamic(this, &UCSTitleScreenWidget::HandleQuitClicked);
	if (GetWorld() && GetWorld()->IsGameWorld() &&
		UGameplayStatics::GetCurrentLevelName(this, true) == TEXT("GrayBoxMap"))
	{
		if (UCSAudioSubsystem* Audio = UCSAudioSubsystem::Get(this);
			Audio && Audio->InitializeAudio(TitleAudioConfig))
		{
			Audio->PlayBGMTrack(TEXT("MainMenu"));
		}
	}
}

void UCSTitleScreenWidget::NativeDestruct()
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(QuitSoundTimer);
	Super::NativeDestruct();
}

void UCSTitleScreenWidget::HandleStartClicked()
{
	// 메뉴가 이미 열려 있거나 메뉴 클래스가 지정되지 않으면 중복 생성을 방지
	if (bIsQuitting || IsValid(ActiveSaveMenu) || SaveMenuClass == nullptr)
	{
		return;
	}

	ActiveSaveMenu = CreateWidget<UCSTitleSaveMenuWidget>(GetOwningPlayer(), SaveMenuClass);
	if (!IsValid(ActiveSaveMenu))
	{
		return;
	}

	// 타이틀 화면 위에 표시되도록 높은 ZOrder로 추가
	ActiveSaveMenu->OnMenuClosed.AddUniqueDynamic(this, &UCSTitleScreenWidget::HandleSaveMenuClosed);
	ActiveSaveMenu->AddToViewport(10);
	if (UCSAudioSubsystem* Audio = UCSAudioSubsystem::Get(this)) Audio->PlaySFX(ECSAudioEvent::MenuButton);
}

void UCSTitleScreenWidget::HandleSaveMenuClosed()
{
	// 바인딩을 해제한 뒤 메뉴를 화면에서 제거
	if (IsValid(ActiveSaveMenu))
	{
		ActiveSaveMenu->OnMenuClosed.RemoveDynamic(this, &UCSTitleScreenWidget::HandleSaveMenuClosed);
		ActiveSaveMenu->RemoveFromParent();
	}

	ActiveSaveMenu = nullptr;
}

void UCSTitleScreenWidget::HandleQuitClicked()
{
	if (bIsQuitting) return;
	bIsQuitting = true;
	if (UCSAudioSubsystem* Audio = UCSAudioSubsystem::Get(this);
		Audio && Audio->PlaySFX(ECSAudioEvent::MenuButton))
	{
		// 프로세스 종료가 클릭음을 잘라 버리지 않도록 이 짧은 효과음만 끝까지 재생합니다.
		const float Duration = Audio->GetSFXDuration(ECSAudioEvent::MenuButton);
		if (Duration > 0.0f && GetWorld())
		{
			GetWorld()->GetTimerManager().SetTimer(QuitSoundTimer, this, &ThisClass::FinishQuit, Duration + 0.05f, false);
			return;
		}
	}
	FinishQuit();
}

void UCSTitleScreenWidget::FinishQuit()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}
