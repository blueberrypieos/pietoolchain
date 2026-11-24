# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

# $(call install_xcode_man,PROJECT[,TARGETS]): the man pages the project's
# man phase installs. PBXPROJ_MAN_FLAGS holds pbxproj-man.py's --man-list,
# --omit and --replace options.
define install_xcode_man
	rm -rf $(OBJ)/man-dst
	python3 $(PIE)/userland/tools/pbxproj-man.py -d $(OBJ)/man-dst $(PBXPROJ_MAN_FLAGS) $(1) $(2)
	install -d $(ROOT)/usr/share/man
	cp -R $(OBJ)/man-dst/usr/share/man/. $(ROOT)/usr/share/man/
endef

# $(call install_man_list,LIST,DIR[,MODE]): each LIST line is "source page link...".
# source is under DIR, page is its installed name, and each link is a symlink to
# page. MODE defaults to 644.
define install_man_list
	grep -v -E '^(#|[[:space:]]*$$)' $(1) | while read -r src dst links; do \
		sect=$${src##*.}; d=$(ROOT)/usr/share/man/man$$sect; \
		install -d $$d && install -m $(or $(3),644) $(2)/$$src $$d/$$dst || exit 1; \
		for l in $$links; do ln -sf $$dst $$d/$$l || exit 1; done; \
	done
endef
