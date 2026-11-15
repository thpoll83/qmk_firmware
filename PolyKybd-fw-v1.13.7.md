# PolyKybd v1.13.7 Host tutorials
The protocol moves to 35. PolyKybdHost 1.26.0 uses it to show tutorial text on the status displays. Older hosts from 1.22.16 on still work with this firmware.

## 1.13.7: Host tutorials 🎓
- **Tutorial text on the status displays:** the host shows two lines per step while a tutorial runs (cmd 49).
- **Spotlight:** every key except the ones the step asks for goes dark, and a step can end with the wave that lights the whole board again.
- **Safe stop:** if the host stops for 10 s, the keyboard clears the text and lights all keys again.
