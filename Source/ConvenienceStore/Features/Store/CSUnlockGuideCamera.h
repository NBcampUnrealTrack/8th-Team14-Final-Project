#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/CSUnlockGuideData.h"
#include "CSUnlockGuideCamera.generated.h"

class UCameraComponent;
class USceneComponent;
class ULevelSequence;
class ACSUnlockGuideCamera;

USTRUCT(BlueprintType)
struct FCSUnlockGuideShot
{
	GENERATED_BODY()
	// 장면에 표시할 이름입니다. 비워 두시면 해금 항목의 제목을 사용합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock Guide") FText Title;
	// 왼쪽 안내에 표시할 문구입니다. 비워 두시면 해금 항목의 설명을 사용합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock Guide", meta = (MultiLine = "true")) FText Message;
	// 레벨에 배치한 안내 카메라 액터를 지정해 주세요. 해당 액터의 Shots는 중첩 재생하지 않습니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock Guide") TObjectPtr<ACSUnlockGuideCamera> Camera;
	// 0이면 대상 카메라의 시간을 사용합니다. 시퀀스는 자체 재생 길이를 사용합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock Guide", meta = (ClampMin = "0.0", Units = "s")) float Duration = 0.0f;
};

// 해금 위치마다 배치하시고 시작 카메라와 도착 지점의 구도를 조정해 주세요.
UCLASS()
class CONVENIENCESTORE_API ACSUnlockGuideCamera : public AActor
{
	GENERATED_BODY()
public:
	ACSUnlockGuideCamera();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock Guide") FCSUnlockGuideEntry Entry;
	// 배열 순서대로 여러 장소를 소개합니다. 비워 두시면 기존 단일 카메라가 재생됩니다.
	// 장소용 카메라 액터의 Entry.Id는 비워 두셔야 별도 해금 항목으로 등록되지 않습니다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Unlock Guide", meta = (TitleProperty = "Title")) TArray<FCSUnlockGuideShot> Shots;
	// 동일 ID는 이 액터의 Entry가 우선하며, 다른 항목은 카탈로그에서 추가합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock Guide") TObjectPtr<UCSUnlockGuideData> Catalog;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Unlock Guide") TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Unlock Guide") TObjectPtr<UCameraComponent> GuideCamera;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Unlock Guide") TObjectPtr<USceneComponent> EndPoint;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Unlock Guide") TObjectPtr<AActor> LookAtActor;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock Guide", meta = (ClampMin = "0.1")) float ShotDuration = 2.5f;
	// 0이면 시작 화각을 유지합니다. 더 작은 값으로 지정하시면 시설을 줌인합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock Guide", meta = (ClampMin = "0.0", ClampMax = "170.0")) float EndFieldOfView = 0.0f;
	// 별도 시퀀스를 지정하셔도 게임 시간이 정지된 상태에서 재생합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock Guide") TObjectPtr<ULevelSequence> Sequence;
};
