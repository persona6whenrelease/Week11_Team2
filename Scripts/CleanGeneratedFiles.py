#!/usr/bin/env python3
"""
CleanGeneratedFiles.py

Deletes all *.generated.h and *.generated.cpp files under KraftonEngine/Source.

Usage:
    python Scripts/CleanGeneratedFiles.py [--source-root PATH] [--dry-run]
"""

import os
import sys
import argparse


def main():
    script_dir  = os.path.dirname(os.path.abspath(__file__))
    repo_root   = os.path.dirname(script_dir)
    default_src = os.path.join(repo_root, 'KraftonEngine', 'Source')

    ap = argparse.ArgumentParser(
        description='Delete all *.generated.h / *.generated.cpp files.'
    )
    ap.add_argument('--source-root', default=default_src, metavar='PATH')
    ap.add_argument('--dry-run', action='store_true',
                    help='Print what would be deleted without removing files')
    args = ap.parse_args()

    source_root = os.path.abspath(args.source_root)
    if not os.path.isdir(source_root):
        print(f'ERROR: source root not found: {source_root}', file=sys.stderr)
        sys.exit(1)

    dry  = args.dry_run
    mode = 'dry-run' if dry else 'deleting'
    print(f'[CleanGeneratedFiles] {mode}  source-root={source_root}')

    count = 0
    for dirpath, _, filenames in os.walk(source_root):
        for fname in sorted(filenames):
            if '.generated.' in fname and (fname.endswith('.h') or fname.endswith('.cpp')):
                path = os.path.join(dirpath, fname)
                rel  = os.path.relpath(path)
                if dry:
                    print(f'  [del] {rel}')
                else:
                    os.remove(path)
                    print(f'  deleted {rel}')
                count += 1

    print(f'\nDone. {count} file(s) {"would be " if dry else ""}deleted.')


if __name__ == '__main__':
    main()
