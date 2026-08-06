#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CSProductVisualRegistry.generated.h"

class UStaticMesh;
class UTexture2D;

USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSProductVisualData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Visual")
	TSoftObjectPtr<UStaticMesh> ProductMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Visual")
	TSoftObjectPtr<UTexture2D> ProductIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Visual")
	FTransform MeshRelativeTransform = FTransform::Identity;
};

UCLASS(BlueprintType)
class CONVENIENCESTORE_API UCSProductVisualRegistry : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Visual")
	TMap<FName, FCSProductVisualData> ProductVisuals;

	const FCSProductVisualData* FindVisualData(FName ProductId) const;
};
