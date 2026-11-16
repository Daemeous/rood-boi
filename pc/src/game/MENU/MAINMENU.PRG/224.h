#pragma once
#include <stddef.h>
#include <libgte.h>

void vs_menu_frameworkLoadActorPreview(int);
void vs_menu_frameworkCloneActorPreview(int arg0);
void vs_menu_frameworkResetActorPreviewPose(void);
void vs_menu_frameworkReleaseActorPreview(void);
int vs_menu_frameworkDrawActorPreview(MATRIX* arg0);
void vs_menu_frameworkResetAshleyEquipmentPose(void);
void vs_menu_frameworkQueueAshleyWeaponAnimations(int arg0);
