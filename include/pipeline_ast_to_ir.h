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


} // namespace ir

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
    if(r.value)Do(*r.value.get()); 
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
  void Do(t_calc::t_call_param& r) {
    Do(r.body);
  }
  void Do(t_calc::t_varcall& r) {
    if (!r.params) {
      result = ir.findVar(r.name);
      return;
    }
    std::vector<ir::ValueId> args;
    for (auto& arg : r.params->arr) {
      Do(arg.body);
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
    
    if(r.body)Do(*r.body.get());
    
    ir.popScope();
    module_.funcs.push_back(std::move(fn));
    
    ir.func = old_func;
    ir.curBlock = old_block;
    ir.nextValue = old_next;
  }
};
