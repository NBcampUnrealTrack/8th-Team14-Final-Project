#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Features/Interaction/CSInteractable.h"
#include "CSJukeboxActor.generated.h"

class UStaticMeshComponent;
class UCSJukeboxWidget;

UCLASS(Blueprintable)
class CONVENIENCESTORE_API ACSJukeboxActor : public AActor, public ICSInteractable
{
	GENERATED_BODY()
public:
	ACSJukeboxActor();
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UStaticMeshComponent> JukeboxMesh;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Jukebox") TSubclassOf<UCSJukeboxWidget> WidgetClass;
private:
	UPROPERTY(Transient) TObjectPtr<UCSJukeboxWidget> ActiveWidget;
};
