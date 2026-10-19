#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
typedef struct {
    int currentHP, maxHP, currentMP, maxMP;
    int strength, totalStrength, intelligence, totalIntelligence, agility, totalAgility;
    struct { int hp, maxHp; } limbs[6];
} vs_battle_actor2;
int pc_difficulty_base_percent, pc_difficulty_clear_percent;
#include "../src/enemy_difficulty.inc"
static vs_battle_actor2 fresh(void) {
    vs_battle_actor2 a = {0};
    a.currentHP = a.maxHP = 100;
    a.strength = a.totalStrength = 100;
    a.intelligence = a.totalIntelligence = 101;
    a.agility = a.totalAgility = 999;
    for (int i=0; i<6; ++i) a.limbs[i].hp = a.limbs[i].maxHp = i ? 1000 : 0;
    return a;
}
static void store(short* saved, vs_battle_actor2* a) {
    saved[2]=a->currentHP; saved[3]=a->maxHP;
    saved[4]=a->currentMP; saved[5]=a->maxMP;
    saved[6]=a->totalStrength; saved[7]=a->totalIntelligence; saved[8]=a->totalAgility;
    for(int i=0;i<6;++i) saved[9+i]=a->limbs[i].hp;
    pc_difficulty_cache_store(saved,a);
}
static void restore(short* saved, vs_battle_actor2* a) {
    pc_difficulty_init_actor(2,12,a);
    a->currentHP=saved[2]; a->maxHP=saved[3]; a->currentMP=saved[4]; a->maxMP=saved[5];
    a->strength=a->totalStrength=saved[6]; a->intelligence=a->totalIntelligence=saved[7]; a->agility=a->totalAgility=saved[8];
    for(int i=0;i<6;++i) a->limbs[i].hp=saved[9+i];
    pc_difficulty_cache_restore(saved,2,12,a);
}
int main(void) {
    vs_battle_actor2 a,b;
    short saved[20]={0};
    pc_difficulty_base_percent=pc_difficulty_clear_percent=30;
    pc_difficulty_begin_scene(2);
    a=fresh(); pc_difficulty_init_actor(2,12,&a);
    assert(a.maxHP==190 && a.currentHP==190 && a.strength==190);
    assert(a.intelligence==191 && a.agility==999 && a.maxMP==0);
    assert(a.limbs[0].hp==0 && a.limbs[1].maxHp==1900);
    b=fresh(); pc_difficulty_init_actor(0,0,&b); a=fresh(); assert(!memcmp(&a,&b,sizeof a));
    for(int slot=1;slot<16;++slot) {
        b=fresh(); pc_difficulty_init_actor(slot,8,&b); assert(!memcmp(&a,&b,sizeof a));
    }
    b=fresh(); pc_difficulty_init_actor(2,12,&b);
    b.currentHP=165; b.limbs[1].hp=1850;
    store(saved,&b);
    assert(saved[3]==100 && saved[6]==100 && saved[7]==101 && saved[8]==999);
    pc_difficulty_base_percent=17; pc_difficulty_clear_percent=0;
    for(int trip=0;trip<100;++trip) {
        b=fresh(); restore(saved,&b);
        assert(b.maxHP==190 && b.currentHP==165 && b.limbs[1].hp==1850 && b.limbs[1].maxHp==1900);
        store(saved,&b);
    }
    /* Card load: sidecar absent, settings latch anew, originals scale once. */
    pc_difficulty_begin_scene(2); b=fresh(); restore(saved,&b);
    assert(b.maxHP==117 && b.strength==117 && b.intelligence==118 && b.limbs[1].maxHp==1170);
    assert(b.currentHP<=165*117/190);
    store(saved,&b);
    int card_hp=saved[2], card_limb=saved[10];
    for(int reload=0;reload<100;++reload) {
        pc_difficulty_begin_scene(2); b=fresh(); restore(saved,&b); store(saved,&b);
        assert(saved[2]==card_hp && saved[10]==card_limb);
    }
    pc_difficulty_base_percent=0; pc_difficulty_begin_scene(2);
    b=fresh(); restore(saved,&b);
    assert(b.maxHP==100 && b.strength==100 && b.intelligence==101 && b.limbs[1].maxHp==1000);
    /* A capped actor's original HP must survive both cache and card restore. */
    pc_difficulty_base_percent=100; pc_difficulty_begin_scene(0);
    b=fresh(); b.maxHP=b.currentHP=800; b.limbs[1].maxHp=b.limbs[1].hp=200;
    pc_difficulty_init_actor(2,12,&b); assert(b.maxHP==999 && b.limbs[1].maxHp==250);
    store(saved,&b); assert(saved[3]==800 && saved[10]==200);
    b=fresh(); b.limbs[1].maxHp=b.limbs[1].hp=200; restore(saved,&b);
    assert(b.maxHP==999 && b.limbs[1].maxHp==250);
    pc_difficulty_begin_scene(0); b=fresh(); b.limbs[1].maxHp=b.limbs[1].hp=200; restore(saved,&b);
    assert(b.maxHP==999 && b.limbs[1].maxHp==250);
    pc_difficulty_base_percent=pc_difficulty_clear_percent=INT_MAX; pc_difficulty_begin_scene(99);
    b=fresh(); pc_difficulty_init_actor(2,12,&b); assert(b.maxHP==999 && b.limbs[1].maxHp==9990);
    assert(pc_difficulty_limb(32767,1,999)==32767 && pc_difficulty_limb(0,400,800)==0);
    assert(pc_difficulty_limb(20,0,0)==20);
    pc_difficulty_base_percent=-1; pc_difficulty_clear_percent=-20; pc_difficulty_begin_scene(0);
    b=fresh(); pc_difficulty_init_actor(2,12,&b); a=fresh(); assert(!memcmp(&a,&b,sizeof a));
    puts("PASS: additive percentages, caps, limb ratios, exclusions, 100 exact cache revisits, 100 stable card reloads and difficulty changes");
}
