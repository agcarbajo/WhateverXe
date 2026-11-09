// CPU test: reuse the firmware's proven 6 bpc eDP depth only for its exact timing.
#include "kern_edp_boot_depth.hpp"
#include <assert.h>
#include <initializer_list>
#include <stdio.h>
#include <string.h>

static void timing(uint8_t *t,uint32_t ha,uint32_t hb,uint32_t va,uint32_t vb,uint16_t encoding,uint16_t depth) {
 memset(t,0,0xA0);
 memcpy(t+0x40,&ha,4);memcpy(t+0x44,&hb,4);memcpy(t+0x50,&va,4);memcpy(t+0x54,&vb,4);
 memcpy(t+0x88,&encoding,2);memcpy(t+0x8A,&depth,2);
}
static uint16_t field(const uint8_t *t,unsigned offset) {uint16_t v;memcpy(&v,t+offset,2);return v;}

int main() {
 // MSI Summit E16 Flip GOP: TRANS_DDI_FUNC_CTL_A 8A200106 (DDI A, DP SST,
 // 6 bpc, x4), HTOTAL 0A9F09FF (2560/2720), VTOTAL 06C3063F (1600/1732).
 const auto boot=EDPBootDepth::capture(0x8A200106,0x0A9F09FF,0x06C3063F,1);
 assert(boot.valid && boot.hActive==2560 && boot.hTotal==2720 && boot.vActive==1600 && boot.vTotal==1732);
 // Only an enabled eDP DP-SST 6 bpc transcoder on a VBT panel port qualifies.
 assert(!EDPBootDepth::capture(0x8A000106,0x0A9F09FF,0x06C3063F,1).valid);   // 8 bpc
 assert(!EDPBootDepth::capture(0x0A200106,0x0A9F09FF,0x06C3063F,1).valid);   // disabled
 assert(!EDPBootDepth::capture(0x8A200106,0x0A9F09FF,0x06C3063F,2).valid);   // DDI A is not the panel
 assert(!EDPBootDepth::capture(0x88200106,0x0A9F09FF,0x06C3063F,1).valid);   // HDMI mode
 assert(!EDPBootDepth::capture(0xA2200106,0x0A9F09FF,0x06C3063F,0xFF).valid);// TC port
 assert(!EDPBootDepth::capture(0x8A200106,0x09FF0A9F,0x06C3063F,1).valid);   // total below active
 assert(EDPBootDepth::capture(0x92200106,0x0A9F09FF,0x06C3063F,2).valid);    // DDI B panel
 uint8_t t[0xA0];
 // The 165 Hz DisplayID timing (same totals) is moved to 6 bpc RGB.
 for(uint16_t depth:{0,2,4,8}) for(uint16_t encoding:{0,1}) {
  timing(t,2560,160,1600,132,encoding,depth);
  assert(boot.apply(t) && field(t,0x88)==1 && field(t,0x8A)==1);
  assert(!boot.apply(t));   // idempotent
 }
 // The 60 Hz base EDID timing (vtotal 3980) and other modes keep their depth.
 timing(t,2560,160,1600,2380,1,2);assert(!boot.apply(t) && field(t,0x8A)==2);
 timing(t,1920,160,1200,132,1,2);assert(!boot.apply(t) && field(t,0x8A)==2);
 timing(t,2560,161,1600,132,1,2);assert(!boot.apply(t) && field(t,0x8A)==2);
 // YCbCr encodings are never changed.
 timing(t,2560,160,1600,132,2,2);assert(!boot.apply(t) && field(t,0x88)==2 && field(t,0x8A)==2);
 // An invalid capture never matches anything.
 const EDPBootDepth none;timing(t,2560,160,1600,132,1,2);assert(!none.apply(t) && !none.matches(t));
 assert(!boot.matches(nullptr));
 // AUO B160QAN02.N DisplayID extension block (Type I, 2560x1600 at 777.51 MHz).
 uint8_t ext[128]={0x70,0x13,0x79,0x00,0x00,0x03,0x01,0x14,0xb6,0x2f,0x01,0x85,0xff,0x09,0x9f,0x00,
  0x2f,0x00,0x1f,0x00,0x3f,0x06,0x83,0x00,0x09,0x00,0x09,0x00};
 ext[126]=0x22;ext[127]=0x90;
 const auto panel=displayIDTiming(ext,sizeof(ext),boot);
 assert(panel.valid && panel.clock==777510000ULL && panel.hBlank==160 && panel.hSyncOffset==48 &&
        panel.hSyncWidth==32 && panel.vBlank==132 && panel.vSyncOffset==10 && panel.vSyncWidth==10);
 assert(!displayIDTiming(ext,sizeof(ext),none).valid);
 assert(!displayIDTiming(ext,64,boot).valid);
 {uint8_t bad[128];memcpy(bad,ext,128);bad[0]=0x02;assert(!displayIDTiming(bad,128,boot).valid);}
 {uint8_t bad[128];memcpy(bad,ext,128);bad[2]=0x7F;assert(!displayIDTiming(bad,128,boot).valid);}   // section overruns
 {uint8_t other[128];memcpy(other,ext,128);other[20]=0x1f;assert(!displayIDTiming(other,128,boot).valid);} // vertical active differs
 // Clone the native 60 Hz entry (2720x3980 at 650 MHz, ids 0x8000100x) to the 165 Hz timing.
 uint8_t native[0xA0],clone[0xA0];timing(native,2560,160,1600,2380,1,2);
 const uint32_t id=0x80001000;const uint64_t clock60=650000000ULL;memcpy(native,&id,4);
 for(unsigned o:{0x28u,0x30u,0x38u})memcpy(native+o,&clock60,8);
 assert(panelTimingClone(native,sizeof(native),clone,boot,panel,0x80001013));
 uint32_t cid,hb,vb,vo;uint64_t c0,c1,c2;memcpy(&cid,clone,4);memcpy(&hb,clone+0x44,4);memcpy(&vb,clone+0x54,4);memcpy(&vo,clone+0x58,4);
 memcpy(&c0,clone+0x28,8);memcpy(&c1,clone+0x30,8);memcpy(&c2,clone+0x38,8);
 assert(cid==0x80001013 && hb==160 && vb==132 && vo==10 && c0==777510000ULL && c1==c0 && c2==c0);
 assert(field(clone,0x88)==1 && field(clone,0x8A)==1 && boot.matches(clone));
 assert(!memcmp(native+4,clone+4,0x24));   // ids aside, scaler and signal fields are copied unchanged
 // Scaled entries, other sizes and an already-165 Hz entry are not cloned.
 {uint8_t s[0xA0];memcpy(s,native,0xA0);const uint32_t w=1440;memcpy(s+0x18,&w,4);assert(!panelTimingClone(s,0xA0,clone,boot,panel,1));}
 {uint8_t s[0xA0];timing(s,1920,160,1200,132,1,2);assert(!panelTimingClone(s,0xA0,clone,boot,panel,1));}
 assert(!panelTimingClone(clone,0xA0,native,boot,panel,1));
 assert(!panelTimingClone(native,0x9F,clone,boot,panel,1) && !panelTimingClone(native,0xA0,clone,boot,EDPPanelTiming{},1));
 puts("PASS eDP boot depth: exact firmware 6 bpc timing only, RGB/unspecified encodings, idempotent");
 puts("PASS eDP panel timing: DisplayID Type I parse and native 60 Hz entry clone at 6 bpc");
}
