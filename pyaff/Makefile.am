DISTCLEANFILES = Makefile.in
EXTRA_DIST = README afflib.pxd pyaff.pxd pyaff.pyx pyaff.c setup.py

if HAVE_PYTHON
all-local: $(top_builddir)/lib/.libs/libafflib.la
	cd $(srcdir) && $(PYTHON) setup.py \
		build --build-base $(abs_builddir)/build \
		build_ext --include-dirs $(abs_top_srcdir)/include --library-dirs $(abs_top_builddir)/lib/.libs

install-exec-local:
	cd $(srcdir) && $(PYTHON) setup.py \
		build --build-base $(abs_builddir)/build \
		egg_info --egg-base $(abs_builddir) \
		install --prefix $(DESTDIR)$(prefix) --single-version-externally-managed --record=$(abs_builddir)/installed.txt \
		bdist_egg --dist-dir $(abs_builddir)/dist
endif
