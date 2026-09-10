#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSEndingWidget.generated.h"

// 만화 컷 위젯을 연결하기 전에도 전체 화면 전환을 확인할 수 있는 기본 화면입니다.
UCLASS()
class CONVENIENCESTORE_API UCSEndingWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
};
