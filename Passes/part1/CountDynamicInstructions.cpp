/* 
 * Insert a updateFunc call in each BB, at run time, the amount of 
 * each instruction will be counted according to the taken branches 
 * and the associated statistics gathered statically.
 * 
 * Before the instrumented function returned, print out the infomation.
 */

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IRBuilder.h"

using namespace llvm;

#include <map>
#include <vector>

namespace {
struct DynamicInstCounter: public FunctionPass {
  static char ID;
  DynamicInstCounter() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override {
    auto M = F.getParent();
    auto &CTX = M->getContext();
    // `void updateInstrInfo(unsigned, uint32_t*, uint32_t*)`
    auto i32Ptr = Type::getInt32Ty(CTX);
    auto updateFunc = M->getOrInsertFunction("updateInstrInfo", FunctionType::get(
      Type::getVoidTy(CTX), {Type::getInt32Ty(CTX), i32Ptr, i32Ptr}, false));
    // `void printOutInstrInfo()`
    auto printFunc = M->getOrInsertFunction("printOutInstrInfo", FunctionType::get(
      Type::getVoidTy(CTX), false));

    std::map<uint, uint> counter;
    Instruction* ret = nullptr;
    for (auto &BB: F) {
      for (auto &I : BB) {
        auto code = I.getOpcode();
        // count inst
        auto p = counter.insert(std::make_pair(code, 1));
        if (p.second == false) {
          p.first->second += 1;
        }
        // is it a `ret` instruction?
        if (isa<ReturnInst>(&I)) {
          ret = &I;
        }
      }
      // insert updateInstrInfo
      IRBuilder<> Builder{&*BB.getFirstInsertionPt()};
      std::vector<Constant *> keys, values;
      for (auto &kv: counter) {
        keys.push_back(ConstantInt::get(Type::getInt32Ty(CTX), kv.first));
        values.push_back(ConstantInt::get(Type::getInt32Ty(CTX), kv.second));
      }

      auto arrayType = ArrayType::get(Type::getInt32Ty(CTX), keys.size());
      auto keys_global = new GlobalVariable(*M, arrayType, true, 
        GlobalVariable::InternalLinkage, ConstantArray::get(arrayType, keys), "key global");
      auto values_global = new GlobalVariable(*M, arrayType, true, 
        GlobalVariable::InternalLinkage, ConstantArray::get(arrayType, values), "value global");
      Builder.CreateCall(updateFunc, {
        ConstantInt::get(Type::getInt32Ty(CTX), keys.size()), 
        Builder.CreatePointerCast(keys_global, i32Ptr),
        Builder.CreatePointerCast(values_global, i32Ptr)});

      // insert printOutInstrInfo
      if (ret != nullptr) {
        Builder.SetInsertPoint(ret);
        Builder.CreateCall(printFunc);
        ret = nullptr;
      }
      counter.clear();
    }
    // IR was modified
    return true;
  }
};
} // namespace

char DynamicInstCounter::ID = 0;
static RegisterPass<DynamicInstCounter> X(
  "cse231-cdi",
  "Collecting Dynamic Instruction Counts",
  false, // This pass modifies the CFG => false
  false // This pass is not a pure analysis pass => false
);