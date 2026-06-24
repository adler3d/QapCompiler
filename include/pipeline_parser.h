#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cctype>
#include <stdexcept>
#include <sstream>

// ============================================================================
// PARSER: Грамматика для t_calc DSL
// ============================================================================
// Строит AST совместимый с t_ast2ssa визитором

namespace parser {

// ============================================================================
// LEXER: Токенизация
// ============================================================================

enum class TokenKind {
  End,
  Number,       // 123.456
  Ident,        // name, x, func
  LParen,       // (
  RParen,       // )
  LBrace,       // {
  RBrace,       // }
  Semicolon,    // ;
  Comma,        // ,
  Equals,       // =
  Plus,         // +
  Minus,        // -
  Star,         // *
  Slash,        // /
  Dot,          // .
  Question,     // ?
  Comment,      // /* */ или //
};

struct Token {
  TokenKind kind;
  std::string text;
  size_t line;
  size_t column;
};

class Lexer {
public:
  explicit Lexer(const std::string& input) 
    : input_(input), pos_(0), line_(1), column_(1) {}
  
  Token next() {
    skipWhitespaceAndComments();
    
    if (pos_ >= input_.size()) {
      return Token{TokenKind::End, "", line_, column_};
    }
    
    char c = input_[pos_];
    size_t start_col = column_;
    
    // Числа
    if (std::isdigit(c)) {
      return lexNumber();
    }
    
    // Идентификаторы
    if (std::isalpha(c) || c == '_' || c == '$' || c == '@') {
      return lexIdent();
    }
    
    // Односимвольные токены
    Token tok{TokenKind::End, std::string(1, c), line_, start_col};
    
    switch (c) {
      case '(': tok.kind = TokenKind::LParen; break;
      case ')': tok.kind = TokenKind::RParen; break;
      case '{': tok.kind = TokenKind::LBrace; break;
      case '}': tok.kind = TokenKind::RBrace; break;
      case ';': tok.kind = TokenKind::Semicolon; break;
      case ',': tok.kind = TokenKind::Comma; break;
      case '=': tok.kind = TokenKind::Equals; break;
      case '+': tok.kind = TokenKind::Plus; break;
      case '-': tok.kind = TokenKind::Minus; break;
      case '*': tok.kind = TokenKind::Star; break;
      case '/': tok.kind = TokenKind::Slash; break;
      case '.': tok.kind = TokenKind::Dot; break;
      case '?': tok.kind = TokenKind::Question; break;
      default:
        throw std::runtime_error(std::string("Unexpected character: ") + c);
    }
    
    advance();
    return tok;
  }
  
  Token peek() {
    size_t saved_pos = pos_;
    size_t saved_line = line_;
    size_t saved_col = column_;
    
    Token tok = next();
    
    pos_ = saved_pos;
    line_ = saved_line;
    column_ = saved_col;
    
    return tok;
  }
  
private:
  std::string input_;
  size_t pos_;
  size_t line_;
  size_t column_;
  
  void advance() {
    if (pos_ < input_.size()) {
      if (input_[pos_] == '\n') {
        line_++;
        column_ = 1;
      } else {
        column_++;
      }
      pos_++;
    }
  }
  
  void skipWhitespaceAndComments() {
    while (pos_ < input_.size()) {
      char c = input_[pos_];
      
      if (std::isspace(c)) {
        advance();
      } else if (c == '/' && pos_ + 1 < input_.size()) {
        if (input_[pos_ + 1] == '/') {
          // C++ comment
          while (pos_ < input_.size() && input_[pos_] != '\n') {
            advance();
          }
        } else if (input_[pos_ + 1] == '*') {
          // C comment
          advance(); // skip /
          advance(); // skip *
          while (pos_ + 1 < input_.size()) {
            if (input_[pos_] == '*' && input_[pos_ + 1] == '/') {
              advance(); // skip *
              advance(); // skip /
              break;
            }
            advance();
          }
        } else {
          break;
        }
      } else {
        break;
      }
    }
  }
  
  Token lexNumber() {
    size_t start = pos_;
    size_t start_col = column_;
    
    // Целая часть
    while (pos_ < input_.size() && std::isdigit(input_[pos_])) {
      advance();
    }
    
    // Дробная часть
    if (pos_ < input_.size() && input_[pos_] == '.') {
      advance();
      while (pos_ < input_.size() && std::isdigit(input_[pos_])) {
        advance();
      }
    }
    
    return Token{
      TokenKind::Number,
      input_.substr(start, pos_ - start),
      line_,
      start_col
    };
  }
  
  Token lexIdent() {
    size_t start = pos_;
    size_t start_col = column_;
    
    while (pos_ < input_.size() && 
           (std::isalnum(input_[pos_]) || input_[pos_] == '_' || 
            input_[pos_] == '$' || input_[pos_] == '@')) {
      advance();
    }
    
    return Token{
      TokenKind::Ident,
      input_.substr(start, pos_ - start),
      line_,
      start_col
    };
  }
};

// ============================================================================
// PARSER: Рекурсивный спуск
// ============================================================================
// Строит AST структуры из namespace t_calc

class Parser {
public:
  explicit Parser(const std::string& input) : lexer_(input), current_(lexer_.next()) {}
  
  std::shared_ptr<t_calc::t_calc> parse() {
    auto calc = std::make_shared<t_calc::t_calc>();
    
    while (current_.kind != TokenKind::End) {
      auto stat = parseStat();
      if (stat) {
        calc->arr.push_back(stat);
      }
    }
    
    return calc;
  }
  
private:
  Lexer lexer_;
  Token current_;
  
  void advance() {
    current_ = lexer_.next();
  }
  
  void expect(TokenKind kind) {
    if (current_.kind != kind) {
      throw std::runtime_error("Unexpected token");
    }
    advance();
  }
  
  // ========================================================================
  // STATEMENTS (i_stat)
  // ========================================================================
  
  std::shared_ptr<t_calc::i_stat> parseStat() {
    // Попробовать t_func_stat: name ( args ) = stat
    if (current_.kind == TokenKind::Ident) {
      Token saved = current_;
      advance();
      
      if (current_.kind == TokenKind::LParen) {
        // Это может быть функция
        pos_back_ = saved;
        return parseFuncStat();
      } else {
        // Нет, это t_assign_stat или переменная в блоке
        pos_back_ = saved;
      }
    }
    
    // t_block_stat: { stats }
    if (current_.kind == TokenKind::LBrace) {
      return parseBlockStat();
    }
    
    // t_solo_stat: expr ;
    // или t_assign_stat: name = expr ;
    return parseExprStat();
  }
  
  std::shared_ptr<t_calc::t_func_stat> parseFuncStat() {
    auto func = std::make_shared<t_calc::t_func_stat>();
    
    // Имя функции
    expect(TokenKind::Ident);
    func->func = pos_back_.text;
    
    // ( args )
    expect(TokenKind::LParen);
    
    while (current_.kind != TokenKind::RParen) {
      if (!func->args.empty()) {
        expect(TokenKind::Comma);
      }
      
      t_calc::t_func_stat::arg_type arg;
      expect(TokenKind::Ident);
      arg.name = pos_back_.text;
      func->args.push_back(arg);
    }
    
    expect(TokenKind::RParen);
    expect(TokenKind::Equals);
    
    // Body
    func->body = parseStat();
    
    return func;
  }
  
  std::shared_ptr<t_calc::t_block_stat> parseBlockStat() {
    auto block = std::make_shared<t_calc::t_block_stat>();
    
    expect(TokenKind::LBrace);
    
    while (current_.kind != TokenKind::RBrace && current_.kind != TokenKind::End) {
      auto stat = parseStat();
      if (stat) {
        block->arr.push_back(stat);
      }
    }
    
    expect(TokenKind::RBrace);
    
    return block;
  }
  
  std::shared_ptr<t_calc::i_stat> parseExprStat() {
    // Попытаться спарсить как t_assign_stat
    if (current_.kind == TokenKind::Ident) {
      Token saved = current_;
      advance();
      
      if (current_.kind == TokenKind::Equals) {
        // Это присваивание
        auto assign = std::make_shared<t_calc::t_assign_stat>();
        assign->var = saved.text;
        
        expect(TokenKind::Equals);
        assign->expr = parseAddSub();
        expect(TokenKind::Semicolon);
        
        return assign;
      } else {
        // Вернуться назад и парсить как выражение
        pos_back_ = saved;
      }
    }
    
    // t_solo_stat: expr ;
    auto solo = std::make_shared<t_calc::t_solo_stat>();
    solo->expr = parseAddSub();
    expect(TokenKind::Semicolon);
    
    return solo;
  }
  
  // ========================================================================
  // EXPRESSIONS (i_term)
  // ========================================================================
  
  std::shared_ptr<t_calc::i_term> parseAddSub() {
    auto first = parseDivMul();
    
    auto addsub = std::make_shared<t_calc::t_addsub>();
    addsub->first = std::static_pointer_cast<t_calc::t_divmul>(first);
    
    while (current_.kind == TokenKind::Plus || current_.kind == TokenKind::Minus) {
      t_calc::t_addsub::elem e;
      e.oper = current_.text;
      advance();
      e.expr = parseDivMul();
      addsub->arr.push_back(e);
    }
    
    if (addsub->arr.empty()) {
      return first;
    }
    
    return addsub;
  }
  
  std::shared_ptr<t_calc::t_divmul> parseDivMul() {
    auto first = parseScope();
    
    auto divmul = std::make_shared<t_calc::t_divmul>();
    divmul->first = first;
    
    while (current_.kind == TokenKind::Star || current_.kind == TokenKind::Slash) {
      t_calc::t_divmul::elem e;
      e.oper = current_.text;
      advance();
      e.expr = parseScope();
      divmul->arr.push_back(e);
    }
    
    if (divmul->arr.empty()) {
      return divmul;
    }
    
    return divmul;
  }
  
  std::shared_ptr<t_calc::i_term> parseScope() {
    // ( expr )
    if (current_.kind == TokenKind::LParen) {
      advance();
      auto expr = parseAddSub();
      expect(TokenKind::RParen);
      
      auto scope = std::make_shared<t_calc::t_scope>();
      scope->value = expr;
      return scope;
    }
    
    return parsePrimary();
  }
  
  std::shared_ptr<t_calc::i_term> parsePrimary() {
    // Число
    if (current_.kind == TokenKind::Number) {
      auto num = std::make_shared<t_calc::t_number>();
      num->value = current_.text;
      advance();
      return num;
    }
    
    // Переменная или вызов функции
    if (current_.kind == TokenKind::Ident) {
      auto call = std::make_shared<t_calc::t_varcall>();
      call->name = current_.text;
      advance();
      
      // Проверить параметры
      if (current_.kind == TokenKind::LParen) {
        auto params = std::make_shared<t_calc::t_call_params>();
        
        advance(); // skip (
        
        while (current_.kind != TokenKind::RParen) {
          if (!params->arr.empty()) {
            expect(TokenKind::Comma);
          }
          
          t_calc::t_call_param param;
          param.body = parseAddSub();
          params->arr.push_back(param);
        }
        
        expect(TokenKind::RParen);
        call->params = params;
      }
      
      return call;
    }
    
    throw std::runtime_error("Unexpected token in expression");
  }
  
  Token pos_back_;
};

} // namespace parser
