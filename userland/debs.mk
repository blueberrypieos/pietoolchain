# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PIE_APT       ?= https://blueberrypieos.com/apt
PIE_DEB_CACHE ?= /var/cache/pietoolchain/debs
X             := $(OBJ)/root

all: $(X)/.done

$(X)/.done: debs copyright sdk-files ../debs.mk
	rm -rf $(OBJ)/debs $(X)
	mkdir -p $(OBJ)/debs $(X)
	set -e; while read path sum; do \
		deb=$${path##*/}; f=$(OBJ)/debs/$$deb; \
		if [ -f $(PIE_DEB_CACHE)/$$deb ]; then cp $(PIE_DEB_CACHE)/$$deb $$f; \
		else python3 -c 'import sys, urllib.request; r = urllib.request.Request(sys.argv[1], headers={"User-Agent": "pietoolchain"}); open(sys.argv[2], "wb").write(urllib.request.urlopen(r, timeout=60).read())' \
			"$(PIE_APT)/$$path" $$f; fi; \
		echo "$$sum  $$f" | sha256sum -c --quiet; \
		dpkg-deb -x $$f $(X); \
		cmp $(X)/usr/share/doc/$${deb%%_*}/copyright copyright; \
	done < debs
	touch $@

installhdrs: all
	set -e; while read f; do \
		case $$f in usr/lib/*) continue ;; esac; \
		install -d $(SDK)/$${f%/*}; \
		rm -f $(SDK)/$$f; \
		cp -P --preserve=mode $(X)/$$f $(SDK)/$$f; \
	done < sdk-files

install: installhdrs
	set -e; while read f; do \
		case $$f in usr/lib/*) ;; *) continue ;; esac; \
		install -d $(SDK)/$${f%/*} $(ROOT)/$${f%/*}; \
		rm -f $(SDK)/$$f $(ROOT)/$$f $(SDK)/$$f.stub; \
		cp -P --preserve=mode $(X)/$$f $(SDK)/$$f; \
		cp -P --preserve=mode $(X)/$$f $(ROOT)/$$f; \
	done < sdk-files

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
