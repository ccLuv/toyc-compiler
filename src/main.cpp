#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
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
        globalNames_[function.name] = true;
        lowerFunction(function);
      }
    }
    const auto main = functions_.find("main");
    if (main == functions_.end() || main->second.type != Type::Int || main->second.arity != 0)
      fail("program must define int main() with no parameters");
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
    if (optimize_) optimizeCurrentFunction();
    current_.registerCount = nextReg_;
    output_.functions.push_back(std::move(current_));
    nextReg_ = 0;
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
  int newLocal() { return current_.localCount++; }
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
          constants.erase(inst.dst);
        }
      } else if (inst.op == IROp::LoadLocal || inst.op == IROp::LoadGlobal ||
                 inst.op == IROp::Call) {
        if (inst.dst >= 0) constants.erase(inst.dst);
      } else if (inst.op == IROp::Label || inst.op == IROp::Jump ||
                 inst.op == IROp::BranchZero) {
        constants.clear();
      }
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
      } else if constexpr (std::is_same_v<T, Stmt::While>) {
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
  std::vector<std::unordered_map<std::string, Symbol>> scopes_;
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
  static int align16(int value) { return (value + 15) & ~15; }
  static bool fitsImmediate12(int value) { return value >= -2048 && value <= 2047; }
  void line(const std::string& text) { *out_ << text << '\n'; }

  int slotOffset(int index) const { return -12 - index * 4; }
  int regSlot(const IRFunction& function, int reg) const {
    return function.localCount + reg;
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
    loadAt(dst, "s0", slotOffset(regSlot(function, reg)));
  }
  void storeReg(const IRFunction& function, int reg, const std::string& src) {
    storeAt(src, "s0", slotOffset(regSlot(function, reg)));
  }
  void loadLocal(int slot, const std::string& dst) {
    loadAt(dst, "s0", slotOffset(slot));
  }
  void storeLocal(int slot, const std::string& src) {
    storeAt(src, "s0", slotOffset(slot));
  }

  void emitFunction(const IRFunction& function) {
    const int valueSlots = function.localCount + function.registerCount;
    const int outgoingBytes = std::max(0, function.maxCallArgs - 8) * 4;
    const int frameSize = align16(8 + valueSlots * 4 + outgoingBytes);
    const std::string epilogue = ".L" + function.name + "_return";

    line("");
    line("  .globl " + function.name);
    line("  .type " + function.name + ", @function");
    line(function.name + ":");
    if (fitsImmediate12(-frameSize)) {
      line("  addi sp, sp, -" + std::to_string(frameSize));
      line("  sw ra, " + std::to_string(frameSize - 4) + "(sp)");
      line("  sw s0, " + std::to_string(frameSize - 8) + "(sp)");
      line("  addi s0, sp, " + std::to_string(frameSize));
    } else {
      line("  li t0, " + std::to_string(frameSize));
      line("  sub sp, sp, t0");
      line("  add t6, sp, t0");
      line("  sw ra, -4(t6)");
      line("  sw s0, -8(t6)");
      line("  mv s0, t6");
    }
    for (size_t i = 0; i < function.params.size(); ++i) {
      if (i < 8) {
        storeLocal(static_cast<int>(i), "a" + std::to_string(i));
      } else {
        loadAt("t0", "s0", static_cast<int>(i - 8) * 4);
        storeLocal(static_cast<int>(i), "t0");
      }
    }

    for (const auto& inst : function.code) {
      switch (inst.op) {
        case IROp::Imm:
          line("  li t0, " + std::to_string(inst.imm));
          storeReg(function, inst.dst, "t0");
          break;
        case IROp::LoadLocal:
          loadLocal(inst.left, "t0");
          storeReg(function, inst.dst, "t0");
          break;
        case IROp::StoreLocal:
          loadReg(function, inst.right, "t0");
          storeLocal(inst.left, "t0");
          break;
        case IROp::LoadGlobal:
          line("  la t1, " + inst.name);
          line("  lw t0, 0(t1)");
          storeReg(function, inst.dst, "t0");
          break;
        case IROp::StoreGlobal:
          loadReg(function, inst.right, "t0");
          line("  la t1, " + inst.name);
          line("  sw t0, 0(t1)");
          break;
        case IROp::Unary:
          loadReg(function, inst.left, "t0");
          if (inst.unary == UnaryOp::Minus) line("  neg t1, t0");
          else if (inst.unary == UnaryOp::Not) line("  seqz t1, t0");
          else line("  mv t1, t0");
          storeReg(function, inst.dst, "t1");
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
          loadReg(function, inst.left, "t0");
          line("  beqz t0, " + inst.name);
          break;
        case IROp::Call:
          emitCall(function, inst);
          break;
        case IROp::Return:
          if (inst.left >= 0) loadReg(function, inst.left, "a0");
          line("  j " + epilogue);
          break;
      }
    }
    line(epilogue + ":");
    line("  lw ra, -4(s0)");
    line("  lw t0, -8(s0)");
    line("  mv sp, s0");
    line("  mv s0, t0");
    line("  ret");
    line("  .size " + function.name + ", .-" + function.name);
  }

  void emitBinary(const IRFunction& function, const IRInst& inst) {
    loadReg(function, inst.left, "t0");
    loadReg(function, inst.right, "t1");
    switch (inst.binary) {
      case BinaryOp::Add: line("  add t2, t0, t1"); break;
      case BinaryOp::Sub: line("  sub t2, t0, t1"); break;
      case BinaryOp::Mul: line("  mul t2, t0, t1"); break;
      case BinaryOp::Div: line("  div t2, t0, t1"); break;
      case BinaryOp::Mod: line("  rem t2, t0, t1"); break;
      case BinaryOp::Lt: line("  slt t2, t0, t1"); break;
      case BinaryOp::Gt: line("  slt t2, t1, t0"); break;
      case BinaryOp::Le:
        line("  slt t2, t1, t0");
        line("  xori t2, t2, 1");
        break;
      case BinaryOp::Ge:
        line("  slt t2, t0, t1");
        line("  xori t2, t2, 1");
        break;
      case BinaryOp::Eq:
        line("  xor t2, t0, t1");
        line("  seqz t2, t2");
        break;
      case BinaryOp::Ne:
        line("  xor t2, t0, t1");
        line("  snez t2, t2");
        break;
      case BinaryOp::And:
      case BinaryOp::Or:
        throw Error("internal error: logical operation was not lowered");
    }
    storeReg(function, inst.dst, "t2");
  }

  void emitCall(const IRFunction& function, const IRInst& inst) {
    for (size_t i = 0; i < inst.args.size(); ++i) {
      loadReg(function, inst.args[i], "t0");
      if (i < 8) {
        line("  mv a" + std::to_string(i) + ", t0");
      } else {
        storeAt("t0", "sp", static_cast<int>(i - 8) * 4);
      }
    }
    line("  call " + inst.name);
    if (inst.dst >= 0) storeReg(function, inst.dst, "a0");
  }

  const IRProgram& program_;
  std::ostream* out_ = nullptr;
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
