#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSTutorialMarkerWidgetBase.generated.h"

// 3D 마커 위에 뜨는 짧은 라벨 위젯의 기반 클래스
// 서브시스템 방송을 직접 구독하지 않고 마커 컴포넌트가 이 함수를 직접 호출해서 내용을 바꿈
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSTutorialMarkerWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	// 마커 라벨 문구가 바뀔 때 호출됨
	// 디자이너는 여기서 텍스트를 반영
	UFUNCTION(BlueprintImplementableEvent, Category = "Tutorial")
	void SetMarkerLabel(const FText& Label);
};
