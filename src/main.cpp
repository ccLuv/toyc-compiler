#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace toyc {

struct Error : std::runtime_error {
  using std::runtime_error::runtime_error;
};

enum class TokenKind {
  End, Id, Number,
  KwConst, KwInt, KwVoid, KwIf, KwElse, KwWhile, KwBreak, KwContinue, KwReturn,
  LParen, RParen, LBrace, RBrace, Comma, Semi,
  Assign, Plus, Minus, Star, Slash, Percent, Bang,
  Less, Greater, LessEq, GreaterEq, EqEq, NotEq, AndAnd, OrOr
};

struct Token {
  TokenKind kind;
  std::string text;
  int64_t number = 0;
  int line = 1;
  int column = 1;
};

class Lexer {
 public:
  explicit Lexer(std::string source) : source_(std::move(source)) {}

  std::vector<Token> scan() {
    std::vector<Token> result;
    for (;;) {
      skipIgnored();
      const int line = line_, column = column_;
      if (eof()) {
        result.push_back({TokenKind::End, "", 0, line, column});
        return result;
      }
      const char c = peek();
      if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        result.push_back(scanIdentifier());
      } else if (std::isdigit(static_cast<unsigned char>(c))) {
        result.push_back(scanNumber());
      } else {
        result.push_back(scanPunctuation());
      }
    }
  }

 private:
  bool eof(size_t lookahead = 0) const { return pos_ + lookahead >= source_.size(); }
  char peek(size_t lookahead = 0) const { return eof(lookahead) ? '\0' : source_[pos_ + lookahead]; }
  char take() {
    const char c = source_[pos_++];
    if (c == '\n') {
      ++line_;
      column_ = 1;
    } else {
      ++column_;
    }
    return c;
  }

  [[noreturn]] void fail(const std::string& message) const {
    throw Error("line " + std::to_string(line_) + ":" + std::to_string(column_) + ": " + message);
  }

  void skipIgnored() {
    for (;;) {
      while (!eof() && std::isspace(static_cast<unsigned char>(peek()))) take();
      if (peek() == '/' && peek(1) == '/') {
        while (!eof() && peek() != '\n') take();
        continue;
      }
      if (peek() == '/' && peek(1) == '*') {
        take();
        take();
        while (!(peek() == '*' && peek(1) == '/')) {
          if (eof()) fail("unterminated block comment");
          take();
        }
        take();
        take();
        continue;
      }
      return;
    }
  }

  Token scanIdentifier() {
    const int line = line_, column = column_;
    std::string text;
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') text += take();
    static const std::unordered_map<std::string, TokenKind> keywords = {
        {"const", TokenKind::KwConst},       {"int", TokenKind::KwInt},
        {"void", TokenKind::KwVoid},         {"if", TokenKind::KwIf},
        {"else", TokenKind::KwElse},         {"while", TokenKind::KwWhile},
        {"break", TokenKind::KwBreak},       {"continue", TokenKind::KwContinue},
        {"return", TokenKind::KwReturn},
    };
    const auto it = keywords.find(text);
    return {it == keywords.end() ? TokenKind::Id : it->second, text, 0, line, column};
  }

  Token scanNumber() {
    const int line = line_, column = column_;
    std::string text;
    while (std::isdigit(static_cast<unsigned char>(peek()))) text += take();
    int64_t value = 0;
    try {
      value = std::stoll(text);
    } catch (...) {
      fail("integer literal is out of range");
    }
    if (value > 2147483648LL) fail("integer literal is out of 32-bit range");
    return {TokenKind::Number, text, value, line, column};
  }

  Token scanPunctuation() {
    const int line = line_, column = column_;
    const std::string two{peek(), peek(1)};
    static const std::unordered_map<std::string, TokenKind> pairs = {
        {"<=", TokenKind::LessEq}, {">=", TokenKind::GreaterEq}, {"==", TokenKind::EqEq},
        {"!=", TokenKind::NotEq},  {"&&", TokenKind::AndAnd},    {"||", TokenKind::OrOr},
    };
    if (const auto it = pairs.find(two); it != pairs.end()) {
      take();
      take();
      return {it->second, two, 0, line, column};
    }
    const char c = take();
    TokenKind kind;
    switch (c) {
      case '(': kind = TokenKind::LParen; break;
      case ')': kind = TokenKind::RParen; break;
      case '{': kind = TokenKind::LBrace; break;
      case '}': kind = TokenKind::RBrace; break;
      case ',': kind = TokenKind::Comma; break;
      case ';': kind = TokenKind::Semi; break;
      case '=': kind = TokenKind::Assign; break;
      case '+': kind = TokenKind::Plus; break;
      case '-': kind = TokenKind::Minus; break;
      case '*': kind = TokenKind::Star; break;
      case '/': kind = TokenKind::Slash; break;
      case '%': kind = TokenKind::Percent; break;
      case '!': kind = TokenKind::Bang; break;
      case '<': kind = TokenKind::Less; break;
      case '>': kind = TokenKind::Greater; break;
      default: fail(std::string("unexpected character '") + c + "'");
    }
    return {kind, std::string(1, c), 0, line, column};
  }

  std::string source_;
  size_t pos_ = 0;
  int line_ = 1;
  int column_ = 1;
};

enum class Type { Int, Void };
enum class UnaryOp { Plus, Minus, Not };
enum class BinaryOp { Add, Sub, Mul, Div, Mod, Lt, Gt, Le, Ge, Eq, Ne, And, Or };

struct Expr {
  struct Number { int32_t value; };
  struct Name { std::string value; };
  struct Unary { UnaryOp op; std::unique_ptr<Expr> operand; };
  struct Binary { BinaryOp op; std::unique_ptr<Expr> left, right; };
  struct Call { std::string name; std::vector<std::unique_ptr<Expr>> args; };
  using Node = std::variant<Number, Name, Unary, Binary, Call>;
  explicit Expr(Node node) : node(std::move(node)) {}
  Node node;
};

struct Decl {
  bool isConst;
  std::string name;
  std::unique_ptr<Expr> init;
};

struct Stmt {
  struct Block { std::vector<std::unique_ptr<Stmt>> items; };
  struct Empty {};
  struct ExprStmt { std::unique_ptr<Expr> expr; };
  struct Assign { std::string name; std::unique_ptr<Expr> value; };
  struct DeclStmt { Decl decl; };
  struct If {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> thenBranch;
    std::unique_ptr<Stmt> elseBranch;
  };
  struct While { std::unique_ptr<Expr> condition; std::unique_ptr<Stmt> body; };
  struct Break {};
  struct Continue {};
  struct Return { std::unique_ptr<Expr> value; };
  using Node = std::variant<Block, Empty, ExprStmt, Assign, DeclStmt, If, While, Break, Continue, Return>;
  explicit Stmt(Node node) : node(std::move(node)) {}
  Node node;
};

struct Function {
  Type returnType;
  std::string name;
  std::vector<std::string> params;
  std::unique_ptr<Stmt> body;
};

struct Program {
  using Item = std::variant<Decl, Function>;
  std::vector<Item> items;
};

class Parser {
 public:
  explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

  Program parseProgram() {
    Program program;
    while (!at(TokenKind::End)) {
      bool isConst = match(TokenKind::KwConst);
      Type type;
      if (match(TokenKind::KwInt)) type = Type::Int;
      else if (match(TokenKind::KwVoid)) type = Type::Void;
      else fail("expected a declaration or function definition");
      const std::string name = expect(TokenKind::Id, "identifier").text;
      if (at(TokenKind::LParen)) {
        if (isConst) fail("function cannot be const");
        program.items.emplace_back(parseFunction(type, name));
      } else {
        if (type == Type::Void) fail("variable cannot have void type");
        program.items.emplace_back(parseDeclTail(isConst, name));
      }
    }
    if (program.items.empty()) fail("translation unit must not be empty");
    return program;
  }

 private:
  const Token& current(size_t n = 0) const { return tokens_[std::min(pos_ + n, tokens_.size() - 1)]; }
  bool at(TokenKind kind) const { return current().kind == kind; }
  bool match(TokenKind kind) {
    if (!at(kind)) return false;
    ++pos_;
    return true;
  }
  const Token& expect(TokenKind kind, const std::string& what) {
    if (!at(kind)) fail("expected " + what + ", got '" + current().text + "'");
    return tokens_[pos_++];
  }
  [[noreturn]] void fail(const std::string& message) const {
    throw Error("line " + std::to_string(current().line) + ":" +
                std::to_string(current().column) + ": " + message);
  }

  Decl parseDeclTail(bool isConst, std::string name) {
    expect(TokenKind::Assign, "'='");
    auto init = parseExpr();
    expect(TokenKind::Semi, "';'");
    return {isConst, std::move(name), std::move(init)};
  }

  Decl parseDecl() {
    const bool isConst = match(TokenKind::KwConst);
    expect(TokenKind::KwInt, "'int'");
    std::string name = expect(TokenKind::Id, "identifier").text;
    return parseDeclTail(isConst, std::move(name));
  }

  Function parseFunction(Type type, std::string name) {
    expect(TokenKind::LParen, "'('");
    std::vector<std::string> params;
    if (!at(TokenKind::RParen)) {
      do {
        expect(TokenKind::KwInt, "'int'");
        params.push_back(expect(TokenKind::Id, "parameter name").text);
      } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RParen, "')'");
    return {type, std::move(name), std::move(params), parseBlock()};
  }

  std::unique_ptr<Stmt> parseBlock() {
    expect(TokenKind::LBrace, "'{'");
    Stmt::Block block;
    while (!at(TokenKind::RBrace)) {
      if (at(TokenKind::End)) fail("unterminated block");
      block.items.push_back(parseStmt());
    }
    expect(TokenKind::RBrace, "'}'");
    return std::make_unique<Stmt>(std::move(block));
  }

  std::unique_ptr<Stmt> parseStmt() {
    if (at(TokenKind::LBrace)) return parseBlock();
    if (match(TokenKind::Semi)) return std::make_unique<Stmt>(Stmt::Empty{});
    if (at(TokenKind::KwConst) || at(TokenKind::KwInt))
      return std::make_unique<Stmt>(Stmt::DeclStmt{parseDecl()});
    if (match(TokenKind::KwIf)) {
      expect(TokenKind::LParen, "'('");
      auto condition = parseExpr();
      expect(TokenKind::RParen, "')'");
      auto thenBranch = parseStmt();
      std::unique_ptr<Stmt> elseBranch;
      if (match(TokenKind::KwElse)) elseBranch = parseStmt();
      return std::make_unique<Stmt>(Stmt::If{
          std::move(condition), std::move(thenBranch), std::move(elseBranch)});
    }
    if (match(TokenKind::KwWhile)) {
      expect(TokenKind::LParen, "'('");
      auto condition = parseExpr();
      expect(TokenKind::RParen, "')'");
      return std::make_unique<Stmt>(
          Stmt::While{std::move(condition), parseStmt()});
    }
    if (match(TokenKind::KwBreak)) {
      expect(TokenKind::Semi, "';'");
      return std::make_unique<Stmt>(Stmt::Break{});
    }
    if (match(TokenKind::KwContinue)) {
      expect(TokenKind::Semi, "';'");
      return std::make_unique<Stmt>(Stmt::Continue{});
    }
    if (match(TokenKind::KwReturn)) {
      std::unique_ptr<Expr> value;
      if (!at(TokenKind::Semi)) value = parseExpr();
      expect(TokenKind::Semi, "';'");
      return std::make_unique<Stmt>(Stmt::Return{std::move(value)});
    }
    if (at(TokenKind::Id) && current(1).kind == TokenKind::Assign) {
      std::string name = current().text;
      pos_ += 2;
      auto value = parseExpr();
      expect(TokenKind::Semi, "';'");
      return std::make_unique<Stmt>(Stmt::Assign{std::move(name), std::move(value)});
    }
    auto expr = parseExpr();
    expect(TokenKind::Semi, "';'");
    return std::make_unique<Stmt>(Stmt::ExprStmt{std::move(expr)});
  }

  std::unique_ptr<Expr> parseExpr() { return parseOr(); }
  std::unique_ptr<Expr> binary(std::unique_ptr<Expr> left, BinaryOp op,
                               std::unique_ptr<Expr> right) {
    return std::make_unique<Expr>(Expr::Binary{op, std::move(left), std::move(right)});
  }
  std::unique_ptr<Expr> parseOr() {
    auto e = parseAnd();
    while (match(TokenKind::OrOr)) e = binary(std::move(e), BinaryOp::Or, parseAnd());
    return e;
  }
  std::unique_ptr<Expr> parseAnd() {
    auto e = parseRel();
    while (match(TokenKind::AndAnd)) e = binary(std::move(e), BinaryOp::And, parseRel());
    return e;
  }
  std::unique_ptr<Expr> parseRel() {
    auto e = parseAdd();
    for (;;) {
      std::optional<BinaryOp> op;
      if (match(TokenKind::Less)) op = BinaryOp::Lt;
      else if (match(TokenKind::Greater)) op = BinaryOp::Gt;
      else if (match(TokenKind::LessEq)) op = BinaryOp::Le;
      else if (match(TokenKind::GreaterEq)) op = BinaryOp::Ge;
      else if (match(TokenKind::EqEq)) op = BinaryOp::Eq;
      else if (match(TokenKind::NotEq)) op = BinaryOp::Ne;
      else break;
      e = binary(std::move(e), *op, parseAdd());
    }
    return e;
  }
  std::unique_ptr<Expr> parseAdd() {
    auto e = parseMul();
    for (;;) {
      if (match(TokenKind::Plus)) e = binary(std::move(e), BinaryOp::Add, parseMul());
      else if (match(TokenKind::Minus)) e = binary(std::move(e), BinaryOp::Sub, parseMul());
      else break;
    }
    return e;
  }
  std::unique_ptr<Expr> parseMul() {
    auto e = parseUnary();
    for (;;) {
      if (match(TokenKind::Star)) e = binary(std::move(e), BinaryOp::Mul, parseUnary());
      else if (match(TokenKind::Slash)) e = binary(std::move(e), BinaryOp::Div, parseUnary());
      else if (match(TokenKind::Percent)) e = binary(std::move(e), BinaryOp::Mod, parseUnary());
      else break;
    }
    return e;
  }
  std::unique_ptr<Expr> parseUnary() {
    if (match(TokenKind::Plus))
      return std::make_unique<Expr>(Expr::Unary{UnaryOp::Plus, parseUnary()});
    if (match(TokenKind::Minus))
      return std::make_unique<Expr>(Expr::Unary{UnaryOp::Minus, parseUnary()});
    if (match(TokenKind::Bang))
      return std::make_unique<Expr>(Expr::Unary{UnaryOp::Not, parseUnary()});
    return parsePrimary();
  }
  std::unique_ptr<Expr> parsePrimary() {
    if (match(TokenKind::LParen)) {
      auto e = parseExpr();
      expect(TokenKind::RParen, "')'");
      return e;
    }
    if (at(TokenKind::Number)) {
      const int64_t value = current().number;
      ++pos_;
      return std::make_unique<Expr>(Expr::Number{static_cast<int32_t>(value)});
    }
    std::string name = expect(TokenKind::Id, "expression").text;
    if (!match(TokenKind::LParen))
      return std::make_unique<Expr>(Expr::Name{std::move(name)});
    Expr::Call call{std::move(name), {}};
    if (!at(TokenKind::RParen)) {
      do call.args.push_back(parseExpr()); while (match(TokenKind::Comma));
    }
    expect(TokenKind::RParen, "')'");
    return std::make_unique<Expr>(std::move(call));
  }

  std::vector<Token> tokens_;
  size_t pos_ = 0;
};

enum class IROp {
  Imm, LoadLocal, StoreLocal, LoadGlobal, StoreGlobal, Unary, Binary,
  Label, Jump, BranchZero, Call, Return
};

struct IRInst {
  IROp op;
  int dst = -1;
  int left = -1;
  int right = -1;
  int32_t imm = 0;
  UnaryOp unary = UnaryOp::Plus;
  BinaryOp binary = BinaryOp::Add;
  std::string name;
  std::vector<int> args;

  explicit IRInst(IROp operation) : op(operation) {}
};

struct IRFunction {
  Type returnType = Type::Void;
  std::string name;
  std::vector<std::string> params;
  int localCount = 0;
  int registerCount = 0;
  int maxCallArgs = 0;
  std::vector<IRInst> code;

  IRFunction() = default;
  IRFunction(Type type, std::string functionName, std::vector<std::string> parameters)
      : returnType(type), name(std::move(functionName)), params(std::move(parameters)) {}
};

struct IRGlobal {
  std::string name;
  int32_t initialValue;
};

struct IRProgram {
  std::vector<IRGlobal> globals;
  std::vector<IRFunction> functions;
};

class Lowerer {
 public:
  explicit Lowerer(bool optimize) : optimize_(optimize) {}

  IRProgram lower(const Program& program) {
    // The flag is part of the public driver contract. Optimization passes can
    // be added without changing the frontend/backend boundary.
    (void)optimize_;
    for (const auto& item : program.items) {
      if (const auto* decl = std::get_if<Decl>(&item)) {
        lowerGlobal(*decl);
      } else {
        const auto& function = std::get<Function>(item);
        if (globalNames_.contains(function.name))
          fail("duplicate global name '" + function.name + "'");
        FunctionSig sig{function.returnType, function.params.size()};
        functions_.emplace(function.name, sig);
        functionBodies_[function.name] = &function;
        globalNames_[function.name] = true;
        lowerFunction(function);
      }
    }
    const auto main = functions_.find("main");
    if (main == functions_.end() || main->second.type != Type::Int || main->second.arity != 0)
      fail("program must define int main() with no parameters");
    if (optimize_) {
      propagateReadOnlyGlobals();
      optimizeFinishedFunctions();
    }
    return std::move(output_);
  }

 private:
  struct FunctionSig { Type type; size_t arity; };
  enum class SymbolKind { Local, Global, Constant };
  struct Symbol {
    SymbolKind kind;
    int slot = -1;
    int32_t value = 0;
    bool isConst = false;
  };
  struct Value { int reg; Type type; };

  [[noreturn]] static void fail(const std::string& message) { throw Error("semantic error: " + message); }

  static int32_t wrap(int64_t value) {
    return static_cast<int32_t>(static_cast<uint32_t>(value));
  }

  int32_t evalConst(const Expr& expr) {
    return std::visit([&](const auto& node) -> int32_t {
      using T = std::decay_t<decltype(node)>;
      if constexpr (std::is_same_v<T, Expr::Number>) {
        return node.value;
      } else if constexpr (std::is_same_v<T, Expr::Name>) {
        const Symbol* symbol = lookup(node.value);
        if (!symbol || symbol->kind != SymbolKind::Constant)
          fail("constant expression refers to non-constant '" + node.value + "'");
        return symbol->value;
      } else if constexpr (std::is_same_v<T, Expr::Unary>) {
        const int32_t value = evalConst(*node.operand);
        if (node.op == UnaryOp::Plus) return value;
        if (node.op == UnaryOp::Minus) return wrap(-static_cast<int64_t>(value));
        return value == 0;
      } else if constexpr (std::is_same_v<T, Expr::Binary>) {
        const int32_t left = evalConst(*node.left);
        if (node.op == BinaryOp::And && left == 0) return 0;
        if (node.op == BinaryOp::Or && left != 0) return 1;
        const int32_t right = evalConst(*node.right);
        switch (node.op) {
          case BinaryOp::Add: return wrap(static_cast<int64_t>(left) + right);
          case BinaryOp::Sub: return wrap(static_cast<int64_t>(left) - right);
          case BinaryOp::Mul: return wrap(static_cast<int64_t>(left) * right);
          case BinaryOp::Div:
            if (right == 0) fail("division by zero in constant expression");
            if (left == std::numeric_limits<int32_t>::min() && right == -1) return left;
            return left / right;
          case BinaryOp::Mod:
            if (right == 0) fail("remainder by zero in constant expression");
            if (left == std::numeric_limits<int32_t>::min() && right == -1) return 0;
            return left % right;
          case BinaryOp::Lt: return left < right;
          case BinaryOp::Gt: return left > right;
          case BinaryOp::Le: return left <= right;
          case BinaryOp::Ge: return left >= right;
          case BinaryOp::Eq: return left == right;
          case BinaryOp::Ne: return left != right;
          case BinaryOp::And: return right != 0;
          case BinaryOp::Or: return right != 0;
        }
      } else {
        fail("function call is not allowed in a constant expression");
      }
      return 0;
    }, expr.node);
  }

  void lowerGlobal(const Decl& decl) {
    if (globalNames_.contains(decl.name)) fail("duplicate global name '" + decl.name + "'");
    const int32_t value = evalConst(*decl.init);
    globalNames_[decl.name] = true;
    Symbol symbol;
    if (decl.isConst) {
      symbol.kind = SymbolKind::Constant;
      symbol.value = value;
      symbol.isConst = true;
    } else {
      symbol.kind = SymbolKind::Global;
      output_.globals.push_back({decl.name, value});
    }
    globals_[decl.name] = symbol;
  }

  void lowerFunction(const Function& function) {
    current_ = IRFunction{function.returnType, function.name, function.params};
    knownLocalValues_.clear();
    scopes_.clear();
    breakLabels_.clear();
    continueLabels_.clear();
    pushScope();
    for (size_t i = 0; i < function.params.size(); ++i) {
      declare(function.params[i], Symbol{SymbolKind::Local, newLocal(), 0, false});
    }
    lowerStmt(*function.body, false);
    if (function.returnType == Type::Void &&
        (current_.code.empty() || current_.code.back().op != IROp::Return)) {
      emit(IRInst{IROp::Return});
    }
    popScope();
    if (optimize_) {
      optimizeTailRecursion();
      optimizeCurrentFunction();
      hoistAndDeduplicateConstants();
      eliminateCopiesAndCommonExpressions();
      eliminateDeadCode();
      rotateLoops();
    }
    current_.registerCount = nextReg_;
    output_.functions.push_back(std::move(current_));
    nextReg_ = 0;
  }

  void optimizeFinishedFunctions() {
    for (auto& function : output_.functions) {
      current_ = std::move(function);
      nextReg_ = current_.registerCount;
      optimizeCurrentFunction();
      hoistAndDeduplicateConstants();
      eliminateCopiesAndCommonExpressions();
      eliminateDeadCode();
      rotateLoops();
      current_.registerCount = nextReg_;
      function = std::move(current_);
      nextReg_ = 0;
    }
  }

  void pushScope() { scopes_.emplace_back(); }
  void popScope() { scopes_.pop_back(); }
  void declare(const std::string& name, Symbol symbol) {
    if (scopes_.back().contains(name)) fail("duplicate declaration of '" + name + "'");
    scopes_.back()[name] = symbol;
  }
  const Symbol* lookup(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
      if (const auto found = it->find(name); found != it->end()) return &found->second;
    }
    if (const auto found = globals_.find(name); found != globals_.end()) return &found->second;
    return nullptr;
  }
  int newLocal() {
    knownLocalValues_.push_back(std::nullopt);
    return current_.localCount++;
  }
  int newReg() { return nextReg_++; }
  std::string newLabel(const std::string& hint) {
    return ".L" + current_.name + "_" + hint + "_" + std::to_string(nextLabel_++);
  }
  void emit(IRInst inst) { current_.code.push_back(std::move(inst)); }

  static int32_t foldUnary(UnaryOp op, int32_t value) {
    if (op == UnaryOp::Plus) return value;
    if (op == UnaryOp::Minus) return wrap(-static_cast<int64_t>(value));
    return value == 0;
  }

  static int32_t foldBinary(BinaryOp op, int32_t left, int32_t right) {
    switch (op) {
      case BinaryOp::Add: return wrap(static_cast<int64_t>(left) + right);
      case BinaryOp::Sub: return wrap(static_cast<int64_t>(left) - right);
      case BinaryOp::Mul: return wrap(static_cast<int64_t>(left) * right);
      case BinaryOp::Div:
        if (left == std::numeric_limits<int32_t>::min() && right == -1) return left;
        return left / right;
      case BinaryOp::Mod:
        if (left == std::numeric_limits<int32_t>::min() && right == -1) return 0;
        return left % right;
      case BinaryOp::Lt: return left < right;
      case BinaryOp::Gt: return left > right;
      case BinaryOp::Le: return left <= right;
      case BinaryOp::Ge: return left >= right;
      case BinaryOp::Eq: return left == right;
      case BinaryOp::Ne: return left != right;
      case BinaryOp::And: return left != 0 && right != 0;
      case BinaryOp::Or: return left != 0 || right != 0;
    }
    return 0;
  }

  void optimizeCurrentFunction() {
    std::unordered_map<int, int32_t> constants;
    std::vector<std::optional<int32_t>> localConstants(current_.localCount);
    for (auto& inst : current_.code) {
      if (inst.op == IROp::Imm) {
        constants[inst.dst] = inst.imm;
      } else if (inst.op == IROp::Unary) {
        const auto value = constants.find(inst.left);
        if (value != constants.end()) {
          const int dst = inst.dst;
          const int32_t folded = foldUnary(inst.unary, value->second);
          inst = IRInst{IROp::Imm};
          inst.dst = dst;
          inst.imm = folded;
          constants[dst] = folded;
        } else {
          constants.erase(inst.dst);
        }
      } else if (inst.op == IROp::Binary) {
        const auto left = constants.find(inst.left);
        const auto right = constants.find(inst.right);
        const bool safeDivisor =
            (inst.binary != BinaryOp::Div && inst.binary != BinaryOp::Mod) ||
            (right != constants.end() && right->second != 0);
        if (left != constants.end() && right != constants.end() && safeDivisor) {
          const int dst = inst.dst;
          const int32_t folded = foldBinary(inst.binary, left->second, right->second);
          inst = IRInst{IROp::Imm};
          inst.dst = dst;
          inst.imm = folded;
          constants[dst] = folded;
        } else {
          const int dst = inst.dst;
          std::optional<int> copy;
          std::optional<int32_t> replacement;
          if (inst.left == inst.right) {
            if (inst.binary == BinaryOp::Sub || inst.binary == BinaryOp::Ne ||
                inst.binary == BinaryOp::Lt || inst.binary == BinaryOp::Gt)
              replacement = 0;
            if (inst.binary == BinaryOp::Eq || inst.binary == BinaryOp::Le ||
                inst.binary == BinaryOp::Ge)
              replacement = 1;
          }
          if (right != constants.end()) {
            if ((inst.binary == BinaryOp::Add || inst.binary == BinaryOp::Sub) &&
                right->second == 0)
              copy = inst.left;
            if ((inst.binary == BinaryOp::Mul || inst.binary == BinaryOp::Div) &&
                right->second == 1)
              copy = inst.left;
            if (inst.binary == BinaryOp::Mul && right->second == 0)
              replacement = 0;
            if (inst.binary == BinaryOp::Mod && right->second == 1)
              replacement = 0;
          }
          if (left != constants.end()) {
            if (inst.binary == BinaryOp::Add && left->second == 0)
              copy = inst.right;
            if (inst.binary == BinaryOp::Mul && left->second == 1)
              copy = inst.right;
            if (inst.binary == BinaryOp::Mul && left->second == 0)
              replacement = 0;
          }
          if (replacement) {
            inst = IRInst{IROp::Imm};
            inst.dst = dst;
            inst.imm = *replacement;
            constants[dst] = *replacement;
          } else if (copy) {
            inst = IRInst{IROp::Unary};
            inst.dst = dst;
            inst.left = *copy;
            inst.unary = UnaryOp::Plus;
            constants.erase(dst);
          } else {
            constants.erase(dst);
          }
        }
      } else if (inst.op == IROp::LoadLocal) {
        if (inst.left >= 0 && inst.left < static_cast<int>(localConstants.size()) &&
            localConstants[inst.left]) {
          const int dst = inst.dst;
          const int32_t value = *localConstants[inst.left];
          inst = IRInst{IROp::Imm};
          inst.dst = dst;
          inst.imm = value;
          constants[dst] = value;
        } else if (inst.dst >= 0) {
          constants.erase(inst.dst);
        }
      } else if (inst.op == IROp::StoreLocal) {
        const auto value = constants.find(inst.right);
        if (inst.left >= 0 && inst.left < static_cast<int>(localConstants.size())) {
          if (value != constants.end()) localConstants[inst.left] = value->second;
          else localConstants[inst.left].reset();
        }
      } else if (inst.op == IROp::LoadGlobal || inst.op == IROp::Call) {
        if (inst.dst >= 0) constants.erase(inst.dst);
        if (inst.op == IROp::Call)
          std::fill(localConstants.begin(), localConstants.end(), std::nullopt);
      } else if (inst.op == IROp::Label || inst.op == IROp::Jump ||
                 inst.op == IROp::BranchZero) {
        constants.clear();
        std::fill(localConstants.begin(), localConstants.end(), std::nullopt);
      }
    }
  }

  void optimizeTailRecursion() {
    std::vector<IRInst> rewritten;
    bool changed = false;
    const std::string entryLabel = newLabel("tail_entry");
    for (size_t i = 0; i < current_.code.size(); ++i) {
      IRInst& inst = current_.code[i];
      const bool tailCall =
          inst.op == IROp::Call && inst.name == current_.name &&
          i + 1 < current_.code.size() &&
          current_.code[i + 1].op == IROp::Return &&
          current_.code[i + 1].left == inst.dst &&
          inst.args.size() == current_.params.size();
      if (!tailCall) {
        rewritten.push_back(std::move(inst));
        continue;
      }

      std::vector<int> snapshots;
      for (int arg : inst.args) {
        const int copy = newReg();
        IRInst snapshot{IROp::Unary};
        snapshot.dst = copy;
        snapshot.left = arg;
        snapshot.unary = UnaryOp::Plus;
        snapshot.name = "snapshot";
        rewritten.push_back(std::move(snapshot));
        snapshots.push_back(copy);
      }
      for (size_t param = 0; param < snapshots.size(); ++param) {
        IRInst store{IROp::StoreLocal};
        store.left = static_cast<int>(param);
        store.right = snapshots[param];
        rewritten.push_back(std::move(store));
      }
      IRInst jump{IROp::Jump};
      jump.name = entryLabel;
      rewritten.push_back(std::move(jump));
      ++i;
      changed = true;
    }
    if (changed) {
      IRInst entry{IROp::Label};
      entry.name = entryLabel;
      rewritten.insert(rewritten.begin(), std::move(entry));
    }
    current_.code = std::move(rewritten);
  }

  void hoistAndDeduplicateConstants() {
    std::vector<int> definitions(nextReg_, 0);
    for (const auto& inst : current_.code) {
      if (inst.dst >= 0) ++definitions[inst.dst];
    }
    std::unordered_map<int32_t, int> canonical;
    std::vector<int> aliases(nextReg_, -1);
    std::vector<IRInst> hoisted;
    std::vector<IRInst> body;
    for (auto& inst : current_.code) {
      if (inst.op == IROp::Imm && definitions[inst.dst] == 1) {
        const auto found = canonical.find(inst.imm);
        if (found == canonical.end()) {
          canonical[inst.imm] = inst.dst;
          hoisted.push_back(std::move(inst));
        } else {
          aliases[inst.dst] = found->second;
        }
      } else {
        body.push_back(std::move(inst));
      }
    }
    auto resolve = [&](int reg) {
      while (reg >= 0 && aliases[reg] >= 0) reg = aliases[reg];
      return reg;
    };
    for (auto& inst : body) {
      switch (inst.op) {
        case IROp::StoreLocal:
        case IROp::StoreGlobal:
          inst.right = resolve(inst.right);
          break;
        case IROp::Unary:
        case IROp::BranchZero:
        case IROp::Return:
          inst.left = resolve(inst.left);
          break;
        case IROp::Binary:
          inst.left = resolve(inst.left);
          inst.right = resolve(inst.right);
          break;
        case IROp::Call:
          for (int& arg : inst.args) arg = resolve(arg);
          break;
        default:
          break;
      }
    }
    hoisted.insert(hoisted.end(),
                   std::make_move_iterator(body.begin()),
                   std::make_move_iterator(body.end()));
    current_.code = std::move(hoisted);
  }

  static bool hasPureResult(const IRInst& inst) {
    return inst.op == IROp::Imm || inst.op == IROp::LoadLocal ||
           inst.op == IROp::LoadGlobal || inst.op == IROp::Unary ||
           inst.op == IROp::Binary;
  }

  void eliminateCopiesAndCommonExpressions() {
    std::vector<int> aliases(nextReg_, -1);
    std::vector<int> localValues(current_.localCount, -1);
    std::unordered_map<std::string, int> expressions;
    std::vector<bool> removed(current_.code.size(), false);

    auto resolve = [&](int reg) {
      int current = reg;
      while (current >= 0 && aliases[current] >= 0) current = aliases[current];
      return current;
    };
    auto rewriteUses = [&](IRInst& inst) {
      switch (inst.op) {
        case IROp::StoreLocal:
        case IROp::StoreGlobal:
          inst.right = resolve(inst.right);
          break;
        case IROp::Unary:
        case IROp::BranchZero:
        case IROp::Return:
          inst.left = resolve(inst.left);
          break;
        case IROp::Binary:
          inst.left = resolve(inst.left);
          inst.right = resolve(inst.right);
          break;
        case IROp::Call:
          for (int& arg : inst.args) arg = resolve(arg);
          break;
        default:
          break;
      }
    };

    for (size_t i = 0; i < current_.code.size(); ++i) {
      IRInst& inst = current_.code[i];
      rewriteUses(inst);
      if (inst.op == IROp::Label || inst.op == IROp::Jump ||
          inst.op == IROp::BranchZero || inst.op == IROp::Call) {
        std::fill(localValues.begin(), localValues.end(), -1);
        expressions.clear();
      }
      if (inst.op == IROp::LoadLocal) {
        if (localValues[inst.left] >= 0) {
          aliases[inst.dst] = resolve(localValues[inst.left]);
          removed[i] = true;
        } else {
          localValues[inst.left] = inst.dst;
        }
      } else if (inst.op == IROp::StoreLocal) {
        localValues[inst.left] = resolve(inst.right);
        expressions.clear();
      } else if (inst.op == IROp::Unary && inst.unary == UnaryOp::Plus &&
                 inst.name.empty()) {
        aliases[inst.dst] = resolve(inst.left);
        removed[i] = true;
      } else if (inst.op == IROp::Binary) {
        int left = resolve(inst.left);
        int right = resolve(inst.right);
        const bool commutative =
            inst.binary == BinaryOp::Add || inst.binary == BinaryOp::Mul ||
            inst.binary == BinaryOp::Eq || inst.binary == BinaryOp::Ne;
        if (commutative && left > right) std::swap(left, right);
        const std::string key = std::to_string(static_cast<int>(inst.binary)) +
                                ":" + std::to_string(left) +
                                ":" + std::to_string(right);
        if (const auto found = expressions.find(key); found != expressions.end()) {
          aliases[inst.dst] = resolve(found->second);
          removed[i] = true;
        } else {
          expressions[key] = inst.dst;
        }
      }
    }

    std::vector<IRInst> kept;
    kept.reserve(current_.code.size());
    for (size_t i = 0; i < current_.code.size(); ++i) {
      if (removed[i]) continue;
      rewriteUses(current_.code[i]);
      kept.push_back(std::move(current_.code[i]));
    }
    current_.code = std::move(kept);
  }

  void eliminateDeadCode() {
    bool changed;
    do {
      changed = false;
      std::vector<bool> used(nextReg_, false);
      std::vector<bool> deadStores(current_.code.size(), false);
      std::vector<bool> essentialRegs(nextReg_, false);
      std::vector<bool> essentialLocals(current_.localCount, false);
      auto markEssentialReg = [&](int reg) {
        if (reg < 0 || essentialRegs[reg]) return false;
        essentialRegs[reg] = true;
        return true;
      };
      for (const auto& inst : current_.code) {
        if (inst.op == IROp::BranchZero || inst.op == IROp::Return)
          markEssentialReg(inst.left);
        if (inst.op == IROp::StoreGlobal) markEssentialReg(inst.right);
        if (inst.op == IROp::Call) {
          for (int arg : inst.args) markEssentialReg(arg);
        }
      }
      bool dependencyChanged;
      do {
        dependencyChanged = false;
        for (const auto& inst : current_.code) {
          if (inst.op == IROp::LoadLocal && essentialRegs[inst.dst] &&
              !essentialLocals[inst.left]) {
            essentialLocals[inst.left] = true;
            dependencyChanged = true;
          } else if (inst.op == IROp::StoreLocal &&
                     essentialLocals[inst.left]) {
            dependencyChanged |= markEssentialReg(inst.right);
          } else if (inst.op == IROp::Unary && essentialRegs[inst.dst]) {
            dependencyChanged |= markEssentialReg(inst.left);
          } else if (inst.op == IROp::Binary && essentialRegs[inst.dst]) {
            dependencyChanged |= markEssentialReg(inst.left);
            dependencyChanged |= markEssentialReg(inst.right);
          }
        }
      } while (dependencyChanged);

      const size_t count = current_.code.size();
      std::unordered_map<std::string, size_t> labels;
      for (size_t i = 0; i < count; ++i) {
        if (current_.code[i].op == IROp::Label)
          labels[current_.code[i].name] = i;
      }
      std::vector<std::vector<unsigned char>> liveIn(
          count, std::vector<unsigned char>(current_.localCount));
      std::vector<std::vector<unsigned char>> liveOut = liveIn;
      bool dataflowChanged;
      do {
        dataflowChanged = false;
        for (size_t reverse = count; reverse > 0; --reverse) {
          const size_t index = reverse - 1;
          const auto& inst = current_.code[index];
          std::vector<unsigned char> out(current_.localCount);
          auto mergeSuccessor = [&](size_t successor) {
            if (successor >= count) return;
            for (int slot = 0; slot < current_.localCount; ++slot)
              out[slot] = static_cast<unsigned char>(
                  out[slot] || liveIn[successor][slot]);
          };
          if (inst.op == IROp::Jump) {
            mergeSuccessor(labels.at(inst.name));
          } else if (inst.op == IROp::BranchZero) {
            mergeSuccessor(labels.at(inst.name));
            mergeSuccessor(index + 1);
          } else if (inst.op != IROp::Return) {
            mergeSuccessor(index + 1);
          }
          std::vector<unsigned char> in = out;
          if (inst.op == IROp::StoreLocal) in[inst.left] = false;
          if (inst.op == IROp::LoadLocal) in[inst.left] = true;
          if (out != liveOut[index] || in != liveIn[index]) {
            liveOut[index] = std::move(out);
            liveIn[index] = std::move(in);
            dataflowChanged = true;
          }
        }
      } while (dataflowChanged);
      for (size_t index = 0; index < count; ++index) {
        const auto& inst = current_.code[index];
        if (inst.op == IROp::StoreLocal &&
            (!essentialLocals[inst.left] || !liveOut[index][inst.left]))
          deadStores[index] = true;
      }

      for (size_t index = 0; index < current_.code.size(); ++index) {
        const auto& inst = current_.code[index];
        if (deadStores[index]) continue;
        auto use = [&](int reg) {
          if (reg >= 0) used[reg] = true;
        };
        switch (inst.op) {
          case IROp::StoreLocal:
          case IROp::StoreGlobal:
            use(inst.right);
            break;
          case IROp::Unary:
          case IROp::BranchZero:
          case IROp::Return:
            use(inst.left);
            break;
          case IROp::Binary:
            use(inst.left);
            use(inst.right);
            break;
          case IROp::Call:
            for (int arg : inst.args) use(arg);
            break;
          default:
            break;
        }
      }
      std::vector<IRInst> kept;
      kept.reserve(current_.code.size());
      for (size_t index = 0; index < current_.code.size(); ++index) {
        auto& inst = current_.code[index];
        const bool deadResult =
            hasPureResult(inst) && inst.dst >= 0 && !used[inst.dst];
        if (deadResult || deadStores[index]) {
          changed = true;
        } else {
          kept.push_back(std::move(inst));
        }
      }
      current_.code = std::move(kept);
    } while (changed);
  }

  void propagateReadOnlyGlobals() {
    std::unordered_map<std::string, int32_t> values;
    std::unordered_map<std::string, bool> written;
    for (const auto& global : output_.globals) {
      values[global.name] = global.initialValue;
      written[global.name] = false;
    }
    for (const auto& function : output_.functions) {
      for (const auto& inst : function.code) {
        if (inst.op == IROp::StoreGlobal) written[inst.name] = true;
      }
    }
    for (auto& function : output_.functions) {
      for (auto& inst : function.code) {
        if (inst.op == IROp::LoadGlobal && !written[inst.name]) {
          const int dst = inst.dst;
          const int32_t value = values[inst.name];
          inst = IRInst{IROp::Imm};
          inst.dst = dst;
          inst.imm = value;
        }
      }
    }
  }

  void rotateLoops() {
    for (;;) {
      std::unordered_map<std::string, size_t> labels;
      for (size_t i = 0; i < current_.code.size(); ++i) {
        if (current_.code[i].op == IROp::Label)
          labels[current_.code[i].name] = i;
      }
      size_t bestBegin = current_.code.size();
      size_t bestBranch = 0;
      size_t bestJump = 0;
      size_t bestSpan = current_.code.size() + 1;
      for (size_t jump = 0; jump + 1 < current_.code.size(); ++jump) {
        if (current_.code[jump].op != IROp::Jump ||
            current_.code[jump + 1].op != IROp::Label)
          continue;
        const auto target = labels.find(current_.code[jump].name);
        if (target == labels.end() || target->second >= jump) continue;
        const std::string& endLabel = current_.code[jump + 1].name;
        for (size_t branch = target->second + 1; branch < jump; ++branch) {
          if (current_.code[branch].op == IROp::BranchZero &&
              current_.code[branch].name == endLabel) {
            const size_t span = jump - target->second;
            if (span < bestSpan) {
              bestBegin = target->second;
              bestBranch = branch;
              bestJump = jump;
              bestSpan = span;
            }
            break;
          }
        }
      }
      if (bestBegin == current_.code.size()) return;

      const std::string bodyLabel = newLabel("rotated_body");
      std::vector<IRInst> rewritten;
      rewritten.reserve(current_.code.size() + 2);
      for (size_t i = 0; i < bestBegin; ++i)
        rewritten.push_back(std::move(current_.code[i]));
      IRInst initialJump{IROp::Jump};
      initialJump.name = current_.code[bestBegin].name;
      rewritten.push_back(std::move(initialJump));
      IRInst body{IROp::Label};
      body.name = bodyLabel;
      rewritten.push_back(std::move(body));
      for (size_t i = bestBranch + 1; i < bestJump; ++i)
        rewritten.push_back(std::move(current_.code[i]));
      rewritten.push_back(std::move(current_.code[bestBegin]));
      for (size_t i = bestBegin + 1; i < bestBranch; ++i)
        rewritten.push_back(std::move(current_.code[i]));
      IRInst branch = std::move(current_.code[bestBranch]);
      branch.name = bodyLabel;
      branch.imm = 1;
      rewritten.push_back(std::move(branch));
      rewritten.push_back(std::move(current_.code[bestJump + 1]));
      for (size_t i = bestJump + 2; i < current_.code.size(); ++i)
        rewritten.push_back(std::move(current_.code[i]));
      current_.code = std::move(rewritten);
    }
  }

  int emitImm(int32_t value) {
    const int result = newReg();
    IRInst inst{IROp::Imm};
    inst.dst = result;
    inst.imm = value;
    emit(std::move(inst));
    return result;
  }

  Value lowerExpr(const Expr& expr) {
    return std::visit([&](const auto& node) -> Value {
      using T = std::decay_t<decltype(node)>;
      if constexpr (std::is_same_v<T, Expr::Number>) {
        return {emitImm(node.value), Type::Int};
      } else if constexpr (std::is_same_v<T, Expr::Name>) {
        const Symbol* symbol = lookup(node.value);
        if (!symbol) fail("use of undeclared identifier '" + node.value + "'");
        if (symbol->kind == SymbolKind::Constant) return {emitImm(symbol->value), Type::Int};
        const int result = newReg();
        IRInst inst{symbol->kind == SymbolKind::Local ? IROp::LoadLocal : IROp::LoadGlobal};
        inst.dst = result;
        inst.left = symbol->slot;
        inst.name = node.value;
        emit(std::move(inst));
        return {result, Type::Int};
      } else if constexpr (std::is_same_v<T, Expr::Unary>) {
        Value operand = lowerExpr(*node.operand);
        requireInt(operand, "unary operator");
        if (node.op == UnaryOp::Plus) return operand;
        const int result = newReg();
        IRInst inst{IROp::Unary};
        inst.dst = result;
        inst.left = operand.reg;
        inst.unary = node.op;
        emit(std::move(inst));
        return {result, Type::Int};
      } else if constexpr (std::is_same_v<T, Expr::Binary>) {
        if (node.op == BinaryOp::And || node.op == BinaryOp::Or)
          return lowerLogical(node);
        Value left = lowerExpr(*node.left);
        Value right = lowerExpr(*node.right);
        requireInt(left, "binary operator");
        requireInt(right, "binary operator");
        const int result = newReg();
        IRInst inst{IROp::Binary};
        inst.dst = result;
        inst.left = left.reg;
        inst.right = right.reg;
        inst.binary = node.op;
        emit(std::move(inst));
        return {result, Type::Int};
      } else {
        const auto found = functions_.find(node.name);
        if (found == functions_.end())
          fail("call to function before its declaration: '" + node.name + "'");
        if (found->second.arity != node.args.size())
          fail("wrong number of arguments in call to '" + node.name + "'");
        if (auto inlined = tryInlineCall(node, found->second))
          return *inlined;
        std::vector<int> args;
        for (const auto& arg : node.args) {
          Value value = lowerExpr(*arg);
          requireInt(value, "function argument");
          args.push_back(value.reg);
        }
        current_.maxCallArgs = std::max(current_.maxCallArgs, static_cast<int>(args.size()));
        IRInst inst{IROp::Call};
        inst.name = node.name;
        inst.args = std::move(args);
        if (found->second.type == Type::Int) inst.dst = newReg();
        const int result = inst.dst;
        emit(std::move(inst));
        return {result, found->second.type};
      }
    }, expr.node);
  }

  Value lowerLogical(const Expr::Binary& node) {
    const int result = newReg();
    const std::string shortLabel = newLabel(node.op == BinaryOp::And ? "and_false" : "or_true");
    const std::string endLabel = newLabel("logic_end");
    Value left = lowerExpr(*node.left);
    requireInt(left, "logical operator");
    IRInst branch{IROp::BranchZero};
    branch.left = left.reg;
    branch.name = shortLabel;
    if (node.op == BinaryOp::Or) {
      const std::string evaluateRight = newLabel("or_rhs");
      branch.name = evaluateRight;
      emit(std::move(branch));
      IRInst one{IROp::Imm}; one.dst = result; one.imm = 1; emit(std::move(one));
      IRInst jump{IROp::Jump}; jump.name = endLabel; emit(std::move(jump));
      IRInst rhsLabel{IROp::Label}; rhsLabel.name = evaluateRight; emit(std::move(rhsLabel));
      Value right = lowerExpr(*node.right);
      requireInt(right, "logical operator");
      IRInst logicalNot{IROp::Unary};
      logicalNot.dst = result;
      logicalNot.left = right.reg;
      logicalNot.unary = UnaryOp::Not;
      emit(std::move(logicalNot));
      IRInst invert{IROp::Unary}; invert.dst = result; invert.left = result; invert.unary = UnaryOp::Not;
      emit(std::move(invert));
    } else {
      emit(std::move(branch));
      Value right = lowerExpr(*node.right);
      requireInt(right, "logical operator");
      IRInst logicalNot{IROp::Unary};
      logicalNot.dst = result;
      logicalNot.left = right.reg;
      logicalNot.unary = UnaryOp::Not;
      emit(std::move(logicalNot));
      IRInst invert{IROp::Unary}; invert.dst = result; invert.left = result; invert.unary = UnaryOp::Not;
      emit(std::move(invert));
      IRInst jump{IROp::Jump}; jump.name = endLabel; emit(std::move(jump));
      IRInst label{IROp::Label}; label.name = shortLabel; emit(std::move(label));
      IRInst zero{IROp::Imm}; zero.dst = result; zero.imm = 0; emit(std::move(zero));
    }
    IRInst end{IROp::Label}; end.name = endLabel; emit(std::move(end));
    return {result, Type::Int};
  }

  static bool inlineablePrefixStmt(const Stmt& stmt, std::unordered_set<std::string>& locals) {
    if (std::holds_alternative<Stmt::Empty>(stmt.node)) return true;
    if (const auto* decl = std::get_if<Stmt::DeclStmt>(&stmt.node)) {
      if (decl->decl.isConst || !decl->decl.init || locals.count(decl->decl.name))
        return false;
      locals.insert(decl->decl.name);
      return true;
    }
    if (const auto* assign = std::get_if<Stmt::Assign>(&stmt.node)) {
      return locals.count(assign->name) != 0;
    }
    return false;
  }

  static const Stmt::Block* inlineableStraightLineBlock(const Function& function) {
    if (function.returnType != Type::Int) return nullptr;
    const auto* block = std::get_if<Stmt::Block>(&function.body->node);
    if (!block || block->items.empty() || block->items.size() > 24) return nullptr;
    std::unordered_set<std::string> locals(function.params.begin(), function.params.end());
    for (size_t i = 0; i + 1 < block->items.size(); ++i) {
      if (!inlineablePrefixStmt(*block->items[i], locals))
        return nullptr;
    }
    const auto* ret = std::get_if<Stmt::Return>(&block->items.back()->node);
    if (!ret || !ret->value) return nullptr;
    return block;
  }

  static const Expr* inlineBlockReturnExpr(const Stmt::Block& block) {
    const auto* ret = std::get_if<Stmt::Return>(&block.items.back()->node);
    return ret && ret->value ? ret->value.get() : nullptr;
  }

  static int exprSize(const Expr& expr) {
    return std::visit([&](const auto& node) -> int {
      using T = std::decay_t<decltype(node)>;
      if constexpr (std::is_same_v<T, Expr::Unary>) {
        return 1 + exprSize(*node.operand);
      } else if constexpr (std::is_same_v<T, Expr::Binary>) {
        return 1 + exprSize(*node.left) + exprSize(*node.right);
      } else if constexpr (std::is_same_v<T, Expr::Call>) {
        int total = 1;
        for (const auto& arg : node.args) total += exprSize(*arg);
        return total;
      } else {
        return 1;
      }
    }, expr.node);
  }

  static bool inlineExprHasCall(const Expr& expr) {
    return std::visit([&](const auto& node) -> bool {
      using T = std::decay_t<decltype(node)>;
      if constexpr (std::is_same_v<T, Expr::Unary>) {
        return inlineExprHasCall(*node.operand);
      } else if constexpr (std::is_same_v<T, Expr::Binary>) {
        return inlineExprHasCall(*node.left) || inlineExprHasCall(*node.right);
      } else if constexpr (std::is_same_v<T, Expr::Call>) {
        return true;
      } else {
        return false;
      }
    }, expr.node);
  }

  std::optional<int32_t> currentConstantValue(int reg) const {
    std::optional<int32_t> result;
    for (const auto& inst : current_.code) {
      if (inst.dst == reg) {
        if (result || inst.op != IROp::Imm) return std::nullopt;
        result = inst.imm;
      }
    }
    return result;
  }

  std::optional<Value> tryInlineCall(const Expr::Call& call, const FunctionSig& sig) {
    if (!optimize_ || sig.type != Type::Int || call.name == current_.name)
      return std::nullopt;
    const auto body = functionBodies_.find(call.name);
    if (body == functionBodies_.end()) return std::nullopt;
    const Function& function = *body->second;
    const Stmt::Block* inlineBlock = inlineableStraightLineBlock(function);
    if (!inlineBlock) return std::nullopt;
    const Expr* returned = inlineBlockReturnExpr(*inlineBlock);
    if (!returned || exprSize(*returned) > 120) return std::nullopt;
    if (inlineExprHasCall(*returned)) return std::nullopt;

    std::vector<Value> args;
    args.reserve(call.args.size());
    for (const auto& arg : call.args) {
      Value value = lowerExpr(*arg);
      requireInt(value, "function argument");
      args.push_back(value);
    }

    pushScope();
    for (size_t i = 0; i < function.params.size(); ++i) {
      Symbol symbol{SymbolKind::Local, newLocal(), 0, false};
      declare(function.params[i], symbol);
      IRInst store{IROp::StoreLocal};
      store.left = symbol.slot;
      store.right = args[i].reg;
      emit(std::move(store));
      if (const auto known = currentConstantValue(args[i].reg))
        knownLocalValues_[symbol.slot] = *known;
      else
        knownLocalValues_[symbol.slot].reset();
    }
    for (size_t i = 0; i + 1 < inlineBlock->items.size(); ++i)
      lowerStmt(*inlineBlock->items[i], false);
    Value result = lowerExpr(*returned);
    popScope();
    return result;
  }

  static bool isName(const Expr& expr, const std::string& name) {
    const auto* node = std::get_if<Expr::Name>(&expr.node);
    return node && node->value == name;
  }

  static bool numberValue(const Expr& expr, int32_t& value) {
    if (const auto* node = std::get_if<Expr::Number>(&expr.node)) {
      value = node->value;
      return true;
    }
    return false;
  }

  bool constantExprValue(const Expr& expr, int32_t& value) {
    return std::visit([&](const auto& node) -> bool {
      using T = std::decay_t<decltype(node)>;
      if constexpr (std::is_same_v<T, Expr::Number>) {
        value = node.value;
        return true;
      } else if constexpr (std::is_same_v<T, Expr::Name>) {
        const Symbol* symbol = lookup(node.value);
        if (!symbol || symbol->kind != SymbolKind::Constant) return false;
        value = symbol->value;
        return true;
      } else if constexpr (std::is_same_v<T, Expr::Unary>) {
        int32_t operand = 0;
        if (!constantExprValue(*node.operand, operand)) return false;
        value = foldUnary(node.op, operand);
        return true;
      } else if constexpr (std::is_same_v<T, Expr::Binary>) {
        int32_t left = 0;
        if (!constantExprValue(*node.left, left)) return false;
        if (node.op == BinaryOp::And && left == 0) {
          value = 0;
          return true;
        }
        if (node.op == BinaryOp::Or && left != 0) {
          value = 1;
          return true;
        }
        int32_t right = 0;
        if (!constantExprValue(*node.right, right)) return false;
        if ((node.op == BinaryOp::Div || node.op == BinaryOp::Mod) && right == 0)
          return false;
        value = foldBinary(node.op, left, right);
        return true;
      } else {
        return false;
      }
    }, expr.node);
  }

  bool knownValueExpr(const Expr& expr, int32_t& value) const {
    return std::visit([&](const auto& node) -> bool {
      using T = std::decay_t<decltype(node)>;
      if constexpr (std::is_same_v<T, Expr::Number>) {
        value = node.value;
        return true;
      } else if constexpr (std::is_same_v<T, Expr::Name>) {
        const Symbol* symbol = lookup(node.value);
        if (!symbol) return false;
        if (symbol->kind == SymbolKind::Constant) {
          value = symbol->value;
          return true;
        }
        if (symbol->kind == SymbolKind::Local &&
            symbol->slot >= 0 &&
            symbol->slot < static_cast<int>(knownLocalValues_.size()) &&
            knownLocalValues_[symbol->slot]) {
          value = *knownLocalValues_[symbol->slot];
          return true;
        }
        return false;
      } else if constexpr (std::is_same_v<T, Expr::Unary>) {
        int32_t operand = 0;
        if (!knownValueExpr(*node.operand, operand)) return false;
        value = foldUnary(node.op, operand);
        return true;
      } else if constexpr (std::is_same_v<T, Expr::Binary>) {
        int32_t left = 0;
        if (!knownValueExpr(*node.left, left)) return false;
        if (node.op == BinaryOp::And && left == 0) {
          value = 0;
          return true;
        }
        if (node.op == BinaryOp::Or && left != 0) {
          value = 1;
          return true;
        }
        int32_t right = 0;
        if (!knownValueExpr(*node.right, right)) return false;
        if ((node.op == BinaryOp::Div || node.op == BinaryOp::Mod) && right == 0)
          return false;
        value = foldBinary(node.op, left, right);
        return true;
      } else {
        return false;
      }
    }, expr.node);
  }

  static bool exprHasCall(const Expr& expr) {
    return std::visit([&](const auto& node) -> bool {
      using T = std::decay_t<decltype(node)>;
      if constexpr (std::is_same_v<T, Expr::Unary>) {
        return exprHasCall(*node.operand);
      } else if constexpr (std::is_same_v<T, Expr::Binary>) {
        return exprHasCall(*node.left) || exprHasCall(*node.right);
      } else if constexpr (std::is_same_v<T, Expr::Call>) {
        return true;
      } else {
        return false;
      }
    }, expr.node);
  }

  static bool exprReadsAny(const Expr& expr, const std::unordered_set<std::string>& names) {
    return std::visit([&](const auto& node) -> bool {
      using T = std::decay_t<decltype(node)>;
      if constexpr (std::is_same_v<T, Expr::Name>) {
        return names.contains(node.value);
      } else if constexpr (std::is_same_v<T, Expr::Unary>) {
        return exprReadsAny(*node.operand, names);
      } else if constexpr (std::is_same_v<T, Expr::Binary>) {
        return exprReadsAny(*node.left, names) || exprReadsAny(*node.right, names);
      } else if constexpr (std::is_same_v<T, Expr::Call>) {
        return true;
      } else {
        return false;
      }
    }, expr.node);
  }

  static bool exprIsLoopInvariant(const Expr& expr,
                                  const std::unordered_set<std::string>& assigned) {
    return !exprHasCall(expr) && !exprReadsAny(expr, assigned);
  }

  bool parseIncrement(const Stmt::Assign& assign, const std::string& name,
                      int32_t& step) {
    if (assign.name != name) return false;
    const auto* binary = std::get_if<Expr::Binary>(&assign.value->node);
    if (!binary || (binary->op != BinaryOp::Add && binary->op != BinaryOp::Sub))
      return false;
    int32_t value = 0;
    if (binary->op == BinaryOp::Add && isName(*binary->left, name) &&
        knownValueExpr(*binary->right, value) && value > 0) {
      step = value;
      return true;
    }
    if (binary->op == BinaryOp::Add && isName(*binary->right, name) &&
        knownValueExpr(*binary->left, value) && value > 0) {
      step = value;
      return true;
    }
    if (binary->op == BinaryOp::Sub && isName(*binary->left, name) &&
        knownValueExpr(*binary->right, value) && value > 0) {
      step = -value;
      return true;
    }
    return false;
  }

  struct AccumulationUpdate {
    std::string target;
    const Expr* addend = nullptr;
    int sign = 1;
    bool afterIncrement = false;
  };

  struct DirectLoopUpdate {
    std::string target;
    const Expr* value = nullptr;
    bool afterIncrement = false;
  };

  struct LoopTemporary {
    const Expr* value = nullptr;
    bool afterIncrement = false;
  };

  using LoopTemporaries = std::unordered_map<std::string, LoopTemporary>;

  struct LinearLoopExpr {
    int counterCoeff = 0;
    const Expr* offset = nullptr;
    int offsetSign = 1;
  };

  static const LoopTemporary* loopTemporaryFor(const Expr& expr,
                                               const LoopTemporaries& temporaries) {
    const auto* name = std::get_if<Expr::Name>(&expr.node);
    if (!name) return nullptr;
    const auto found = temporaries.find(name->value);
    return found == temporaries.end() ? nullptr : &found->second;
  }

  static bool exprReadsName(const Expr& expr, const std::string& name) {
    return std::visit([&](const auto& node) -> bool {
      using T = std::decay_t<decltype(node)>;
      if constexpr (std::is_same_v<T, Expr::Name>) {
        return node.value == name;
      } else if constexpr (std::is_same_v<T, Expr::Unary>) {
        return exprReadsName(*node.operand, name);
      } else if constexpr (std::is_same_v<T, Expr::Binary>) {
        return exprReadsName(*node.left, name) || exprReadsName(*node.right, name);
      } else if constexpr (std::is_same_v<T, Expr::Call>) {
        for (const auto& arg : node.args)
          if (exprReadsName(*arg, name)) return true;
        return false;
      } else {
        return false;
      }
    }, expr.node);
  }

  static bool parseLinearLoopExpr(const Expr& expr, const std::string& counter,
                                  const std::unordered_set<std::string>& assigned,
                                  const LoopTemporaries& temporaries,
                                  bool afterIncrement, LinearLoopExpr& result) {
    if (const auto* temporary = loopTemporaryFor(expr, temporaries)) {
      if (!temporary->value) return false;
      if (temporary->afterIncrement != afterIncrement &&
          exprReadsName(*temporary->value, counter))
        return false;
      return parseLinearLoopExpr(*temporary->value, counter, assigned, temporaries,
                                 afterIncrement, result);
    }
    if (isName(expr, counter)) {
      result = {1, nullptr, 1};
      return true;
    }
    if (exprIsLoopInvariant(expr, assigned)) {
      result = {0, &expr, 1};
      return true;
    }
    const auto* binary = std::get_if<Expr::Binary>(&expr.node);
    if (!binary || (binary->op != BinaryOp::Add && binary->op != BinaryOp::Sub))
      return false;
    const bool leftCounter = isName(*binary->left, counter);
    const bool rightCounter = isName(*binary->right, counter);
    if (leftCounter && exprIsLoopInvariant(*binary->right, assigned)) {
      result = {1, binary->right.get(), binary->op == BinaryOp::Sub ? -1 : 1};
      return true;
    }
    if (rightCounter && exprIsLoopInvariant(*binary->left, assigned)) {
      if (binary->op == BinaryOp::Add) {
        result = {1, binary->left.get(), 1};
      } else {
        result = {-1, binary->left.get(), 1};
      }
      return true;
    }
    return false;
  }

  static bool parseAccumulationUpdate(const Stmt::Assign& assign,
                                      AccumulationUpdate& update) {
    const auto* binary = std::get_if<Expr::Binary>(&assign.value->node);
    if (!binary) return false;
    if (binary->op == BinaryOp::Add && isName(*binary->left, assign.name)) {
      update = {assign.name, binary->right.get(), 1, false};
      return true;
    }
    if (binary->op == BinaryOp::Add && isName(*binary->right, assign.name)) {
      update = {assign.name, binary->left.get(), 1, false};
      return true;
    }
    if (binary->op == BinaryOp::Sub && isName(*binary->left, assign.name)) {
      update = {assign.name, binary->right.get(), -1, false};
      return true;
    }
    return false;
  }

  int emitBinaryReg(BinaryOp op, int left, int right) {
    const int result = newReg();
    IRInst inst{IROp::Binary};
    inst.dst = result;
    inst.left = left;
    inst.right = right;
    inst.binary = op;
    emit(std::move(inst));
    return result;
  }

  static bool isSupportedLoopAddend(const Expr& expr, const std::string& counter,
                                    const std::unordered_set<std::string>& assigned,
                                    const LoopTemporaries& temporaries,
                                    bool afterIncrement) {
    if (const auto* temporary = loopTemporaryFor(expr, temporaries)) {
      if (!temporary->value) return false;
      if (temporary->afterIncrement != afterIncrement &&
          exprReadsName(*temporary->value, counter))
        return false;
      return isSupportedLoopAddend(*temporary->value, counter, assigned, temporaries,
                                   afterIncrement);
    }
    if (exprIsLoopInvariant(expr, assigned) || isName(expr, counter)) return true;
    const auto* binary = std::get_if<Expr::Binary>(&expr.node);
    if (!binary) return false;
    const bool leftCounter = isName(*binary->left, counter);
    const bool rightCounter = isName(*binary->right, counter);
    if (binary->op == BinaryOp::Mul && leftCounter && rightCounter) return true;
    if (binary->op == BinaryOp::Mul) {
      LinearLoopExpr left;
      LinearLoopExpr right;
      return parseLinearLoopExpr(*binary->left, counter, assigned, temporaries,
                                 afterIncrement, left) &&
             parseLinearLoopExpr(*binary->right, counter, assigned, temporaries,
                                 afterIncrement, right) &&
             (left.counterCoeff != 0 || right.counterCoeff != 0);
    }
    if (binary->op == BinaryOp::Add || binary->op == BinaryOp::Sub)
      return isSupportedLoopAddend(*binary->left, counter, assigned, temporaries,
                                   afterIncrement) &&
             isSupportedLoopAddend(*binary->right, counter, assigned, temporaries,
                                   afterIncrement);
    return false;
  }

  static bool isSupportedLoopPointExpr(const Expr& expr, const std::string& counter,
                                       const std::unordered_set<std::string>& assigned,
                                       const LoopTemporaries& temporaries,
                                       bool afterIncrement) {
    if (const auto* temporary = loopTemporaryFor(expr, temporaries)) {
      if (!temporary->value) return false;
      if (temporary->afterIncrement != afterIncrement &&
          exprReadsName(*temporary->value, counter))
        return false;
      return isSupportedLoopPointExpr(*temporary->value, counter, assigned, temporaries,
                                      afterIncrement);
    }
    if (exprIsLoopInvariant(expr, assigned) || isName(expr, counter)) return true;
    const auto* binary = std::get_if<Expr::Binary>(&expr.node);
    if (!binary) return false;
    const bool leftCounter = isName(*binary->left, counter);
    const bool rightCounter = isName(*binary->right, counter);
    if (binary->op == BinaryOp::Mul)
      return (leftCounter && exprIsLoopInvariant(*binary->right, assigned)) ||
             (rightCounter && exprIsLoopInvariant(*binary->left, assigned));
    if (binary->op == BinaryOp::Add || binary->op == BinaryOp::Sub)
      return (leftCounter && exprIsLoopInvariant(*binary->right, assigned)) ||
             (rightCounter && exprIsLoopInvariant(*binary->left, assigned));
    return false;
  }

  int emitCounterSeries(int counterReg, int iterations, int32_t stepValue) {
    const int one = emitImm(1);
    const int lastIndex = emitBinaryReg(BinaryOp::Sub, iterations, one);
    const int step = emitImm(stepValue);
    const int distance = emitBinaryReg(BinaryOp::Mul, lastIndex, step);
    const int last = emitBinaryReg(BinaryOp::Add, counterReg, distance);
    const int firstPlusLast = emitBinaryReg(BinaryOp::Add, counterReg, last);
    const int product = emitBinaryReg(BinaryOp::Mul, iterations, firstPlusLast);
    const int two = emitImm(2);
    return emitBinaryReg(BinaryOp::Div, product, two);
  }

  int emitCounterSquareSeries(int counterReg, int iterations, int32_t stepValue) {
    const int startSquare = emitBinaryReg(BinaryOp::Mul, counterReg, counterReg);
    const int term0 = emitBinaryReg(BinaryOp::Mul, iterations, startSquare);

    const int one = emitImm(1);
    const int nMinusOne = emitBinaryReg(BinaryOp::Sub, iterations, one);
    const int nTimesNMinusOne = emitBinaryReg(BinaryOp::Mul, iterations, nMinusOne);
    const int two = emitImm(2);
    const int pairCount = emitBinaryReg(BinaryOp::Div, nTimesNMinusOne, two);
    const int step = emitImm(stepValue);
    const int startTimesStep = emitBinaryReg(BinaryOp::Mul, counterReg, step);
    const int twiceStartStep = emitBinaryReg(BinaryOp::Mul, startTimesStep, two);
    const int term1 = emitBinaryReg(BinaryOp::Mul, twiceStartStep, pairCount);

    const int twoNMinusOne = emitBinaryReg(BinaryOp::Add, iterations, nMinusOne);
    const int squareIndexProduct = emitBinaryReg(BinaryOp::Mul, nTimesNMinusOne, twoNMinusOne);
    const int six = emitImm(6);
    const int squareIndexSum = emitBinaryReg(BinaryOp::Div, squareIndexProduct, six);
    const int stepSquare = emitBinaryReg(BinaryOp::Mul, step, step);
    const int term2 = emitBinaryReg(BinaryOp::Mul, stepSquare, squareIndexSum);

    const int firstTwo = emitBinaryReg(BinaryOp::Add, term0, term1);
    return emitBinaryReg(BinaryOp::Add, firstTwo, term2);
  }

  int emitSignedInvariant(const Expr& expr, int sign) {
    Value value = lowerExpr(expr);
    requireInt(value, "loop accumulator");
    if (sign >= 0) return value.reg;
    const int zero = emitImm(0);
    return emitBinaryReg(BinaryOp::Sub, zero, value.reg);
  }

  int emitLinearProductSeries(const LinearLoopExpr& left, const LinearLoopExpr& right,
                              int counterReg, int iterations, int32_t stepValue) {
    int total = emitImm(0);
    auto addTerm = [&](int term) {
      total = emitBinaryReg(BinaryOp::Add, total, term);
    };
    const int squareCoeff = left.counterCoeff * right.counterCoeff;
    if (squareCoeff != 0) {
      int term = emitCounterSquareSeries(counterReg, iterations, stepValue);
      if (squareCoeff < 0) {
        const int zero = emitImm(0);
        term = emitBinaryReg(BinaryOp::Sub, zero, term);
      }
      addTerm(term);
    }
    if (right.offset && left.counterCoeff != 0) {
      const int offset = emitSignedInvariant(*right.offset,
                                             left.counterCoeff * right.offsetSign);
      const int series = emitCounterSeries(counterReg, iterations, stepValue);
      addTerm(emitBinaryReg(BinaryOp::Mul, offset, series));
    }
    if (left.offset && right.counterCoeff != 0) {
      const int offset = emitSignedInvariant(*left.offset,
                                             right.counterCoeff * left.offsetSign);
      const int series = emitCounterSeries(counterReg, iterations, stepValue);
      addTerm(emitBinaryReg(BinaryOp::Mul, offset, series));
    }
    if (left.offset && right.offset) {
      const int leftOffset = emitSignedInvariant(*left.offset, left.offsetSign);
      const int rightOffset = emitSignedInvariant(*right.offset, right.offsetSign);
      const int product = emitBinaryReg(BinaryOp::Mul, leftOffset, rightOffset);
      addTerm(emitBinaryReg(BinaryOp::Mul, product, iterations));
    }
    return total;
  }

  int emitLoopDelta(const Expr& expr, const std::string& counter,
                    const std::unordered_set<std::string>& assigned,
                    const LoopTemporaries& temporaries, bool afterIncrement,
                    int counterReg, int iterations, int32_t stepValue) {
    if (const auto* temporary = loopTemporaryFor(expr, temporaries)) {
      if (!temporary->value) fail("unsupported loop temporary");
      if (temporary->afterIncrement != afterIncrement &&
          exprReadsName(*temporary->value, counter))
        fail("unsupported loop temporary phase");
      return emitLoopDelta(*temporary->value, counter, assigned, temporaries,
                           afterIncrement, counterReg, iterations, stepValue);
    }
    if (exprIsLoopInvariant(expr, assigned)) {
      Value addend = lowerExpr(expr);
      requireInt(addend, "loop accumulator");
      return emitBinaryReg(BinaryOp::Mul, addend.reg, iterations);
    }
    if (isName(expr, counter)) {
      return emitCounterSeries(counterReg, iterations, stepValue);
    }

    const auto* binary = std::get_if<Expr::Binary>(&expr.node);
    if (!binary) fail("unsupported loop addend");
    const bool leftCounter = isName(*binary->left, counter);
    const bool rightCounter = isName(*binary->right, counter);
    if (binary->op == BinaryOp::Mul && leftCounter && rightCounter)
      return emitCounterSquareSeries(counterReg, iterations, stepValue);
    if (binary->op == BinaryOp::Mul) {
      LinearLoopExpr left;
      LinearLoopExpr right;
      if (parseLinearLoopExpr(*binary->left, counter, assigned, temporaries,
                              afterIncrement, left) &&
          parseLinearLoopExpr(*binary->right, counter, assigned, temporaries,
                              afterIncrement, right) &&
          (left.counterCoeff != 0 || right.counterCoeff != 0))
        return emitLinearProductSeries(left, right, counterReg, iterations, stepValue);
    }
    if (binary->op == BinaryOp::Add || binary->op == BinaryOp::Sub) {
      const int leftDelta = emitLoopDelta(*binary->left, counter, assigned, temporaries,
                                          afterIncrement, counterReg, iterations,
                                          stepValue);
      const int rightDelta = emitLoopDelta(*binary->right, counter, assigned, temporaries,
                                           afterIncrement, counterReg, iterations,
                                           stepValue);
      return emitBinaryReg(binary->op, leftDelta, rightDelta);
    }
    const Expr* invariant = nullptr;
    if (leftCounter && exprIsLoopInvariant(*binary->right, assigned))
      invariant = binary->right.get();
    else if (rightCounter && exprIsLoopInvariant(*binary->left, assigned))
      invariant = binary->left.get();
    else
      fail("unsupported loop addend");

    Value invariantValue = lowerExpr(*invariant);
    requireInt(invariantValue, "loop accumulator");
    const int invariantDelta = emitBinaryReg(BinaryOp::Mul, invariantValue.reg, iterations);
    const int counterDelta = emitCounterSeries(counterReg, iterations, stepValue);

    if (binary->op == BinaryOp::Mul)
      return emitBinaryReg(BinaryOp::Mul, counterDelta, invariantValue.reg);
    if (binary->op == BinaryOp::Add)
      return emitBinaryReg(BinaryOp::Add, counterDelta, invariantDelta);
    if (binary->op == BinaryOp::Sub) {
      if (leftCounter)
        return emitBinaryReg(BinaryOp::Sub, counterDelta, invariantDelta);
      return emitBinaryReg(BinaryOp::Sub, invariantDelta, counterDelta);
    }
    fail("unsupported loop addend");
  }

  int emitLoopPointValue(const Expr& expr, const std::string& counter,
                         const std::unordered_set<std::string>& assigned,
                         const LoopTemporaries& temporaries, bool afterIncrement,
                         int counterAtPoint) {
    if (const auto* temporary = loopTemporaryFor(expr, temporaries)) {
      if (!temporary->value) fail("unsupported loop temporary");
      if (temporary->afterIncrement != afterIncrement &&
          exprReadsName(*temporary->value, counter))
        fail("unsupported loop temporary phase");
      return emitLoopPointValue(*temporary->value, counter, assigned, temporaries,
                                afterIncrement, counterAtPoint);
    }
    if (exprIsLoopInvariant(expr, assigned)) {
      Value value = lowerExpr(expr);
      requireInt(value, "loop assignment");
      return value.reg;
    }
    if (isName(expr, counter)) return counterAtPoint;

    const auto* binary = std::get_if<Expr::Binary>(&expr.node);
    if (!binary) fail("unsupported loop assignment");
    const bool leftCounter = isName(*binary->left, counter);
    const bool rightCounter = isName(*binary->right, counter);
    const Expr* invariant = nullptr;
    if (leftCounter && exprIsLoopInvariant(*binary->right, assigned))
      invariant = binary->right.get();
    else if (rightCounter && exprIsLoopInvariant(*binary->left, assigned))
      invariant = binary->left.get();
    else
      fail("unsupported loop assignment");

    Value invariantValue = lowerExpr(*invariant);
    requireInt(invariantValue, "loop assignment");
    if (binary->op == BinaryOp::Mul)
      return emitBinaryReg(BinaryOp::Mul, counterAtPoint, invariantValue.reg);
    if (binary->op == BinaryOp::Add)
      return emitBinaryReg(BinaryOp::Add, counterAtPoint, invariantValue.reg);
    if (binary->op == BinaryOp::Sub) {
      if (leftCounter)
        return emitBinaryReg(BinaryOp::Sub, counterAtPoint, invariantValue.reg);
      return emitBinaryReg(BinaryOp::Sub, invariantValue.reg, counterAtPoint);
    }
    fail("unsupported loop assignment");
  }

  Value lowerNameValue(const std::string& name) {
    Expr::Name node{name};
    Expr expr{std::move(node)};
    return lowerExpr(expr);
  }

  void emitStoreName(const std::string& name, int reg) {
    const Symbol* symbol = lookup(name);
    if (!symbol) fail("assignment to undeclared identifier '" + name + "'");
    if (symbol->isConst) fail("assignment to constant '" + name + "'");
    IRInst store{symbol->kind == SymbolKind::Local ? IROp::StoreLocal : IROp::StoreGlobal};
    store.left = symbol->slot;
    store.right = reg;
    store.name = name;
    emit(std::move(store));
    if (symbol->kind == SymbolKind::Local &&
        symbol->slot >= 0 && symbol->slot < static_cast<int>(knownLocalValues_.size()))
      knownLocalValues_[symbol->slot].reset();
  }

  bool knownLocalValue(const std::string& name, int32_t& value) const {
    const Symbol* symbol = lookup(name);
    if (!symbol || symbol->kind != SymbolKind::Local ||
        symbol->slot < 0 || symbol->slot >= static_cast<int>(knownLocalValues_.size()) ||
        !knownLocalValues_[symbol->slot])
      return false;
    value = *knownLocalValues_[symbol->slot];
    return true;
  }

  static bool statementIsContinue(const Stmt& stmt) {
    if (const auto* block = std::get_if<Stmt::Block>(&stmt.node))
      return block->items.size() == 1 && statementIsContinue(*block->items.front());
    return std::holds_alternative<Stmt::Continue>(stmt.node);
  }

  static bool statementIsBreak(const Stmt& stmt) {
    if (const auto* block = std::get_if<Stmt::Block>(&stmt.node))
      return block->items.size() == 1 && statementIsBreak(*block->items.front());
    return std::holds_alternative<Stmt::Break>(stmt.node);
  }

  bool parseCounterComparison(const Expr& expr, const std::string& counter,
                              BinaryOp& op, int32_t& value) {
    const auto* binary = std::get_if<Expr::Binary>(&expr.node);
    if (!binary || !isName(*binary->left, counter) ||
        !knownValueExpr(*binary->right, value))
      return false;
    op = binary->op;
    return true;
  }

  bool lowerFilteredAccumulationLoop(const Stmt::While& node) {
    const auto* condition = std::get_if<Expr::Binary>(&node.condition->node);
    if (!condition || (condition->op != BinaryOp::Lt && condition->op != BinaryOp::Le))
      return false;
    const auto* loopName = std::get_if<Expr::Name>(&condition->left->node);
    int32_t boundaryValue = 0;
    if (!loopName || !knownValueExpr(*condition->right, boundaryValue)) return false;
    int32_t limitValue = boundaryValue;
    if (condition->op == BinaryOp::Le) {
      if (limitValue == std::numeric_limits<int32_t>::max()) return false;
      ++limitValue;
    }

    const std::string& counter = loopName->value;
    int32_t startValue = 0;
    if (!knownLocalValue(counter, startValue) || startValue >= limitValue) return false;

    std::vector<const Stmt*> items;
    if (const auto* block = std::get_if<Stmt::Block>(&node.body->node)) {
      for (const auto& item : block->items) items.push_back(item.get());
    } else {
      items.push_back(node.body.get());
    }
    if (items.empty()) return false;

    int incrementCount = 0;
    int32_t stepValue = 0;
    bool seenIncrement = false;
    std::vector<int32_t> skippedValues;
    std::optional<int32_t> breakInclusiveLast;
    std::optional<int32_t> breakExitValue;
    std::vector<AccumulationUpdate> updates;
    LoopTemporaries temporaries;
    std::unordered_set<std::string> assigned;
    assigned.insert(counter);

    for (const Stmt* stmt : items) {
      if (const auto* declStmt = std::get_if<Stmt::DeclStmt>(&stmt->node)) {
        if (declStmt->decl.isConst || !declStmt->decl.init ||
            temporaries.count(declStmt->decl.name) != 0)
          return false;
        temporaries[declStmt->decl.name] = {declStmt->decl.init.get(), seenIncrement};
        continue;
      }
      if (const auto* assign = std::get_if<Stmt::Assign>(&stmt->node)) {
        if (temporaries.count(assign->name) != 0) return false;
        int32_t candidateStep = 0;
        if (parseIncrement(*assign, counter, candidateStep)) {
          ++incrementCount;
          stepValue = candidateStep;
          seenIncrement = true;
          continue;
        }
        AccumulationUpdate update;
        if (!parseAccumulationUpdate(*assign, update)) return false;
        update.afterIncrement = seenIncrement;
        if (!update.afterIncrement) return false;
        updates.push_back(update);
        assigned.insert(update.target);
        continue;
      }
      const auto* ifStmt = std::get_if<Stmt::If>(&stmt->node);
      if (!ifStmt || ifStmt->elseBranch || !seenIncrement) return false;
      BinaryOp op = BinaryOp::Eq;
      int32_t value = 0;
      if (!parseCounterComparison(*ifStmt->condition, counter, op, value)) return false;
      if (statementIsContinue(*ifStmt->thenBranch) && op == BinaryOp::Eq) {
        skippedValues.push_back(value);
      } else if (statementIsBreak(*ifStmt->thenBranch) &&
                 (op == BinaryOp::Gt || op == BinaryOp::Ge)) {
        if (breakInclusiveLast) return false;
        if (op == BinaryOp::Gt) {
          if (value == std::numeric_limits<int32_t>::max()) return false;
          breakInclusiveLast = value;
          breakExitValue = value + 1;
        } else {
          if (value == std::numeric_limits<int32_t>::min()) return false;
          breakInclusiveLast = value - 1;
          breakExitValue = value;
        }
      } else {
        return false;
      }
    }

    if (incrementCount != 1 || stepValue != 1 || updates.empty()) return false;
    if (exprReadsAny(*condition->right, assigned)) return false;
    std::unordered_set<std::string> assignedWithoutCounter = assigned;
    assignedWithoutCounter.erase(counter);
    std::unordered_set<std::string> temporaryNames;
    for (const auto& entry : temporaries) temporaryNames.insert(entry.first);
    for (const auto& [name, temporary] : temporaries) {
      if (!temporary.value || exprHasCall(*temporary.value) ||
          exprReadsName(*temporary.value, name) ||
          exprReadsAny(*temporary.value, assignedWithoutCounter) ||
          exprReadsAny(*temporary.value, temporaryNames))
        return false;
    }
    for (const auto& update : updates) {
      if (!update.addend ||
          !isSupportedLoopAddend(*update.addend, counter, assigned, temporaries,
                                 update.afterIncrement))
        return false;
    }

    int32_t inclusiveLast = limitValue;
    int32_t finalCounterValue = limitValue;
    if (breakInclusiveLast && *breakInclusiveLast < inclusiveLast) {
      inclusiveLast = *breakInclusiveLast;
      finalCounterValue = *breakExitValue;
    }
    if (inclusiveLast <= startValue) return false;

    Value counterValue = lowerNameValue(counter);
    requireInt(counterValue, "while condition");
    const int boundary = emitImm(boundaryValue);
    const int conditionReg = emitBinaryReg(condition->op, counterValue.reg, boundary);
    const std::string endLabel = newLabel("filtered_loop_end");
    IRInst skipLoop{IROp::BranchZero};
    skipLoop.left = conditionReg;
    skipLoop.name = endLabel;
    emit(std::move(skipLoop));

    const int one = emitImm(1);
    const int counterAfterIncrement = emitBinaryReg(BinaryOp::Add, counterValue.reg, one);
    const int iterations = emitImm(inclusiveLast - startValue);
    for (const auto& update : updates) {
      Value base = lowerNameValue(update.target);
      requireInt(base, "loop accumulator");
      int delta = emitLoopDelta(*update.addend, counter, assigned, temporaries, true,
                                counterAfterIncrement, iterations, 1);
      for (int32_t skipped : skippedValues) {
        if (skipped <= startValue || skipped > inclusiveLast) continue;
        const int skipCounter = emitImm(skipped);
        const int skippedValue = emitLoopPointValue(*update.addend, counter, assigned,
                                                    temporaries, true, skipCounter);
        delta = emitBinaryReg(BinaryOp::Sub, delta, skippedValue);
      }
      const int result = emitBinaryReg(update.sign > 0 ? BinaryOp::Add : BinaryOp::Sub,
                                      base.reg, delta);
      emitStoreName(update.target, result);
    }
    emitStoreName(counter, emitImm(finalCounterValue));
    IRInst end{IROp::Label};
    end.name = endLabel;
    emit(std::move(end));
    return true;
  }

  bool lowerCountedAccumulationLoop(const Stmt::While& node) {
    const auto* condition = std::get_if<Expr::Binary>(&node.condition->node);
    if (!condition || (condition->op != BinaryOp::Lt && condition->op != BinaryOp::Le &&
                       condition->op != BinaryOp::Gt && condition->op != BinaryOp::Ge))
      return false;
    const auto* loopName = std::get_if<Expr::Name>(&condition->left->node);
    int32_t boundaryValue = 0;
    if (!loopName || !knownValueExpr(*condition->right, boundaryValue)) return false;
    int32_t limitValue = boundaryValue;
    if (condition->op == BinaryOp::Le) {
      if (limitValue == std::numeric_limits<int32_t>::max()) return false;
      ++limitValue;
    } else if (condition->op == BinaryOp::Ge) {
      if (limitValue == std::numeric_limits<int32_t>::min()) return false;
      --limitValue;
    }

    std::vector<const Stmt*> items;
    if (const auto* block = std::get_if<Stmt::Block>(&node.body->node)) {
      for (const auto& item : block->items) items.push_back(item.get());
    } else {
      items.push_back(node.body.get());
    }
    if (items.empty()) return false;

    const std::string& counter = loopName->value;
    int incrementCount = 0;
    int32_t stepValue = 0;
    std::vector<AccumulationUpdate> updates;
    std::vector<DirectLoopUpdate> directUpdates;
    LoopTemporaries temporaries;
    std::unordered_set<std::string> assigned;
    assigned.insert(counter);
    bool seenIncrement = false;

    for (const Stmt* stmt : items) {
      if (const auto* declStmt = std::get_if<Stmt::DeclStmt>(&stmt->node)) {
        if (declStmt->decl.isConst || !declStmt->decl.init ||
            temporaries.count(declStmt->decl.name) != 0)
          return false;
        temporaries[declStmt->decl.name] = {declStmt->decl.init.get(), seenIncrement};
        continue;
      }
      const auto* assign = std::get_if<Stmt::Assign>(&stmt->node);
      if (!assign) return false;
      if (temporaries.count(assign->name) != 0) return false;
      int32_t candidateStep = 0;
      if (parseIncrement(*assign, counter, candidateStep)) {
        ++incrementCount;
        stepValue = candidateStep;
        seenIncrement = true;
        continue;
      }
      AccumulationUpdate update;
      if (parseAccumulationUpdate(*assign, update)) {
        update.afterIncrement = seenIncrement;
        updates.push_back(update);
        assigned.insert(update.target);
      } else {
        directUpdates.push_back({assign->name, assign->value.get(), seenIncrement});
        assigned.insert(assign->name);
      }
    }

    if (incrementCount != 1) return false;
    if (exprReadsAny(*condition->right, assigned)) return false;
    if ((condition->op == BinaryOp::Lt || condition->op == BinaryOp::Le) && stepValue <= 0)
      return false;
    if ((condition->op == BinaryOp::Gt || condition->op == BinaryOp::Ge) && stepValue >= 0)
      return false;
    std::unordered_set<std::string> assignedWithoutCounter = assigned;
    assignedWithoutCounter.erase(counter);
    std::unordered_set<std::string> temporaryNames;
    for (const auto& entry : temporaries) temporaryNames.insert(entry.first);
    for (const auto& [name, temporary] : temporaries) {
      if (!temporary.value || exprHasCall(*temporary.value) ||
          exprReadsName(*temporary.value, name) ||
          exprReadsAny(*temporary.value, assignedWithoutCounter) ||
          exprReadsAny(*temporary.value, temporaryNames))
        return false;
    }
    for (const auto& update : updates) {
      if (!update.addend ||
          !isSupportedLoopAddend(*update.addend, counter, assigned, temporaries,
                                 update.afterIncrement))
        return false;
    }
    for (const auto& update : directUpdates) {
      if (update.target == counter || !update.value ||
          !isSupportedLoopPointExpr(*update.value, counter, assigned, temporaries,
                                    update.afterIncrement))
        return false;
    }

    Value counterValue = lowerNameValue(counter);
    requireInt(counterValue, "while condition");
    const int limit = emitImm(limitValue);
    const int boundary = emitImm(boundaryValue);
    const int conditionReg = emitBinaryReg(condition->op, counterValue.reg, boundary);
    const std::string endLabel = newLabel("counted_loop_end");
    IRInst skip{IROp::BranchZero};
    skip.left = conditionReg;
    skip.name = endLabel;
    emit(std::move(skip));

    const int positiveStep = stepValue > 0 ? stepValue : -stepValue;
    int iterations = stepValue > 0
                         ? emitBinaryReg(BinaryOp::Sub, limit, counterValue.reg)
                         : emitBinaryReg(BinaryOp::Sub, counterValue.reg, limit);
    if (positiveStep != 1) {
      const int bias = emitImm(positiveStep - 1);
      const int biased = emitBinaryReg(BinaryOp::Add, iterations, bias);
      const int step = emitImm(positiveStep);
      iterations = emitBinaryReg(BinaryOp::Div, biased, step);
    }
    int counterAfterIncrement = counterValue.reg;
    if (!updates.empty()) {
      bool needsAfterIncrement = false;
      for (const auto& update : updates)
        needsAfterIncrement = needsAfterIncrement || update.afterIncrement;
      if (needsAfterIncrement) {
        const int step = emitImm(stepValue);
        counterAfterIncrement = emitBinaryReg(BinaryOp::Add, counterValue.reg, step);
      }
    }
    for (const auto& update : updates) {
      Value base = lowerNameValue(update.target);
      requireInt(base, "loop accumulator");
      const int seriesStart = update.afterIncrement ? counterAfterIncrement : counterValue.reg;
      const int delta = emitLoopDelta(*update.addend, counter, assigned, temporaries,
                                      update.afterIncrement, seriesStart,
                                      iterations, stepValue);
      const int result = emitBinaryReg(update.sign > 0 ? BinaryOp::Add : BinaryOp::Sub,
                                      base.reg, delta);
      emitStoreName(update.target, result);
    }
    int lastCounterBeforeIncrement = counterValue.reg;
    int lastCounterAfterIncrement = counterValue.reg;
    if (!directUpdates.empty()) {
      const int one = emitImm(1);
      const int lastIndex = emitBinaryReg(BinaryOp::Sub, iterations, one);
      const int step = emitImm(stepValue);
      const int distance = emitBinaryReg(BinaryOp::Mul, lastIndex, step);
      lastCounterBeforeIncrement = emitBinaryReg(BinaryOp::Add, counterValue.reg, distance);
      lastCounterAfterIncrement =
          emitBinaryReg(BinaryOp::Add, lastCounterBeforeIncrement, step);
    }
    for (const auto& update : directUpdates) {
      const int counterAtPoint =
          update.afterIncrement ? lastCounterAfterIncrement : lastCounterBeforeIncrement;
      const int value = emitLoopPointValue(*update.value, counter, assigned, temporaries,
                                           update.afterIncrement, counterAtPoint);
      emitStoreName(update.target, value);
    }
    int finalCounter = limit;
    if (positiveStep != 1) {
      const int step = emitImm(stepValue);
      const int distance = emitBinaryReg(BinaryOp::Mul, iterations, step);
      finalCounter = emitBinaryReg(BinaryOp::Add, counterValue.reg, distance);
    }
    emitStoreName(counter, finalCounter);
    IRInst end{IROp::Label};
    end.name = endLabel;
    emit(std::move(end));
    return true;
  }

  static void requireInt(Value value, const std::string& context) {
    if (value.type != Type::Int) fail(context + " requires an int value");
  }

  void lowerStmt(const Stmt& stmt, bool createScope = true) {
    std::visit([&](const auto& node) {
      using T = std::decay_t<decltype(node)>;
      if constexpr (std::is_same_v<T, Stmt::Block>) {
        if (createScope) pushScope();
        for (const auto& item : node.items) lowerStmt(*item);
        if (createScope) popScope();
      } else if constexpr (std::is_same_v<T, Stmt::Empty>) {
      } else if constexpr (std::is_same_v<T, Stmt::ExprStmt>) {
        lowerExpr(*node.expr);
      } else if constexpr (std::is_same_v<T, Stmt::DeclStmt>) {
        Symbol symbol;
        symbol.isConst = node.decl.isConst;
        if (node.decl.isConst) {
          symbol.kind = SymbolKind::Constant;
          symbol.value = evalConst(*node.decl.init);
          declare(node.decl.name, symbol);
        } else {
          Value init = lowerExpr(*node.decl.init);
          requireInt(init, "variable initializer");
          symbol.kind = SymbolKind::Local;
          symbol.slot = newLocal();
          declare(node.decl.name, symbol);
          IRInst store{IROp::StoreLocal}; store.left = symbol.slot; store.right = init.reg;
          emit(std::move(store));
          int32_t known = 0;
          if (knownValueExpr(*node.decl.init, known))
            knownLocalValues_[symbol.slot] = known;
          else
            knownLocalValues_[symbol.slot].reset();
        }
      } else if constexpr (std::is_same_v<T, Stmt::Assign>) {
        const Symbol* symbol = lookup(node.name);
        if (!symbol) fail("assignment to undeclared identifier '" + node.name + "'");
        if (symbol->isConst) fail("assignment to constant '" + node.name + "'");
        Value value = lowerExpr(*node.value);
        requireInt(value, "assignment");
        IRInst store{symbol->kind == SymbolKind::Local ? IROp::StoreLocal : IROp::StoreGlobal};
        store.left = symbol->slot;
        store.right = value.reg;
        store.name = node.name;
        emit(std::move(store));
        if (symbol->kind == SymbolKind::Local &&
            symbol->slot >= 0 && symbol->slot < static_cast<int>(knownLocalValues_.size())) {
          int32_t known = 0;
          if (knownValueExpr(*node.value, known))
            knownLocalValues_[symbol->slot] = known;
          else
            knownLocalValues_[symbol->slot].reset();
        }
      } else if constexpr (std::is_same_v<T, Stmt::If>) {
        Value condition = lowerExpr(*node.condition);
        requireInt(condition, "if condition");
        const std::string elseLabel = newLabel("else");
        const std::string endLabel = newLabel("if_end");
        IRInst branch{IROp::BranchZero}; branch.left = condition.reg;
        branch.name = node.elseBranch ? elseLabel : endLabel; emit(std::move(branch));
        lowerStmt(*node.thenBranch);
        if (node.elseBranch) {
          IRInst jump{IROp::Jump}; jump.name = endLabel; emit(std::move(jump));
          IRInst label{IROp::Label}; label.name = elseLabel; emit(std::move(label));
          lowerStmt(*node.elseBranch);
        }
        IRInst end{IROp::Label}; end.name = endLabel; emit(std::move(end));
        std::fill(knownLocalValues_.begin(), knownLocalValues_.end(), std::nullopt);
      } else if constexpr (std::is_same_v<T, Stmt::While>) {
        if (optimize_ &&
            (lowerFilteredAccumulationLoop(node) ||
             lowerCountedAccumulationLoop(node))) return;
        const std::string conditionLabel = newLabel("while_cond");
        const std::string endLabel = newLabel("while_end");
        IRInst begin{IROp::Label}; begin.name = conditionLabel; emit(std::move(begin));
        Value condition = lowerExpr(*node.condition);
        requireInt(condition, "while condition");
        IRInst branch{IROp::BranchZero}; branch.left = condition.reg; branch.name = endLabel;
        emit(std::move(branch));
        breakLabels_.push_back(endLabel);
        continueLabels_.push_back(conditionLabel);
        lowerStmt(*node.body);
        continueLabels_.pop_back();
        breakLabels_.pop_back();
        IRInst jump{IROp::Jump}; jump.name = conditionLabel; emit(std::move(jump));
        IRInst end{IROp::Label}; end.name = endLabel; emit(std::move(end));
        std::fill(knownLocalValues_.begin(), knownLocalValues_.end(), std::nullopt);
      } else if constexpr (std::is_same_v<T, Stmt::Break>) {
        if (breakLabels_.empty()) fail("break used outside a loop");
        IRInst jump{IROp::Jump}; jump.name = breakLabels_.back(); emit(std::move(jump));
      } else if constexpr (std::is_same_v<T, Stmt::Continue>) {
        if (continueLabels_.empty()) fail("continue used outside a loop");
        IRInst jump{IROp::Jump}; jump.name = continueLabels_.back(); emit(std::move(jump));
      } else if constexpr (std::is_same_v<T, Stmt::Return>) {
        IRInst ret{IROp::Return};
        if (node.value) {
          if (current_.returnType == Type::Void) fail("void function cannot return a value");
          Value value = lowerExpr(*node.value);
          requireInt(value, "return");
          ret.left = value.reg;
        } else if (current_.returnType == Type::Int) {
          fail("int function must return a value");
        }
        emit(std::move(ret));
      }
    }, stmt.node);
  }

  bool optimize_;
  IRProgram output_;
  std::unordered_map<std::string, Symbol> globals_;
  std::unordered_map<std::string, bool> globalNames_;
  std::unordered_map<std::string, FunctionSig> functions_;
  std::unordered_map<std::string, const Function*> functionBodies_;
  std::vector<std::unordered_map<std::string, Symbol>> scopes_;
  std::vector<std::optional<int32_t>> knownLocalValues_;
  std::vector<std::string> breakLabels_, continueLabels_;
  IRFunction current_;
  int nextReg_ = 0;
  int nextLabel_ = 0;
};

class RiscVEmitter {
 public:
  explicit RiscVEmitter(const IRProgram& program) : program_(program) {}

  void emit(std::ostream& out) {
    out_ = &out;
    if (!program_.globals.empty()) {
      line("  .data");
      line("  .align 2");
      for (const auto& global : program_.globals) {
        line("  .globl " + global.name);
        line("  .type " + global.name + ", @object");
        line("  .size " + global.name + ", 4");
        line(global.name + ":");
        line("  .word " + std::to_string(global.initialValue));
      }
    }
    line("  .text");
    for (const auto& function : program_.functions) emitFunction(function);
    line("  .section .note.GNU-stack,\"\",@progbits");
  }

 private:
  struct Allocation {
    std::vector<int> localRegisters;
    std::vector<int> valueRegisters;
    std::vector<int> valueLocalAliases;
    std::vector<int> spillSlots;
    int savedRegisterCount = 0;
    int spillCount = 0;
    bool savesReturnAddress = true;
  };

  static int align16(int value) { return (value + 15) & ~15; }
  static bool fitsImmediate12(int value) { return value >= -2048 && value <= 2047; }
  void line(const std::string& text) { *out_ << text << '\n'; }

  static std::string savedRegister(int index) {
    return "s" + std::to_string(index + 1);
  }
  static int savedPhysicalCount() { return 11; }
  static int temporaryPhysicalCount() { return 3; }
  static int argumentPhysicalCount() { return 8; }
  static int argumentPhysicalStart() {
    return savedPhysicalCount() + temporaryPhysicalCount();
  }
  static std::string physicalRegister(int index) {
    if (index < savedPhysicalCount()) return savedRegister(index);
    if (index < argumentPhysicalStart())
      return "t" + std::to_string(index - savedPhysicalCount() + 3);
    return "a" + std::to_string(index - argumentPhysicalStart());
  }

  static void markUse(std::vector<int>& first, std::vector<int>& last,
                      int reg, int position) {
    if (reg < 0) return;
    first[reg] = std::min(first[reg], position);
    last[reg] = std::max(last[reg], position);
  }

  Allocation allocateRegisters(const IRFunction& function) const {
    Allocation allocation;
    allocation.localRegisters.assign(function.localCount, -1);
    allocation.valueRegisters.assign(function.registerCount, -1);
    allocation.valueLocalAliases.assign(function.registerCount, -1);
    allocation.spillSlots.assign(function.registerCount, -1);

    std::vector<int> localAccesses(function.localCount, 0);
    for (const auto& inst : function.code) {
      if ((inst.op == IROp::LoadLocal || inst.op == IROp::StoreLocal) &&
          inst.left >= 0) {
        ++localAccesses[inst.left];
      }
      if (inst.op == IROp::LoadLocal && inst.dst >= 0)
        allocation.valueLocalAliases[inst.dst] = inst.left;
    }
    std::vector<int> locals(function.localCount);
    for (int i = 0; i < function.localCount; ++i) locals[i] = i;
    std::stable_sort(locals.begin(), locals.end(), [&](int left, int right) {
      return localAccesses[left] > localAccesses[right];
    });
    bool hasCall = false;
    for (const auto& inst : function.code) {
      if (inst.op == IROp::Call) {
        hasCall = true;
        break;
      }
    }
    const int physicalCount = savedPhysicalCount() + temporaryPhysicalCount() +
                              (hasCall ? 0 : argumentPhysicalCount());
    allocation.savesReturnAddress = hasCall;
    std::vector<int> localPool;
    if (!hasCall) {
      const int directParams =
          std::min<int>(function.params.size(), argumentPhysicalCount());
      for (int param = 0; param < directParams; ++param)
        allocation.localRegisters[param] = argumentPhysicalStart() + param;
    }
    if (!hasCall) {
      for (int physical = savedPhysicalCount(); physical < physicalCount; ++physical)
        localPool.push_back(physical);
    }
    for (int physical = 0; physical < savedPhysicalCount(); ++physical)
      localPool.push_back(physical);
    const int valueReserve = hasCall ? 4 : 5;
    const int localRegisterCount = std::min(
        function.localCount,
        std::max(0, static_cast<int>(localPool.size()) - valueReserve));
    int assignedLocals = 0;
    for (int physical : allocation.localRegisters)
      if (physical >= 0) ++assignedLocals;
    for (int local : locals) {
      if (assignedLocals >= localRegisterCount) break;
      if (allocation.localRegisters[local] >= 0) continue;
      int physical = -1;
      while (!localPool.empty()) {
        physical = localPool.front();
        localPool.erase(localPool.begin());
        bool alreadyUsed = false;
        for (int assigned : allocation.localRegisters) {
          if (assigned == physical) {
            alreadyUsed = true;
            break;
          }
        }
        if (!alreadyUsed) break;
        physical = -1;
      }
      if (physical < 0) break;
      allocation.localRegisters[local] = physical;
      ++assignedLocals;
    }

    const int infinity = std::numeric_limits<int>::max();
    std::vector<int> first(function.registerCount, infinity);
    std::vector<int> last(function.registerCount, -1);
    for (size_t i = 0; i < function.code.size(); ++i) {
      const auto& inst = function.code[i];
      const int position = static_cast<int>(i);
      if (inst.dst >= 0) markUse(first, last, inst.dst, position);
      switch (inst.op) {
        case IROp::StoreLocal:
        case IROp::StoreGlobal:
          markUse(first, last, inst.right, position);
          break;
        case IROp::Unary:
        case IROp::BranchZero:
        case IROp::Return:
          markUse(first, last, inst.left, position);
          break;
        case IROp::Binary:
          markUse(first, last, inst.left, position);
          markUse(first, last, inst.right, position);
          break;
        case IROp::Call:
          for (int arg : inst.args) markUse(first, last, arg, position);
          break;
        default:
          break;
      }
    }

    std::vector<int> useCounts(function.registerCount, 0);
    std::vector<int> definitionPositions(function.registerCount, -1);
    for (size_t i = 0; i < function.code.size(); ++i) {
      const auto& inst = function.code[i];
      if (inst.dst >= 0) definitionPositions[inst.dst] = static_cast<int>(i);
      auto countUse = [&](int reg) {
        if (reg >= 0) ++useCounts[reg];
      };
      switch (inst.op) {
        case IROp::StoreLocal:
        case IROp::StoreGlobal:
          countUse(inst.right);
          break;
        case IROp::Unary:
        case IROp::BranchZero:
        case IROp::Return:
          countUse(inst.left);
          break;
        case IROp::Binary:
          countUse(inst.left);
          countUse(inst.right);
          break;
        case IROp::Call:
          for (int arg : inst.args) countUse(arg);
          break;
        default:
          break;
      }
    }
    for (size_t i = 1; i < function.code.size(); ++i) {
      const auto& store = function.code[i];
      if (store.op != IROp::StoreLocal || store.right < 0 ||
          useCounts[store.right] != 1 ||
          definitionPositions[store.right] != static_cast<int>(i - 1))
        continue;
      const int localPhysical = allocation.localRegisters[store.left];
      if (localPhysical >= 0)
        allocation.valueRegisters[store.right] = localPhysical;
    }

    std::unordered_map<std::string, int> labelPositions;
    for (size_t i = 0; i < function.code.size(); ++i) {
      if (function.code[i].op == IROp::Label)
        labelPositions[function.code[i].name] = static_cast<int>(i);
    }
    for (size_t i = 0; i < function.code.size(); ++i) {
      const auto& inst = function.code[i];
      if (inst.op != IROp::Jump && inst.op != IROp::BranchZero) continue;
      const auto target = labelPositions.find(inst.name);
      if (target == labelPositions.end() ||
          target->second >= static_cast<int>(i))
        continue;
      for (int reg = 0; reg < function.registerCount; ++reg) {
        if (first[reg] < target->second && last[reg] >= target->second &&
            last[reg] < static_cast<int>(i)) {
          last[reg] = static_cast<int>(i);
        }
      }
    }

    std::vector<int> callPositions;
    if (hasCall) {
      for (size_t i = 0; i < function.code.size(); ++i)
        if (function.code[i].op == IROp::Call)
          callPositions.push_back(static_cast<int>(i));
    }
    auto crossesCall = [&](int reg) {
      if (!hasCall) return false;
      for (int call : callPositions) {
        if (first[reg] < call && last[reg] > call) return true;
      }
      return false;
    };

    std::vector<int> values;
    for (int reg = 0; reg < function.registerCount; ++reg) {
      if (first[reg] != infinity && allocation.valueLocalAliases[reg] < 0)
        if (allocation.valueRegisters[reg] < 0) values.push_back(reg);
    }
    std::stable_sort(values.begin(), values.end(), [&](int left, int right) {
      return first[left] < first[right];
    });

    struct Active { int value; int physical; };
    std::vector<Active> active;
    std::vector<bool> used(physicalCount, false);
    for (int physical : allocation.localRegisters)
      if (physical >= 0) used[physical] = true;
    for (int value : values) {
      for (auto it = active.begin(); it != active.end();) {
        if (last[it->value] < first[value]) {
          used[it->physical] = false;
          it = active.erase(it);
        } else {
          ++it;
        }
      }
      int physical = -1;
      std::vector<int> candidates;
      const bool crosses = crossesCall(value);
      if (!crosses) {
        for (int candidate = savedPhysicalCount(); candidate < argumentPhysicalStart() &&
                                           candidate < physicalCount; ++candidate)
          candidates.push_back(candidate);
        for (int candidate = argumentPhysicalStart(); candidate < physicalCount; ++candidate)
          candidates.push_back(candidate);
      }
      for (int candidate = 0; candidate < savedPhysicalCount(); ++candidate)
        candidates.push_back(candidate);
      for (int candidate : candidates) {
        if (crosses && candidate >= savedPhysicalCount()) continue;
        if (!used[candidate]) {
          physical = candidate;
          break;
        }
      }
      if (physical >= 0) {
        allocation.valueRegisters[value] = physical;
        used[physical] = true;
        active.push_back({value, physical});
      } else {
        int spillIndex = -1;
        int farthestEnd = last[value];
        for (size_t i = 0; i < active.size(); ++i) {
          if (crosses && active[i].physical >= savedPhysicalCount()) continue;
          const int activeEnd = last[active[i].value];
          if (activeEnd > farthestEnd) {
            farthestEnd = activeEnd;
            spillIndex = static_cast<int>(i);
          }
        }
        if (spillIndex >= 0) {
          Active spilled = active[spillIndex];
          allocation.valueRegisters[spilled.value] = -1;
          allocation.spillSlots[spilled.value] = allocation.spillCount++;
          allocation.valueRegisters[value] = spilled.physical;
          active[spillIndex] = {value, spilled.physical};
        } else {
          allocation.spillSlots[value] = allocation.spillCount++;
        }
      }
    }
    allocation.savedRegisterCount = 0;
    for (int physical : allocation.localRegisters) {
      if (physical >= 0 && physical < savedPhysicalCount())
        allocation.savedRegisterCount = std::max(allocation.savedRegisterCount, physical + 1);
    }
    for (int physical : allocation.valueRegisters) {
      if (physical >= 0 && physical < savedPhysicalCount())
        allocation.savedRegisterCount = std::max(allocation.savedRegisterCount, physical + 1);
    }
    return allocation;
  }

  int slotOffset(int index) const {
    return -12 - allocation_->savedRegisterCount * 4 - index * 4;
  }
  void addressFrom(const std::string& dst, const std::string& base, int offset) {
    line("  li " + dst + ", " + std::to_string(offset));
    line("  add " + dst + ", " + base + ", " + dst);
  }
  void loadAt(const std::string& dst, const std::string& base, int offset) {
    if (fitsImmediate12(offset)) {
      line("  lw " + dst + ", " + std::to_string(offset) + "(" + base + ")");
    } else {
      addressFrom("t6", base, offset);
      line("  lw " + dst + ", 0(t6)");
    }
  }
  void storeAt(const std::string& src, const std::string& base, int offset) {
    if (fitsImmediate12(offset)) {
      line("  sw " + src + ", " + std::to_string(offset) + "(" + base + ")");
    } else {
      addressFrom("t6", base, offset);
      line("  sw " + src + ", 0(t6)");
    }
  }
  void loadReg(const IRFunction& function, int reg, const std::string& dst) {
    const int localAlias = allocation_->valueLocalAliases[reg];
    if (localAlias >= 0) {
      loadLocal(localAlias, dst);
      return;
    }
    const int physical = allocation_->valueRegisters[reg];
    if (physical >= 0) {
      const std::string source = physicalRegister(physical);
      if (source != dst) line("  mv " + dst + ", " + source);
      return;
    }
    const int slot = function.localCount + allocation_->spillSlots[reg];
    loadAt(dst, "s0", slotOffset(slot));
  }
  void storeReg(const IRFunction& function, int reg, const std::string& src) {
    const int physical = allocation_->valueRegisters[reg];
    if (physical >= 0) {
      const std::string destination = physicalRegister(physical);
      if (destination != src) line("  mv " + destination + ", " + src);
      return;
    }
    const int slot = function.localCount + allocation_->spillSlots[reg];
    storeAt(src, "s0", slotOffset(slot));
  }

  std::string valueOperand(const IRFunction& function, int reg,
                           const std::string& temporary) {
    const int localAlias = allocation_->valueLocalAliases[reg];
    if (localAlias >= 0) {
      const int physical = allocation_->localRegisters[localAlias];
      if (physical >= 0) return physicalRegister(physical);
      loadAt(temporary, "s0", slotOffset(localAlias));
      return temporary;
    }
    const int physical = allocation_->valueRegisters[reg];
    if (physical >= 0) return physicalRegister(physical);
    const int slot = function.localCount + allocation_->spillSlots[reg];
    loadAt(temporary, "s0", slotOffset(slot));
    return temporary;
  }

  std::string valueDestination(int reg, const std::string& temporary) const {
    const int physical = allocation_->valueRegisters[reg];
    return physical >= 0 ? physicalRegister(physical) : temporary;
  }

  void finishValue(const IRFunction& function, int reg,
                   const std::string& producedIn) {
    if (allocation_->valueRegisters[reg] >= 0) return;
    const int slot = function.localCount + allocation_->spillSlots[reg];
    storeAt(producedIn, "s0", slotOffset(slot));
  }
  void loadLocal(int slot, const std::string& dst) {
    const int physical = allocation_->localRegisters[slot];
    if (physical >= 0) {
      const std::string source = physicalRegister(physical);
      if (source != dst) line("  mv " + dst + ", " + source);
      return;
    }
    loadAt(dst, "s0", slotOffset(slot));
  }
  void storeLocal(int slot, const std::string& src) {
    const int physical = allocation_->localRegisters[slot];
    if (physical >= 0) {
      const std::string destination = physicalRegister(physical);
      if (destination != src) line("  mv " + destination + ", " + src);
      return;
    }
    storeAt(src, "s0", slotOffset(slot));
  }

  static std::optional<int32_t> constantValue(const IRFunction& function, int reg) {
    std::optional<int32_t> result;
    for (const auto& inst : function.code) {
      if (inst.dst == reg) {
        if (result || inst.op != IROp::Imm) return std::nullopt;
        result = inst.imm;
      }
    }
    return result;
  }

  static std::optional<int> positivePowerOfTwoShift(int32_t value) {
    if (value <= 0) return std::nullopt;
    const uint32_t bits = static_cast<uint32_t>(value);
    if ((bits & (bits - 1)) != 0) return std::nullopt;
    int shift = 0;
    while ((bits >> shift) != 1) ++shift;
    return shift;
  }

  static std::optional<int> constantDivisorShift(int32_t value) {
    if (value == 0) return std::nullopt;
    const int64_t absolute = value < 0 ? -static_cast<int64_t>(value) : value;
    if (absolute > std::numeric_limits<int32_t>::max()) return std::nullopt;
    return positivePowerOfTwoShift(static_cast<int32_t>(absolute));
  }

  bool emitMultiplyByConstant(const std::string& source, int32_t constant,
                              const std::string& destination) {
    if (constant == 0) {
      line("  li " + destination + ", 0");
      return true;
    }
    if (constant == 1) {
      if (destination != source) line("  mv " + destination + ", " + source);
      return true;
    }
    if (constant == -1) {
      line("  neg " + destination + ", " + source);
      return true;
    }

    const int64_t absolute = constant < 0 ? -static_cast<int64_t>(constant) : constant;
    if (absolute > std::numeric_limits<int32_t>::max()) return false;
    std::vector<int> shifts;
    uint32_t bits = static_cast<uint32_t>(absolute);
    for (int shift = 0; bits != 0; ++shift, bits >>= 1) {
      if ((bits & 1U) != 0) shifts.push_back(shift);
    }
    if (shifts.empty() || shifts.size() > 3) return false;
    if (destination == source && shifts.size() > 1) return false;

    bool initialized = false;
    for (const int shift : shifts) {
      if (!initialized) {
        if (shift == 0) {
          if (destination != source) line("  mv " + destination + ", " + source);
        } else {
          line("  slli " + destination + ", " + source + ", " + std::to_string(shift));
        }
        initialized = true;
      } else {
        if (shift == 0) {
          line("  add " + destination + ", " + destination + ", " + source);
        } else {
          line("  slli t6, " + source + ", " + std::to_string(shift));
          line("  add " + destination + ", " + destination + ", t6");
        }
      }
    }
    if (constant < 0) line("  neg " + destination + ", " + destination);
    return true;
  }

  void emitFunction(const IRFunction& function) {
    const Allocation allocation = allocateRegisters(function);
    allocation_ = &allocation;
    const int valueSlots = function.localCount + allocation.spillCount;
    const int outgoingBytes = std::max(0, function.maxCallArgs - 8) * 4;
    const int savedBytes = allocation.savedRegisterCount * 4;
    const int frameSize = align16(8 + savedBytes + valueSlots * 4 + outgoingBytes);
    const std::string epilogue = ".L" + function.name + "_return";

    line("");
    line("  .globl " + function.name);
    line("  .type " + function.name + ", @function");
    line(function.name + ":");
    if (fitsImmediate12(-frameSize)) {
      line("  addi sp, sp, -" + std::to_string(frameSize));
      if (allocation.savesReturnAddress)
        line("  sw ra, " + std::to_string(frameSize - 4) + "(sp)");
      line("  sw s0, " + std::to_string(frameSize - 8) + "(sp)");
      line("  addi s0, sp, " + std::to_string(frameSize));
      for (int i = 0; i < allocation.savedRegisterCount; ++i)
        line("  sw " + savedRegister(i) + ", " +
             std::to_string(frameSize - 12 - i * 4) + "(sp)");
    } else {
      line("  li t0, " + std::to_string(frameSize));
      line("  sub sp, sp, t0");
      line("  add t6, sp, t0");
      if (allocation.savesReturnAddress) line("  sw ra, -4(t6)");
      line("  sw s0, -8(t6)");
      line("  mv s0, t6");
      for (int i = 0; i < allocation.savedRegisterCount; ++i)
        line("  sw " + savedRegister(i) + ", " +
             std::to_string(-12 - i * 4) + "(s0)");
    }
    for (size_t i = 0; i < function.params.size(); ++i) {
      if (i < 8) {
        storeLocal(static_cast<int>(i), "a" + std::to_string(i));
      } else {
        loadAt("t0", "s0", static_cast<int>(i - 8) * 4);
        storeLocal(static_cast<int>(i), "t0");
      }
    }

    std::vector<int> useCounts(function.registerCount, 0);
    for (const auto& candidate : function.code) {
      auto countUse = [&](int reg) {
        if (reg >= 0) ++useCounts[reg];
      };
      switch (candidate.op) {
        case IROp::StoreLocal:
        case IROp::StoreGlobal: countUse(candidate.right); break;
        case IROp::Unary:
        case IROp::BranchZero:
        case IROp::Return: countUse(candidate.left); break;
        case IROp::Binary:
          countUse(candidate.left);
          countUse(candidate.right);
          break;
        case IROp::Call:
          for (int arg : candidate.args) countUse(arg);
          break;
        default: break;
      }
    }
    for (size_t pc = 0; pc < function.code.size(); ++pc) {
      const auto& inst = function.code[pc];
      if (inst.op == IROp::Binary && pc + 1 < function.code.size() &&
          function.code[pc + 1].op == IROp::BranchZero &&
          function.code[pc + 1].left == inst.dst &&
          useCounts[inst.dst] == 1) {
        const std::string left = valueOperand(function, inst.left, "t0");
        const std::string right = valueOperand(function, inst.right, "t1");
        const auto& branch = function.code[pc + 1];
        const std::string& target = branch.name;
        if (branch.imm != 0) {
          switch (inst.binary) {
            case BinaryOp::Lt: line("  blt " + left + ", " + right + ", " + target); break;
            case BinaryOp::Gt: line("  blt " + right + ", " + left + ", " + target); break;
            case BinaryOp::Le: line("  bge " + right + ", " + left + ", " + target); break;
            case BinaryOp::Ge: line("  bge " + left + ", " + right + ", " + target); break;
            case BinaryOp::Eq: line("  beq " + left + ", " + right + ", " + target); break;
            case BinaryOp::Ne: line("  bne " + left + ", " + right + ", " + target); break;
            default:
              emitBinary(function, inst);
              line("  bnez " + valueOperand(function, inst.dst, "t0") + ", " + target);
              break;
          }
        } else switch (inst.binary) {
          case BinaryOp::Lt: line("  bge " + left + ", " + right + ", " + target); break;
          case BinaryOp::Gt: line("  bge " + right + ", " + left + ", " + target); break;
          case BinaryOp::Le: line("  blt " + right + ", " + left + ", " + target); break;
          case BinaryOp::Ge: line("  blt " + left + ", " + right + ", " + target); break;
          case BinaryOp::Eq: line("  bne " + left + ", " + right + ", " + target); break;
          case BinaryOp::Ne: line("  beq " + left + ", " + right + ", " + target); break;
          default:
            emitBinary(function, inst);
            line("  beqz " + valueOperand(function, inst.dst, "t0") + ", " + target);
            break;
        }
        ++pc;
        continue;
      }
      switch (inst.op) {
        case IROp::Imm:
          {
            const std::string dst = valueDestination(inst.dst, "t0");
            line("  li " + dst + ", " + std::to_string(inst.imm));
            finishValue(function, inst.dst, dst);
          }
          break;
        case IROp::LoadLocal:
          // The virtual value aliases the local's storage until its only use.
          break;
        case IROp::StoreLocal:
          storeLocal(inst.left, valueOperand(function, inst.right, "t0"));
          break;
        case IROp::LoadGlobal:
          {
            const std::string dst = valueDestination(inst.dst, "t0");
            line("  la t1, " + inst.name);
            line("  lw " + dst + ", 0(t1)");
            finishValue(function, inst.dst, dst);
          }
          break;
        case IROp::StoreGlobal:
          {
            const std::string value = valueOperand(function, inst.right, "t0");
            line("  la t1, " + inst.name);
            line("  sw " + value + ", 0(t1)");
          }
          break;
        case IROp::Unary:
          {
            const std::string source = valueOperand(function, inst.left, "t0");
            const std::string dst = valueDestination(inst.dst, "t1");
            if (inst.unary == UnaryOp::Minus) line("  neg " + dst + ", " + source);
            else if (inst.unary == UnaryOp::Not) line("  seqz " + dst + ", " + source);
            else if (dst != source) line("  mv " + dst + ", " + source);
            finishValue(function, inst.dst, dst);
          }
          break;
        case IROp::Binary:
          emitBinary(function, inst);
          break;
        case IROp::Label:
          line(inst.name + ":");
          break;
        case IROp::Jump:
          line("  j " + inst.name);
          break;
        case IROp::BranchZero:
          if (const auto constant = constantValue(function, inst.left)) {
            const bool takeBranch = inst.imm != 0 ? *constant != 0 : *constant == 0;
            if (takeBranch) line("  j " + inst.name);
          } else {
            line("  b" + std::string(inst.imm != 0 ? "nez " : "eqz ") +
                 valueOperand(function, inst.left, "t0") + ", " + inst.name);
          }
          break;
        case IROp::Call:
          emitCall(function, inst);
          break;
        case IROp::Return:
          if (inst.left >= 0) {
            const std::string value = valueOperand(function, inst.left, "t0");
            if (value != "a0") line("  mv a0, " + value);
          }
          line("  j " + epilogue);
          break;
      }
    }
    line(epilogue + ":");
    for (int i = 0; i < allocation.savedRegisterCount; ++i)
      loadAt(savedRegister(i), "s0", -12 - i * 4);
    if (allocation.savesReturnAddress) line("  lw ra, -4(s0)");
    line("  lw t0, -8(s0)");
    line("  mv sp, s0");
    line("  mv s0, t0");
    line("  ret");
    line("  .size " + function.name + ", .-" + function.name);
    allocation_ = nullptr;
  }

  void emitBinary(const IRFunction& function, const IRInst& inst) {
    const std::optional<int32_t> leftConst = constantValue(function, inst.left);
    const std::optional<int32_t> rightConst = constantValue(function, inst.right);
    auto leftOperand = [&]() { return valueOperand(function, inst.left, "t0"); };
    auto rightOperand = [&]() { return valueOperand(function, inst.right, "t1"); };
    std::string producedIn;
    auto destination = [&]() {
      producedIn = valueDestination(inst.dst, "t2");
      return producedIn;
    };
    switch (inst.binary) {
      case BinaryOp::Add:
        if (rightConst && fitsImmediate12(*rightConst)) {
          const std::string left = leftOperand();
          const std::string dst = destination();
          line("  addi " + dst + ", " + left + ", " + std::to_string(*rightConst));
        } else if (leftConst && fitsImmediate12(*leftConst)) {
          const std::string right = rightOperand();
          const std::string dst = destination();
          line("  addi " + dst + ", " + right + ", " + std::to_string(*leftConst));
        } else {
          const std::string left = leftOperand();
          const std::string right = rightOperand();
          const std::string dst = destination();
          line("  add " + dst + ", " + left + ", " + right);
        }
        break;
      case BinaryOp::Sub:
        if (rightConst) {
          const int64_t negated = -static_cast<int64_t>(*rightConst);
          if (negated >= -2048 && negated <= 2047) {
            const std::string left = leftOperand();
            const std::string dst = destination();
            line("  addi " + dst + ", " + left + ", " + std::to_string(negated));
          } else if (leftConst && *leftConst == 0) {
            const std::string right = rightOperand();
            const std::string dst = destination();
            line("  neg " + dst + ", " + right);
          } else {
            const std::string left = leftOperand();
            const std::string right = rightOperand();
            const std::string dst = destination();
            line("  sub " + dst + ", " + left + ", " + right);
          }
        } else if (leftConst && *leftConst == 0) {
          const std::string right = rightOperand();
          const std::string dst = destination();
          line("  neg " + dst + ", " + right);
        } else {
          const std::string left = leftOperand();
          const std::string right = rightOperand();
          const std::string dst = destination();
          line("  sub " + dst + ", " + left + ", " + right);
        }
        break;
      case BinaryOp::Mul:
        if (rightConst) {
          const std::string left = leftOperand();
          const std::string dst = destination();
          if (!emitMultiplyByConstant(left, *rightConst, dst)) {
            const std::string left = leftOperand();
            const std::string right = rightOperand();
            const std::string dst = destination();
            line("  mul " + dst + ", " + left + ", " + right);
          }
        } else if (leftConst) {
          const std::string right = rightOperand();
          const std::string dst = destination();
          if (!emitMultiplyByConstant(right, *leftConst, dst)) {
            const std::string left = leftOperand();
            const std::string right = rightOperand();
            const std::string dst = destination();
            line("  mul " + dst + ", " + left + ", " + right);
          }
        } else {
          const std::string left = leftOperand();
          const std::string right = rightOperand();
          const std::string dst = destination();
          line("  mul " + dst + ", " + left + ", " + right);
        }
        break;
      case BinaryOp::Div:
        if (rightConst && *rightConst != 0) {
          const int32_t divisor = *rightConst;
          const auto shift = constantDivisorShift(divisor);
          if (shift) {
            const std::string left = leftOperand();
            const std::string dst = destination();
            if (*shift == 0) {
              if (dst != left) line("  mv " + dst + ", " + left);
            } else {
              line("  srai t6, " + left + ", 31");
              line("  srli t6, t6, " + std::to_string(32 - *shift));
              line("  add " + dst + ", " + left + ", t6");
              line("  srai " + dst + ", " + dst + ", " + std::to_string(*shift));
            }
            if (divisor < 0) line("  neg " + dst + ", " + dst);
          } else {
            const std::string left = leftOperand();
            const std::string right = rightOperand();
            const std::string dst = destination();
            line("  div " + dst + ", " + left + ", " + right);
          }
        } else {
          const std::string left = leftOperand();
          const std::string right = rightOperand();
          const std::string dst = destination();
          line("  div " + dst + ", " + left + ", " + right);
        }
        break;
      case BinaryOp::Mod:
        if (rightConst && *rightConst != 0) {
          const int32_t divisor = *rightConst;
          const auto shift = constantDivisorShift(divisor);
          if (shift) {
            const std::string left = leftOperand();
            const std::string dst = destination();
            if (*shift == 0) {
              line("  li " + dst + ", 0");
            } else {
              line("  srai t6, " + left + ", 31");
              line("  srli t6, t6, " + std::to_string(32 - *shift));
              line("  add t6, " + left + ", t6");
              line("  srai t6, t6, " + std::to_string(*shift));
              line("  slli t6, t6, " + std::to_string(*shift));
              line("  sub " + dst + ", " + left + ", t6");
            }
          } else {
            const std::string left = leftOperand();
            const std::string right = rightOperand();
            const std::string dst = destination();
            line("  rem " + dst + ", " + left + ", " + right);
          }
        } else {
          const std::string left = leftOperand();
          const std::string right = rightOperand();
          const std::string dst = destination();
          line("  rem " + dst + ", " + left + ", " + right);
        }
        break;
      case BinaryOp::Lt:
        if (rightConst && fitsImmediate12(*rightConst)) {
          const std::string left = leftOperand();
          const std::string dst = destination();
          line("  slti " + dst + ", " + left + ", " + std::to_string(*rightConst));
        } else {
          const std::string left = leftOperand();
          const std::string right = rightOperand();
          const std::string dst = destination();
          line("  slt " + dst + ", " + left + ", " + right);
        }
        break;
      case BinaryOp::Gt:
        {
          const std::string left = leftOperand();
          const std::string right = rightOperand();
          const std::string dst = destination();
          line("  slt " + dst + ", " + right + ", " + left);
        }
        break;
      case BinaryOp::Le:
        {
          const std::string left = leftOperand();
          const std::string right = rightOperand();
          const std::string dst = destination();
          line("  slt " + dst + ", " + right + ", " + left);
          line("  xori " + dst + ", " + dst + ", 1");
        }
        break;
      case BinaryOp::Ge:
        if (rightConst && fitsImmediate12(*rightConst)) {
          const std::string left = leftOperand();
          const std::string dst = destination();
          line("  slti " + dst + ", " + left + ", " + std::to_string(*rightConst));
          line("  xori " + dst + ", " + dst + ", 1");
        } else {
          const std::string left = leftOperand();
          const std::string right = rightOperand();
          const std::string dst = destination();
          line("  slt " + dst + ", " + left + ", " + right);
          line("  xori " + dst + ", " + dst + ", 1");
        }
        break;
      case BinaryOp::Eq:
        if (rightConst && *rightConst == 0) {
          const std::string left = leftOperand();
          const std::string dst = destination();
          line("  seqz " + dst + ", " + left);
        } else if (leftConst && *leftConst == 0) {
          const std::string right = rightOperand();
          const std::string dst = destination();
          line("  seqz " + dst + ", " + right);
        } else {
          const std::string left = leftOperand();
          const std::string right = rightOperand();
          const std::string dst = destination();
          line("  xor " + dst + ", " + left + ", " + right);
          line("  seqz " + dst + ", " + dst);
        }
        break;
      case BinaryOp::Ne:
        if (rightConst && *rightConst == 0) {
          const std::string left = leftOperand();
          const std::string dst = destination();
          line("  snez " + dst + ", " + left);
        } else if (leftConst && *leftConst == 0) {
          const std::string right = rightOperand();
          const std::string dst = destination();
          line("  snez " + dst + ", " + right);
        } else {
          const std::string left = leftOperand();
          const std::string right = rightOperand();
          const std::string dst = destination();
          line("  xor " + dst + ", " + left + ", " + right);
          line("  snez " + dst + ", " + dst);
        }
        break;
      case BinaryOp::And:
      case BinaryOp::Or:
        throw Error("internal error: logical operation was not lowered");
    }
    finishValue(function, inst.dst, producedIn);
  }

  void emitCall(const IRFunction& function, const IRInst& inst) {
    for (size_t i = 0; i < inst.args.size(); ++i) {
      const std::string value = valueOperand(function, inst.args[i], "t0");
      if (i < 8) {
        const std::string arg = "a" + std::to_string(i);
        if (value != arg) line("  mv " + arg + ", " + value);
      } else {
        storeAt(value, "sp", static_cast<int>(i - 8) * 4);
      }
    }
    line("  call " + inst.name);
    if (inst.dst >= 0) {
      const std::string dst = valueDestination(inst.dst, "t0");
      if (dst != "a0") line("  mv " + dst + ", a0");
      finishValue(function, inst.dst, dst);
    }
  }

  const IRProgram& program_;
  std::ostream* out_ = nullptr;
  const Allocation* allocation_ = nullptr;
};

}  // namespace toyc

int main(int argc, char** argv) {
  try {
    bool optimize = false;
    for (int i = 1; i < argc; ++i) {
      if (std::string(argv[i]) == "-opt") optimize = true;
    }
    (void)optimize;
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    auto tokens = toyc::Lexer(buffer.str()).scan();
    auto program = toyc::Parser(std::move(tokens)).parseProgram();
    auto ir = toyc::Lowerer(optimize).lower(program);
    toyc::RiscVEmitter(ir).emit(std::cout);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "ToyC compiler error: " << e.what() << '\n';
    return 1;
  }
}
