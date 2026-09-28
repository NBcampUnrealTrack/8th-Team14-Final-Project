#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CSSaveIdComponent.generated.h"

// 세이브 복원 시 액터를 다시 찾기 위한 안정 식별자 컴포넌트
// 액터 이름은 재배치 시 변경되므로 레벨 에셋에 직렬화되는 FGuid로 매칭
// 시설 인터페이스 대신 컴포넌트로 두어 기존 구현 액터와 BP의 수정을 방지
UCLASS(ClassGroup = (Save), meta = (BlueprintSpawnableComponent))
class CONVENIENCESTORE_API UCSSaveIdComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCSSaveIdComponent();

	virtual void OnRegister() override;

#if WITH_EDITOR
	// 레벨 저장 직전에 식별자가 비어 있으면 발급
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;

	// 복사·붙여넣기나 Alt 드래그로 복제한 액터가 원본 식별자를 공유하지 않도록 새로 발급
	virtual void PostEditImport() override;
#endif

	// 현재 식별자를 반환
	UFUNCTION(BlueprintPure, Category = "Save")
	FGuid GetSaveId() const { return SaveId; }

	// 게임 월드에서 새로 발급된 식별자인지 확인
	// 레벨 배치 액터는 에디터에서 발급된 식별자를 가지므로 런타임 스폰 여부 판단에 사용
	UFUNCTION(BlueprintPure, Category = "Save")
	bool WasSpawnedAtRuntime() const { return bWasSpawnedAtRuntime; }

	// 복원한 액터가 저장 당시 식별자를 이어받도록 설정
	void OverrideSaveId(const FGuid& InSaveId, bool bInWasSpawnedAtRuntime);

	// 액터의 식별자를 조회하고 컴포넌트가 없으면 유효하지 않은 값을 반환
	static FGuid FindSaveId(const AActor* InActor);

	// 액터의 컴포넌트를 조회하고 없으면 런타임에 추가해 반환
	static UCSSaveIdComponent* FindOrAddComponent(AActor* InActor);

protected:
	// 레벨 에셋에 직렬화되는 안정 식별자
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Save")
	FGuid SaveId;

	// 게임 월드에서 식별자를 새로 발급한 액터 여부
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Save")
	bool bWasSpawnedAtRuntime = false;
};
