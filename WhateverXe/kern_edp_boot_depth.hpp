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
#endif
