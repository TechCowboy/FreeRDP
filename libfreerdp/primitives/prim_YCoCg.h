/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Primitives copy
 *
 * Copyright 2024 Armin Novak <anovak@thincast.com>
 * Copyright 2024 Thincast Technologies GmbH
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FREERDP_LIB_PRIM_YCoCg_H
#define FREERDP_LIB_PRIM_YCoCg_H

#include <winpr/wtypes.h>
#include <winpr/sysinfo.h>

#include <freerdp/config.h>
#include <freerdp/primitives.h>

#include "prim_internal.h"

FREERDP_LOCAL void primitives_init_YCoCg_ssse3_int(primitives_t* WINPR_RESTRICT prims);
static inline void primitives_init_YCoCg_ssse3(primitives_t* WINPR_RESTRICT prims)
{
	if (!IsProcessorFeaturePresentEx(PF_EX_SSSE3) ||
	    !IsProcessorFeaturePresent(PF_SSE2_INSTRUCTIONS_AVAILABLE) ||
	    !IsProcessorFeaturePresent(PF_SSE3_INSTRUCTIONS_AVAILABLE))
		return;

	primitives_init_YCoCg_ssse3_int(prims);
}

FREERDP_LOCAL void primitives_init_YCoCg_neon_int(primitives_t* WINPR_RESTRICT prims);
static inline void primitives_init_YCoCg_neon(primitives_t* WINPR_RESTRICT prims)
{
	if (!IsProcessorFeaturePresent(PF_ARM_NEON_INSTRUCTIONS_AVAILABLE))
		return;
	primitives_init_YCoCg_neon_int(prims);
}

#endif
