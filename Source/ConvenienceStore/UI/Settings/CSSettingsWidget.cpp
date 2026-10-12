#include "UI/Settings/CSSettingsWidget.h"

#include "Components/Button.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "Widgets/SWindow.h"

// 화면 구성은 WBP 디자이너 그대로 유지, 닫기 버튼 연결만 초기화.
void UCSSettingsWidget::NativeConstruct()
{
	SetIsFocusable(true);
	Super::NativeConstruct();
	if (CloseButton)
		CloseButton->OnClicked.AddUniqueDynamic(this, &UCSSettingsWidget::RequestClose);
}

void UCSSettingsWidget::NativeDestruct()
{
	if (CloseButton)
		CloseButton->OnClicked.RemoveDynamic(this, &UCSSettingsWidget::RequestClose);
	Super::NativeDestruct();
}

// 자식 버튼에 포커스가 있어도 Esc를 먼저 가로채 닫기 처리.
FReply UCSSettingsWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Escape)
	{
		RequestClose();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(Geometry, Event);
}

void UCSSettingsWidget::RequestClose()
{
	if (auto* Controller = Cast<ACSStorePlayerController>(GetOwningPlayer()))
		Controller->CloseSettings();
}

void UCSSettingsWidget::SetWindowModeIndex(int32 Index)
{
	UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!Settings)
	{
		return;
	}

	const EWindowMode::Type Mode = Index == 0 ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed;

	// 설정창을 열 때마다(Esc) 드롭다운 초기화가 지금 값 표시 목적으로 이 함수를 다시 호출.
	if (Index == GetWindowModeIndex())
	{
		Settings->SetFullscreenMode(Mode);
		return;
	}

	if (Mode == EWindowMode::WindowedFullscreen)
	{
		Settings->SetScreenResolution(Settings->GetDesktopResolution());
	}
	else
	{
		// 전체화면에서 창모드로 돌아올 때 해상도를 안 건드리면 직전 전체화면의 데스크톱 전체 크기가
		// 그대로 남아 창모드 전환이 티 안 남 — 데스크톱보다 작은 통용 해상도(1600x900)로 되돌림
		const FIntPoint Desktop = Settings->GetDesktopResolution();
		Settings->SetScreenResolution(FIntPoint(FMath::Min(1600, Desktop.X), FMath::Min(900, Desktop.Y)));
	}

	Settings->SetFullscreenMode(Mode);
}

int32 UCSSettingsWidget::GetWindowModeIndex() const
{
	// GameUserSettings에 저장된 FullscreenMode는 이번 세션에서 한 번도 안 바뀌었으면 엔진
	// 기본값일 뿐이라 실제 창 상태와 다를 수 있음. 창이 살아있으면 그쪽을 우선 신뢰.
	if (GEngine && GEngine->GameViewport)
	{
		if (const TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow())
		{
			return Window->GetWindowMode() == EWindowMode::Windowed ? 1 : 0;
		}
	}

	const UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	return (Settings && Settings->GetFullscreenMode() == EWindowMode::Windowed) ? 1 : 0;
}

void UCSSettingsWidget::ApplyGraphicsSettings()
{
	if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		Settings->ApplySettings(false);
	}
}

namespace
{
// 어두움 / 중간(기본) / 밝음 — 기본값(2.2) 기준 ±0.2. ±0.4(1.8~2.6)는 너무 밝거나
// 어두워 화면이 부자연스러워 더 좁힘.
constexpr float GBrightnessGammaByIndex[] = { 2.0f, 2.2f, 2.4f };
// 최하 / 하 / 중간 / 상 / 최상(기본) — 50%는 체감상 너무 흐릿해서 바닥을 70%로 올림.
constexpr float GResolutionScaleByIndex[] = { 70.0f, 80.0f, 88.0f, 95.0f, 100.0f };
}

void UCSSettingsWidget::SetBrightnessIndex(int32 Index)
{
	// 설정창을 열 때마다 드롭다운 초기화가 "지금 값 표시" 목적으로 재호출 — 이미 같은 값이면
	// 불필요한 재적용을 건너뜀.
	if (Index == GetBrightnessIndex())
	{
		return;
	}

	if (GEngine)
	{
		GEngine->DisplayGamma = GBrightnessGammaByIndex[FMath::Clamp(Index, 0, UE_ARRAY_COUNT(GBrightnessGammaByIndex) - 1)];
	}
}

int32 UCSSettingsWidget::GetBrightnessIndex() const
{
	if (!GEngine)
	{
		return 1;
	}

	int32 ClosestIndex = 1;
	float ClosestDistance = MAX_flt;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(GBrightnessGammaByIndex); ++Index)
	{
		const float Distance = FMath::Abs(GEngine->DisplayGamma - GBrightnessGammaByIndex[Index]);
		if (Distance < ClosestDistance)
		{
			ClosestDistance = Distance;
			ClosestIndex = Index;
		}
	}
	return ClosestIndex;
}

void UCSSettingsWidget::SetResolutionScaleIndex(int32 Index)
{
	// 위 SetBrightnessIndex와 같은 이유로 조기 리턴.
	if (Index == GetResolutionScaleIndex())
	{
		return;
	}

	if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		Settings->SetResolutionScaleValueEx(GResolutionScaleByIndex[FMath::Clamp(Index, 0, UE_ARRAY_COUNT(GResolutionScaleByIndex) - 1)]);
	}
}

int32 UCSSettingsWidget::GetResolutionScaleIndex() const
{
	const UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!Settings)
	{
		return UE_ARRAY_COUNT(GResolutionScaleByIndex) - 1;
	}

	float CurrentScaleNormalized = 1.0f, CurrentScaleValue = 100.0f, MinScaleValue = 25.0f, MaxScaleValue = 100.0f;
	Settings->GetResolutionScaleInformationEx(CurrentScaleNormalized, CurrentScaleValue, MinScaleValue, MaxScaleValue);

	int32 ClosestIndex = UE_ARRAY_COUNT(GResolutionScaleByIndex) - 1;
	float ClosestDistance = MAX_flt;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(GResolutionScaleByIndex); ++Index)
	{
		const float Distance = FMath::Abs(CurrentScaleValue - GResolutionScaleByIndex[Index]);
		if (Distance < ClosestDistance)
		{
			ClosestDistance = Distance;
			ClosestIndex = Index;
		}
	}
	return ClosestIndex;
}

void UCSSettingsWidget::ApplyStartupGraphicsDefault()
{
	if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		Settings->SetResolutionScaleValueEx(GResolutionScaleByIndex[2]);
		Settings->ApplySettings(false);
	}
}

void UCSSettingsWidget::QuitGame()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		UKismetSystemLibrary::QuitGame(this, PC, EQuitPreference::Quit, false);
	}
}
