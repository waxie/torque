PROG_ROOT = ../..

AM_CFLAGS = -g -DTEST_FUNCTION -I${PROG_ROOT}/ -I${PROG_ROOT}/${PBS_MACH} --coverage

lib_LTLIBRARIES = libget_hostaddr.la

AM_LDFLAGS = @CHECK_LIBS@ ${lib_LTLIBRARIES}

check_PROGRAMS = test_get_hostaddr

libget_hostaddr_la_SOURCES = scaffolding.c ${PROG_ROOT}/get_hostaddr.c
libget_hostaddr_la_LDFLAGS = @CHECK_LIBS@ -shared -lgcov

test_get_hostaddr_SOURCES = test_get_hostaddr.c

check_SCRIPTS = ${PROG_ROOT}/../../test/coverage_run.sh

TESTS = ${check_PROGRAMS} ${check_SCRIPTS}

CLEANFILES = *.gcno *.gcda *.gcov core *.lo
