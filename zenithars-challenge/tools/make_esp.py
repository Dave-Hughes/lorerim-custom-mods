"""
Writes dist/ZenitharsChallenge.esp: the two records the mod needs that a plugin DLL cannot make on
its own, so the player can SEE that the challenge is on.

  xx000800  MGEF  Zenithar's Challenge   the line under Active Effects
  xx000801  SPEL  Zenithar's Challenge   an ability carrying that effect (added by the DLL)
  xx000802  MGEF  Zenithar's Ledger      the power's own (hidden) effect
  xx000803  SPEL  Zenithar's Ledger      a lesser power; the DLL shows the run so far when it is used

Light (ESL-flagged), one master (Skyrim.esm), no scripts. The byte layouts are copied from records
in Callings.esp that Mutagen wrote and LoreRim loads (same flags, same 152-byte effect data).

  python3 tools/make_esp.py
"""
import os
import struct

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'dist', 'ZenitharsChallenge.esp')
OWN = 0x01000000          # our forms: one master, so load index 01
VOICE_SLOT = 0x00025BEE   # Skyrim.esm equip slot for powers and shouts


def z(text):
    return text.encode('cp1252') + b'\x00'


def sub(name, data):
    return name + struct.pack('<H', len(data)) + data


def record(kind, form, body, flags=0):
    return kind + struct.pack('<IIIIHH', len(body), flags, form, 0, 44, 0) + body


def group(kind, records):
    body = b''.join(records)
    return b'GRUP' + struct.pack('<I', 24 + len(body)) + kind + struct.pack('<III', 0, 0, 0) + body


def effect_data(flags, cast_type):
    """The 152 bytes of a magic effect: a scripted effect on self that does nothing."""
    f = [0] * 38
    f[0] = flags
    f[3] = f[4] = 0xFFFFFFFF      # no magic skill, no resistance
    f[16] = 1                     # archetype: Script
    f[17] = 0xFFFFFFFF            # no actor value
    f[20] = cast_type             # 0 constant effect, 1 fire and forget
    f[22] = 0xFFFFFFFF            # no second actor value
    return struct.pack('<38I', *f)


def spell_data(kind, cast_type):
    # cost, flags, type (3 lesser power, 4 ability), charge time, cast type, delivery (self), cast duration, range, half-cost perk
    return struct.pack('<IIIfIIffI', 0, 0, kind, 0.0, cast_type, 0, 0.0, 0.0, 0)


NO_DURATION_MAGNITUDE_AREA = 0x00000E00
PAINLESS = 0x04000000
HIDE_IN_UI = 0x00008000

ABOUT = "You have taken Zenithar's Challenge: craft everything you use. What you did not make cannot be used or sold."

effects = [
    record(b'MGEF', OWN | 0x800,
           sub(b'EDID', z('Zenithar_ChallengeEffect')) + sub(b'FULL', z("Zenithar's Challenge")) +
           sub(b'DATA', effect_data(NO_DURATION_MAGNITUDE_AREA | PAINLESS, 0)) + sub(b'DNAM', z(ABOUT))),
    record(b'MGEF', OWN | 0x802,
           sub(b'EDID', z('Zenithar_LedgerEffect')) + sub(b'FULL', z("Zenithar's Ledger")) +
           sub(b'DATA', effect_data(NO_DURATION_MAGNITUDE_AREA | PAINLESS | HIDE_IN_UI, 1)) + sub(b'DNAM', z(''))),
]
no_magnitude = struct.pack('<fII', 0.0, 0, 0)
spells = [
    record(b'SPEL', OWN | 0x801,
           sub(b'EDID', z('Zenithar_ChallengeAbility')) + sub(b'OBND', bytes(12)) + sub(b'FULL', z("Zenithar's Challenge")) +
           sub(b'DESC', z(ABOUT)) + sub(b'SPIT', spell_data(4, 0)) +
           sub(b'EFID', struct.pack('<I', OWN | 0x800)) + sub(b'EFIT', no_magnitude)),
    record(b'SPEL', OWN | 0x803,
           sub(b'EDID', z('Zenithar_LedgerPower')) + sub(b'OBND', bytes(12)) + sub(b'FULL', z("Zenithar's Ledger")) +
           sub(b'ETYP', struct.pack('<I', VOICE_SLOT)) +
           sub(b'DESC', z('Look over your run: what you have made, the rules you chose and any you have broken.')) +
           sub(b'SPIT', spell_data(3, 1)) +
           sub(b'EFID', struct.pack('<I', OWN | 0x802)) + sub(b'EFIT', no_magnitude)),
]

header = record(b'TES4', 0,
                sub(b'HEDR', struct.pack('<fII', 1.71, len(effects) + len(spells), 0x804)) +
                sub(b'CNAM', z('Dave')) + sub(b'SNAM', z("Zenithar's Challenge: the sign that the challenge is on, and the Ledger power.")) +
                sub(b'MAST', z('Skyrim.esm')) + sub(b'DATA', bytes(8)),
                flags=0x200)  # light

with open(OUT, 'wb') as out:
    out.write(header + group(b'MGEF', effects) + group(b'SPEL', spells))
print('wrote', os.path.normpath(OUT), os.path.getsize(OUT), 'bytes')
