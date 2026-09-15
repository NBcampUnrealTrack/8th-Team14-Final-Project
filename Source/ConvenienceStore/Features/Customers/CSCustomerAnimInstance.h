#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "CSCustomerAnimInstance.generated.h"

// Keeps the customer's C++ single-sequence playback, adding only a short pose crossfade.
UCLASS(Transient, NotBlueprintable)
class CONVENIENCESTORE_API UCSCustomerAnimInstance : public UAnimSingleNodeInstance
{
	GENERATED_BODY()

public:
	void PlaySequence(UAnimSequence* Sequence, bool bLooping, float BlendSeconds);

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
};
