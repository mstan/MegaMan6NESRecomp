"""Validate the supported original USA cartridge payload before generation."""
from pathlib import Path
import hashlib
import sys


def main():
    data = Path(sys.argv[1]).read_bytes()
    if len(data) < 16 or data[:4] != b'NES\x1a':
        raise SystemExit('An iNES Mega Man 6 (USA) ROM is required')
    start = 16 + (512 if data[6] & 4 else 0)
    size = data[4] * 16384 + data[5] * 8192
    mapper = (data[6] >> 4) | (data[7] & 0xf0)
    if mapper != 4 or len(data) != start + size:
        raise SystemExit('Mega Man 6 requires the original MMC3 cartridge layout')
    if hashlib.sha1(data[start:]).hexdigest() != '26d33987cff21c651fe3d638f4a739c33a18c240':
        raise SystemExit('Unsupported cartridge payload; supply Mega Man 6 (USA)')
    print('NTSC;usa')


if __name__ == '__main__':
    main()
