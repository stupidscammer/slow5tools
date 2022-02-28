#!/bin/bash
# test slow5 index

# Relative path to "slow5/tests/"
REL_PATH="$(dirname $0)/"

RED='\033[0;31m' ; GREEN='\033[0;32m' ; NC='\033[0m' # No Color
die() { echo -e "${RED}$1${NC}" 1>&3 2>&4 ; echo ; exit 1 ; } # terminate script
info() {  echo ; echo -e "${GREEN}$1${NC}" 1>&3 2>&4 ; }

#redirect
verbose=0
exec 3>&1
exec 4>&2
if ((verbose)); then
  echo "verbose=1"
else
  echo "verbose=0"
  exec 1>/dev/null
  exec 2>/dev/null
fi
#echo "this should be seen if verbose"
#echo "this should always be seen" 1>&3 2>&4

OUTPUT_DIR="$REL_PATH/data/out/slow5tools_index"
test -d  $OUTPUT_DIR && rm -r "$OUTPUT_DIR"
mkdir $OUTPUT_DIR || die "Creating $OUTPUT_DIR failed"

SLOW5_DIR=$REL_PATH/data/exp/index
SLOW5_EXEC_WITHOUT_VALGRIND=$REL_PATH/../slow5tools
if [ "$1" = 'mem' ]; then
    SLOW5_EXEC="valgrind --leak-check=full --error-exitcode=1 $SLOW5_EXEC_WITHOUT_VALGRIND"
else
    SLOW5_EXEC=$SLOW5_EXEC_WITHOUT_VALGRIND
fi

info "-------------------slow5tools version-------------------"
$SLOW5_EXEC --version || die "slow5tools version failed"

TESTCASE=1
info "------------------- slow5tools index testcase $TESTCASE -------------------"
$SLOW5_EXEC index $SLOW5_DIR/example_multi_rg_v0.1.0.blow5 || die "testcase 1 failed"
diff -q $SLOW5_DIR/example_multi_rg_v0.1.0.blow5.idx.exp $SLOW5_DIR/example_multi_rg_v0.1.0.blow5.idx &>/dev/null
if [ $? -ne 0 ]; then
    info "ERROR: diff failed for 'slow5tools index testcase 1'"
    exit 1
fi
info "testcase $TESTCASE passed"

TESTCASE=2
info "------------------- slow5tools index testcase $TESTCASE -------------------"
$SLOW5_EXEC index $SLOW5_DIR/example_multi_rg_v0.1.0.slow5 || die "testcase 2 failed"
diff -q $SLOW5_DIR/example_multi_rg_v0.1.0.slow5.idx.exp $SLOW5_DIR/example_multi_rg_v0.1.0.slow5.idx &>/dev/null
if [ $? -ne 0 ]; then
    info "ERROR: diff failed for 'slow5tools index testcase 2'"
    exit 1
fi
info "testcase $TESTCASE passed"

TESTCASE=3
info "------------------- slow5tools index testcase $TESTCASE -------------------"
$SLOW5_EXEC index $SLOW5_DIR/example_multi_rg_v0.2.0.blow5 || die "testcase 3 failed"
diff -q $SLOW5_DIR/example_multi_rg_v0.2.0.blow5.idx.exp $SLOW5_DIR/example_multi_rg_v0.2.0.blow5.idx &>/dev/null
if [ $? -ne 0 ]; then
    info "ERROR: diff failed for 'slow5tools index testcase 3'"
    exit 1
fi
info "testcase $TESTCASE passed"

rm -r $OUTPUT_DIR || die "Removing $OUTPUT_DIR failed"

exit 0
