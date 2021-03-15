#!/bin/bash

echo "PATH: $PATH"

if [ ! -f /LLVM_ROOT/build/lib/submission_pt3.so ]; then
    echo "FROM BASH SCRIPT: FILE submission_pt3.so NOT FOUND. SKIPPING RUNS"
    python3 /autograder/part3_grading.py
    exit
else
    echo "FROM BASH SCRIPT: FILE submission_pt3.so FOUND"
fi

mkdir -p /autograder/results
cd /LLVM_ROOT/llvm/lib/Transforms/GradingTests

runPasses () {
    outputDir=$1
    moduleName=$2

    mkdir $outputDir

    # These test are insufficient to evaluate the May-Point-To analysis.
    for t in c_bsort c_fib20 c_fib30 c_matrixmult; do
        clang -O0 -S -emit-llvm ${t}/${t}.c -o ${t}/${t}.ll
        # Run CSI
        opt -load ${moduleName}.so -cse231-liveness < ./${t}/${t}.ll > /dev/null 2> $outputDir/${t}.liveness
        opt -load ${moduleName}.so -cse231-maypointto < ./${t}/${t}.ll > /dev/null 2> $outputDir/${t}.maypointto

    done
}

# Run provided solution passes
runPasses "/output_solution" "231_solution"

# Run student passes
runPasses "/output_student" "submission_pt3"

#### GRADE IT ####
python3 /LLVM_ROOT/llvm/lib/Transforms/part3_grading.py
#### GRADING DONE ####

#### Print your score ####
cat /autograder/results/results.json
