#pragma once

#include "CoreMinimal.h"
#include "Features/Tools/CSUsableToolActor.h"
#include "CSMagicWandActor.generated.h"

class ACSMagicProjectileActor;
class USceneComponent;

// 주 사용 입력으로 마법 투사체를 발사하는 지팡이 도구입니다.
UCLASS()
class CONVENIENCESTORE_API ACSMagicWandActor : public ACSUsableToolActor
{
	GENERATED_BODY()

public:
	ACSMagicWandActor();

	virtual bool UsePrimary_Implementation(AActor* User) override;

protected:
	// 블루프린트에서 지팡이 끝에 배치하는 실제 투사체 발사 위치입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> Muzzle;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Magic")
	TSubclassOf<ACSMagicProjectileActor> MagicProjectileClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Magic",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PrimaryUseCooldown = 0.35f;

private:
	float LastPrimaryUseTime = -FLT_MAX;
};
