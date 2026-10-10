#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/CSCraftTypes.h"
#include "CSCraftFailureVisualRegistry.generated.h"

class UStaticMesh;
class UTexture2D;

USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSCraftFailureVisualEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting|Failure Visual")
	TSoftObjectPtr<UStaticMesh> StaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting|Failure Visual")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting|Failure Visual")
	FTransform MeshRelativeTransform = FTransform::Identity;
};

// Failure trash stays separate from products. Keys are the existing crafting outcomes.
UCLASS(BlueprintType)
class CONVENIENCESTORE_API UCSCraftFailureVisualRegistry : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting|Failure Visual")
	TMap<ECSCraftOutcome, FCSCraftFailureVisualEntry> FailureVisuals;

	const FCSCraftFailureVisualEntry* FindVisualData(ECSCraftOutcome Outcome) const
	{
		return FailureVisuals.Find(Outcome);
	}
};
