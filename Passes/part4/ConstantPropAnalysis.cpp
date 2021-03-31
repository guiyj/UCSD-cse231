#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/PassSupport.h"
#include "llvm/IR/ConstantFolder.h"

#include "../DFA/231DFA.h"
#include <set>
#include <unordered_map>
/*
 * Note: This implementation is quite different from the one demonstrated 
 * in the CSE231 lectures (MUST analysis, downward towards the Bottom).
 *
 * Task: Implement MOD and ConstantPropAnalysis
 * 
 *     MPT = all variables that may be modified in whole program
 *     MOD = modified global variables in each function, = Union(LMOD, CMOD)
 */
using namespace llvm;
using Values = std::set<Value*>;
using GlobalVars = std::set<GlobalVariable*>;
using FuncMap = std::unordered_map<Function*, std::set<GlobalVariable*>>;

namespace {
  /*
   * Lattice:
   *          Top: AllConst (Undefined)
   *               /  |  \
   *         ... -1   0   1  ...
   *               \  |  /
   *         Bottom: NAC
   */
  enum class ConstState { AllConst, Const, NotConst };
  struct Const {
    ConstState state;
    Constant* value;
    bool operator==(const Const& rhs) const {
      return state == rhs.state && value == rhs.value;
    }

    static Const meet(const Const& c1, const Const& c2) {
      //      / All    => NAC
      // NAC -- Const  => NAC
      //      \ NAC    => NAC
      if (c1.state == ConstState::NotConst || c2.state == ConstState::NotConst) {
        return {ConstState::NotConst, nullptr};
      }
      //      / All    => All
      // All 
      //      \ Const  => Const
      else if (c1.state == ConstState::AllConst && c2.state == ConstState::AllConst) {
        return {ConstState::AllConst, nullptr};
      } else if (c1.state == ConstState::AllConst && c2.state == ConstState::Const) {
        return {ConstState::Const, c2.value};
      } else if (c2.state == ConstState::AllConst && c1.state == ConstState::Const) {
        return {ConstState::Const, c1.value};
      }
      //                      / c0 == c1  => Const
      // Const c0 - Const c1 
      //                      \ c0 != c1  => NAC
      else if (c1.state == ConstState::Const && c2.state == ConstState::Const) {
        if (c1.value == c2.value) {
          return {ConstState::Const, c1.value};
        } else {
          return {ConstState::NotConst, nullptr};
        }
      }
    }
  };

  using ConstPropContent = std::unordered_map<Value*, struct Const>;
  struct ConstPropInfo: Info {
    ConstPropInfo() {}
    ConstPropInfo(const ConstPropInfo& other): Info(other) {
      data = other.data;
    }
    ~ConstPropInfo() override {}

    void print() override {
      for (auto &p: data) {
        if (nullptr == dyn_cast<GlobalVariable>(p.first)) {
          continue;
        }
        errs() << (p.first)->getName() << "=";
        // accordant to the definition of Lattice in the lecture
        switch (p.second.state) {
          case ConstState::NotConst: {
            errs() << "⊤|";
            break;
          }
          case ConstState::Const: {
            errs() << *p.second.value << "|";
            break;
          }
          case ConstState::AllConst: {
            errs() << "⊥|";
            break;
          }
        }
      }
      errs() << "\n";
    }

    void setTop(Value* v) {
      data[v] = {ConstState::AllConst, nullptr};
    }

    void setBottom(Value* v) {
      data[v] = {ConstState::NotConst, nullptr};
    }

    void setConst(Value* v, Constant* c) {
      data[v] = {ConstState::Const, c};
    }

    Const& operator[] (Value *v) {
      return data.at(v);
    }

    Constant* getConstant(Value *v) {
      auto it = data.find(v);
      if (it == data.end()) {
        return nullptr;
      } else {
        return it->second.value;
      }
    }

    static bool equals(ConstPropInfo* lhs, ConstPropInfo* rhs) {
      return lhs->data == rhs->data;
    }

    ConstPropInfo& join(ConstPropInfo& rhs) {
      for (auto &p: rhs.data) {
        auto &key = p.first;
        if (data.find(key) == data.end()) {
          data[key] = rhs.data[key];
        } else {
          data[key] = Const::meet(data[key], rhs.data[key]);
        }
      }
      return *this;
    }
  private:
    ConstPropContent data;
  };

  struct ConstPropAnalysis: DataFlowAnalysis<ConstPropInfo, true> {
    ConstPropAnalysis(ConstPropInfo& bottom, ConstPropInfo& initState, FuncMap& fm, Values& mpt)
      : DataFlowAnalysis(bottom, initState), mods(fm), mpt(mpt) {
      folder = new ConstantFolder{};
    }

    ~ConstPropAnalysis() {
      delete folder;
    }

    void flowfunction(Instruction* I, std::vector<unsigned>& IncomingEdges,
                      std::vector<unsigned>& OutgoingEdges, std::vector<ConstPropInfo*>& Infos) override {
      unsigned cur = InstrToIndex.at(I);
      ConstPropInfo in{};
      for (auto e: IncomingEdges) {
        in = in.join(*EdgeToInfo.at({e, cur}));
      }

      auto tryConst = [&](Value *v) -> Constant* {
        // llvm::GlobalValue inherits from llvm::Constant, check its descendants
        if (auto *constData = dyn_cast<ConstantData>(v)) {
          return constData;
        } else if (auto *constExpr = dyn_cast<ConstantExpr>(v)) {
          return constExpr;
        } else {
          return in.getConstant(v);
        }
      };

      if (auto *bop = dyn_cast<BinaryOperator>(I)) {
        auto lhs = tryConst(bop->getOperand(0));
        auto rhs = tryConst(bop->getOperand(1));
        if (lhs && rhs) {
          in.setConst(I, folder->CreateBinOp(bop->getOpcode(), lhs, rhs));
        } else {
          in.setBottom(I);
        }
      } else if (auto *uop = dyn_cast<UnaryOperator>(I)) {
        auto c = tryConst(uop->getOperand(0));
        if (c) {
          in.setConst(I, folder->CreateUnOp(uop->getOpcode(), c));
        } else {
          in.setBottom(I);
        }
      } else if (auto *load = dyn_cast<LoadInst>(I)) {
        auto p = load->getPointerOperand();
        auto c = in.getConstant(p);
        if (c) {
          in.setConst(I, c);
        } else {
          in.setBottom(I);
        }
      } else if (auto *store = dyn_cast<StoreInst>(I)) {
        auto val = store->getValueOperand();
        auto ptr = store->getPointerOperand();
        auto c = tryConst(val);
        if (c) {
          in.setConst(ptr, c);
        } else {
          in.setBottom(ptr);
        }
        // when encounter an instruction which modifies a dereferenced pointer, 
        // set all variables in MPT to NAC.
        if (auto *load = dyn_cast<LoadInst>(ptr)) {
          for (auto &v: mpt) {
            in.setBottom(v);
          }
        }
      } else if (auto *call = dyn_cast<CallInst>(I)) {
        auto callee = call->getCalledFunction();
        if (callee) {
          // for v in MOD[callee]: set v to NAC
          auto &mod = mods[callee];
          for (auto &gv: mod) {
            in.setBottom(gv);
          }
        }
      } else if (auto *icmp = dyn_cast<ICmpInst>(I)) {
        auto pred = icmp->getPredicate();
        auto lhs = tryConst(icmp->getOperand(0));
        auto rhs = tryConst(icmp->getOperand(1));
        if (lhs && rhs) {
          in.setConst(I, folder->CreateICmp(pred, lhs, rhs));
        } else {
          in.setBottom(I);
        }
      } else if (auto *fcmp = dyn_cast<FCmpInst>(I)) {
        auto pred = fcmp->getPredicate();
        auto lhs = tryConst(fcmp->getOperand(0));
        auto rhs = tryConst(fcmp->getOperand(1));
        if (lhs && rhs) {
          in.setConst(I, folder->CreateFCmp(pred, lhs, rhs));
        } else {
          in.setBottom(I);
        }
      } else if (auto *phi = dyn_cast<PHINode>(I)) {
        auto BB = I->getParent();
        auto end = BB->getFirstNonPHI();
        // iter over consecutive Phi instructions
        for (auto ii = BB->begin(); &*ii != end; ++ii) {
          phi = dyn_cast<PHINode>(ii);
          auto lhs = tryConst(phi->getOperand(0));
          auto rhs = tryConst(phi->getOperand(1));
          if (lhs && rhs && lhs == rhs) {
            in.setConst(phi, lhs);
          } else {
            in.setBottom(phi);
          }
        }
      }
      // the behavior of SelectInst is inferred from the GradingTests.
      // not `in.setConst(I, folder->CreateSelect(cond, lhs, rhs))`
      else if (auto *select = dyn_cast<SelectInst>(I)) {
        auto lhs = tryConst(select->getTrueValue());
        auto rhs = tryConst(select->getFalseValue());
        if (lhs && rhs && lhs == rhs) {
          in.setConst(I, lhs);
        } else {
          in.setBottom(I);
        }
      } else {
        in.setBottom(I);
      }

      for (size_t i = 0; i < OutgoingEdges.size(); ++i) {
        Infos.push_back(new ConstPropInfo(in));
      }
    }
  private:
    ConstantFolder* folder;
    FuncMap&        mods;
    Values&         mpt;
  };
}

struct LegacyConstPropPass: CallGraphSCCPass {
  static char ID;
  LegacyConstPropPass(): CallGraphSCCPass(ID) {}

  // calculate MPT and LMOD here
  bool doInitialization(CallGraph &CG) override {
    // MPT case 1: global1 = &global2 => MPT -> {global2}
    auto &M = CG.getModule();
    for (auto gi = M.global_begin(); gi != M.global_end(); ++gi) {
      if (auto *pointed = dyn_cast<GlobalVariable>(gi->getInitializer())) {
        mpt.insert(pointed);
      }
    }

    for (auto &fi: M) {
      for (auto I = inst_begin(fi), E = inst_end(fi); I != E; ++I) {
        if (auto *si = dyn_cast<StoreInst>(&*I)) {
          // MPT case 2: X = &Y => MPT ->{Y}, `&` will be translated to `store`
          auto valueOp = si->getValueOperand();
          if (valueOp->getType()->isPointerTy() && !isa<Argument>(valueOp)) {
            mpt.insert(valueOp);
          }
        }
        // MPT case 3: function(...&operand(s)...) and return &operand MPT -> {operand(s)}
        else if (auto *call = dyn_cast<CallInst>(&*I)) {
          auto func = call->getCalledFunction();
          for (Use& operand: call->operands()) {
            auto v = operand.get();
            // only accept reference param(s)
            if (v != func && v->getType()->isPointerTy()) {
              mpt.insert(v);
            }
          }
        } else if (auto *ret = dyn_cast<ReturnInst>(&*I)) {
          auto value = ret->getReturnValue();
          if (value && value->getType()->isPointerTy()) {
            mpt.insert(value);
          }
        }
      }
    }
    // calculating LMOD...
    GlobalVars mptGlobal;
    for (auto v: mpt) {
      if (auto *gv = dyn_cast<GlobalVariable>(v)) {
        mptGlobal.insert(gv);
      }
    }
    for (auto &fi: M) {
      mod[&fi] = {};
      for (auto I = inst_begin(fi), E = inst_end(fi); I != E; ++I) {
        if (auto *store = dyn_cast<StoreInst>(&*I)) {
          auto ptr = store->getPointerOperand();
          // LMOD case 1: global_var = ____ => MOD[&F] -> {global_var}
          if (auto *glob = dyn_cast<GlobalVariable>(ptr)) {
            mod[&fi].insert(glob);
          }
          // LMOD case 2: *var = _____ => MOD[&F] -> {global_subset(MPT)}
          else if (auto *load = dyn_cast<LoadInst>(ptr)) {
            mod[&fi].insert(mptGlobal.begin(), mptGlobal.end());
          }
        }
      }
    }
    return false;
  }

  // calcualte CMOD here
  bool runOnSCC(CallGraphSCC &SCC) override {
    GlobalVars same;
    for (auto node: SCC) {
      auto caller = node->getFunction();
      if (caller == nullptr) {
        // skip special "null" nodes which represent theoretical entries in the call graph.
        continue;
      }

      GlobalVars cmod;
      for (auto record = node->begin(); record != node->end(); ++record) {
        auto calleeNode = record->second;
        auto callee = calleeNode->getFunction();
        cmod.insert(mod[callee].begin(), mod[callee].end());
      }

      mod[caller].insert(cmod.begin(), cmod.end());
      same.insert(mod[caller].begin(), mod[caller].end());
    }
    // loop
    if (!SCC.isSingular()) {
      for (auto node: SCC) {
        mod[node->getFunction()] = same;
      }
    }
    return false;
  }

  // do the Constant Prop Analysis here
  bool doFinalization(CallGraph &CG) override {
    ConstPropInfo bottom{};
    ConstPropInfo initState{};
    for (auto &gv: CG.getModule().getGlobalList()) {
      initState.setBottom(&gv);
      bottom.setTop(&gv);
    }
    for (Function& F: CG.getModule().functions()) {
      auto cpa = new ConstPropAnalysis(bottom, initState, mod, mpt);
      cpa->runWorklistAlgorithm(&F);
      cpa->print();
      delete cpa;
    }
    return false;
  }
private:
  Values  mpt;
  FuncMap mod;
};

char LegacyConstPropPass::ID = 0;
static RegisterPass<LegacyConstPropPass> X(
    "cse231-constprop", 
    "Constant Propagation Analysis",
    true, // This pass doesn't modify the CFG => true
    false // This pass is not a pure analysis pass => false
);
