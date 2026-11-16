#include "common.h"
#include "../../SLUS_010.40/main.h"
#include "../BATTLE.PRG/4A0A8.h"
#include "../BATTLE.PRG/573B8.h"

static int (*vs_event_skipOpcodeHandlers[])(u_char*, short) = { vs_battle_invalidOpcode,
    vs_battle_invalidOpcode, vs_battle_invalidOpcode, vs_battle_invalidOpcode,
    vs_battle_invalidOpcode, vs_battle_invalidOpcode, vs_battle_invalidOpcode,
    vs_battle_invalidOpcode, vs_battle_invalidOpcode, vs_battle_invalidOpcode,
    vs_event_scriptWaitForEventFile, vs_battle_invalidOpcode, func_800B6724, vs_event_scriptBeginSkippableCutscene, vs_event_scriptEndCutsceneAndJump,
    vs_event_scriptEndCutscene, func_800B66E4, func_800B66E4, vs_battle_script_dismissTextBox,
    func_800B66E4, vs_event_scriptYieldFrame, vs_event_scriptYieldFrame, vs_battle_script_loadGim, func_800B7014,
    func_800B70E8, func_800B71C4, vs_event_scriptYieldFrame, vs_battle_script_setStringContextInt,
    vs_battle_invalidOpcode, vs_event_scriptSetDialogTransitionDuration, func_800B7428, vs_event_scriptStoreActorTile, vs_event_scriptSpawnActor,
    vs_event_scriptNavigateActorToTile, func_800B66E4, vs_event_scriptSetActorGroupFlags, vs_event_scriptSetActorSoundMix, vs_event_scriptWalkActorToTile,
    vs_event_scriptWalkActorToWorldPos, vs_event_scriptSlideActorToTile, vs_event_scriptSlideActorToWorldPos, vs_event_scriptSetActorAttachment, vs_event_scriptNavigateActorToWorldPos,
    vs_event_scriptTurnActorToFacing, vs_event_scriptFaceOtherActor, vs_event_scriptFaceTile, vs_event_scriptScaleActor, vs_event_scriptRemoveActorGroup,
    vs_event_scriptYieldFrame, func_800B81A0, func_800B66E4, vs_event_scriptRotateActorAxes, func_800B8360,
    func_800B66E4, func_800B66E4, vs_event_scriptSetActorIdleAnimation, func_800B8510, func_800B8590,
    vs_event_scriptLoadObjectModel, vs_event_scriptWaitForObjectLoad, vs_event_scriptReleaseObjectModel, vs_event_scriptSelectObjectTextureBank, func_800B8788,
    vs_event_scriptSetActorWeaponDrawn, vs_battle_script_jumpFwdIfFlag, vs_battle_script_jumpFwdIfFlags,
    vs_battle_script_jumpFwd, func_800B66E4, func_800B66E4, vs_battle_script_jumpFwd2,
    vs_battle_script_setFlagImmContext, vs_battle_script_jumpFwdIfFlagContext,
    func_800B66E4, vs_event_scriptSetLoopCounter, vs_battle_script_jumpBackIf, vs_battle_script_jumpFwd4,
    vs_battle_script_jumpFwd3, vs_battle_invalidOpcode, vs_battle_invalidOpcode,
    vs_battle_invalidOpcode, vs_battle_script_setFlagImm, vs_battle_script_setFlag,
    vs_battle_script_setFlagIfItemInInventory, func_800B91C4, func_800B66E4,
    vs_event_scriptSetNpcUpdatesEnabled, vs_battle_script_setFlagIfButton, vs_event_scriptRestoreActorVitals, func_800B92D0,
    func_800B92F4, vs_event_scriptConfigureTimer, vs_event_scriptSetDialogSpeaker, vs_event_scriptSetActorEquipmentModels, vs_event_scriptSetActorYawOscillation,
    vs_event_scriptYieldFrame, vs_event_scriptConfigureEasingCurve, func_800B9770, func_800B979C, func_800B97E4,
    vs_event_scriptClearAshleyStatusEffects, func_800B9818, func_800B983C, func_800B9860, func_800B988C,
    func_800B98C0, func_800B66E4, func_800B66E4, func_800B9AC4, func_800B9AE8,
    func_800B66E4, vs_event_scriptYieldFrame, func_800B9C18, func_800B9C58, func_800B9D74,
    vs_event_scriptSetAshleyBattleMode, func_800B9F28, func_800B9F4C, func_800B9F70, func_800B9F90,
    func_800B9FC0, func_800BA0E4, func_800BA194, func_800BA108, func_800BA218,
    func_800BA264, func_800BA288, func_800BA2B4, vs_battle_invalidOpcode, func_800B66E4,
    func_800B66E4, func_800B66E4, vs_event_commandStopAllSfx, vs_event_commandStopSfx,
    vs_battle_script_loadSfxSlot, vs_battle_script_freeSfxSlot, vs_battle_script_freeSfx,
    vs_battle_script_setCurrentSfx, vs_battle_script_loadSoundFile,
    vs_battle_invalidOpcode, vs_battle_invalidOpcode, vs_battle_invalidOpcode,
    vs_battle_invalidOpcode, vs_battle_invalidOpcode, func_800B66E4,
    vs_battle_script_loadMusicSlot, vs_battle_script_freeMusic, func_800B66E4,
    func_800B66E4, func_800B66E4, func_800B66E4, func_800B66E4, func_800B66E4,
    func_800B66E4, vs_battle_script_clearMusicLoadSlot, vs_event_scriptYieldFrame, func_800B66E4,
    func_800B66E4, vs_battle_script_loadSoundFile2, vs_battle_script_processSoundQueue,
    func_800B66E4, vs_event_commandPlayActorEffect, func_800BA90C, vs_event_commandWaitEffectState, vs_event_commandYieldEffectRelease,
    vs_event_commandWaitEffectContinuation, vs_event_commandWaitEffectRelease, vs_event_commandMarkEffectStageReady, vs_battle_invalidOpcode, vs_event_commandSetMusicJumpThreshold,
    func_800B66E4, vs_event_commandSetActorTrail, vs_battle_invalidOpcode, vs_battle_invalidOpcode,
    vs_battle_invalidOpcode, vs_battle_invalidOpcode, vs_battle_invalidOpcode,
    vs_battle_invalidOpcode, vs_event_commandSetEnteredDoor, vs_event_commandStoreActorRangeBearing, func_800BAD78,
    vs_battle_invalidOpcode, vs_event_commandConsumeMiscItem, vs_event_commandStoreActorDistance, vs_battle_script_specialOp,
    vs_event_commandWaitScreenEffectLoad, func_800BD6C4, func_800BD6C4, func_800B66E4, vs_event_commandLoadScreenEffects,
    func_800BB304, vs_event_commandFreeScreenEffects, func_800BD6C4, func_800BC2E8, func_800BC2E8,
    func_800BC2E8, func_800BC2E8, func_800BC2E8, func_800BC2E8, vs_battle_invalidOpcode,
    func_800BC2E8, func_800BC2E8, func_800BC2E8, func_800BC2E8, func_800BC2E8,
    vs_battle_invalidOpcode, vs_battle_invalidOpcode, vs_battle_invalidOpcode,
    vs_battle_invalidOpcode, func_800BC2E8, func_800BC2E8, func_800BC2E8, func_800BC2E8,
    func_800BC2E8, func_800BC2E8, vs_battle_invalidOpcode, func_800BC2E8, func_800BC2E8,
    func_800BC2E8, func_800BC2E8, func_800BC2E8, vs_battle_invalidOpcode,
    vs_battle_invalidOpcode, func_800BB33C, func_800BB35C, vs_event_scriptYieldFrame, func_800BB450,
    func_800BD444, func_800BD444, func_800BD6C4, func_800BD6C4, func_800BD6C4,
    func_800BD6C4, func_800BB38C, func_800BB3BC, func_800BDC9C, func_800BDC9C,
    func_800BDC9C, func_800BD6C4, func_800BDC9C, func_800BDC9C, vs_event_scriptYieldFrame,
    vs_event_commandSetTickSpeed, vs_battle_script_playMenuSelectSfx, vs_event_commandSetScanlineOverlay, vs_event_commandWaitScriptContext,
    vs_event_commandStartScriptContext, vs_event_commandStopScriptContext, vs_event_commandLoadNextEvent, vs_battle_invalidOpcode,
    vs_battle_invalidOpcode, vs_battle_invalidOpcode, vs_battle_invalidOpcode,
    vs_battle_invalidOpcode, vs_battle_invalidOpcode, vs_event_commandFinishEvent, vs_event_commandFinishEvent };

void _initScriptFunctionTable(void)
{
    D_800F4C08 = 0;
    vs_event_roomChangeState = 0;
    vs_event_scriptBuffer = vs_main_allocHeap(0x1800);
    vs_event_vibrationPatternBuffer = vs_main_allocHeap(0x80);
    vs_event_legacySkipHandlerCopy = vs_main_allocHeap(PSX_SIZE(__typeof__(vs_event_skipOpcodeHandlers), 0x400));
    vs_main_memcpy(vs_event_legacySkipHandlerCopy, vs_event_skipOpcodeHandlers, PSX_SIZE(__typeof__(vs_event_skipOpcodeHandlers), 0x400));
}
