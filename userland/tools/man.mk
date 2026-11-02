# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

define install_xcode_man
	rm -rf $(OBJ)/man-dst
	python3 $(PIE)/userland/tools/pbxproj-man.py -d $(OBJ)/man-dst $(1) $(2)
	install -d $(ROOT)/usr/share/man
	cp -R $(OBJ)/man-dst/usr/share/man/. $(ROOT)/usr/share/man/
endef

define install_man_list
	grep -v -E '^(#|[[:space:]]*$$)' $(1) | while read -r src dst links; do \
		sect=$${src##*.}; d=$(ROOT)/usr/share/man/man$$sect; \
		install -d $$d && install -m 644 $(2)/$$src $$d/$$dst || exit 1; \
		for l in $$links; do ln -sf $$src $$d/$$l || exit 1; done; \
	done
endef
