.include "macro.inc"

.set noat
.set noreorder

.section .text, "ax"

glabel vs_battle_aiDoesActionRequireCooldown
    lbu     $v0, 0x13($a0)
    lw      $v1, 0xC($a1)
    bnez    $v0, .L800DBCA4
    lw      $v0, 0x4($a1)
    lui     $t1, %hi(vs_field_gimmickInstances)
    addiu   $t1, $t1, %lo(vs_field_gimmickInstances)
    lw      $t0, 0x10($a1)
    andi    $v1, $v1, 0xF
    srl     $t0, $t0, 4
    andi    $t0, $t0, 0x1
    beqz    $t0, .L800DBCAC
    sll     $v1, $v1, 2
    addu    $v1, $v1, $t1
    lw      $v1, 0x0($v1)
    nop
    bnez    $v1, .L800DBCAC
    nop
.L800DBCA4:
    jr      $ra
    addiu   $v0, $zero, 0x1
.L800DBCAC:
    jr      $ra
    addu    $v0, $zero, $zero
endlabel vs_battle_aiDoesActionRequireCooldown

glabel vs_battle_aiIsActionCooldownReady
    lhu     $t0, 0x90($a0)
    addu    $t9, $ra, $zero
    bnez    $t0, .L800DBCAC
    nop
    jal     vs_battle_aiDoesActionRequireCooldown
    nop
    addu    $ra, $t9, $zero
    beqz    $v0, .L800DBCA4
    nop
    lbu     $t0, 0x13F($a0)
    nop
    bnez    $t0, .L800DBCAC
    nop
    bgez    $zero, .L800DBCA4
endlabel vs_battle_aiIsActionCooldownReady

glabel vs_battle_aiHasNearbyEnemy
    lui     $t8, %hi(vs_battle_actors)
    addiu   $t8, $t8, %lo(vs_battle_actors)
    mtc2    $a0, $2
    mtc2    $a1, $4
.L800DBCFC:
    beqz    $t8, .L800DBCA4
    mfc2    $a0, $2
    mfc2    $a1, $4
    lw      $t1, 0x4($t8)
    addu    $t9, $ra, $zero
    sll     $t1, $t1, 2
    addiu   $v0, $t1, 0x1A4
    addu    $v0, $a0, $v0
    lw      $v0, 0x0($v0)
    nop
    srl     $v0, $v0, 6
    andi    $v0, $v0, 0x3
    slti    $v0, $v0, 0x3
    bnez    $v0, .L800DBD74
    lui     $v0, %hi(vs_actors_actorRecords)
    addiu   $v0, $v0, %lo(vs_actors_actorRecords)
    addu    $v0, $v0, $t1
    lw      $v0, 0x0($v0)
    lui     $v1, %hi(vs_field_gimmickInstances)
    addiu   $v1, $v1, %lo(vs_field_gimmickInstances)
    sll     $a1, $a1, 2
    addu    $v1, $v1, $a1
    lw      $v1, 0x0($v1)
    lw      $a0, 0x5C($v0)
    jal     vs_core_mathSquaredTileDistance
    lw      $a1, 0x5C($v1)
    addu    $ra, $t9, $zero
    slti    $v0, $v0, 0x3
    bnez    $v0, .L800DBCAC
    mfc2    $a0, $2
.L800DBD74:
    lw      $t8, 0x0($t8)
    bgez    $zero, .L800DBCFC
    nop
endlabel vs_battle_aiHasNearbyEnemy
