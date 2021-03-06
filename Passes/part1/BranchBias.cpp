/*
 * How to compile?
 * 1. run /test/test-example/run.sh
 * 2. `opt -load submission_pt1.so -cse231-bb < /tmp/test1.ll -o my-bb.bc`
 * 3. `llvm-dis my-bb.bc` to get my-bb.ll
 * 4. set FLAGS=`llvm-config --system-libs --cppflags --ldflags --libs core`
 *        FLAGS="$FLAGS -Wno-unused-command-line-argument"
 * 5. clang++ /tmp/lib231.ll /tmp/test1-main.ll my-bb.ll $FLAGS -o my-bb
 * 6. `./my-bb 2> my-bb.result`
 * 7. compare my-bb.result with /tmp/bb.result
 */

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IRBuilder.h"
using namespace llvm;

namespace {
struct BranchBiasProfiler: public FunctionPass {
  static char ID;
  BranchBiasProfiler() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override {
    bool modified = false;
    auto M = F.getParent();
    auto &CTX = M->getContext();
    // `void updateBranchInfo(bool taken)`
    auto updateFunc = M->getOrInsertFunction("updateBranchInfo", FunctionType::get(
      Type::getVoidTy(CTX), {Type::getInt1Ty(CTX)}, false));
    // `void printOutBranchInfo()`
    auto printFunc = M->getOrInsertFunction("printOutBranchInfo", FunctionType::get(
      Type::getVoidTy(CTX), false));

    IRBuilder<> Builder(CTX);
    for (auto &BB: F) {
      // https://llvm.org/docs/LangRef.html#terminators
      auto terminator = BB.getTerminator();
      if (isa<BranchInst>(terminator) && cast<BranchInst>(terminator)->isConditional()) {
        Builder.SetInsertPoint(terminator);
        Builder.CreateCall(updateFunc, {cast<BranchInst>(terminator)->getCondition()});
        modified = true;
      }
      if (isa<ReturnInst>(terminator)) {
        Builder.SetInsertPoint(terminator);
        Builder.CreateCall(printFunc);
        modified = true;
      }
    }
    return modified;
  }
};
} // namespace

char BranchBiasProfiler::ID = 0;
static RegisterPass<BranchBiasProfiler> X(
  "cse231-bb",
  "Profiling Branch Bias",
  false, // CFGOnly?
  false // is_analysis?
);