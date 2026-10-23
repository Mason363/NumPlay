#!/usr/bin/env python3
"""Lossless standalone NWA packing. Python standard library + ARM binutils.

Keep the standard ELF/NWI formats: no runtime decoder, extra RAM, or archive.
"""
import argparse
from bisect import bisect_left, insort
import json
from pathlib import Path
import subprocess
import tempfile

ICON_BYTES = 55 * 56 * 2


def decode_lz4(data, expected_size):
    """Decode a raw LZ4 block, with an exact output bound for launcher icons."""
    out = bytearray()
    pos = 0

    def length(value):
        nonlocal pos
        if value == 15:
            while True:
                if pos >= len(data):
                    raise ValueError('Truncated LZ4 length')
                extra = data[pos]
                pos += 1
                value += extra
                if extra != 255:
                    break
        return value

    while pos < len(data):
        token = data[pos]
        pos += 1
        size = length(token >> 4)
        if pos + size > len(data) or len(out) + size > expected_size:
            raise ValueError('Invalid LZ4 literal run')
        out += data[pos:pos + size]
        pos += size
        if pos == len(data):
            break
        if pos + 2 > len(data):
            raise ValueError('Truncated LZ4 offset')
        distance = data[pos] | data[pos + 1] << 8
        pos += 2
        size = length(token & 15) + 4
        if not distance or distance > len(out) or len(out) + size > expected_size:
            raise ValueError('Invalid LZ4 match')
        for _ in range(size):
            out.append(out[-distance])
    if len(out) != expected_size:
        raise ValueError('Unexpected decoded icon size')
    return bytes(out)


def _extension_cost(value, threshold):
    return 0 if value < threshold else 1 + (value - threshold) // 255


def compress_lz4(data):
    """Find a minimum-size raw LZ4 parse for a small (<= 65535 byte) image.

    A suffix array finds the longest legal previous match. Dynamic programming
    chooses literal/match lengths, including extension-byte costs. The final
    five bytes are literals; the last match starts at least twelve bytes from
    the end, as required by standard LZ4 block decoders.
    """
    n = len(data)
    if n > 65535:
        raise ValueError('Only small build-time assets are supported')
    if not n:
        return b'\0'
    suffixes = sorted(range(n), key=lambda i: data[i:])
    rank = [0] * n
    for r, pos in enumerate(suffixes):
        rank[pos] = r
    seen = []
    matches = [(0, 0)] * n
    for i in range(n - 11):
        r = rank[i]
        at = bisect_left(seen, r)
        best, distance = 0, 0
        # Among earlier suffixes, a lexicographic neighbour has the longest
        # common prefix. Overlapping matches are legal in LZ4.
        for q in seen[max(0, at - 1):at + 1]:
            j = suffixes[q]
            lo, hi = 0, n - i - 5
            while lo < hi:
                mid = (lo + hi + 1) // 2
                if data[i:i + mid] == data[j:j + mid]:
                    lo = mid
                else:
                    hi = mid - 1
            if lo > best:
                best, distance = lo, i - j
        matches[i] = best, distance
        insort(seen, r)

    dp = [0] * (n + 1)
    plan = [None] * n
    match_cost = [0] * n
    match_length = [0] * n
    starts = []
    for i in range(n - 1, -1, -1):
        best = n - i + 1 + _extension_cost(n - i, 15)
        longest, _ = matches[i]
        if longest >= 4:
            cost, size = min((2 + _extension_cost(size, 19) + dp[i + size], size)
                             for size in range(4, longest + 1))
            match_cost[i], match_length[i] = cost, size
            starts.append(i)
        for j in reversed(starts):
            literal = j - i
            if literal + 3 >= best:
                break
            cost = 1 + literal + _extension_cost(literal, 15) + match_cost[j]
            if cost < best:
                best = cost
                plan[i] = j, match_length[j], matches[j][1]
        dp[i] = best

    out = bytearray()

    def emit_length(value):
        while value >= 255:
            out.append(255)
            value -= 255
        out.append(value)

    anchor = 0
    while anchor < n:
        entry = plan[anchor]
        if entry is None:
            literal = n - anchor
            out.append(min(literal, 15) << 4)
            if literal >= 15:
                emit_length(literal - 15)
            out += data[anchor:]
            break
        j, size, distance = entry
        literal = j - anchor
        out.append(min(literal, 15) << 4 | min(size - 4, 15))
        if literal >= 15:
            emit_length(literal - 15)
        out += data[anchor:j]
        out += bytes((distance & 255, distance >> 8))
        if size >= 19:
            emit_length(size - 19)
        anchor = j + size
    if len(out) != dp[0] or decode_lz4(out, n) != data:
        raise ValueError('Lossless compression verification failed')
    return bytes(out)


def compact_icon(source, output, objcopy):
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent) as folder:
        folder = Path(folder)
        original, packed, obj = folder/'original.nwi', folder/'packed.nwi', folder/'icon.o'
        section = '.rodata.eadk_app_icon'
        subprocess.run([objcopy, '--dump-section=' + section + '=' + str(original),
                        str(source), str(obj)], check=True)
        before = original.read_bytes()
        pixels = decode_lz4(before, ICON_BYTES)
        after = compress_lz4(pixels)
        if len(after) > len(before):
            after = before
        packed.write_bytes(after)
        subprocess.run([objcopy, '--update-section=' + section + '=' + str(packed),
                        str(obj), str(output)], check=True)
        # Verify the bytes actually stored in the resulting ELF, too.
        subprocess.run([objcopy, '--dump-section=' + section + '=' + str(packed),
                        str(output), str(folder/'verify.o')], check=True)
        if decode_lz4(packed.read_bytes(), ICON_BYTES) != pixels:
            raise ValueError('Packed icon pixels changed')
    return dict(source_icon_bytes=len(before), icon_bytes=len(after), decoded_icon_bytes=ICON_BYTES)


def compact_app(source, output, linker):
    output.parent.mkdir(parents=True, exist_ok=True)
    script = Path(__file__).with_name('standalone.ld')
    with tempfile.TemporaryDirectory(dir=output.parent) as folder:
        packed = Path(folder)/'packed.nwa'
        subprocess.run([linker, '--relocatable', '--script', str(script), str(source),
                        '-o', str(packed)], check=True)
        output.write_bytes(packed.read_bytes())


def compact_release(source, output, objcopy):
    """Remove unused ARM mapping symbols from the transport ELF only.

    GNU objcopy preserves $t/$d symbols specially even when unused. Rename
    them in a temporary release copy, then let objcopy decide which symbols
    relocations still need. Keep the debug ELF intact for ARM disassembly.
    Python builders compare release/debug bytes on all supported profiles;
    Make's check target compares them on the smallest RAM profile.
    """
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent) as folder:
        folder = Path(folder)
        stripped, renamed = folder/'stripped.nwa', folder/'renamed.nwa'
        subprocess.run([objcopy, '--strip-unneeded', str(source), str(stripped)], check=True)
        subprocess.run([objcopy, '--redefine-sym=$t=unused_thumb_marker',
                        '--redefine-sym=$d=unused_data_marker', str(stripped),
                        str(renamed)], check=True)
        subprocess.run([objcopy, '--strip-unneeded', str(renamed), str(output)], check=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('kind', choices=('icon', 'app', 'release'))
    ap.add_argument('source', type=Path)
    ap.add_argument('output', type=Path)
    ap.add_argument('--objcopy', default='arm-none-eabi-objcopy')
    ap.add_argument('--ld', default='arm-none-eabi-ld')
    args = ap.parse_args()
    if args.kind == 'icon':
        print(json.dumps(compact_icon(args.source, args.output, args.objcopy)))
    elif args.kind == 'release':
        compact_release(args.source, args.output, args.objcopy)
    else:
        compact_app(args.source, args.output, args.ld)


if __name__ == '__main__':
    main()
