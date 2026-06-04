// fontconvert -ffonts/Noto_Sans_Hebrew/static/NotoSansHebrew-Medium.ttf -s22 -v _HebrewDots_ 0x5b0 0x5c7 
// Visualize your font via https://tchapi.github.io/Adafruit-GFX-Font-Customiser

/* num ranges: 1 */
const uint8_t NotoSansHebrew_Medium_HebrewDots_22pt16bBitmaps[] PROGMEM = {
  /* range 0 (0x5b0 - 0x5c7): */  0xF0, 0xF0, 0xCC, 0xF3, 0x30, 0x00, 0x00, 0x30, 0xCC, 0x20, 0x00, 0xFF,
  0x30, 0x00, 0x00, 0x00, 0xC0, 0x30, 0xFE, 0xFF, 0xB1, 0x00, 0x40, 0x10,
  0xC0, 0x30, 0xF0, 0xCF, 0x30, 0xCF, 0x30, 0x00, 0x30, 0xC0, 0xFE, 0xFE,
  0x20, 0x40, 0x81, 0x00, 0xF0, 0xF0, 0xC1, 0x80, 0x40, 0xC0, 0x60, 0xC0,
  0xF0, 0xFC, 0xFF, 0xC0, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xF0,
  0xF0, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x3C, 0xF0, 0xF0, 0x1F, 0x0C, 0x04,
  0x02, 0x00, 0x80, 0x20, 0x08, 0x02, 0x00, 0x80, 0x20, 0x08, 0x02, 0x00,
  0x80, 0x20, 0x08, 0x02, 0x00, 0x80, 0x20, 0x08, 0x02, 0x00, 0x80, 0x20,
  0x08, 0x02, 0x00, 0xFF, 0xC0, 0xF2, 0x20
 };

const GFXglyph NotoSansHebrew_Medium_HebrewDots_22pt16bGlyphs[] PROGMEM = {
// bmpOff,   w,   h,xAdv, xOff, yOff      range 0 (0x5b0 - 0x5c7)
  {     0,   2,   6,   0,    2,    4 },   // 0x5B0 uni05B0  (#0)
  {     2,  10,   6,   0,    2,    4 },   // 0x5B1 uni05B1  (#1)
  {    10,  10,   6,   0,    2,    4 },   // 0x5B2 uni05B2  (#2)
  {    18,  10,   6,   0,    2,    4 },   // 0x5B3 uni05B3  (#3)
  {    26,   2,   2,   0,    2,    4 },   // 0x5B4 uni05B4  (#4)
  {    27,   6,   2,   0,    2,    4 },   // 0x5B5 uni05B5  (#5)
  {    29,   6,   6,   0,    2,    4 },   // 0x5B6 uni05B6  (#6)
  {    34,   7,   1,   0,    2,    5 },   // 0x5B7 uni05B7  (#7)
  {    35,   7,   5,   0,    2,    5 },   // 0x5B8 uni05B8  (#8)
  {    40,   2,   2,   0,    2,  -29 },   // 0x5B9 uni05B9  (#9)
  {    41,   2,   2,   0,    2,  -29 },   // 0x5BA uni05BA  (#10)
  {    42,   7,   6,   0,    2,    4 },   // 0x5BB uni05BB  (#11)
  {    48,   2,   2,   0,    2,  -13 },   // 0x5BC uni05BC  (#12)
  {    49,   1,   6,   0,    2,    4 },   // 0x5BD uni05BD  (#13)
  {    50,  10,   1,  14,    2,  -24 },   // 0x5BE uni05BE  (#14)
  {    52,   7,   1,   0,    2,  -29 },   // 0x5BF uni05BF  (#15)
  {    53,   1,  44,  23,   11,  -32 },   // 0x5C0 uni05C0  (#16)
  {    59,   2,   2,   0,    2,  -29 },   // 0x5C1 uni05C1  (#17)
  {    60,   2,   2,   0,    2,  -29 },   // 0x5C2 uni05C2  (#18)
  {    61,   2,  23,   9,    4,  -22 },   // 0x5C3 uni05C3  (#19)
  {    67,   2,   2,   0,    2,  -29 },   // 0x5C4 uni05C4  (#20)
  {    68,   2,   2,   0,    2,    4 },   // 0x5C5 uni05C5  (#21)
  {    69,  10,  25,  15,    4,  -24 },   // 0x5C6 uni05C6  (#22)
  {   101,   4,   3,   0,    2,    5 } }; // 0x5C7 uni05C7  (#23)

const GFXfont NotoSansHebrew_Medium_HebrewDots_22pt16b PROGMEM = {
  (uint8_t  *)NotoSansHebrew_Medium_HebrewDots_22pt16bBitmaps,
  (GFXglyph *)NotoSansHebrew_Medium_HebrewDots_22pt16bGlyphs,
  0x5B0, // first
  0x5C7, // last
  59   //height
 };

// Approx. 278 bytes
