#pragma once

#define HID_CMD_IDX 1
#define HID_DATA_IDX 2
#define LANG_TO_UI32(a,b,c,d) (((uint32_t)(a))<<24 | ((uint32_t)(b))<<16 | ((uint32_t)(c))<<8 | (d))
#define LANG_TO_UI32_ARR(arr) (((uint32_t)(arr[0]))<<24 | ((uint32_t)(arr[1]))<<16 | ((uint32_t)(arr[2]))<<8 | (arr[3]))


