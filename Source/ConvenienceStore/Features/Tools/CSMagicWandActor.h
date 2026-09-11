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
	virtual void OnEquipped(ACSPlayerCharacter* NewOwner) override;
	virtual void OnUnequipped() override;
	virtual void PrepareForPickupPhysics() override;
	// Drop 위치 계산은 해제 후 복원될 크기를 사용하되, 아직 장착된 Scale은 유지합니다.
	FBox GetUnheldBounds() const;

	// 최초 집기 전 현재 AttachParent를 원래 거치대로 기억하며, 유효한 기존 정보는 덮어쓰지 않습니다.
	bool RememberHomeDockFromCurrentAttachment();
	USceneComponent* GetHomeDockComponent() const;
	AActor* GetHomeDockOwner() const;

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
	void RestoreWorldScale();

	// 장착 중에만 유지합니다. 복원 후 비워 다음 획득에서도 원래 월드 크기를 기록합니다.
	TOptional<FVector> CachedWorldScale;

	UPROPERTY(Transient)
	TWeakObjectPtr<USceneComponent> HomeDockComponent;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> HomeDockOwner;

	float LastPrimaryUseTime = -FLT_MAX;
};
