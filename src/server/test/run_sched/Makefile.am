PROG_ROOT = ../..

AM_CFLAGS = -g -DTEST_FUNCTION -I${PROG_ROOT}/ --coverage

lib_LTLIBRARIES = librun_sched.la

AM_LDFLAGS = @CHECK_LIBS@ ${lib_LTLIBRARIES}

check_PROGRAMS = test_run_sched

librun_sched_la_SOURCES = scaffolding.c ${PROG_ROOT}/run_sched.c
librun_sched_la_LDFLAGS = @CHECK_LIBS@ -shared -lgcov

test_run_sched_SOURCES = test_run_sched.c

check_SCRIPTS = ${PROG_ROOT}/../test/coverage_run.sh

TESTS = $(check_PROGRAMS) ${check_SCRIPTS}

CLEANFILES = *.gcno *.gcda *.gcov core *.lo
