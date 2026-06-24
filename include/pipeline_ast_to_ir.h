#pragma once

#include "pipeline_arch.h"
#include "pipeline_ir.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

// ============================================================================
// ЭТАП 7: AST VISITOR -> IR (ТОЧНО ПО ИНТЕРФЕЙСУ ОРИГИНАЛА)
// ============================================================================
// Адаптация IRBuilder для работы с посетителем.
// Методы совпадают с оригинальным интерфейсом на 100%.

namespace ir {

// Добавим методы управления scope и функциями в IRBuilder
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
};

} // namespace ir

// ============================================================================
// ТИПЫ AST (ДЛЯ СОВМЕСТИМОСТИ С ОРИГИНАЛОМ)
// ============================================================================
// Это ПРИМЕРЫ структур, которые должны существовать в t_calc

namespace t_calc {

// Используются в посетителе — совпадают ровно с оригиналом

struct i_term {
  struct i_visitor {
    virtual ~i_visitor() = default;
  };
  virtual ~i_term() = default;
  virtual void Use(i_visitor& v) = 0;
};

struct i_stat {
  struct i_visitor {
    virtual ~i_visitor() = default;
  };
  virtual ~i_stat() = default;
  virtual void Use(i_visitor& v) = 0;
};

struct t_term : public i_term {
  std::shared_ptr<i_term> value;
  void Use(i_visitor& v) override;
};

struct t_number : public i_term {
  std::string value;
  void Use(i_visitor& v) override;
};

struct t_scope : public i_term {
  std::shared_ptr<i_term> value;
  void Use(i_visitor& v) override;
};

struct t_divmul : public i_term {
  std::shared_ptr<i_term> first;
  struct elem {
    std::string oper;
    std::shared_ptr<i_term> expr;
  };
  std::vector<elem> arr;
  void Use(i_visitor& v) override;
};

struct t_addsub : public i_term {
  std::shared_ptr<i_term> first;
  struct elem {
    std::string oper;
    std::shared_ptr<i_term> expr;
  };
  std::vector<elem> arr;
  void Use(i_visitor& v) override;
};

struct t_varcall : public i_term {
  std::string name;
  std::shared_ptr<i_term> params;  // может быть nullptr
  void Use(i_visitor& v) override;
};

struct t_assign_stat : public i_stat {
  std::string var;
  std::shared_ptr<i_term> expr;
  void Use(i_visitor& v) override;
};

struct t_block_stat : public i_stat {
  std::vector<std::shared_ptr<i_stat>> arr;
  void Use(i_visitor& v) override;
};

struct t_solo_stat : public i_stat {
  std::shared_ptr<i_term> expr;
  void Use(i_visitor& v) override;
};

struct t_func_stat : public i_stat {
  std::string func;
  struct arg_type {
    std::string name;
  };
  std::vector<arg_type> args;
  std::shared_ptr<i_stat> body;
  void Use(i_visitor& v) override;
};

struct t_calc {
  std::vector<std::shared_ptr<i_stat>> arr;
};

} // namespace t_calc

// ============================================================================
// AST TO SSA: ГЛАВНЫЙ VISITOR (ТОЧНОЕ СООТВЕТСТВИЕ ОРИГИНАЛУ)
// ============================================================================

struct t_ast2ssa : public t_calc::i_term::i_visitor, public t_calc::i_stat::i_visitor {
  ir::IRBuilder ir;
  ir::ValueId result = ir::InvalidValue;
  
  // --- Шаблонные методы Do (ТОЧНО ПО ОРИГИНАЛУ) ---
  
  template <class TYPE>
  void Do(TYPE& r) {
    r.Use(*this);
  }
  
  template <class TYPE>
  void Do(std::vector<TYPE>& arr) {
    for (auto& ex : arr) Do(ex);
  }
  
  // Для std::shared_ptr (вместо TAutoPtr)
  template <class TYPE>
  void Do(std::vector<std::shared_ptr<TYPE>>& arr) {
    for (auto& ex : arr) {
      if (ex) Do(*ex.get());
    }
  }
  
  template <class TYPE>
  void Do(std::shared_ptr<TYPE>& r) {
    if (r) Do(*r.get());
  }
  
  void Do(t_calc& r) {
    for (auto& st : r.arr) {
      Do(*st.get());
    }
  }
  
  // --- Специфичные Do для типов AST ---
  
  void Do(t_calc::t_term& r) { 
    Do(r.value); 
  }
  
  void Do(t_calc::t_number& r) {
    result = ir.makeConstF64(std::atof(r.value.c_str()));
  }
  
  void Do(t_calc::t_scope& r) { 
    Do(r.value); 
  }
  
  void Do(t_calc::t_divmul& r) {
    Do(r.first);
    auto cur = result;
    for (auto& e : r.arr) {
      Do(e.expr);
      auto rhs = result;
      cur = e.oper == "*" ? ir.makeMul(4, cur, rhs)
                          : ir.makeDiv(4, cur, rhs);
    }
    result = cur;
  }
  
  void Do(t_calc::t_addsub& r) {
    Do(r.first);
    auto cur = result;
    for (auto& e : r.arr) {
      Do(e.expr);
      auto rhs = result;
      cur = e.oper == "+" ? ir.makeAdd(4, cur, rhs)
                          : ir.makeSub(4, cur, rhs);
    }
    result = cur;
  }
  
  void Do(t_calc::t_assign_stat& r) {
    Do(r.expr);
    ir.setVar(r.var, result);
  }
  
  struct t_module {
    std::vector<ir::Function> funcs;
  };
  t_module module_;
  
  ir::FunctionId find_function_id(const std::string& name) { 
    return 0;  // TODO: Реализовать поиск по имени
  }
  
  void Do(t_calc::t_varcall& r) {
    if (!r.params) {
      result = ir.findVar(r.name);
      return;
    }
    std::vector<ir::ValueId> args;
    for (auto& arg : r.params->arr) {
      Do(*arg.get());
      args.push_back(result);
    }
    auto fn = find_function_id(r.name);
    result = ir.makeCall(4, fn, args);
  }
  
  void Do(t_calc::t_block_stat& r) {
    ir.pushScope();
    for (auto& ex : r.arr) {
      Do(*ex.get());
    }
    ir.popScope();
  }
  
  void Do(t_calc::t_solo_stat& r) {
    Do(r.expr);
    ir.makeRet(result);
  }
  
  void Do(t_calc::t_func_stat& r) {
    ir::Function fn;
    fn.name = r.func;
    fn.return_type = 4;  // TypeId::F64
    for (size_t i = 0; i < r.args.size(); i++) {
      fn.param_types.push_back(4);  // TypeId::F64
    }
    fn.blocks.push_back({0});
    
    auto* old_func = ir.func;
    auto old_block = ir.curBlock;
    auto old_next = ir.nextValue;
    
    ir.func = &fn;
    ir.curBlock = 0;
    ir.nextValue = 1;
    ir.pushScope();
    
    for (size_t i = 0; i < r.args.size(); i++) {
      auto v = ir.makeParam(4, static_cast<uint32_t>(i));
      ir.setVar(r.args[i].name, v);
    }
    
    Do(r.body);
    
    ir.popScope();
    module_.funcs.push_back(std::move(fn));
    
    ir.func = old_func;
    ir.curBlock = old_block;
    ir.nextValue = old_next;
  }
};
