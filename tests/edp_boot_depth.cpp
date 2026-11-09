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
 puts("PASS eDP boot depth: exact firmware 6 bpc timing only, RGB/unspecified encodings, idempotent");
}
