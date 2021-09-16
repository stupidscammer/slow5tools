#!/bin/bash
# Run f2s with different file, input and output formats.
Usage="get_test.sh"

# Relative path to "slow5/tests/"
REL_PATH="$(dirname $0)/"

NC='\033[0m' # No Color
RED='\033[0;31m'
GREEN='\033[0;32m'

# terminate script
die() {
    echo -e "${RED}$1${NC}" >&2
    echo
    exit 1
}

OUTPUT_DIR="$REL_PATH/data/out/get"
test -d $OUTPUT_DIR && rm -r "$OUTPUT_DIR"
mkdir $OUTPUT_DIR || die "Creating $OUTPUT_DIR failed"

EXP_DIR=$REL_PATH/data/exp/get
RAW_DIR=$REL_PATH/data/raw/get
SLOW5_EXEC_WITHOUT_VALGRIND=$REL_PATH/../slow5tools
if [ "$1" = 'mem' ]; then
    SLOW5_EXEC="valgrind --leak-check=full --error-exitcode=1 $SLOW5_EXEC_WITHOUT_VALGRIND"
else
    SLOW5_EXEC=$SLOW5_EXEC_WITHOUT_VALGRIND
fi

echo "-------------------slow5tools version-------------------"
$SLOW5_EXEC --version || die "slow5tools version failed"
echo

#indexing slow5file
#    exit 1
#fi

TESTCASE=1
echo
echo "------------------- slow5tools get testcase $TESTCASE -------------------"
$SLOW5_EXEC get "$RAW_DIR/example2.slow5" r1 --to slow5 > "$OUTPUT_DIR/extracted_reads.slow5" || die "testcase $TESTCASE failed"
diff -q "$EXP_DIR/expected_extracted_reads.slow5" "$OUTPUT_DIR/extracted_reads.slow5" &>/dev/null
if [ $? -ne 0 ]; then
    echo -e "${RED}ERROR: diff failed for 'slow5tools get testcase $TESTCASE'${NC}"
    exit 1
fi
echo -e "${GREEN}testcase $TESTCASE passed${NC}"

TESTCASE=2
echo
echo "------------------- slow5tools get testcase $TESTCASE -------------------"
$SLOW5_EXEC get "$RAW_DIR/example2.slow5" r1 r5 r3 --to slow5 > "$OUTPUT_DIR/extracted_reads2.slow5" || die "testcase $TESTCASE failed"
diff -q "$EXP_DIR/expected_extracted_reads2.slow5" "$OUTPUT_DIR/extracted_reads2.slow5" &>/dev/null
if [ $? -ne 0 ]; then
    echo -e "${RED}ERROR: diff failed for 'slow5tools get testcase $TESTCASE'${NC}"
    exit 1
fi
echo -e "${GREEN}testcase $TESTCASE passed${NC}"

TESTCASE=3
echo
echo "------------------- slow5tools get testcase $TESTCASE -------------------"
$SLOW5_EXEC get "$RAW_DIR/example2.slow5" --list "$RAW_DIR/list.txt" --to slow5 > "$OUTPUT_DIR/extracted_reads3.slow5" || die "testcase $TESTCASE failed"
diff -q "$EXP_DIR/expected_extracted_reads3.slow5" "$OUTPUT_DIR/extracted_reads3.slow5" &>/dev/null
if [ $? -ne 0 ]; then
    echo -e "${RED}ERROR: diff failed for 'slow5tools get testcase $TESTCASE'${NC}"
    exit 1
fi
echo -e "${GREEN}testcase $TESTCASE passed${NC}"

TESTCASE=4
echo
echo "------------------- slow5tools get testcase $TESTCASE -------------------"
$SLOW5_EXEC get "$RAW_DIR/example2.slow5" -t 2 r1 r5 r3 --to slow5 > "$OUTPUT_DIR/extracted_reads2.slow5" || die "testcase $TESTCASE failed"
diff -q "$EXP_DIR/expected_extracted_reads2.slow5" "$OUTPUT_DIR/extracted_reads2.slow5" &>/dev/null
if [ $? -ne 0 ]; then
    echo -e "${RED}ERROR: diff failed for 'slow5tools get testcase $TESTCASE'${NC}"
    exit 1
fi
echo -e "${GREEN}testcase $TESTCASE passed${NC}"

TESTCASE=5
echo
echo "------------------- slow5tools get testcase $TESTCASE -------------------"
$SLOW5_EXEC get "$RAW_DIR/example2.slow5"  r1 r5 r3 --to blow5 -c zlib -s none > "$OUTPUT_DIR/extracted_reads2.blow5" || die "testcase $TESTCASE failed"
diff -q "$EXP_DIR/expected_extracted_reads.blow5" "$OUTPUT_DIR/extracted_reads2.blow5" &>/dev/null
if [ $? -ne 0 ]; then
    echo -e "${RED}ERROR: diff failed for 'slow5tools get testcase $TESTCASE'${NC}"
    exit 1
fi
echo -e "${GREEN}testcase $TESTCASE passed${NC}"

TESTCASE=6
echo
echo "------------------- slow5tools get testcase $TESTCASE -------------------"
$SLOW5_EXEC get "$RAW_DIR/example2.slow5" r1 r5 r3 -o "$OUTPUT_DIR/extracted_reads2.blow5"  -c zlib -s none  || die "testcase $TESTCASE failed"
diff -q "$EXP_DIR/expected_extracted_reads.blow5" "$OUTPUT_DIR/extracted_reads2.blow5" &>/dev/null
if [ $? -ne 0 ]; then
    echo -e "${RED}ERROR: diff failed for 'slow5tools get testcase $TESTCASE'${NC}"
    exit 1
fi
echo -e "${GREEN}testcase $TESTCASE passed${NC}"


rm -r $OUTPUT_DIR || die "Removing $OUTPUT_DIR failed"
exit 0