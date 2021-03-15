#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/BitVector.h"

#include "../DFA/231DFA.h"
#include <map>

namespace {
using namespace llvm;

struct LivenessInfo: Info {
  LivenessInfo() = default;
  LivenessInfo(unsigned s): bits{s} {}
  LivenessInfo(const LivenessInfo& other) {
    this->bits = other.bits;
  }
  ~LivenessInfo() override = default;

  void print() override {
    for (size_t i = 0; i != bits.size(); ++i) {
      if (bits[i]) {
        errs() << i << '|';
      }
    }
    errs() << '\n';
  }

  void set(unsigned idx) {
    bits.set(idx);
  }

  void reset(unsigned idx) {
    bits.reset(idx);
  }

  static bool equals(LivenessInfo* lhs, LivenessInfo* rhs) {
    return lhs->bits == rhs->bits;
  }

  LivenessInfo& join(const LivenessInfo& other) {
    bits |= other.bits;
    return *this;
  }
private:
  BitVector bits;
};

struct LivenessAnalysis: DataFlowAnalysis<LivenessInfo, false> {
  LivenessAnalysis(LivenessInfo bottom, LivenessInfo initState): DataFlowAnalysis(bottom, initState) {}
  ~LivenessAnalysis() override {}

private:
  void flowfunction(Instruction* I, std::vector<unsigned>& IncomingEdges,
                    std::vector<unsigned>& OutgoingEdges, std::vector<LivenessInfo*>& Infos) override {
    unsigned cur = InstrToIndex.at(I);

    LivenessInfo in;
    for (auto &i: IncomingEdges) {
      in.join(*(EdgeToInfo.at({i, cur})));
    }

    Infos = {OutgoingEdges.size(), nullptr};

    auto joinDefs = [&](LivenessInfo& info, Instruction* I) {
      for (Use &U: I->operands()) {
        Value *v = U.get();
        if (auto *instr = dyn_cast<Instruction>(v)) {
          info.set(InstrToIndex[instr]);
        }
      }
    };

    if (isa<BinaryOperator>(I) || isa<AllocaInst>(I) || isa<LoadInst>(I) || 
        isa<GetElementPtrInst>(I) || isa<CmpInst>(I) || isa<SelectInst>(I)) {
      // First Category: IR instructions that return a value
      joinDefs(in, I);
      in.reset(cur);
      // return n copies of out, for n outgoing edges
      for (size_t i = 0; i < OutgoingEdges.size(); ++i) {
        Infos[i] = new LivenessInfo(in);
      }
    } else if (isa<PHINode>(I)) {
      auto BB = I->getParent();
      auto end = BB->getFirstNonPHI();
      // iter over consecutive Phi instructions
      for (auto ii = BB->begin(); &*ii != end; ++ii) {
        in.reset(InstrToIndex[&*ii]);
      }
      std::map<uint, uint> edge2idx;
      for (size_t i = 0; i < OutgoingEdges.size(); ++i) {
        edge2idx.insert(std::make_pair(OutgoingEdges[i], i));
        Infos[i] = new LivenessInfo(in);
      }
      // iter over consecutive Phi instructions again
      for (auto ii = BB->begin(); &*ii != end; ++ii) {
        auto *phi = dyn_cast<PHINode>(&*ii);
        for (auto &v: phi->incoming_values()) {
          if (auto *instr = dyn_cast<Instruction>(v)) {
            auto dst = InstrToIndex[instr];
            Infos[edge2idx[dst]]->set(dst);
          }
        }
      }
    } else {
      // Second Category: IR instructions that do not return a value
      // includes BranchInst, SwitchInst, StoreInst, CallInst and the not mentioned
      joinDefs(in, I);
      // return n copies of out, for n outgoing edges
      for (size_t i = 0; i < OutgoingEdges.size(); ++i) {
        Infos[i] = new LivenessInfo(in);
      }
    }
  }
};

} // namespace

struct LegacyLivenessPass: public FunctionPass {
  static char ID;
  LegacyLivenessPass(): FunctionPass(ID) {}

  bool runOnFunction(Function &F) override {
    uint size = 1;
    for (auto &BB: F) {
      size += BB.size();
    }
    LivenessInfo bottom{size};
    LivenessInfo initState{size};

    auto la = new LivenessAnalysis{bottom, initState};
    la->runWorklistAlgorithm(&F);
    la->print();
    delete la;
    // Doesn't modify the input unit of IR, hence 'false'
    return false;
  }
};

char LegacyLivenessPass::ID = 0;
static RegisterPass<LegacyLivenessPass> X(
    "cse231-liveness", 
    "Liveness Analysis",
    true, // This pass doesn't modify the CFG => true
    false // This pass is not a pure analysis pass => false
);
