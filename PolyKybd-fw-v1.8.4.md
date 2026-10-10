# PolyKybd v1.8.4 Dimmed browser icons, stall recovery

**Protocol moves to 23.** Update PolyKybdHost to 1.17.0 first, then this firmware. ⚠️ With PolyKybdHost 1.15.8 or older, OS shortcut hints stay blank, because from 1.8.0 their icons come from a font that only app 1.16.0 and later installs.

## 1.8.3: Website overlays stand out 🌐
- **On a website, the browser's own icons draw dimmed under the site's shortcuts.** The key legend keeps full strength. This needs PolyKybdHost 1.16.2 or later.

## 1.8.1 – 1.8.4: The keyboard keeps listening 🔧
- **The keyboard now recovers by itself when its second core stalls.** Before, typing kept working, but every app update timed out until you replugged.
- **Retrying an unsigned firmware image after refusing it works again.** The keycaps no longer wake and redraw in the middle of the flash.
- **Resetting the keymap from the app no longer floods the link between the halves with retries.**
- The boot banner now names the exact build, and prints again when the app reconnects.

## 1.8.0: One icon family for OS shortcut hints ✨
- **Every OS shortcut hint now uses one icon style**, drawn from the symbol font bundle. The firmware image is 9 KB smaller.
- Ctrl/Alt (Cmd on macOS) + letter, F2 and F5 no longer have built-in hints. The app overlays show those per app.
