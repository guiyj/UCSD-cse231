#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include "../DFA/231DFA.h"
#include <set>

namespace {
using namespace llvm;

struct ReachingInfo: Info {
  ReachingInfo() = default;
  ReachingInfo(const ReachingInfo& other) {
    this->reaches = other.reaches;
  }
  ~ReachingInfo() override = default;

  void print() override {
    for (auto &i: reaches)
      errs() << i << '|';
    errs() << '\n';
  }

  void set(unsigned i) {
    reaches.insert(i);
  }

  static bool equals(ReachingInfo* lhs, ReachingInfo* rhs) {
    return lhs->reaches == rhs->reaches;
  }

  // Union operation of sets
  ReachingInfo& operator+(const ReachingInfo& other) {
    reaches.insert(other.reaches.begin(), other.reaches.end());
    return *this;
  }
  // I won't use this method
  static ReachingInfo* join(ReachingInfo* info1, ReachingInfo* info2, ReachingInfo* result) {
    return nullptr;
  }
private:
  // use std::set to represent bit-vector
  std::set<unsigned> reaches;
};

struct ReachingDefinitionAnalysis: DataFlowAnalysis<ReachingInfo, true> {
  ReachingDefinitionAnalysis(): DataFlowAnalysis(bottom, initState) {}
  ~ReachingDefinitionAnalysis() override {}

private:
  void flowfunction(Instruction* I, std::vector<unsigned>& IncomingEdges,
                    std::vector<unsigned>& OutgoingEdges, std::vector<ReachingInfo*>& Infos) override {
    unsigned cur = InstrToIndex.at(I);

    ReachingInfo in, out;
    for (auto &i: IncomingEdges) {
      in = in + *(EdgeToInfo.at({i, cur}));
    }

    if (isa<BranchInst>(I) || isa<SwitchInst>(I) || isa<StoreInst>(I)) {
      out = in;
    } else if (isa<BinaryOperator>(I) || isa<AllocaInst>(I) || isa<LoadInst>(I) || 
               isa<GetElementPtrInst>(I) || isa<CmpInst>(I) || isa<SelectInst>(I)) {
      in.set(cur);
      out = in;
    } else if (isa<PHINode>(I)) {
      auto BB = I->getParent();
      auto end = BB->getFirstNonPHI();
      // iter over consecutive Phi instructions
      for (auto ii = BB->begin(); &*ii != end; ++ii) {
        in.set(InstrToIndex[&*ii]);
      }
      out = in;
    } else {
      // treated as do not return a value
      out = in;
    }
    // return n copies of out, for n outgoing edges
    for (size_t i = 0; i < OutgoingEdges.size(); ++i) {
      Infos.push_back(new ReachingInfo(out));
    }
    return;
  }

  static ReachingInfo bottom;
  static ReachingInfo initState;
};

ReachingInfo ReachingDefinitionAnalysis::bottom = ReachingInfo{};
ReachingInfo ReachingDefinitionAnalysis::initState = ReachingInfo{};

} // namespace

struct NewReachingPass: PassInfoMixin<NewReachingPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    auto r = new ReachingDefinitionAnalysis();
    r->runWorklistAlgorithm(&F);
    r->print();
    delete r;
    return PreservedAnalyses::all();
  }
};

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "Reaching Definition Analysis", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
              [](StringRef Name, FunctionPassManager &FPM, ArrayRef<PassBuilder::PipelineElement>) {
                if (Name == "cse231-reaching") {
                  FPM.addPass(NewReachingPass());
                  return true;
                }
                return false;
            });
          }};
}

struct LegacyReachingPass: public FunctionPass {
  static char ID;
  LegacyReachingPass() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override {
    auto r = new ReachingDefinitionAnalysis();
    r->runWorklistAlgorithm(&F);
    r->print();
    delete r;
    // Doesn't modify the input unit of IR, hence 'false'
    return false;
  }
};

char LegacyReachingPass::ID = 0;
static RegisterPass<LegacyReachingPass> X(
    "cse231-reaching", 
    "Reaching Definition Analysis",
    true, // This pass doesn't modify the CFG => true
    false // This pass is not a pure analysis pass => false
);
