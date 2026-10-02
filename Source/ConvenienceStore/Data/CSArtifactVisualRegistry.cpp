#include "Data/CSArtifactVisualRegistry.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

bool UCSArtifactVisualRegistry::GetVisualData(FName ArtifactId, FCSArtifactVisualEntry& OutVisual) const
{
	if (const FCSArtifactVisualEntry* Entry = ArtifactVisuals.Find(ArtifactId))
	{
		OutVisual = *Entry;
		return true;
	}
	OutVisual = FCSArtifactVisualEntry();
	return false;
}

bool UCSArtifactVisualRegistry::ApplyToMesh(FName ArtifactId, UStaticMeshComponent* MeshComponent) const
{
	const FCSArtifactVisualEntry* Entry = ArtifactVisuals.Find(ArtifactId);
	if (!Entry || !IsValid(MeshComponent)) return false;
	UStaticMesh* Mesh = Entry->StaticMesh.LoadSynchronous();
	if (!IsValid(Mesh)) return false;
	MeshComponent->SetStaticMesh(Mesh);
	MeshComponent->SetRelativeTransform(Entry->MeshRelativeTransform);
	return true;
}
