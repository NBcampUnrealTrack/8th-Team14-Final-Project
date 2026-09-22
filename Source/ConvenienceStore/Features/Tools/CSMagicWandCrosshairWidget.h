#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSMagicWandCrosshairWidget.generated.h"

class UWidgetAnimation;

// 마법봉 조준점 WBP 전용 부모. 발사 시점만 C++이 알려주고, 실제 색 변경 등 연출은 WBP 애니메이션이 담당합니다.
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSMagicWandCrosshairWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 마법봉이 발사될 때마다 호출합니다. FireReactionAnimation이 있으면 그 자리에서 재생합니다.
	void PlayFireReaction();

protected:
	// WBP에서 같은 이름("FireReactionAnimation")의 애니메이션을 만들면 자동으로 연결됩니다.
	UPROPERTY(Transient, meta = (BindWidgetAnimOptional))
	TObjectPtr<UWidgetAnimation> FireReactionAnimation;
};
