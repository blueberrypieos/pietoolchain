# Arm optimized-routines

From https://github.com/ARM-software/optimized-routines, tag v26.07
(commit 4be260a5117480382690c6d8c300bc784e927d76), `string/aarch64`:
`memchr.S`, `memcmp.S`, `memcpy-advsimd.S`, `memset.S`, `strchr.S`,
`strcmp.S`, `strcpy.S`, `strlen.S`, `strncmp.S` and `strnlen.S`. Licence:
MIT OR Apache-2.0 WITH LLVM-exception (`LICENSE`).

`memcmp.S`, `strcmp.S` and `strncmp.S` were modified to be compatible with
memcmp(3), strcmp(3) and strncmp(3) as the Darwin man pages document them:
they return the difference of the first two differing bytes. The files
include `../asmdefs.h` in place of Arm's.
