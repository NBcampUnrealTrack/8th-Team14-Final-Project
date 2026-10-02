#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CSArtifactVisualRegistry.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSArtifactVisualEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Artifact|Visual")
	TSoftObjectPtr<UStaticMesh> StaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Artifact|Visual")
	FTransform MeshRelativeTransform = FTransform::Identity;
};

// Keys are DT_ArtifactMaster row names. Effects and ownership stay in the artifact system.
UCLASS(BlueprintType)
class CONVENIENCESTORE_API UCSArtifactVisualRegistry : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Artifact|Visual")
	TMap<FName, FCSArtifactVisualEntry> ArtifactVisuals;

	UFUNCTION(BlueprintPure, Category = "Artifact|Visual")
	bool GetVisualData(FName ArtifactId, FCSArtifactVisualEntry& OutVisual) const;

	// Missing entries/meshes leave the component intact so consumers can use their fallback.
	UFUNCTION(BlueprintCallable, Category = "Artifact|Visual")
	bool ApplyToMesh(FName ArtifactId, UStaticMeshComponent* MeshComponent) const;
};
