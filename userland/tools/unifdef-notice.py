#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
"""usage: unifdef-notice.py HEADER UNIFDEF-FLAGS...

Add a dated modification notice to HEADER, which unifdef changed, after its
licence header: Apache-2.0 section 4(b) and APSL 2.0 section 2.2(b) ask for
one in each modified file. A header without a licence header gets it at the
top. The copyright files give such Apple files APSL-2.0.
"""
import sys

DATE = "2026-11-02"
MARKERS = (("@APPLE_APACHE_LICENSE_HEADER_END@", "Apache-2.0 section 4(b)"),
           ("@APPLE_OSREFERENCE_LICENSE_HEADER_END@", "APSL 2.0 section 2.2(b)"),
           ("@APPLE_LICENSE_HEADER_END@", "APSL 2.0 section 2.2(b)"))

path, flags = sys.argv[1], " ".join(sys.argv[2:])
text = open(path).read()
for marker, section in MARKERS:
    at = text.find(marker)
    if at >= 0:
        end = text.find("*/", at)
        if end < 0:
            sys.exit("unifdef-notice.py: %s: the licence header does not end" % path)
        end = text.find("\n", end) + 1 or len(text)
        break
else:
    if "LICENSE" in text:
        sys.exit("unifdef-notice.py: %s: a licence header it does not know" % path)
    end, section = 0, "APSL 2.0 section 2.2(b)"
notice = "/* Modified %s: unifdef %s (%s). */\n" % (DATE, flags, section)
open(path, "w").write(text[:end] + notice + text[end:])
