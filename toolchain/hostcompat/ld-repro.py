#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
"""Give ld64 stable object paths before it writes debug maps or signatures.

Keep workspace aliases available for the subsequent dsymutil invocation.
The linker still reads every original object and archive; no output bytes
are rewritten and no debug/symbol records are discarded.
"""
import os
from pathlib import Path
import subprocess
import sys


def main():
    real = os.environ['PIE_REAL_TARGET_LD']
    if sys.argv[1:] in (['-v'], ['-version'], ['--version'], ['-help']):
        return subprocess.run([real, *sys.argv[1:]]).returncode
    working = Path.cwd()
    directory = working / '.pie-link-inputs'
    if directory.is_symlink():
        raise ValueError('link input directory must not be a symlink')
    directory.mkdir(exist_ok=True)
    mappings = []
    for name, variable in (('obj', 'PIE_REPRO_OBJROOT'), ('sdk', 'PIE_SDKROOT')):
        root = Path(os.environ[variable]).resolve(strict=True)
        alias = directory / name
        if alias.is_symlink():
            if alias.resolve(strict=True) != root:
                raise ValueError('existing debug input alias points elsewhere: ' + str(alias))
        elif alias.exists():
            raise ValueError('debug input alias conflicts with a file: ' + str(alias))
        else:
            alias.symlink_to(root, target_is_directory=True)
        mappings.append((root, Path('.pie-link-inputs') / name))
    mappings.sort(key=lambda item: len(item[0].parts), reverse=True)

    def mapped(value, directory_operand=False):
        path = Path(value)
        if not path.is_absolute() and '/' not in value and not directory_operand and path.suffix not in ('.o', '.a', '.dylib', '.tbd'):
            return value
        if not path.is_absolute():
            path = working / path
        path = path.resolve()
        for root, alias in mappings:
            if path.is_relative_to(root):
                return str(alias / path.relative_to(root))
        return value

    args, index, filelists = [], 0, 0
    original = sys.argv[1:]
    while index < len(original):
        value = original[index]
        if value == '-filelist':
            index += 1
            source = Path(original[index])
            if ',' in str(source):
                raise ValueError('filelist base directories require explicit expanded inputs')
            target = directory / ('filelist-' + str(filelists))
            target.write_text(''.join(mapped(line) + '\n' for line in source.read_text().splitlines()))
            args += [value, str(target)]
            filelists += 1
        elif value in ('-o', '-map', '-dependency_info', '-oso_prefix'):
            # Output names remain exactly as requested; replace any older
            # debug-map prefix below with this invocation's working directory.
            index += 1
            if value != '-oso_prefix':
                args += [value, original[index]]
        elif value.startswith(('-L', '-F')) and len(value) > 2:
            args.append(value[:2] + mapped(value[2:], directory_operand=True))
        elif value.startswith('-'):
            args.append(value)
        else:
            args.append(mapped(value))
        index += 1
    args += ['-oso_prefix', str(working) + '/']
    return subprocess.run([real, *args]).returncode


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, OSError, KeyError, IndexError) as error:
        raise SystemExit('ld-repro: ' + str(error))
