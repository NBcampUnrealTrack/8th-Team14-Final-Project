#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSSettingsWidget.generated.h"

class UButton;

// 소리 조절은 UCSAudioSubsystem의 BlueprintCallable 함수를 WBP에서 직접 호출.
UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API UCSSettingsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// X 버튼이 호출하는 닫기 함수. Esc도 같은 경로로 닫힘.
	UFUNCTION(BlueprintCallable, Category = "Settings|Navigation")
	void RequestClose();

	// 진짜 전체화면(EWindowMode::Fullscreen)은 창이 해상도 프리셋 크기로 고정돼 모니터 네이티브보다
	// 작으면 업스케일로 화질 저하 — 메뉴에서 제외. 창모드가 아니면 항상 WindowedFullscreen
	// (창이 늘 모니터 전체 크기) 사용. Index: 0 전체화면 / 1 창모드
	UFUNCTION(BlueprintCallable, Category = "Settings|Graphics")
	void SetWindowModeIndex(int32 Index);
	UFUNCTION(BlueprintPure, Category = "Settings|Graphics")
	int32 GetWindowModeIndex() const;
	// 창모드·밝기·렌더링 배율 변경을 실제로 적용. 값 세터들은 GameUserSettings에만 기록하고 반영은 안 함.
	UFUNCTION(BlueprintCallable, Category = "Settings|Graphics")
	void ApplyGraphicsSettings();

	// 연속 슬라이더는 끝까지 내리면 화면이 깨져서 단계형으로 변경.
	// Index: 0 어두움 / 1 중간(기본) / 2 밝음
	UFUNCTION(BlueprintCallable, Category = "Settings|Graphics")
	void SetBrightnessIndex(int32 Index);
	UFUNCTION(BlueprintPure, Category = "Settings|Graphics")
	int32 GetBrightnessIndex() const;

	// Index: 0 최하 / 1 하 / 2 중간 / 3 상 / 4 최상(기본)
	UFUNCTION(BlueprintCallable, Category = "Settings|Graphics")
	void SetResolutionScaleIndex(int32 Index);
	UFUNCTION(BlueprintPure, Category = "Settings|Graphics")
	int32 GetResolutionScaleIndex() const;

	// 실제 플레이 시작마다 한 번 강제 — 저장된 값이 없으면 엔진이 하드웨어 벤치마크로 채우는
	// 기본 화질이 실행마다 달라져 최상/최하로 들쭉날쭉하게 시작하는 문제 방지.
	UFUNCTION(BlueprintCallable, Category = "Settings|Graphics")
	static void ApplyStartupGraphicsDefault();

	UFUNCTION(BlueprintCallable, Category = "Settings|System")
	void QuitGame();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;

	// 같은 이름의 위젯이 있으면 자동 연결, 없어도 WBP 이벤트로 구성 가능.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;
};
