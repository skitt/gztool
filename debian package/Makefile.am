SUBDIRS = src
EXTRA_DIST = gplv3.txt.gz gplv3.txt.gzi.ORIG tests
TESTS = tests/test.sh
TESTS_ENVIRONMENT = \
        gztool_abspath='$(abs_top_builddir)'/src/gztool; \
	export gztool_abspath;
dist_doc_DATA = README
dist_noinst_DATA = gplv3.txt.gz gplv3.txt.gzi.ORIG tests
