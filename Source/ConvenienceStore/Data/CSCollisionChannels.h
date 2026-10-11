#pragma once

#include "Engine/EngineTypes.h"

namespace CSCollisionChannels
{
	// 제압된 손님의 물리 몸체만 구분하는 오브젝트 채널
	constexpr ECollisionChannel SubduedBody = ECC_GameTraceChannel2;
}
