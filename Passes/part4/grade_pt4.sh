#!/bin/bash

echo "PATH: $PATH"

if [ ! -f /LLVM_ROOT/build/lib/submission_pt4.so ]; then
    echo "FROM BASH SCRIPT: FILE submission_pt4.so NOT FOUND. SKIPPING RUNS"
    python3 /autograder/part4_grading.py
    exit
else
    echo "FROM BASH SCRIPT: FILE submission_pt4.so FOUND"
fi

mkdir -p /autograder/results
cd /LLVM_ROOT/llvm/lib/Transforms/GradingTests

#### GENERATE OUTPUTS ####
runPasses () {
    outputDir=$1
    moduleName=$2

    mkdir $outputDir

    #all tests: #alias-to-global alias-to-reference argument-flipper argument-flipper-minus-1 call-changes-value call-changes-value-indirectly call-takes-address constant-fold const-prop-X2 floats keep-info-after-call mod-global mod-local mpt-includes-locals pass-by-pointer pass-by-reference pass-by-value phi-node phi-node-all-consts phi-node-one-const pointer-to-pointer remove-info-after-call same-names
    #most tests: #alias-to-global argument-flipper call-changes-value call-takes-address constant-fold floats keep-info-after-call mpt-includes-locals pass-by-pointer phi-node phi-node-all-consts pointer-to-pointer remove-info-after-call same-names 
    for t in alias-to-global argument-flipper call-changes-value call-takes-address constant-fold floats keep-info-after-call mpt-includes-locals pass-by-pointer phi-node phi-node-all-consts pointer-to-pointer remove-info-after-call same-names ; do
        clang++ -O0 -S -emit-llvm ${t}/main.cpp -o ${t}/${t}.ll
        # Run CSI
        opt -load ${moduleName}.so -cse231-constprop < ${t}/${t}.ll > /dev/null 2> $outputDir/${t}.constprop

    done
}

# Run provided solution passes
runPasses "/output_solution" "231_solution"

# Run student passes
runPasses "/output_student" "submission_pt4"

#### OUTPUTS GENERATED ####

#### GRADE IT ####
python3 /LLVM_ROOT/llvm/lib/Transforms/part4_grading.py
#### GRADING DONE ####

#### Print your score ####
cat /autograder/results/results.json
