#!/bin/bash  

if [ $# -ne 2 ]; then
    echo Usage: run_tests.sh EXPECTED_OUTPUT BUILD_DIR
    echo "EXPECTED_OUTPUT is the expected output generated by the tests"
    echo "BUILD_DIR is the build directory (e.g., build) where tests are located"
    exit 1
fi

OLDLOG=$1
DIR=$2

if [ ! -d "$DIR/test-bin" ]; then
    echo "$DIR/test-bin does not exist"
    exit 1
fi

if [ ! -f "$OLDLOG" ]; then
    echo "$OLDLOG does not exist"
    exit 1
fi

#DIFF=/Applications/DiffMerge.app/Contents/MacOS/DiffMerge
DIFF=diff

timestamp=$(date +"%m_%d_%y.%H_%M")  
NEWLOG=results_${timestamp}.out

## Run all the tests
for test in $DIR/test-bin/*
do
  $test >> $NEWLOG
done

LOGDIR=$(mktemp -d "${TMPDIR:-/tmp/}$(basename $0).XXXXXXXXXXXX")
LOG="$LOGDIR/log.txt"

## Diff the output of the tests with the expected output
$DIFF --suppress-common-lines $OLDLOG $NEWLOG >& $LOG
STATUS=$?

###################################################################
## Comment this line if we want to keep the generated output
###################################################################
rm -f $NEWLOG

if [ $STATUS -eq 0 ]; then
    echo "All tests passed successfully!"
    exit 0
else
    echo "Some tests produced unexpected output:"
    cat $LOG
    exit 1  
fi