# FreeBSD qsort_r.c and block_abi.h

From https://github.com/freebsd/freebsd-src, tag release/13.5.0, commit
882b9f3f2218b50fc1d2d31ee71b7765c7f09f85:

- `lib/libc/stdlib/qsort_r.c`, sha256
  f769a40c2c9de72cf9d86e28515ff697e7b3214e0cc919287119fb6d2b296c70. It is in
  the public domain. Garrett A. Wollman wrote its first version, and David
  Chisnall added qsort_b.
- `lib/libc/include/block_abi.h`, sha256
  16097c65c14327667f2e47c9588ca33fcaa14fef02e1ad34688be31c41dc8812. Licence:
  BSD-2-Clause. Its holder is David T Chisnall, 2014.

Both files are unmodified. qsort_r.c builds qsort_r from Libc's own
`stdlib/FreeBSD/qsort.c`, and qsort_b on top of it.
