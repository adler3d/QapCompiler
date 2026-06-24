#pragma once

#include "pipeline_arch.h"
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>

// ============================================================================
// ЭТАП 2: SSA INTERMEDIATE REPRESENTATION
// ============================================================================
// Минималистичное представление программы в SSA форме.
// - Просто данные, никаких методов "emit", "make*" и т.д.
// - Посетитель AST -> SSA происходит в отдельном месте (t_ast2ssa)
// - Всё остальное только читает/трансформирует существующий IR

namespace ir {

using ValueId = uint32_t;
using BlockId = uint32_t;
using FunctionId = uint32_t;

constexpr ValueId InvalidValue = ~ValueId{0};
constexpr BlockId InvalidBlock = ~BlockId{0};
constexpr FunctionId InvalidFunction = ~FunctionId{0};

// --- Операции IR ---
enum class Operation : uint16_t {
  // Управление потоком
  Nop,
  
  // Константы и параметры
  Const,      // imm
  Param,      // index
  
  // Унарные операции
  Neg,        // a
  Cast,       // a, + type info
  
  // Бинарные операции
  Add,        // a, b
  Sub,        // a, b
  Mul,        // a, b
  Div,        // a, b
  
  // Управление потоком (высокоуровневые)
  Call,       // fnId, args[]
  Phi,        // a, b (управление CFG)
  
  // Ветвление
  Jump,       // target block
  Branch,     // cond, true_block, false_block
  Ret,        // value
};

// --- Константные значения ---
union Immediate {
  uint64_t u64;
  int64_t  i64;
  float    f32;
  double   f64;
  
  Immediate() : u64(0) {}
  explicit Immediate(uint64_t v) : u64(v) {}
  explicit Immediate(int64_t v) : i64(v) {}
  explicit Immediate(float v) : f32(v) {}
  explicit Immediate(double v) : f64(v) {}
};

// --- Инструкция IR (один узел в SSA-графе) ---
struct Instruction {
  Operation op = Operation::Nop;
  arch::TypeId type = 0;  // результирующий тип
  
  // Операнды
  ValueId dst = InvalidValue;      // левая часть присваивания
  ValueId operand_a = InvalidValue;
  ValueId operand_b = InvalidValue;
  
  // Зависит от операции
  Immediate imm;
  FunctionId fn_id = InvalidFunction;
  std::vector<ValueId> call_args;
  
  // Метаинформация
  uint32_t line = 0;
  uint32_t column = 0;
};

// --- Базовый блок (BB) ---
struct BasicBlock {
  BlockId id = 0;
  std::string label;
  std::vector<Instruction> insts;
  
  // Для управления потоком
  std::vector<BlockId> predecessors;
  std::vector<BlockId> successors;
};

// --- Функция ---
struct Function {
  FunctionId id = InvalidFunction;
  std::string name;
  arch::TypeId return_type = 0;
  std::vector<arch::TypeId> param_types;
  
  std::vector<BasicBlock> blocks;
};

// --- Модуль (вся программа в SSA) ---
struct Module {
  std::vector<Function> functions;
};

// ============================================================================
// ПОСТРОИТЕЛЬ IR
// ============================================================================
// Помощник для конструирования IR вручную (используется в тестах и генераторе)

class IRBuilder {
public:

  IRBuilder() 
    : func(nullptr),
      curBlock(0),
      nextValue(1) {}
  
  // --- Управление функциями ---
  ir::Function* func;     // текущая функция (полностью соответствует оригиналу)
  ir::BlockId curBlock;   // текущий блок
  ir::ValueId nextValue;  // следующий ValueId
  
  // --- Таблица символов ---
  std::vector<std::unordered_map<std::string, ir::ValueId>> scopes;
  
  // --- Методы для управления scope ---
  
  void pushScope() {
    scopes.push_back({});
  }
  
  void popScope() {
    if (!scopes.empty()) {
      scopes.pop_back();
    }
  }
  
  void setVar(const std::string& name, ir::ValueId value) {
    if (scopes.empty()) {
      scopes.push_back({});
    }
    scopes.back()[name] = value;
  }
  
  ir::ValueId findVar(const std::string& name) {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
      auto found = it->find(name);
      if (found != it->end()) {
        return found->second;
      }
    }
    return ir::InvalidValue;
  }
  
  // --- Методы из оригинала (точная сигнатура) ---
  
  ir::ValueId allocValue() {
    return nextValue++;
  }
  
  ir::BasicBlock& block() {
    if (!func || curBlock >= func->blocks.size()) {
      throw std::runtime_error("Invalid current block");
    }
    return func->blocks[curBlock];
  }
  
  ir::ValueId emit(const ir::Instruction& src) {
    ir::Instruction node = src;
    node.dst = allocValue();
    block().insts.push_back(node);
    return node.dst;
  }
  
  void emitVoid(const ir::Instruction& node) {
    block().insts.push_back(node);
  }
  
  // --- Создание констант ---
  
  ir::ValueId makeConstF64(double v) {
    ir::Instruction n;
    n.op = ir::Operation::Const;
    n.type = 4;  // TypeId::F64 (из pipeline_arch.h, это индекс 4)
    n.imm.f64 = v;
    return emit(n);
  }
  
  ir::ValueId makeConstI64(int64_t v) {
    ir::Instruction n;
    n.op = ir::Operation::Const;
    n.type = 3;  // TypeId::I64 (индекс 3)
    n.imm.i64 = v;
    return emit(n);
  }
  
  // --- Создание параметров ---
  
  ir::ValueId makeParam(arch::TypeId type, uint32_t index) {
    ir::Instruction n;
    n.op = ir::Operation::Param;
    n.type = type;
    n.imm.u64 = index;
    return emit(n);
  }
  
  // --- Бинарные операции ---
  
  ir::ValueId makeAdd(arch::TypeId type, ir::ValueId a, ir::ValueId b) {
    ir::Instruction n;
    n.op = ir::Operation::Add;
    n.type = type;
    n.operand_a = a;
    n.operand_b = b;
    return emit(n);
  }
  
  ir::ValueId makeSub(arch::TypeId type, ir::ValueId a, ir::ValueId b) {
    ir::Instruction n;
    n.op = ir::Operation::Sub;
    n.type = type;
    n.operand_a = a;
    n.operand_b = b;
    return emit(n);
  }
  
  ir::ValueId makeMul(arch::TypeId type, ir::ValueId a, ir::ValueId b) {
    ir::Instruction n;
    n.op = ir::Operation::Mul;
    n.type = type;
    n.operand_a = a;
    n.operand_b = b;
    return emit(n);
  }
  
  ir::ValueId makeDiv(arch::TypeId type, ir::ValueId a, ir::ValueId b) {
    ir::Instruction n;
    n.op = ir::Operation::Div;
    n.type = type;
    n.operand_a = a;
    n.operand_b = b;
    return emit(n);
  }
  
  // --- Унарные операции ---
  
  ir::ValueId makeNeg(arch::TypeId type, ir::ValueId a) {
    ir::Instruction n;
    n.op = ir::Operation::Neg;
    n.type = type;
    n.operand_a = a;
    return emit(n);
  }
  
  // --- Вызовы функций ---
  
  ir::ValueId makeCall(arch::TypeId type, ir::FunctionId fn, const std::vector<ir::ValueId>& args) {
    ir::Instruction n;
    n.op = ir::Operation::Call;
    n.type = type;
    n.fn_id = fn;
    n.call_args = args;
    return emit(n);
  }
  
  // --- Return ---
  
  void makeRet(ir::ValueId v) {
    ir::Instruction n;
    n.op = ir::Operation::Ret;
    n.operand_a = v;
    emitVoid(n);
  }
  
  void startFunction(
    FunctionId id,
    const std::string& name,
    arch::TypeId return_type,
    const std::vector<arch::TypeId>& param_types
  ) {
    current_fn = Function{
      /*.id = */id,
      /*.name = */name,
      /*.return_type = */return_type,
      /*.param_types = */param_types,
    };
    current_fn.blocks.push_back(BasicBlock{/*.id = */0,/* .label = */"entry"});
    current_block_id = 0;
    next_value_id = 1;
  }
  
  void endFunction() {
    // Валидация: последняя инструкция должна быть Ret или Jump
    auto& last_block = current_fn.blocks.back();
    if (!last_block.insts.empty()) {
      auto& last = last_block.insts.back();
      if (last.op != Operation::Ret && last.op != Operation::Jump) {
        // Можем добавить error handling или неявный ret
      }
    }
  }
  
  // --- Конструирование инструкций ---
  
  ValueId emitConst(arch::TypeId type, const Immediate& imm) {
    Instruction inst;
    inst.op = Operation::Const;
    inst.type = type;
    inst.dst = allocValue();
    inst.imm = imm;
    return emit(inst);
  }
  
  ValueId emitParam(arch::TypeId type, uint32_t param_index) {
    Instruction inst;
    inst.op = Operation::Param;
    inst.type = type;
    inst.dst = allocValue();
    inst.imm = Immediate(uint64_t(param_index));
    return emit(inst);
  }
  
  ValueId emitBinary(
    Operation op,
    arch::TypeId type,
    ValueId a,
    ValueId b
  ) {
    if (op != Operation::Add && op != Operation::Sub && 
        op != Operation::Mul && op != Operation::Div) {
      throw std::runtime_error("Invalid binary operation");
    }
    
    Instruction inst;
    inst.op = op;
    inst.type = type;
    inst.dst = allocValue();
    inst.operand_a = a;
    inst.operand_b = b;
    return emit(inst);
  }
  
  ValueId emitNeg(arch::TypeId type, ValueId a) {
    Instruction inst;
    inst.op = Operation::Neg;
    inst.type = type;
    inst.dst = allocValue();
    inst.operand_a = a;
    return emit(inst);
  }
  
  ValueId emitCast(arch::TypeId to_type, ValueId from_value) {
    Instruction inst;
    inst.op = Operation::Cast;
    inst.type = to_type;
    inst.dst = allocValue();
    inst.operand_a = from_value;
    return emit(inst);
  }
  
  ValueId emitCall(
    arch::TypeId return_type,
    FunctionId fn_id,
    const std::vector<ValueId>& args
  ) {
    Instruction inst;
    inst.op = Operation::Call;
    inst.type = return_type;
    inst.dst = allocValue();
    inst.fn_id = fn_id;
    inst.call_args = args;
    return emit(inst);
  }
  
  BlockId createBlock(const std::string& label = "") {
    BlockId id = current_fn.blocks.size();
    current_fn.blocks.push_back(BasicBlock{
      /*.id = */id,
      /*.label = */label.empty() ? ("bb" + std::to_string(id)) : label,
    });
    return id;
  }
  
  void switchBlock(BlockId block_id) {
    if (block_id >= current_fn.blocks.size()) {
      throw std::runtime_error("Invalid block ID");
    }
    current_block_id = block_id;
  }
  
  void emitJump(BlockId target) {
    Instruction inst;
    inst.op = Operation::Jump;
    inst.imm = Immediate(uint64_t(target));
    currentBlock().insts.push_back(inst);
    
    // Обновить граф потока
    currentBlock().successors.push_back(target);
    current_fn.blocks[target].predecessors.push_back(current_block_id);
  }
  
  void emitBranch(ValueId cond, BlockId true_block, BlockId false_block) {
    Instruction inst;
    inst.op = Operation::Branch;
    inst.operand_a = cond;
    inst.imm = Immediate(uint64_t(true_block));  // true_block в imm
    // false_block нужно хранить в другом месте — пока добавим в operand_b для простоты
    // TODO: лучше расширить структуру
    currentBlock().insts.push_back(inst);
    
    currentBlock().successors.push_back(true_block);
    currentBlock().successors.push_back(false_block);
    current_fn.blocks[true_block].predecessors.push_back(current_block_id);
    current_fn.blocks[false_block].predecessors.push_back(current_block_id);
  }
  
  void emitRet(ValueId value) {
    Instruction inst;
    inst.op = Operation::Ret;
    inst.operand_a = value;
    currentBlock().insts.push_back(inst);
  }
  
  // --- Доступ к текущему состоянию ---
  
  const Function& getFunction() const { return current_fn; }
  Function& getMutableFunction() { return current_fn; }
  
  BasicBlock& currentBlock() {
    if (current_block_id >= current_fn.blocks.size()) {
      throw std::runtime_error("Invalid current block");
    }
    return current_fn.blocks[current_block_id];
  }
  
private:
  Function current_fn;
  BlockId current_block_id = 0;
  ValueId next_value_id = 1;
  
};

// ============================================================================
// УТИЛИТЫ
// ============================================================================

inline std::string operationName(Operation op) {
  switch (op) {
    case Operation::Nop:     return "nop";
    case Operation::Const:   return "const";
    case Operation::Param:   return "param";
    case Operation::Neg:     return "neg";
    case Operation::Cast:    return "cast";
    case Operation::Add:     return "add";
    case Operation::Sub:     return "sub";
    case Operation::Mul:     return "mul";
    case Operation::Div:     return "div";
    case Operation::Call:    return "call";
    case Operation::Phi:     return "phi";
    case Operation::Jump:    return "jump";
    case Operation::Branch:  return "branch";
    case Operation::Ret:     return "ret";
    default:                 return "?";
  }
}

inline std::string valueToStr(ValueId v) {
  if (v == InvalidValue) return "-";
  return "v" + std::to_string(v);
}

inline std::string dumpInstruction(const Instruction& inst) {
  std::string result;
  
  if (inst.dst != InvalidValue) {
    result += valueToStr(inst.dst) + " = ";
  }
  
  result += operationName(inst.op);
  
  switch (inst.op) {
    case Operation::Const: {
      result += " ";
      auto& type_spec = arch::ArchRegistry::type(inst.type);
      if (inst.type == arch::TypeId(4)) {  // F64
        result += std::to_string(inst.imm.f64);
      } else if (inst.type == arch::TypeId(3)) {  // I64
        result += std::to_string(inst.imm.i64);
      } else {
        result += "?";
      }
      break;
    }
    
    case Operation::Param: {
      result += " " + std::to_string(inst.imm.u64);
      break;
    }
    
    case Operation::Add:
    case Operation::Sub:
    case Operation::Mul:
    case Operation::Div: {
      result += " " + valueToStr(inst.operand_a) + ", " + valueToStr(inst.operand_b);
      break;
    }
    
    case Operation::Neg: {
      result += " " + valueToStr(inst.operand_a);
      break;
    }
    
    case Operation::Call: {
      result += " f" + std::to_string(inst.fn_id) + "(";
      for (size_t i = 0; i < inst.call_args.size(); i++) {
        if (i > 0) result += ", ";
        result += valueToStr(inst.call_args[i]);
      }
      result += ")";
      break;
    }
    
    case Operation::Ret: {
      result += " " + valueToStr(inst.operand_a);
      break;
    }
    
    default:
      break;
  }
  
  return result;
}

inline std::string dumpFunction(const Function& fn) {
  std::string result;
  result += "function " + fn.name + "(";
  
  for (size_t i = 0; i < fn.param_types.size(); i++) {
    if (i > 0) result += ", ";
    auto& type_spec = arch::ArchRegistry::type(fn.param_types[i]);
    result += std::string(type_spec.name);
  }
  
  result += ") -> ";
  result += std::string(arch::ArchRegistry::type(fn.return_type).name);
  result += " {\n";
  
  for (const auto& block : fn.blocks) {
    result += "\n  block " + block.label + ":\n";
    for (const auto& inst : block.insts) {
      result += "    " + dumpInstruction(inst) + "\n";
    }
  }
  
  result += "}\n";
  return result;
}

} // namespace ir
