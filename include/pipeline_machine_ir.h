#pragma once

#include "pipeline_arch.h"
#include "pipeline_ir.h"
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <cassert>

// ============================================================================
// ЭТАП 3: MACHINE IR
// ============================================================================
// Представление, близкое к x86-64 ассемблеру.
// - Виртуальные регистры (VReg) с последующей раскраской
// - Машинные операции (MachineOp) соответствуют реальным инструкциям
// - Операнды: VReg, PhysReg, Immediate, Memory

namespace machine {

using VRegId = uint32_t;
using MachineBlockId = uint32_t;

constexpr VRegId InvalidVReg = ~VRegId(0);
constexpr MachineBlockId InvalidMachineBlock = ~MachineBlockId(0);

// ============================================================================
// ВИРТУАЛЬНЫЕ РЕГИСТРЫ
// ============================================================================

enum class VRegClass : uint8_t {
  Integer,    // RAX, RBX, ...
  Float,      // XMM0, XMM1, ...
};

struct VReg {
  VRegId id;
  arch::TypeId type;
  VRegClass vreg_class;
  uint32_t use_count;   // для простого распределения
  
  VReg() : id(InvalidVReg), type(0), vreg_class(VRegClass::Integer), use_count(0) {}
  
  VReg(VRegId id_, arch::TypeId type_, VRegClass cls)
    : id(id_), type(type_), vreg_class(cls), use_count(0) {}
};

// ============================================================================
// ОПЕРАНДЫ МАШИННОГО КОДА
// ============================================================================

enum class MachineOperandKind : uint8_t {
  None,
  VirtualReg,   // Виртуальный регистр (перед раскраской)
  PhysicalReg,  // Физический регистр (после раскраски)
  ImmediateI64,
  ImmediateF64,
  Memory,       // base + displacement
};

struct MachineOperand {
  MachineOperandKind kind;
  
  union {
    VRegId vreg;
    arch::RegId preg;
    int64_t imm_i64;
    double imm_f64;
    struct {
      arch::RegId base;
      int32_t displacement;
    } mem;
  };
  
  MachineOperand() : kind(MachineOperandKind::None) {}
  
  static MachineOperand VReg(VRegId id) {
    MachineOperand op;
    op.kind = MachineOperandKind::VirtualReg;
    op.vreg = id;
    return op;
  }
  
  static MachineOperand PReg(arch::RegId id) {
    MachineOperand op;
    op.kind = MachineOperandKind::PhysicalReg;
    op.preg = id;
    return op;
  }
  
  static MachineOperand ImmI64(int64_t val) {
    MachineOperand op;
    op.kind = MachineOperandKind::ImmediateI64;
    op.imm_i64 = val;
    return op;
  }
  
  static MachineOperand ImmF64(double val) {
    MachineOperand op;
    op.kind = MachineOperandKind::ImmediateF64;
    op.imm_f64 = val;
    return op;
  }
  
  static MachineOperand Mem(arch::RegId base, int32_t disp) {
    MachineOperand op;
    op.kind = MachineOperandKind::Memory;
    op.mem.base = base;
    op.mem.displacement = disp;
    return op;
  }
};

// ============================================================================
// МАШИННЫЕ ОПЕРАЦИИ
// ============================================================================

enum class MachineOp : uint16_t {
  // Псевдо-операции
  Param,        // Параметр функции (для ABI lowering)
  Arg,          // Аргумент функции (для ABI lowering)
  
  // Реальные x86-64 инструкции
  
  // Движение данных
  Mov,          // mov dst, src (Integer)
  Movsd,        // movsd dst, src (Double)
  Movss,        // movss dst, src (Float)
  
  // Целые арифметические операции
  Add,          // add dst, src
  Sub,          // sub dst, src
  Imul,         // imul dst, src (может быть 1 или 2 операнда)
  Idiv,         // idiv rax:rdx, src (специальная)
  
  // Плавающая точка
  Addsd,        // addsd dst, src
  Subsd,        // subsd dst, src
  Mulsd,        // mulsd dst, src
  Divsd,        // divsd dst, src
  Cvtsi2sd,     // cvtsi2sd dst, src (int->double)
  Cvttsd2si,    // cvttsd2si dst, src (double->int, truncate)
  
  // Управление потоком
  Call,         // Вызов функции
  Ret,          // Возврат
  Jmp,          // Безусловный прыжок
  Jcc,          // Условный прыжок (сохраняет условие в отдельное поле)
  
  // Стек
  Push,         // push src
  Pop,          // pop dst
  
  // Адресная арифметика
  Lea,          // lea dst, [rip + displacement] или lea dst, [base + disp]
};

struct MachineInst {
  MachineOp op;
  arch::TypeId type;          // Тип данных для операции
  
  MachineOperand dst;
  MachineOperand src1;
  MachineOperand src2;
  
  // Метаинформация
  uint32_t line;
  uint32_t column;
  
  // Для условных прыжков
  MachineBlockId jump_target;
  
  MachineInst() 
    : op(MachineOp::Mov), type(0), line(0), column(0),
      jump_target(InvalidMachineBlock) {}
};

// ============================================================================
// МАШИННЫЕ БЛОКИ
// ============================================================================

struct MachineBlock {
  MachineBlockId id;
  std::string label;
  std::vector<MachineInst> insts;
  
  std::vector<MachineBlockId> predecessors;
  std::vector<MachineBlockId> successors;
  
  MachineBlock() : id(0) {}
  explicit MachineBlock(MachineBlockId id_, const std::string& label_ = "")
    : id(id_), label(label_) {}
};

// ============================================================================
// МАШИННАЯ ФУНКЦИЯ
// ============================================================================

struct MachineFunction {
  ir::FunctionId id;
  std::string name;
  arch::TypeId return_type;
  
  std::vector<VReg> vregs;
  std::vector<MachineBlock> blocks;
  
  // Для ABI lowering
  std::vector<arch::TypeId> param_types;
  
  MachineFunction() : id(ir::InvalidFunction), return_type(0) {}
};

// ============================================================================
// МАШИННЫЙ МОДУЛЬ
// ============================================================================

struct MachineModule {
  std::vector<MachineFunction> functions;
};

// ============================================================================
// ПОСТРОИТЕЛЬ MACHINE IR
// ============================================================================

class MachineIRBuilder {
public:
  MachineIRBuilder() : next_vreg_id_(1), next_block_id_(0) {}
  
  void startFunction(
    ir::FunctionId fn_id,
    const std::string& name,
    arch::TypeId return_type,
    const std::vector<arch::TypeId>& param_types
  ) {
    current_fn_ = MachineFunction();
    current_fn_.id = fn_id;
    current_fn_.name = name;
    current_fn_.return_type = return_type;
    current_fn_.param_types = param_types;
    
    // Создать entry block
    current_fn_.blocks.push_back(MachineBlock(0, "entry"));
    current_block_id_ = 0;
    
    next_vreg_id_ = 1;
    next_block_id_ = 1;
  }
  
  const MachineFunction& getFunction() const {
    return current_fn_;
  }
  
  MachineFunction getMutableFunction() {
    return current_fn_;
  }
  
  VRegId allocVReg(arch::TypeId type, VRegClass vreg_class) {
    VRegId id = next_vreg_id_++;
    current_fn_.vregs.push_back(VReg(id, type, vreg_class));
    return id;
  }
  
  VRegId allocVRegForType(arch::TypeId type) {
    auto& type_spec = arch::ArchRegistry::type(type);
    VRegClass cls = (type_spec.preferred_class == arch::RegClass::Xmm)
                      ? VRegClass::Float
                      : VRegClass::Integer;
    return allocVReg(type, cls);
  }
  
  MachineBlockId createBlock(const std::string& label = "") {
    MachineBlockId id = next_block_id_++;
    std::string block_label = label;
    if (block_label.empty()) {
      block_label = "bb" + std::to_string(id);
    }
    current_fn_.blocks.push_back(MachineBlock(id, block_label));
    return id;
  }
  
  void switchBlock(MachineBlockId block_id) {
    for (size_t i = 0; i < current_fn_.blocks.size(); i++) {
      if (current_fn_.blocks[i].id == block_id) {
        current_block_id_ = block_id;
        return;
      }
    }
    assert(false && "Invalid block ID");
  }
  
  void emit(const MachineInst& inst) {
    MachineBlock* block = currentBlock();
    assert(block && "No current block");
    block->insts.push_back(inst);
  }
  
  // Удобные методы для генерации инструкций
  
  void emitMov(arch::TypeId type, const MachineOperand& dst, const MachineOperand& src) {
    MachineInst inst;
    inst.op = (type == 4 || type == 5) ? MachineOp::Movsd : MachineOp::Mov;
    inst.type = type;
    inst.dst = dst;
    inst.src1 = src;
    emit(inst);
  }
  
  void emitAdd(arch::TypeId type, const MachineOperand& dst, const MachineOperand& src) {
    MachineInst inst;
    inst.op = (type == 4 || type == 5) ? MachineOp::Addsd : MachineOp::Add;
    inst.type = type;
    inst.dst = dst;
    inst.src1 = src;
    emit(inst);
  }
  
  void emitCall(arch::TypeId return_type, const MachineOperand& fn_ref) {
    MachineInst inst;
    inst.op = MachineOp::Call;
    inst.type = return_type;
    inst.src1 = fn_ref;
    emit(inst);
  }
  
  void emitRet(const MachineOperand& value) {
    MachineInst inst;
    inst.op = MachineOp::Ret;
    inst.src1 = value;
    emit(inst);
  }
  
  void emitJmp(MachineBlockId target) {
    MachineInst inst;
    inst.op = MachineOp::Jmp;
    inst.jump_target = target;
    emit(inst);
  }
  
private:
  MachineFunction current_fn_;
  MachineBlockId current_block_id_;
  VRegId next_vreg_id_;
  MachineBlockId next_block_id_;
  
  MachineBlock* currentBlock() {
    for (auto& block : current_fn_.blocks) {
      if (block.id == current_block_id_) {
        return &block;
      }
    }
    return nullptr;
  }
};

// ============================================================================
// УТИЛИТЫ
// ============================================================================

inline const char* machineOpName(MachineOp op) {
  switch (op) {
    case MachineOp::Param:       return "param";
    case MachineOp::Arg:         return "arg";
    case MachineOp::Mov:         return "mov";
    case MachineOp::Movsd:       return "movsd";
    case MachineOp::Movss:       return "movss";
    case MachineOp::Add:         return "add";
    case MachineOp::Sub:         return "sub";
    case MachineOp::Imul:        return "imul";
    case MachineOp::Idiv:        return "idiv";
    case MachineOp::Addsd:       return "addsd";
    case MachineOp::Subsd:       return "subsd";
    case MachineOp::Mulsd:       return "mulsd";
    case MachineOp::Divsd:       return "divsd";
    case MachineOp::Cvtsi2sd:    return "cvtsi2sd";
    case MachineOp::Cvttsd2si:   return "cvttsd2si";
    case MachineOp::Call:        return "call";
    case MachineOp::Ret:         return "ret";
    case MachineOp::Jmp:         return "jmp";
    case MachineOp::Jcc:         return "jcc";
    case MachineOp::Push:        return "push";
    case MachineOp::Pop:         return "pop";
    case MachineOp::Lea:         return "lea";
    default:                     return "?";
  }
}

inline std::string operandToStr(const MachineOperand& op) {
  switch (op.kind) {
    case MachineOperandKind::None:
      return "-";
    case MachineOperandKind::VirtualReg:
      return "%v" + std::to_string(op.vreg);
    case MachineOperandKind::PhysicalReg:
      return arch::ArchRegistry::reg(op.preg).name.data();
    case MachineOperandKind::ImmediateI64:
      return std::to_string(op.imm_i64);
    case MachineOperandKind::ImmediateF64:
      return std::to_string(op.imm_f64);
    case MachineOperandKind::Memory: {
      auto& reg_spec = arch::ArchRegistry::reg(op.mem.base);
      return "[" + std::string(reg_spec.name) + " + " + std::to_string(op.mem.displacement) + "]";
    }
    default:
      return "?";
  }
}

inline std::string dumpMachineInst(const MachineInst& inst) {
  std::string result = machineOpName(inst.op);
  
  if (inst.dst.kind != MachineOperandKind::None) {
    result += " " + operandToStr(inst.dst);
  }
  
  if (inst.src1.kind != MachineOperandKind::None) {
    result += ", " + operandToStr(inst.src1);
  }
  
  if (inst.src2.kind != MachineOperandKind::None) {
    result += ", " + operandToStr(inst.src2);
  }
  
  return result;
}

inline std::string dumpMachineFunction(const MachineFunction& fn) {
  std::string result;
  result += "machine_function " + fn.name + " {\n";
  
  result += "  vregs:\n";
  for (const auto& vreg : fn.vregs) {
    result += "    %v" + std::to_string(vreg.id) + " ";
    result += arch::ArchRegistry::type(vreg.type).name.data();
    result += " (uses=" + std::to_string(vreg.use_count) + ")\n";
  }
  
  result += "\n";
  for (const auto& block : fn.blocks) {
    result += "  " + block.label + ":\n";
    for (const auto& inst : block.insts) {
      result += "    " + dumpMachineInst(inst) + "\n";
    }
  }
  
  result += "}\n";
  return result;
}

} // namespace machine
