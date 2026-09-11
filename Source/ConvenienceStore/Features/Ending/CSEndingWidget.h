#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSEndingWidget.generated.h"

class UButton;

// 만화 컷 위젯을 연결하기 전에도 전체 화면 전환을 확인할 수 있는 기본 화면입니다.
UCLASS()
class CONVENIENCESTORE_API UCSEndingWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	// 커스텀 엔딩 BP에서도 같은 종료 동작을 버튼에 연결할 수 있습니다.
	UFUNCTION(BlueprintCallable, Category = "Ending")
	void QuitEndingGame();
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// 파생 위젯에 같은 이름의 버튼을 배치하면 클릭 이벤트를 자동 연결합니다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Ending")
	TObjectPtr<UButton> QuitButton;
};
