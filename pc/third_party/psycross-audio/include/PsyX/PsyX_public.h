/* rood-boi: minimal subset of PsyCross PsyX_public.h needed by the software SPU.
 * Sound replacement (modding) hook; rood-boi leaves it unset. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*PsyX_SfxOverrideFn)(int spuAddr, const short** outPcm, int* outSampleCount,
                                  int* outRate);
extern PsyX_SfxOverrideFn g_PsyX_SfxOverride;
#ifdef __cplusplus
}
#endif
