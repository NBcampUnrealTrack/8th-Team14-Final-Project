#include "Features/Tools/CSMagicWandCrosshairWidget.h"

#include "Animation/WidgetAnimation.h"

void UCSMagicWandCrosshairWidget::PlayFireReaction()
{
	if (IsValid(FireReactionAnimation))
	{
		PlayAnimation(FireReactionAnimation);
	}
}
