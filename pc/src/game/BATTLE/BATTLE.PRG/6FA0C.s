.include "macro.inc"

.set noat
.set noreorder

.section .text, "ax"

glabel vs_battle_aiSetActorAwarenessIndicator
    beqz    $a0, L800D8278
    addu    $v0, $zero, $zero
    j       vs_battle_setActorMarker
    nop
endlabel vs_battle_aiSetActorAwarenessIndicator

glabel vs_battle_aiResetActorAwarenessIndicator
    addiu   $a1, $zero, 0x0
    addiu   $sp, $sp, -0x20
    sw      $s0, 0x14($sp)
    sw      $ra, 0x18($sp)
    lbu     $a0, 0x88($a0)
    jal     vs_battle_aiSetActorAwarenessIndicator
    addu    $s0, $a0, $zero
    addu    $a0, $s0, $zero
    jal     vs_battle_aiSetActorAwarenessIndicator
    addiu   $a1, $zero, 0x40
    lw      $ra, 0x18($sp)
    lw      $s0, 0x14($sp)
    jr      $ra
    addiu   $sp, $sp, 0x20
alabel L800D8254
    lbu     $a0, 0x88($a0)
    bgez    $zero, vs_battle_aiSetActorAwarenessIndicator
    addiu   $a1, $zero, 0x0
endlabel vs_battle_aiResetActorAwarenessIndicator
