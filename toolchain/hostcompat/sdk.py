#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
"""Portable workspace SDK merging and framework/availability maintenance."""
import argparse
import os
from pathlib import Path
import shutil


def remove(path):
    if path.is_dir() and not path.is_symlink():
        path.chmod(0o755)
        shutil.rmtree(path)
    elif path.exists() or path.is_symlink():
        path.unlink()


def merge(source, destination):
    src, dst = source.resolve(), destination.resolve()
    if src == dst or src in dst.parents or dst in src.parents:
        raise ValueError('SDK source and destination trees must be separate')
    if not source.is_dir() or source.is_symlink():
        raise ValueError('SDK merge input must be a directory')
    if destination.is_symlink() or destination.exists() and not destination.is_dir():
        remove(destination)
    destination.mkdir(parents=True, exist_ok=True)
    destination.chmod(0o755)
    for original in sorted(source.iterdir()):
        target = destination / original.name
        if original.is_symlink():
            remove(target)
            target.symlink_to(os.readlink(original))
        elif original.is_dir():
            merge(original, target)
        elif original.is_file():
            remove(target)
            shutil.copy2(original, target)
        else:
            raise ValueError('unsupported SDK input type: ' + str(original))


def frameworks(sdk):
    for framework in sorted((sdk / 'System/Library/Frameworks').glob('*.framework')):
        versions = framework / 'Versions'
        if not versions.is_dir():
            continue
        names = sorted(p.name for p in versions.iterdir() if p.name != 'Current' and p.is_dir())
        if not names:
            continue
        version = versions / names[0]
        for kind in ('Headers', 'PrivateHeaders'):
            flat, target = framework / kind, version / kind
            if flat.is_dir() and not flat.is_symlink():
                merge(flat, target)
                remove(flat)
            if flat.is_symlink():
                flat.unlink()
            if target.is_dir():
                flat.symlink_to('Versions/Current/' + kind)
        current = versions / 'Current'
        if current.exists() and not current.is_symlink():
            raise ValueError('framework Current must be a symlink: ' + str(current))
        remove(current)
        current.symlink_to(names[0])


def xros(sdk):
    path = sdk / 'usr/include/AvailabilityInternal.h'
    text = path.read_text()
    if '__API_AVAILABLE_PLATFORM_xros' in text:
        return
    anchor = '#define __API_UNAVAILABLE_PLATFORM_visionos visionos,unavailable'
    if text.count(anchor) != 1:
        raise ValueError('unexpected availability alias definition')
    aliases = ('\n#define __API_AVAILABLE_PLATFORM_xros(x) visionos,introduced=x'
               '\n#define __API_DEPRECATED_PLATFORM_xros(x,y) visionos,introduced=x,deprecated=y'
               '\n#define __API_OBSOLETED_PLATFORM_xros(x,y,z) visionos,introduced=x,deprecated=y,obsoleted=z'
               '\n#define __API_UNAVAILABLE_PLATFORM_xros visionos,unavailable')
    end = ' * @APPLE_LICENSE_HEADER_END@\n */\n'
    if text.count(end) != 1:
        raise ValueError('unexpected licence header')
    notice = ('/* Modified 2026-11-02: the xros aliases of the visionos availability macros'
              ' (APSL 2.0 section 2.2(b)). */\n')
    mode = path.stat().st_mode & 0o777
    path.unlink()
    path.write_text(text.replace(anchor, anchor + aliases).replace(end, end + notice))
    path.chmod(mode)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('merge', 'frameworks', 'xros'))
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', nargs='?', type=Path)
    args = parser.parse_args()
    if args.action == 'merge':
        if args.destination is None:
            parser.error('merge needs source and destination')
        merge(args.source, args.destination)
    else:
        globals()[args.action](args.source)
