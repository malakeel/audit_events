# contrib/audit_events/Makefile

MODULE_big = audit
OBJS = audit_events.o serializer.o

# Note: because we don't tell the Makefile there are any regression tests,
# we have to clean those result files explicitly
EXTRA_CLEAN = $(pg_regress_clean_files) ./regression_output ./isolation_output

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/audit_events
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

# Disabled because these tests require "wal_level=logical", which
# typical installcheck users do not have (e.g. buildfarm clients).
installcheck:;

# But it can nonetheless be very helpful to run tests on preexisting
# installation, allow to do so, but only if requested explicitly.
installcheck-force: regresscheck-install-force isolationcheck-install-force

check: regresscheck isolationcheck

submake-regress:
	$(MAKE) -C $(top_builddir)/src/test/regress all

submake-isolation:
	$(MAKE) -C $(top_builddir)/src/test/isolation all

submake-audit_events:
	$(MAKE) -C $(top_builddir)/contrib/audit_events

REGRESSCHECKS=ddl rewrite toast permissions decoding_in_xact decoding_into_rel binary prepared

regresscheck: all | submake-regress submake-audit_events
	$(MKDIR_P) regression_output
	$(pg_regress_check) \
	    --temp-config $(top_srcdir)/contrib/audit_events/logical.conf \
	    --temp-install=./tmp_check \
	    --extra-install=contrib/audit_events \
	    --outputdir=./regression_output \
	    $(REGRESSCHECKS)

regresscheck-install-force: | submake-regress submake-audit_events
	$(pg_regress_installcheck) \
	    --extra-install=contrib/audit_events \
	    $(REGRESSCHECKS)

ISOLATIONCHECKS=mxact delayed_startup ondisk_startup concurrent_ddl_dml

isolationcheck: all | submake-isolation submake-audit_events
	$(MKDIR_P) isolation_output
	$(pg_isolation_regress_check) \
	    --temp-config $(top_srcdir)/contrib/audit_events/logical.conf \
	    --extra-install=contrib/audit_events \
	    --outputdir=./isolation_output \
	    $(ISOLATIONCHECKS)

isolationcheck-install-force: all | submake-isolation submake-audit_events
	$(pg_isolation_regress_installcheck) \
	    --extra-install=contrib/audit_events \
	    $(ISOLATIONCHECKS)

PHONY: submake-audit_events submake-regress check \
	regresscheck regresscheck-install-force \
	isolationcheck isolationcheck-install-force




PG_TOP_SRC_DIR=../..
INC=-I$(PG_TOP_SRC_DIR)/src/include -I.

.PHONY: check-syntax

check-syntax:
	$(CC) $(INC) $(CFLAGS) -fsyntax-only ${CHK_SOURCES}


