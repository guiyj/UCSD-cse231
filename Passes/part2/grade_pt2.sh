#!/bin/bash

if [ ! -f /LLVM_ROOT/build/lib/submission_pt2.so ]; then
    echo "FROM BASH SCRIPT: FILE submission_pt2.so NOT FOUND. SKIPPING RUNS"
    exit
else
    echo "FROM BASH SCRIPT: FILE submission_pt2.so FOUND"
fi

mkdir -p /autograder/results
cd /LLVM_ROOT/llvm/lib/Transforms/GradingTests

runPasses () {
    outputDir=$1
    moduleName=$2

    mkdir $outputDir

    for t in c_bsort c_fib20 c_fib30 c_matrixmult; do
        clang -O0 -S -emit-llvm ${t}/${t}.c -o ${t}/${t}.ll
        # Run CSI
        opt -load ${moduleName}.so -cse231-reaching < ./${t}/${t}.ll > /dev/null 2> $outputDir/${t}.reaching

    done
}

# Run provided solution passes
runPasses "/output_solution" "231_solution"

# Run student passes
runPasses "/output_student" "submission_pt2"

#### GRADE IT ####
python3 /LLVM_ROOT/llvm/lib/Transforms/part2_grading.py

#### Print your score ####
cat /autograder/results/results.json