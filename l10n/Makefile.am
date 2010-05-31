LANGS=

dist-hook:
	@for l in $(LANGS) ; do \
		cp -p "$(srcdir)/$(PACKAGE)-$$l.yts" "$(distdir)/$(PACKAGE)-$$l.yts" ; \
	done

install-data-hook:
	test -z "$(DESTDIR)$(datadir)/sylverant/l10n" || mkdir -p "$(DESTDIR)$(datadir)/sylverant/l10n"
	@for l in $(LANGS) ; do \
		echo " $(INSTALL_DATA) $(PACKAGE)-$$l.yts $(DESTDIR)$(datadir)/sylverant/l10n/$(PACKAGE)-$$l.yts" ; \
		$(INSTALL_DATA) "$(srcdir)/$(PACKAGE)-$$l.yts" "$(DESTDIR)$(datadir)/sylverant/l10n/$(PACKAGE)-$$l.yts" ; \
	done

uninstall-hook:
	@for l in $(LANGS) ; do \
		echo " rm -f $(DESTDIR)$(datadir)/sylverant/l10n/$(PACKAGE)-$$l.yts" ; \
		rm -f "$(DESTDIR)$(datadir)/sylverant/l10n/$(PACKAGE)-$$l.yts" ; \
	done
