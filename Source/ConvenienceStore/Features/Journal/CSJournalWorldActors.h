#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Features/Interaction/CSInteractable.h"
#include "Data/CSJournalTypes.h"
#include "CSJournalWorldActors.generated.h"

class UBoxComponent;
class USceneComponent;
class UStaticMeshComponent;

// 레벨에 배치하여 종족 기록을 해금하는 독립 상호작용 지점입니다.
// 자식 블루프린트에서 게시판, 유물, 비석 등 원하는 외형으로 구성하실 수 있습니다.
UCLASS(Blueprintable)
class CONVENIENCESTORE_API ACSJournalDiscoveryPoint : public AActor, public ICSInteractable
{
	GENERATED_BODY()

public:
	ACSJournalDiscoveryPoint();
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;
	// 지정된 기록을 모두 얻었는지 확인하여 안내 전용 상태를 구분합니다.
	bool IsDiscoveryComplete() const;
	// 프로젝트 설정의 공용 카탈로그에 등록된 기록 ID만 선택해 주세요.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Journal", meta = (GetOptions = "GetEntryIdOptions"))
	TArray<FName> EntryIds;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Journal") FText InteractionText;

	// 에디터에서 Entry Ids를 선택할 때 공용 카탈로그의 ID를 제공합니다.
	UFUNCTION() TArray<FString> GetEntryIdOptions() const;

protected:
	// 자식 블루프린트에서 상호작용 범위와 외형 메시를 조정할 수 있도록 노출합니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Journal|Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Journal|Components")
	TObjectPtr<UBoxComponent> InteractionBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Journal|Components")
	TObjectPtr<UStaticMeshComponent> DiscoveryMesh;
};

// 건물의 위치와 이름을 지도에 표시하며 구현 예정 건물은 회색으로 구분합니다.
UCLASS(Blueprintable)
class CONVENIENCESTORE_API ACSJournalMapMarker : public AActor
{
	GENERATED_BODY()
public:
	ACSJournalMapMarker();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Journal") FText BuildingName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Journal") ECSJournalRace Race = ECSJournalRace::Human;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Journal") bool bImplemented = true;
};

// 레벨마다 하나씩 배치하여 지도 이미지와 범위를 설정합니다. 액터의 로컬 좌표를 사용하므로 지도 회전도 반영됩니다.
UCLASS(Blueprintable)
class CONVENIENCESTORE_API ACSJournalMapSettings : public AActor
{
	GENERATED_BODY()
public:
	ACSJournalMapSettings();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Journal") TObjectPtr<UTexture2D> MapTexture;
	// 지도 이미지가 나타내는 로컬 X·Y 영역의 전체 길이를 Unreal 단위로 지정해 주시면 됩니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Journal") FVector2D MapSize = FVector2D(20000, 20000);
};

// 레벨 에셋을 수정하지 않고 알려진 건물의 기본 지도 표식을 생성합니다.
void CSConfigureJournalWorld(UWorld* World);
