#include "Features/Customers/CSCustomerAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
	struct FCSCustomerAnimInstanceProxy : FAnimSingleNodeInstanceProxy
	{
		explicit FCSCustomerAnimInstanceProxy(UAnimInstance* Instance)
			: FAnimSingleNodeInstanceProxy(Instance)
		{
		}

		TArray<FTransform> SourcePose;
		FTransform SourceMeshTransform = FTransform::Identity;
		FTransform RootSpaceConversion = FTransform::Identity;
		float BlendDuration = 0.0f;
		float BlendElapsed = 0.0f;

		virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
		{
			FAnimSingleNodeInstanceProxy::PreUpdate(Instance, DeltaSeconds);
			if (BlendDuration > 0.0f)
			{
				BlendElapsed = FMath::Min(BlendElapsed + DeltaSeconds, BlendDuration);
				// Thinking/Pick use a different imported root axis and existing mesh correction.
				// Rebase the outgoing root so blending never rotates the whole body sideways.
				RootSpaceConversion = SourceMeshTransform.GetRelativeTransform(
					Instance->GetSkelMeshComponent()->GetRelativeTransform());
			}
		}

		virtual bool Evaluate(FPoseContext& Output) override
		{
			FAnimSingleNodeInstanceProxy::Evaluate(Output);
			if (BlendDuration <= 0.0f || BlendElapsed >= BlendDuration)
			{
				SourcePose.Reset();
				BlendDuration = 0.0f;
				return true;
			}

			const float Alpha = FMath::SmoothStep(0.0f, 1.0f, BlendElapsed / BlendDuration);
			const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
			for (FCompactPoseBoneIndex Index : Output.Pose.ForEachBoneIndex())
			{
				const int32 MeshIndex = Bones.MakeMeshPoseIndex(Index).GetInt();
				if (!SourcePose.IsValidIndex(MeshIndex))
				{
					continue;
				}
				FTransform Source = SourcePose[MeshIndex];
				if (Output.Pose.GetParentBoneIndex(Index) == INDEX_NONE)
				{
					Source = Source * RootSpaceConversion;
				}
				const FTransform Target = Output.Pose[Index];
				Output.Pose[Index].Blend(Source, Target, Alpha);
				Output.Pose[Index].NormalizeRotation();
			}
			return true;
		}
	};
}

void UCSCustomerAnimInstance::PlaySequence(UAnimSequence* Sequence, bool bLooping, float BlendSeconds)
{
	if (!IsValid(Sequence))
	{
		return;
	}

	FCSCustomerAnimInstanceProxy& Proxy = GetProxyOnGameThread<FCSCustomerAnimInstanceProxy>();
	USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	if (IsValid(CurrentAsset) && IsValid(Mesh) && BlendSeconds > 0.0f)
	{
		// Snapshot the visible (possibly already blended) pose, so interrupted transitions stay continuous.
		Proxy.SourcePose = Mesh->GetBoneSpaceTransforms();
		Proxy.SourceMeshTransform = Mesh->GetRelativeTransform();
		Proxy.RootSpaceConversion = FTransform::Identity;
		Proxy.BlendDuration = BlendSeconds;
		Proxy.BlendElapsed = 0.0f;
	}
	else
	{
		Proxy.SourcePose.Reset();
		Proxy.BlendDuration = 0.0f;
	}
	SetAnimationAsset(Sequence, bLooping, 1.0f);
	PlayAnim(bLooping, 1.0f, 0.0f);
}

FAnimInstanceProxy* UCSCustomerAnimInstance::CreateAnimInstanceProxy()
{
	return new FCSCustomerAnimInstanceProxy(this);
}

void UCSCustomerAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FCSCustomerAnimInstanceProxy*>(InProxy);
}
