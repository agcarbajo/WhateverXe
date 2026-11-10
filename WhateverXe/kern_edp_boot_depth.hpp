// msi-e16flip: an eDP GOP may scan its native mode out at 6 bpc when that mode
// does not fit the link at 8 bpc (MSI Summit E16 Flip, 46A6: 2560x1600 at
// 165 Hz on HBR2 x4). The firmware has then proven that exact timing at 18 bpp
// on this panel and link. Request the same depth for the same timing so AGDC
// validation admits it and the donor programs 18 bpp with dithering. Every
// other timing keeps the donor's own depth selection.
#ifndef KERN_EDP_BOOT_DEPTH_HPP
#define KERN_EDP_BOOT_DEPTH_HPP
#include <stdint.h>
#include <string.h>
struct EDPBootDepth {
 bool valid {};
 uint32_t hActive {},hTotal {},vActive {},vTotal {};
 // TRANS_DDI_FUNC_CTL, TRANS_HTOTAL and TRANS_VTOTAL of the single active
 // firmware pipe; panelMask is the VBT eDP DDI mask (Combo A/B only).
 static EDPBootDepth capture(uint32_t trans,uint32_t htotal,uint32_t vtotal,uint32_t panelMask) {
  EDPBootDepth d;
  const auto select=(trans>>27)&15;
  if(!(trans&(1U<<31)) || select<1 || select>2 || !(panelMask&(1U<<(select-1))) ||
     ((trans>>24)&7)!=2 || ((trans>>20)&7)!=2)return d; // enabled DP SST eDP transcoder at 6 bpc
  d.hActive=(htotal&0x1FFF)+1;d.hTotal=((htotal>>16)&0x1FFF)+1;
  d.vActive=(vtotal&0x1FFF)+1;d.vTotal=((vtotal>>16)&0x1FFF)+1;
  d.valid=d.hActive<d.hTotal && d.vActive<d.vTotal;
  if(!d.valid)d={};
  return d;
 }
 // IODetailedTimingInformationV2 (see kern_dsi_apple_timing.hpp): active and
 // blanking at 0x40/0x44/0x50/0x54, pixelEncoding at 0x88 and
 // bitsPerColorComponent at 0x8A (single-bit selection; bit 0 is 6 bpc).
 bool matches(const uint8_t *t) const {
  if(!valid || !t)return false;
  uint32_t ha,hb,va,vb;
  memcpy(&ha,t+0x40,4);memcpy(&hb,t+0x44,4);memcpy(&va,t+0x50,4);memcpy(&vb,t+0x54,4);
  return ha==hActive && uint64_t(ha)+hb==hTotal && va==vActive && uint64_t(va)+vb==vTotal;
 }
 // Only RGB or unspecified encodings are changed; YCbCr and an existing
 // 6 bpc request are left untouched. Returns true when the timing changed.
 bool apply(uint8_t *t) const {
  if(!matches(t))return false;
  uint16_t encoding,depth;memcpy(&encoding,t+0x88,2);memcpy(&depth,t+0x8A,2);
  if(encoding>1 || (encoding==1 && depth==1))return false;
  encoding=1;depth=1;
  memcpy(t+0x88,&encoding,2);memcpy(t+0x8A,&depth,2);
  return true;
 }
};
// The firmware timing usually lives only in the panel's DisplayID extension
// (its pixel clock exceeds the 655.35 MHz an EDID DTD can carry), and macOS
// installs built-in panel modes from the base block alone.
struct EDPPanelTiming {
 bool valid {};
 uint64_t clock {}; // Hz
 uint32_t hBlank {},hSyncOffset {},hSyncWidth {},vBlank {},vSyncOffset {},vSyncWidth {};
};
// DisplayID 1.x section in an EDID extension block; Type I (tag 0x03)
// descriptors store every field minus one and 15-bit sync offsets.
inline EDPPanelTiming displayIDTiming(const uint8_t *ext,size_t length,const EDPBootDepth &boot) {
 EDPPanelTiming r;
 if(!boot.valid || !ext || length<128 || ext[0]!=0x70)return r;
 const size_t end=5+size_t(ext[2]);
 if(end>length-2)return r; // section checksum and EDID checksum follow
 for(size_t at=5;at+3<=end;) {
  const uint8_t tag=ext[at],bytes=ext[at+2];
  if(!tag && !bytes)break;
  if(at+3+bytes>end)break;
  if(tag==0x03 && bytes && !(bytes%20)) {
   for(size_t t=at+3;t+20<=at+3+bytes;t+=20) {
    const auto u16=[&](size_t o){return uint32_t(ext[t+o])|(uint32_t(ext[t+o+1])<<8);};
    const uint64_t clock=(uint64_t(ext[t])|(uint64_t(ext[t+1])<<8)|(uint64_t(ext[t+2])<<16))+1;
    const uint32_t ha=u16(4)+1,hb=u16(6)+1,ho=(u16(8)&0x7FFF)+1,hw=u16(10)+1;
    const uint32_t va=u16(12)+1,vb=u16(14)+1,vo=(u16(16)&0x7FFF)+1,vw=u16(18)+1;
    if(ha==boot.hActive && ha+hb==boot.hTotal && va==boot.vActive && va+vb==boot.vTotal) {
     r.valid=true;r.clock=clock*10000;
     r.hBlank=hb;r.hSyncOffset=ho;r.hSyncWidth=hw;r.vBlank=vb;r.vSyncOffset=vo;r.vSyncWidth=vw;
     return r;
    }
   }
  }
  at+=3+bytes;
 }
 return r;
}
// The donor caps an internal panel's timing range at 720 MHz (getOnlineInfo);
// IOFramebuffer clients drop any installed timing above it. Admit the panel
// timing the firmware already drives, rounded up to the next MHz.
inline uint64_t panelMaxPixelClock(uint64_t maximum,const EDPPanelTiming &panel) {
 if(!panel.valid || maximum>=panel.clock)return maximum;
 return (panel.clock+999999)/1000000*1000000;
}
// Clone one unscaled native IODetailedTimingInformationV2 entry of the
// installed mode list to the panel timing at RGB 6 bpc, with a new mode id.
inline bool panelTimingClone(const uint8_t *src,size_t length,uint8_t *dst,const EDPBootDepth &boot,
                             const EDPPanelTiming &panel,uint32_t id) {
 if(!src || !dst || length<0xA0 || !boot.valid || !panel.valid)return false;
 uint32_t ha,va,hs,vs;uint64_t clock;
 memcpy(&ha,src+0x40,4);memcpy(&va,src+0x50,4);memcpy(&hs,src+0x18,4);memcpy(&vs,src+0x1C,4);
 memcpy(&clock,src+0x28,8);
 if(ha!=boot.hActive || va!=boot.vActive || hs || vs || clock==panel.clock)return false;
 memcpy(dst,src,length);
 memcpy(dst,&id,4);
 memcpy(dst+0x28,&panel.clock,8);memcpy(dst+0x30,&panel.clock,8);memcpy(dst+0x38,&panel.clock,8);
 memcpy(dst+0x44,&panel.hBlank,4);memcpy(dst+0x48,&panel.hSyncOffset,4);memcpy(dst+0x4C,&panel.hSyncWidth,4);
 memcpy(dst+0x54,&panel.vBlank,4);memcpy(dst+0x58,&panel.vSyncOffset,4);memcpy(dst+0x5C,&panel.vSyncWidth,4);
 const uint16_t encoding=1,depth=1;
 memcpy(dst+0x88,&encoding,2);memcpy(dst+0x8A,&depth,2);
 return true;
}
#endif
