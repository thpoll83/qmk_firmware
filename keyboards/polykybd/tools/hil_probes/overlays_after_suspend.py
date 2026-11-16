"""Do the app overlays come back on after a suspend?

poly_suspend() (the 20 min turn-off, USB suspend, host display-off cmd 24)
clears DISPLAY_OVERLAYS; the fix remembers it and a wake prints
"Wake: overlays back on" when it turns them back on. Case 1 must print the
line; case 2 (overlays turned off by the host while suspended) must not.
The permanent version is polykybd-ctnd test_overlays_back_after_suspend.
"""
import time

from station.console_log import TAP

NAME = "overlays back on after a suspend"
NEEDS_CONSOLE = True

P = 0x50
GET_ID, FLAGS_ON, FLAGS_OFF, IDLE_STATE, DISPLAY_OFF = 6, 11, 12, 15, 24
DISPLAY_OVERLAYS = 0x01
LINE = "Wake: overlays back on"


def _ack(raw, log, label, request, cmd):
    reply = raw.send(bytes(request))
    log(f"  {label} -> {reply!r}")
    return reply is not None and len(reply) >= 3 and reply[0] == P and reply[1] == cmd and reply[2] == ord(".")


def _round(raw, log, clear_while_suspended):
    if not _ack(raw, log, "overlays on (cmd 11)", [P, FLAGS_ON, DISPLAY_OVERLAYS], FLAGS_ON):
        return None
    if not _ack(raw, log, "display off (cmd 24)", [P, DISPLAY_OFF], DISPLAY_OFF):
        return None
    if clear_while_suspended and not _ack(raw, log, "overlays off while suspended (cmd 12)",
                                          [P, FLAGS_OFF, DISPLAY_OVERLAYS], FLAGS_OFF):
        return None
    mark = TAP.mark()
    if not _ack(raw, log, "wake (cmd 15 stop idle)", [P, IDLE_STATE, 0], IDLE_STATE):
        return None
    time.sleep(3.0)
    lines = list(TAP.since(mark))
    for line in lines:
        log(f"  console: {line.strip()}")
    return any(LINE in line for line in lines)


def probe(raw, log):
    try:
        back = _round(raw, log, clear_while_suspended=False)
        log(f"case 1 (overlays on before the suspend): line seen = {back}")
        if back is not True:
            return False
        back = _round(raw, log, clear_while_suspended=True)
        log(f"case 2 (host turned them off while suspended): line seen = {back}")
        if back is not False:
            return False
        return raw.send(bytes([P, GET_ID])) is not None
    finally:
        raw.send(bytes([P, FLAGS_OFF, DISPLAY_OVERLAYS]))
        raw.send(bytes([P, IDLE_STATE, 0]))
