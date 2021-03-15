
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/InstVisitor.h"

#include "../DFA/231DFA.h"
#include <map>
#include <set>

namespace {
using namespace llvm;
using Identifier = std::pair<char, uint>;
using Data = std::map<Identifier, std::set<uint>>;

struct MayPointToInfo: Info {
  MayPointToInfo() = default;
  MayPointToInfo(const MayPointToInfo& other) {
    data = other.data;
  }
  ~MayPointToInfo() override = default;

  void print() override {
    for (auto &kv: data) {
      if (kv.second.empty())
        continue;
      errs() << kv.first.first << kv.first.second << "->(";
      for (auto m: kv.second) {
        errs() << "M" << m << "/";
      }
      errs() << ")|";
    }
    errs() << "\n";
  }

  void insert(Identifier id, uint m) {
    data[id].insert(m);
  }

  void insert(Identifier id, std::set<uint> ms) {
    data[id].insert(ms.begin(), ms.end());
  }

  std::set<uint>& operator[](Identifier id) { return data[id]; }
  void clear() { data.clear(); }

  static bool equals(MayPointToInfo* lhs, MayPointToInfo* rhs) {
    return lhs->data == rhs->data;
  }

  MayPointToInfo& join(const MayPointToInfo& other) {
    for (auto &kv: other.data) {
      insert(kv.first, kv.second);
    }
  }
private:
  Data data;
};

struct MayPointToAnalysis: DataFlowAnalysis<MayPointToInfo, true>,
                           InstVisitor<MayPointToAnalysis> {
  MayPointToAnalysis(MayPointToInfo bottom, MayPointToInfo initState)
   : DataFlowAnalysis(bottom, initState) {}
  ~MayPointToAnalysis() override {}

  void visitAllocaInst(AllocaInst &I) {
    auto idx = InstrToIndex[&I];
    in.insert({'R', idx}, idx);
  }
  void visitBitCastInst(BitCastInst &I) {
    auto idx = InstrToIndex[&I];
    auto op = I.getOperand(0);
    if (auto *instr = dyn_cast<Instruction>(op)) {
      auto X = in[{'R', InstrToIndex[instr]}];
      in.insert({'R', idx}, X);
    }
  }
  void visitGetElementPtrInst(GetElementPtrInst &I) {
    auto idx = InstrToIndex[&I];
    auto ptr = I.getPointerOperand();
    if (auto *instr = dyn_cast<Instruction>(ptr)) {
      auto X = in[{'R', InstrToIndex[instr]}];
      in.insert({'R', idx}, X);
    }
  }
  void visitLoadInst(LoadInst &I) {
    auto idx = InstrToIndex[&I];
    auto value = I.getPointerOperand();
    if (auto *rp = dyn_cast<Instruction>(value)) {
      auto X = in[{'R', InstrToIndex[rp]}];
      for (auto x: X) {
        auto Y = in[{'M', x}];
        in.insert({'R', idx}, Y);
      }
    }
  }
  void visitStoreInst(StoreInst &I) {
    auto idx = InstrToIndex[&I];
    auto *rv = dyn_cast<Instruction>(I.getValueOperand());
    auto *rp = dyn_cast<Instruction>(I.getPointerOperand());
    if (rv && rp) {
      auto X = in[{'R', InstrToIndex[rv]}];
      auto Y = in[{'R', InstrToIndex[rp]}];
      for (auto x: X) {
        for (auto y: Y) {
          in.insert({'M', y}, x);
        }
      }
    }
  }
  void visitSelectInst(SelectInst &I) {
    auto idx = InstrToIndex[&I];
    for (auto &op: I.operands()) {
      if (auto *ri = dyn_cast<Instruction>(op.get())) {
        auto X = in[{'R', InstrToIndex[ri]}];
        in.insert({'R', idx}, X);
      }
    }
  }
  void visitPHINode(PHINode &I) {
    auto idx = InstrToIndex[&I];
    
    auto BB = I.getParent();
    auto end = BB->getFirstNonPHI();
    // iter over consecutive Phi instructions
    for (auto ii = BB->begin(); &*ii != end; ++ii) {
      for (auto &op: I.operands()) {
        if (auto *ri = dyn_cast<Instruction>(op.get())) {
          auto X = in[{'R', InstrToIndex[ri]}];
          in.insert({'R', idx}, X);
        }
      }
    }
  }
  void visitInstruction(Instruction &I) {
    // out = in, do nothing
  }

private:
  void flowfunction(Instruction* I, std::vector<unsigned>& IncomingEdges,
                    std::vector<unsigned>& OutgoingEdges, std::vector<MayPointToInfo*>& Infos) override {
    unsigned cur = InstrToIndex.at(I);

    for (auto &i: IncomingEdges) {
      in.join(*(EdgeToInfo.at({i, cur})));
    }

    visit(*I);
    
    for (size_t i = 0; i < OutgoingEdges.size(); ++i) {
      Infos.push_back(new MayPointToInfo(in));
    }
    in.clear();
  }

  MayPointToInfo in;
};

} // namespace

struct LegacyMayPointToPass: public FunctionPass {
  static char ID;
  LegacyMayPointToPass(): FunctionPass(ID) {}

  bool runOnFunction(Function &F) override {
    MayPointToInfo bottom{};
    MayPointToInfo initState{};
    
    auto mpt = new MayPointToAnalysis(bottom, initState);
    mpt->runWorklistAlgorithm(&F);
    mpt->print();
    
    delete mpt;
    // Doesn't modify the input unit of IR, hence 'false'
    return false;
  }
};

char LegacyMayPointToPass::ID = 0;
static RegisterPass<LegacyMayPointToPass> X(
    "cse231-maypointto", 
    "May-point-to Analysis",
    true, // This pass doesn't modify the CFG => true
    false // This pass is not a pure analysis pass => false
);
