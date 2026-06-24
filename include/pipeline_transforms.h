#pragma once

#include "pipeline_arch.h"
#include "pipeline_ir.h"
#include "pipeline_machine_ir.h"
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cassert>

// ============================================================================
// ЭТАП 4: ТРАНСФОРМАЦИИ (InstructionSelector, AbiLowering, RegisterAllocator, Legalizer)
// ============================================================================

namespace transforms {

// ============================================================================
// INSTRUCTION SELECTOR: IR -> Machine IR (выбор инструкций)
// ============================================================================

class InstructionSelector {
public:
  machine::MachineFunction run(const ir::Function& ir_func) {
    machine::MachineIRBuilder builder;
    builder.startFunction(
      ir_func.id,
      ir_func.name,
      ir_func.return_type,
      ir_func.param_types
    );
    
    // Маппинг IR ValueId -> Machine VRegId
    value_map_.clear();
    
    // Обработать все блоки
    for (const auto& ir_block : ir_func.blocks) {
      machine::MachineBlockId machine_block_id = (ir_block.id == 0)
        ? 0
        : builder.createBlock("bb" + std::to_string(ir_block.id));
      
      builder.switchBlock(machine_block_id);
      
      for (const auto& ir_inst : ir_block.insts) {
        selectInstruction(builder, ir_inst);
      }
    }
    
    return builder.getMutableFunction();
  }
  
private:
  std::unordered_map<ir::ValueId, machine::VRegId> value_map_;
  
  void selectInstruction(machine::MachineIRBuilder& builder, const ir::Instruction& ir_inst) {
    switch (ir_inst.op) {
      case ir::Operation::Const:
        selectConst(builder, ir_inst);
        break;
      case ir::Operation::Param:
        selectParam(builder, ir_inst);
        break;
      case ir::Operation::Add:
      case ir::Operation::Sub:
      case ir::Operation::Mul:
      case ir::Operation::Div:
        selectBinary(builder, ir_inst);
        break;
      case ir::Operation::Neg:
        selectNeg(builder, ir_inst);
        break;
      case ir::Operation::Cast:
        selectCast(builder, ir_inst);
        break;
      case ir::Operation::Call:
        selectCall(builder, ir_inst);
        break;
      case ir::Operation::Ret:
        selectRet(builder, ir_inst);
        break;
      case ir::Operation::Jump:
        selectJump(builder, ir_inst);
        break;
      default:
        break;
    }
  }
  
  void selectConst(machine::MachineIRBuilder& builder, const ir::Instruction& ir_inst) {
    auto vreg = builder.allocVRegForType(ir_inst.type);
    
    machine::MachineInst inst;
    inst.op = (ir_inst.type == 4) ? machine::MachineOp::Movsd : machine::MachineOp::Mov;
    inst.type = ir_inst.type;
    inst.dst = machine::MachineOperand::VReg(vreg);
    
    if (ir_inst.type == 4) {  // F64
      inst.src1 = machine::MachineOperand::ImmF64(ir_inst.imm.f64);
    } else {
      inst.src1 = machine::MachineOperand::ImmI64(ir_inst.imm.i64);
    }
    
    builder.emit(inst);
    value_map_[ir_inst.dst] = vreg;
  }
  
  void selectParam(machine::MachineIRBuilder& builder, const ir::Instruction& ir_inst) {
    auto vreg = builder.allocVRegForType(ir_inst.type);
    
    machine::MachineInst inst;
    inst.op = machine::MachineOp::Param;
    inst.type = ir_inst.type;
    inst.dst = machine::MachineOperand::VReg(vreg);
    inst.src1 = machine::MachineOperand::ImmI64(ir_inst.imm.u64);
    
    builder.emit(inst);
    value_map_[ir_inst.dst] = vreg;
  }
  
  void selectBinary(machine::MachineIRBuilder& builder, const ir::Instruction& ir_inst) {
    auto vreg_dst = builder.allocVRegForType(ir_inst.type);
    auto vreg_a = value_map_.at(ir_inst.operand_a);
    auto vreg_b = value_map_.at(ir_inst.operand_b);
    
    machine::MachineOp mop;
    bool is_fp = (ir_inst.type == 4 || ir_inst.type == 5);  // F32, F64
    
    switch (ir_inst.op) {
      case ir::Operation::Add:
        mop = is_fp ? machine::MachineOp::Addsd : machine::MachineOp::Add;
        break;
      case ir::Operation::Sub:
        mop = is_fp ? machine::MachineOp::Subsd : machine::MachineOp::Sub;
        break;
      case ir::Operation::Mul:
        mop = is_fp ? machine::MachineOp::Mulsd : machine::MachineOp::Imul;
        break;
      case ir::Operation::Div:
        mop = is_fp ? machine::MachineOp::Divsd : machine::MachineOp::Idiv;
        break;
      default:
        return;
    }
    
    machine::MachineInst inst;
    inst.op = mop;
    inst.type = ir_inst.type;
    inst.dst = machine::MachineOperand::VReg(vreg_dst);
    inst.src1 = machine::MachineOperand::VReg(vreg_a);
    inst.src2 = machine::MachineOperand::VReg(vreg_b);
    
    builder.emit(inst);
    value_map_[ir_inst.dst] = vreg_dst;
  }
  
  void selectNeg(machine::MachineIRBuilder& builder, const ir::Instruction& ir_inst) {
    // Реализуется как 0 - x
    auto vreg_a = value_map_.at(ir_inst.operand_a);
    auto vreg_zero = builder.allocVRegForType(ir_inst.type);
    auto vreg_dst = builder.allocVRegForType(ir_inst.type);
    
    // Загрузить 0
    machine::MachineInst load_zero;
    load_zero.op = (ir_inst.type == 4) ? machine::MachineOp::Movsd : machine::MachineOp::Mov;
    load_zero.type = ir_inst.type;
    load_zero.dst = machine::MachineOperand::VReg(vreg_zero);
    load_zero.src1 = (ir_inst.type == 4)
      ? machine::MachineOperand::ImmF64(0.0)
      : machine::MachineOperand::ImmI64(0);
    builder.emit(load_zero);
    
    // Вычитать
    machine::MachineInst sub_inst;
    sub_inst.op = (ir_inst.type == 4) ? machine::MachineOp::Subsd : machine::MachineOp::Sub;
    sub_inst.type = ir_inst.type;
    sub_inst.dst = machine::MachineOperand::VReg(vreg_dst);
    sub_inst.src1 = machine::MachineOperand::VReg(vreg_zero);
    sub_inst.src2 = machine::MachineOperand::VReg(vreg_a);
    builder.emit(sub_inst);
    
    value_map_[ir_inst.dst] = vreg_dst;
  }
  
  void selectCast(machine::MachineIRBuilder& builder, const ir::Instruction& ir_inst) {
    auto vreg_src = value_map_.at(ir_inst.operand_a);
    auto vreg_dst = builder.allocVRegForType(ir_inst.type);
    
    // Определить тип преобразования
    auto src_type_spec = arch::ArchRegistry::type(ir_inst.operand_a);  // Ошибка: нужен тип
    // TODO: Правильно получить исходный тип из value_map
    
    machine::MachineOp mop = machine::MachineOp::Mov;  // Placeholder
    
    machine::MachineInst inst;
    inst.op = mop;
    inst.type = ir_inst.type;
    inst.dst = machine::MachineOperand::VReg(vreg_dst);
    inst.src1 = machine::MachineOperand::VReg(vreg_src);
    builder.emit(inst);
    
    value_map_[ir_inst.dst] = vreg_dst;
  }
  
  void selectCall(machine::MachineIRBuilder& builder, const ir::Instruction& ir_inst) {
    // Загрузить все аргументы как Arg псевдо-инструкции
    for (const auto& arg_id : ir_inst.call_args) {
      auto vreg_arg = value_map_.at(arg_id);
      machine::MachineInst arg_inst;
      arg_inst.op = machine::MachineOp::Arg;
      arg_inst.src1 = machine::MachineOperand::VReg(vreg_arg);
      builder.emit(arg_inst);
    }
    
    // Сам Call
    auto vreg_dst = builder.allocVRegForType(ir_inst.type);
    machine::MachineInst call_inst;
    call_inst.op = machine::MachineOp::Call;
    call_inst.type = ir_inst.type;
    call_inst.dst = machine::MachineOperand::VReg(vreg_dst);
    call_inst.src1 = machine::MachineOperand::ImmI64(ir_inst.fn_id);
    builder.emit(call_inst);
    
    value_map_[ir_inst.dst] = vreg_dst;
  }
  
  void selectRet(machine::MachineIRBuilder& builder, const ir::Instruction& ir_inst) {
    auto vreg_ret = value_map_.at(ir_inst.operand_a);
    machine::MachineInst ret_inst;
    ret_inst.op = machine::MachineOp::Ret;
    ret_inst.src1 = machine::MachineOperand::VReg(vreg_ret);
    builder.emit(ret_inst);
  }
  
  void selectJump(machine::MachineIRBuilder& builder, const ir::Instruction& ir_inst) {
    machine::MachineBlockId target = ir_inst.imm.u64;
    builder.emitJmp(target);
  }
};

// ============================================================================
// ABI LOWERING: Преобразование Arg/Param в физические регистры
// ============================================================================

class AbiLowering {
public:
  void run(machine::MachineFunction& mfn, const arch::CallingConvention& abi) {
    abi_ = &abi;
    
    for (auto& block : mfn.blocks) {
      lowerBlock(block);
    }
  }
  
private:
  const arch::CallingConvention* abi_;
  std::vector<machine::MachineOperand> pending_args_;
  
  void lowerBlock(machine::MachineBlock& block) {
    std::vector<machine::MachineInst> result;
    pending_args_.clear();
    
    for (const auto& inst : block.insts) {
      if (inst.op == machine::MachineOp::Arg) {
        // Собрать аргумент
        pending_args_.push_back(inst.src1);
      } else if (inst.op == machine::MachineOp::Call) {
        // Выделить аргументы регистрам согласно ABI
        assignArgsToRegs(result);
        result.push_back(inst);
        pending_args_.clear();
      } else if (inst.op == machine::MachineOp::Param) {
        // Загрузить параметр из регистра аргумента
        // TODO: Реализовать правильно
        result.push_back(inst);
      } else {
        result.push_back(inst);
      }
    }
    
    block.insts = result;
  }
  
  void assignArgsToRegs(std::vector<machine::MachineInst>& result) {
    uint32_t int_index = 0;
    uint32_t fp_index = 0;
    
    for (const auto& arg_operand : pending_args_) {
      // Определить, целое или FP
      bool is_fp = false;
      // TODO: Получить тип аргумента из VReg
      
      const arch::CallConvArgPassage* passage = nullptr;
      uint32_t* index = nullptr;
      
      for (uint32_t i = 0; i < abi_->arg_passage_count; i++) {
        auto& p = abi_->arg_passages[i];
        if ((is_fp && p.reg_class == arch::RegClass::Xmm) ||
            (!is_fp && p.reg_class == arch::RegClass::Gpr)) {
          passage = &p;
          index = is_fp ? &fp_index : &int_index;
          break;
        }
      }
      
      if (!passage || *index >= passage->count) {
        // Аргумент на стеке — TODO
        continue;
      }
      
      arch::RegId preg = passage->regs[(*index)++];
      
      machine::MachineInst mov;
      mov.op = (is_fp) ? machine::MachineOp::Movsd : machine::MachineOp::Mov;
      mov.dst = machine::MachineOperand::PReg(preg);
      mov.src1 = arg_operand;
      result.push_back(mov);
    }
  }
};

// ============================================================================
// REGISTER ALLOCATOR: VReg -> PhysReg (раскраска графа конфликтов)
// ============================================================================

class RegisterAllocator {
public:
  void run(machine::MachineFunction& mfn, const arch::CallingConvention& abi) {
    abi_ = &abi;
    vregToPreg_.clear();
    
    // Построить пулы доступных регистров
    buildRegisterPools();
    
    // Вычислить счётчики использования
    computeUseCount(mfn);
    
    // Простое распределение "первый подходящий"
    for (auto& block : mfn.blocks) {
      for (auto& inst : block.insts) {
        allocateOperand(mfn, inst.dst);
        allocateOperand(mfn, inst.src1);
        allocateOperand(mfn, inst.src2);
        
        // После использования — освободить
        consumeOperand(mfn, inst.src1);
        consumeOperand(mfn, inst.src2);
      }
    }
  }
  
private:
  const arch::CallingConvention* abi_;
  std::unordered_map<machine::VRegId, arch::RegId> vregToPreg_;
  std::vector<arch::RegId> available_gpr_;
  std::vector<arch::RegId> available_xmm_;
  
  void buildRegisterPools() {
    available_gpr_.clear();
    available_xmm_.clear();
    
    arch::ArchRegistry::forEachAllocatableReg(
      [this](arch::RegId reg_id, const arch::RegisterSpec& spec) {
        if (spec.cls == arch::RegClass::Gpr) {
          available_gpr_.push_back(reg_id);
        } else if (spec.cls == arch::RegClass::Xmm) {
          available_xmm_.push_back(reg_id);
        }
      }
    );
  }
  
  void computeUseCount(machine::MachineFunction& mfn) {
    for (auto& vreg : mfn.vregs) {
      vreg.use_count = 0;
    }
    
    for (const auto& block : mfn.blocks) {
      for (const auto& inst : block.insts) {
        if (inst.src1.kind == machine::MachineOperandKind::VirtualReg) {
          for (auto& vreg : mfn.vregs) {
            if (vreg.id == inst.src1.vreg) {
              vreg.use_count++;
              break;
            }
          }
        }
        if (inst.src2.kind == machine::MachineOperandKind::VirtualReg) {
          for (auto& vreg : mfn.vregs) {
            if (vreg.id == inst.src2.vreg) {
              vreg.use_count++;
              break;
            }
          }
        }
      }
    }
  }
  
  void allocateOperand(machine::MachineFunction& mfn, machine::MachineOperand& op) {
    if (op.kind != machine::MachineOperandKind::VirtualReg) return;
    
    auto it = vregToPreg_.find(op.vreg);
    if (it != vregToPreg_.end()) {
      op = machine::MachineOperand::PReg(it->second);
      return;
    }
    
    // Найти VReg
    machine::VReg* vreg_ptr = nullptr;
    for (auto& vreg : mfn.vregs) {
      if (vreg.id == op.vreg) {
        vreg_ptr = &vreg;
        break;
      }
    }
    
    if (!vreg_ptr) return;
    
    // Выделить регистр
    auto& pool = (vreg_ptr->vreg_class == machine::VRegClass::Float)
      ? available_xmm_
      : available_gpr_;
    
    if (pool.empty()) {
      // TODO: Spilling
      return;
    }
    
    arch::RegId preg = pool.back();
    pool.pop_back();
    vregToPreg_[op.vreg] = preg;
    op = machine::MachineOperand::PReg(preg);
  }
  
  void consumeOperand(machine::MachineFunction& mfn, machine::MachineOperand& op) {
    if (op.kind != machine::MachineOperandKind::PhysicalReg) return;
    
    // Найти VReg, который использует этот преграм
    for (auto it = vregToPreg_.begin(); it != vregToPreg_.end(); ++it) {
      if (it->second == op.preg) {
        auto vreg_id = it->first;
        for (auto& vreg : mfn.vregs) {
          if (vreg.id == vreg_id) {
            if (vreg.use_count > 0) {
              vreg.use_count--;
            }
            if (vreg.use_count == 0) {
              // Освободить регистр
              auto& pool = (vreg.vreg_class == machine::VRegClass::Float)
                ? available_xmm_
                : available_gpr_;
              pool.push_back(op.preg);
              vregToPreg_.erase(it);
            }
            return;
          }
        }
      }
    }
  }
};

// ============================================================================
// LEGALIZER: Исправление x86-64 constraint'ов
// ============================================================================

class Legalizer {
public:
  void run(machine::MachineFunction& mfn) {
    for (auto& block : mfn.blocks) {
      legalizeBlock(block);
    }
  }
  
private:
  void legalizeBlock(machine::MachineBlock& block) {
    std::vector<machine::MachineInst> result;
    
    for (const auto& inst : block.insts) {
      // Проверка: mov не может быть mem->mem
      if ((inst.op == machine::MachineOp::Mov || inst.op == machine::MachineOp::Movsd) &&
          isMem(inst.src1) && isMem(inst.dst)) {
        // Добавить промежуточный регистр (RAX)
        machine::MachineInst load;
        load.op = inst.op;
        load.dst = machine::MachineOperand::PReg(0);  // RAX
        load.src1 = inst.src1;
        result.push_back(load);
        
        machine::MachineInst store;
        store.op = inst.op;
        store.dst = inst.dst;
        store.src1 = machine::MachineOperand::PReg(0);  // RAX
        result.push_back(store);
        continue;
      }
      
      // Проверка: двоичные операции не могут быть imm->imm
      if (isBinaryOp(inst.op) && isImm(inst.src1) && isImm(inst.src2)) {
        // Загрузить первый операнд в регистр
        machine::MachineInst load;
        load.op = inst.op == machine::MachineOp::Add ? machine::MachineOp::Mov : inst.op;
        load.dst = machine::MachineOperand::PReg(0);  // RAX
        load.src1 = inst.src1;
        result.push_back(load);
        
        machine::MachineInst op = inst;
        op.src1 = machine::MachineOperand::PReg(0);
        result.push_back(op);
        continue;
      }
      
      result.push_back(inst);
    }
    
    block.insts = result;
  }
  
  bool isMem(const machine::MachineOperand& op) const {
    return op.kind == machine::MachineOperandKind::Memory;
  }
  
  bool isImm(const machine::MachineOperand& op) const {
    return op.kind == machine::MachineOperandKind::ImmediateI64 ||
           op.kind == machine::MachineOperandKind::ImmediateF64;
  }
  
  bool isBinaryOp(machine::MachineOp op) const {
    return op == machine::MachineOp::Add || op == machine::MachineOp::Sub ||
           op == machine::MachineOp::Imul || op == machine::MachineOp::Idiv ||
           op == machine::MachineOp::Addsd || op == machine::MachineOp::Subsd ||
           op == machine::MachineOp::Mulsd || op == machine::MachineOp::Divsd;
  }
};

// ============================================================================
// PIPELINE: Последовательное применение всех трансформаций
// ============================================================================

class CompilationPipeline {
public:
  machine::MachineModule compileToBinaryCode(
    const ir::Module& ir_module,
    const arch::CallingConvention& abi
  ) {
    machine::MachineModule result;
    
    InstructionSelector selector;
    AbiLowering abi_lower;
    RegisterAllocator regalloc;
    Legalizer legalizer;
    
    for (const auto& ir_fn : ir_module.functions) {
      // ЭТАП 1: Выбор инструкций
      auto mfn = selector.run(ir_fn);
      
      // ЭТАП 2: Lowering соглашения вызовов
      abi_lower.run(mfn, abi);
      
      // ЭТАП 3: Распределение регистров
      regalloc.run(mfn, abi);
      
      // ЭТАП 4: Исправление констрейнтов
      legalizer.run(mfn);
      
      result.functions.push_back(mfn);
    }
    
    return result;
  }
};

} // namespace transforms
