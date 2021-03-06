/*
 * UCSD CSE-231 wi20 LLVM Project 1: counting
 * This project is based on llvm9.
 *
 * Generate *.ll file: 
 *     `clang -O0 -S -emit-llvm <input-llvm-file>`
 * How to build?
 *     `cd /LLVM_ROOT/build`
 *     `cmake /LLVM_ROOT/llvm`
 *     `cd /LLVM_ROOT/build/lib/Transforms/CSE231_Project`
 *     `make`
 * 
 * Legacy PM implementation and registration were used in part1.
 * 
 */

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/InstIterator.h"
using namespace llvm;

#include <map>
#include <string>

namespace {
using std::map;
using std::string;

struct StaticInstCounter: public FunctionPass {
  static char ID;
  StaticInstCounter() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override {
    visitor(F);
    for (auto &kv: counter) {
      errs() << kv.first << "\t" << kv.second << "\n";
    }
    // "You may assume that each test case (compilation unit) only has one function."
    counter.clear();
    // Doesn't modify the input unit of IR, hence 'false'
    return false;
  }

private:
  void visitor(Function &F) {
    for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
      auto ret = counter.insert(std::make_pair(I->getOpcodeName(), 1));
      if (ret.second == false) {
        ret.first->second += 1;
      }
    }
  }

private:
  map<const char*, uint> counter;
};
} // namespace

char StaticInstCounter::ID = 0;

// `opt -load submission_pt1.so -cse231-csi < input.ll > /dev/null`
static RegisterPass<StaticInstCounter> X(
  "cse231-csi",
  "Collecting Static Instruction Counts",
  true, // This pass doesn't modify the CFG => true
  false // This pass is not a pure analysis pass => false
);