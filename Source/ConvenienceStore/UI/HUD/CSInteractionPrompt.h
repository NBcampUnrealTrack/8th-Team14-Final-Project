#pragma once

#include "CoreMinimal.h"

// 안내마다 별도의 행과 경고 스타일을 지정하실 수 있습니다.
struct FCSInteractionPrompt
{
	FText Message;
	bool bDanger = false;
};
