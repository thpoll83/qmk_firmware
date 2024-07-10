// fontconvert -f~/repos/noto-sans/NotoSans-Regular.ttf -s14 -v _LetterMod_ 0x2c6 0x2dd 
// Visualize your font via https://tchapi.github.io/Adafruit-GFX-Font-Customiser

const uint8_t NotoSans_Regular_LetterMod_14pt16bBitmaps[] PROGMEM = {
  0x1C, 0x1F, 0x0D, 0xDC, 0x70, 0xC1, 0xB9, 0x8F, 0x83, 0xC0, 0xFF, 0xFC,
  0xFF, 0xFF, 0x3C, 0xE7, 0x38, 0xE1, 0xC3, 0x07, 0xFF, 0xFF, 0xFF, 0xFF,
  0xE1, 0xC3, 0x07, 0x3C, 0xE7, 0x38, 0xFB, 0x98, 0x40, 0x00, 0x00, 0x00,
  0x00, 0x42, 0x3B, 0xE0, 0xFB, 0x98, 0x40, 0x99, 0xB7, 0x80, 0x3F, 0x4C,
  0xC0, 0x30, 0xC3, 0x3F, 0xFC, 0xFF, 0xF3, 0x0C, 0x30, 0x30, 0xCF, 0xCC,
  0x30, 0xFF, 0xF0, 0xC1, 0xBF, 0x9F, 0x80, 0xFD, 0x00, 0x7B, 0x3C, 0x73,
  0x78, 0x33, 0x31, 0x8F, 0xBC, 0x78, 0x5F, 0xFC, 0x78, 0x39, 0xCC, 0x66,
  0x33, 0x18 };

const GFXglyph NotoSans_Regular_LetterMod_14pt16bGlyphs[] PROGMEM = {
// bmpOff,   w,   h,xAdv, xOff, yOff      range 0 (0x2c6 - 0x2dd)
  {     0,   9,   4,  11,    1,  -20 },   // 0x2C6 circumflex  (#0)
  {     5,   9,   4,  11,    1,  -20 },   // 0x2C7 caron  (#1)
  {    10,   2,   7,   4,    1,  -20 },   // 0x2C8 uni02C8  (#2)
  {    12,   8,   2,  10,    1,  -18 },   // 0x2C9 macronmod  (#3)
  {    14,   6,   4,   8,    1,  -20 },   // 0x2CA acutemod  (#4)
  {    17,   6,   4,   8,    1,  -20 },   // 0x2CB gravemod  (#5)
  {    20,   2,   8,   4,    1,   -1 },   // 0x2CC uni02CC  (#6)
  {    22,   8,   2,  10,    1,    3 },   // 0x2CD uni02CD  (#7)
  {    24,   6,   4,   8,    1,    2 },   // 0x2CE uni02CE  (#8)
  {    27,   6,   4,   8,    1,    2 },   // 0x2CF uni02CF  (#9)
  {    30,   5,  15,   9,    2,  -14 },   // 0x2D0 uni02D0  (#10)
  {    40,   5,   4,   9,    2,  -14 },   // 0x2D1 uni02D1  (#11)
  {    43,   3,   6,   5,    1,  -11 },   // 0x2D2 uni02D2  (#12)
  {    46,   3,   6,   5,    1,  -11 },   // 0x2D3 uni02D3  (#13)
  {    49,   6,   5,   7,    1,  -10 },   // 0x2D4 uni02D4  (#14)
  {    53,   6,   5,   7,    1,  -10 },   // 0x2D5 uni02D5  (#15)
  {    57,   6,   5,   7,    1,  -10 },   // 0x2D6 uni02D6  (#16)
  {    61,   6,   2,   8,    1,   -8 },   // 0x2D7 uni02D7  (#17)
  {    63,   9,   3,  11,    1,  -19 },   // 0x2D8 breve  (#18)
  {    67,   3,   3,   5,    1,  -19 },   // 0x2D9 dotaccent  (#19)
  {    69,   6,   5,   8,    1,  -21 },   // 0x2DA ring  (#20)
  {    73,   5,   6,   7,    1,    1 },   // 0x2DB ogonek  (#21)
  {    77,  10,   3,  12,    1,  -19 },   // 0x2DC tilde  (#22)
  {    81,  10,   4,  12,    1,  -20 } }; // 0x2DD hungarumlaut  (#23)

const GFXfont NotoSans_Regular_LetterMod_14pt16b PROGMEM = {
  (uint8_t  *)NotoSans_Regular_LetterMod_14pt16bBitmaps,
  (GFXglyph *)NotoSans_Regular_LetterMod_14pt16bGlyphs,
  0x2C6, // first
  0x2DD, // last
  37   //height
 };

// Approx. 261 bytes
