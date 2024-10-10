// fontconvert -ffonts/noto-sans-jp/NotoSansJP-Regular.ttf -s15 -v _Punct2_ 0x30fb 0x30fc 
// Visualize your font via https://tchapi.github.io/Adafruit-GFX-Font-Customiser

/* num ranges: 1 */
const uint8_t NotoSansJP_Regular_Punct2_15pt16bBitmaps[] PROGMEM = {
  /* range 0 (0x30fb - 0x30fc): */  0x7B, 0xFF, 0xFF, 0xF9, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xF8
 };

const GFXglyph NotoSansJP_Regular_Punct2_15pt16bGlyphs[] PROGMEM = {
// bmpOff,   w,   h,xAdv, xOff, yOff      range 0 (0x30fb - 0x30fc)
  {     0,   6,   6,  29,   12,  -13 },   // 0x30FB   (#0)
  {     5,  23,   3,  29,    3,  -12 } }; // 0x30FC   (#1)

const GFXfont NotoSansJP_Regular_Punct2_15pt16b PROGMEM = {
  (uint8_t  *)NotoSansJP_Regular_Punct2_15pt16bBitmaps,
  (GFXglyph *)NotoSansJP_Regular_Punct2_15pt16bGlyphs,
  0x30FB, // first
  0x30FC, // last
  43   //height
 };

// Approx. 35 bytes
