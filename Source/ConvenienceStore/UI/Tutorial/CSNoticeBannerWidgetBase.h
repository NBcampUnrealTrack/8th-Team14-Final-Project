#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSNoticeBannerWidgetBase.generated.h"

// 화면 중앙에 잠깐 떴다 사라지는 범용 경고 배너
// UCSTutorialBannerWidgetBase와 이벤트 이름(OnBannerTextChanged/OnBannerHidden)이 같아서
// WBP_TutorialBanner를 복제해 이 클래스로 리페어런트하면 기존 등장·퇴장 애니메이션을 그대로 재사용할 수 있음
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSNoticeBannerWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	// 배너 문구를 갱신해 띄우고 BannerDisplaySeconds 뒤 자동으로 숨김
	void ShowMessage(const FText& Message);

protected:
	virtual void NativeDestruct() override;

	// 배너 문구가 바뀔 때마다 호출됨
	// 디자이너는 여기서 텍스트를 반영하고 등장 애니메이션을 재생
	UFUNCTION(BlueprintImplementableEvent, Category = "Notice")
	void OnBannerTextChanged(const FText& NewText);

	// BannerDisplaySeconds가 지나면 호출됨
	// 디자이너는 여기서 사라지는 애니메이션을 재생
	UFUNCTION(BlueprintImplementableEvent, Category = "Notice")
	void OnBannerHidden();

	// 배너가 화면에 떠 있는 시간(초)
	// WBP의 Class Defaults에서 조정할 수 있음
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Notice")
	float BannerDisplaySeconds = 2.5f;

private:
	void HideBanner();

	FTimerHandle BannerHideTimerHandle;
};
