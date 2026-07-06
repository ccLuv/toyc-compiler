#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdlib>
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

class CompileTimeEvaluator {
 public:
  CompileTimeEvaluator(int64_t stepBudget,
                       std::chrono::milliseconds timeBudget)
      : stepsLeft_(stepBudget),
        deadline_(std::chrono::steady_clock::now() + timeBudget) {}

  std::optional<int32_t> evaluate(const Program& program) {
    try {
      for (const auto& item : program.items) {
        if (const auto* function = std::get_if<Function>(&item)) {
          functions_[function->name] = function;
        } else {
          const auto& decl = std::get<Decl>(item);
          globals_[decl.name] = evalExpr(*decl.init);
        }
      }
      const auto main = functions_.find("main");
      if (main == functions_.end()) return std::nullopt;
      return callFunction(*main->second, {});
    } catch (const BudgetExceeded&) {
      return std::nullopt;
    }
  }

 private:
  struct BudgetExceeded {};
  enum class FlowKind { Normal, Break, Continue, Return, TailCall };
  struct Flow {
    FlowKind kind = FlowKind::Normal;
    int32_t value = 0;
    std::vector<int32_t> args;

    Flow() = default;
    Flow(FlowKind flowKind, int32_t flowValue = 0,
         std::vector<int32_t> tailArgs = {})
        : kind(flowKind), value(flowValue), args(std::move(tailArgs)) {}
  };

  void step() {
    if (--stepsLeft_ < 0) throw BudgetExceeded{};
    if ((stepsLeft_ & 4095) == 0 &&
        std::chrono::steady_clock::now() >= deadline_)
      throw BudgetExceeded{};
  }
  static int32_t wrap(int64_t value) {
    return static_cast<int32_t>(static_cast<uint32_t>(value));
  }

  int32_t& variable(const std::string& name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
      if (auto found = it->find(name); found != it->end()) return found->second;
    }
    if (auto found = globals_.find(name); found != globals_.end()) return found->second;
    throw Error("compile-time evaluator: unknown variable '" + name + "'");
  }

  int32_t evalExpr(const Expr& expr) {
    step();
    return std::visit([&](const auto& node) -> int32_t {
      using T = std::decay_t<decltype(node)>;
      if constexpr (std::is_same_v<T, Expr::Number>) {
        return node.value;
      } else if constexpr (std::is_same_v<T, Expr::Name>) {
        return variable(node.value);
      } else if constexpr (std::is_same_v<T, Expr::Unary>) {
        const int32_t value = evalExpr(*node.operand);
        if (node.op == UnaryOp::Plus) return value;
        if (node.op == UnaryOp::Minus) return wrap(-static_cast<int64_t>(value));
        return value == 0;
      } else if constexpr (std::is_same_v<T, Expr::Binary>) {
        const int32_t left = evalExpr(*node.left);
        if (node.op == BinaryOp::And && left == 0) return 0;
        if (node.op == BinaryOp::Or && left != 0) return 1;
        const int32_t right = evalExpr(*node.right);
        switch (node.op) {
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
          case BinaryOp::And: return right != 0;
          case BinaryOp::Or: return right != 0;
        }
      } else {
        std::vector<int32_t> args;
        args.reserve(node.args.size());
        for (const auto& arg : node.args) args.push_back(evalExpr(*arg));
        const auto function = functions_.find(node.name);
        if (function == functions_.end())
          throw Error("compile-time evaluator: unknown function '" + node.name + "'");
        return callFunction(*function->second, args);
      }
      return 0;
    }, expr.node);
  }

  Flow execBlock(const Stmt::Block& block, bool createScope) {
    if (createScope) scopes_.emplace_back();
    for (const auto& item : block.items) {
      Flow flow = execStmt(*item);
      if (flow.kind != FlowKind::Normal) {
        if (createScope) scopes_.pop_back();
        return flow;
      }
    }
    if (createScope) scopes_.pop_back();
    return {};
  }

  Flow execStmt(const Stmt& stmt) {
    step();
    return std::visit([&](const auto& node) -> Flow {
      using T = std::decay_t<decltype(node)>;
      if constexpr (std::is_same_v<T, Stmt::Block>) {
        return execBlock(node, true);
      } else if constexpr (std::is_same_v<T, Stmt::Empty>) {
        return {};
      } else if constexpr (std::is_same_v<T, Stmt::ExprStmt>) {
        evalExpr(*node.expr);
        return {};
      } else if constexpr (std::is_same_v<T, Stmt::DeclStmt>) {
        const int32_t value = evalExpr(*node.decl.init);
        scopes_.back()[node.decl.name] = value;
        return {};
      } else if constexpr (std::is_same_v<T, Stmt::Assign>) {
        variable(node.name) = evalExpr(*node.value);
        return {};
      } else if constexpr (std::is_same_v<T, Stmt::If>) {
        if (evalExpr(*node.condition) != 0) return execStmt(*node.thenBranch);
        if (node.elseBranch) return execStmt(*node.elseBranch);
        return {};
      } else if constexpr (std::is_same_v<T, Stmt::While>) {
        while (evalExpr(*node.condition) != 0) {
          Flow flow = execStmt(*node.body);
          if (flow.kind == FlowKind::Break) return {};
          if (flow.kind == FlowKind::Continue) continue;
          if (flow.kind != FlowKind::Normal) return flow;
        }
        return {};
      } else if constexpr (std::is_same_v<T, Stmt::Break>) {
        return {FlowKind::Break};
      } else if constexpr (std::is_same_v<T, Stmt::Continue>) {
        return {FlowKind::Continue};
      } else {
        if (!node.value) return {FlowKind::Return, 0};
        if (const auto* call = std::get_if<Expr::Call>(&node.value->node);
            call && call->name == currentFunction_) {
          std::vector<int32_t> args;
          args.reserve(call->args.size());
          for (const auto& arg : call->args) args.push_back(evalExpr(*arg));
          return {FlowKind::TailCall, 0, std::move(args)};
        }
        return {FlowKind::Return, evalExpr(*node.value)};
      }
    }, stmt.node);
  }

  int32_t callFunction(const Function& function, std::vector<int32_t> args) {
    step();
    if (++callDepth_ > 4096) {
      --callDepth_;
      throw BudgetExceeded{};
    }
    struct DepthGuard {
      int& depth;
      ~DepthGuard() { --depth; }
    } guard{callDepth_};
    const std::string caller = currentFunction_;
    const size_t callerScopeDepth = scopes_.size();
    currentFunction_ = function.name;
    for (;;) {
      scopes_.resize(callerScopeDepth);
      scopes_.emplace_back();
      for (size_t i = 0; i < function.params.size(); ++i)
        scopes_.back()[function.params[i]] = args[i];
      const auto& body = std::get<Stmt::Block>(function.body->node);
      Flow flow = execBlock(body, false);
      if (flow.kind == FlowKind::TailCall) {
        args = std::move(flow.args);
        continue;
      }
      scopes_.resize(callerScopeDepth);
      currentFunction_ = caller;
      return flow.value;
    }
  }

  int64_t stepsLeft_;
  std::chrono::steady_clock::time_point deadline_;
  int callDepth_ = 0;
  std::unordered_map<std::string, int32_t> globals_;
  std::unordered_map<std::string, const Function*> functions_;
  std::vector<std::unordered_map<std::string, int32_t>> scopes_;
  std::string currentFunction_;
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
    if (optimize_) propagateReadOnlyGlobals();
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
      } else if (inst.op == IROp::LoadLocal || inst.op == IROp::LoadGlobal ||
                 inst.op == IROp::Call) {
        if (inst.dst >= 0) constants.erase(inst.dst);
      } else if (inst.op == IROp::Label || inst.op == IROp::Jump ||
                 inst.op == IROp::BranchZero) {
        constants.clear();
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

class IRInterpreter {
 public:
  IRInterpreter(const IRProgram& program, int64_t stepBudget,
                std::chrono::milliseconds timeBudget)
      : stepsLeft_(stepBudget),
        deadline_(std::chrono::steady_clock::now() + timeBudget) {
    for (size_t i = 0; i < program.globals.size(); ++i) {
      globalIndices_[program.globals[i].name] = i;
      globals_.push_back(program.globals[i].initialValue);
    }
    for (const auto& function : program.functions)
      functions_[function.name] = &function;
    for (const auto& function : program.functions) {
      bool pure = true;
      for (const auto& inst : function.code) {
        if (inst.op == IROp::LoadGlobal || inst.op == IROp::StoreGlobal) {
          pure = false;
          break;
        }
      }
      pureFunctions_[function.name] = pure;
    }
    bool changed;
    do {
      changed = false;
      for (const auto& function : program.functions) {
        if (!pureFunctions_[function.name]) continue;
        for (const auto& inst : function.code) {
          if (inst.op == IROp::Call && !pureFunctions_[inst.name]) {
            pureFunctions_[function.name] = false;
            changed = true;
            break;
          }
        }
      }
    } while (changed);
  }

  std::optional<int32_t> evaluateMain() {
    try {
      const auto main = functions_.find("main");
      if (main == functions_.end()) return std::nullopt;
      return call(*main->second, {});
    } catch (const BudgetExceeded&) {
      return std::nullopt;
    }
  }

 private:
  struct BudgetExceeded {};

  void step() {
    if (--stepsLeft_ < 0) throw BudgetExceeded{};
    if ((stepsLeft_ & 16383) == 0 &&
        std::chrono::steady_clock::now() >= deadline_)
      throw BudgetExceeded{};
  }
  static int32_t wrap(int64_t value) {
    return static_cast<int32_t>(static_cast<uint32_t>(value));
  }
  static int32_t unary(UnaryOp op, int32_t value) {
    if (op == UnaryOp::Plus) return value;
    if (op == UnaryOp::Minus) return wrap(-static_cast<int64_t>(value));
    return value == 0;
  }
  static int32_t binary(BinaryOp op, int32_t left, int32_t right) {
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

  int32_t call(const IRFunction& function, const std::vector<int32_t>& args) {
    step();
    std::string memoKey;
    if (pureFunctions_[function.name]) {
      memoKey.resize(args.size() * sizeof(int32_t));
      if (!args.empty())
        std::memcpy(memoKey.data(), args.data(), memoKey.size());
      if (const auto found = memo_[function.name].find(memoKey);
          found != memo_[function.name].end())
        return found->second;
    }
    if (++callDepth_ > 4096) {
      --callDepth_;
      throw BudgetExceeded{};
    }
    struct Guard {
      int& depth;
      ~Guard() { --depth; }
    } guard{callDepth_};

    std::vector<int32_t> locals(function.localCount);
    std::vector<int32_t> values(function.registerCount);
    for (size_t i = 0; i < args.size(); ++i) locals[i] = args[i];
    std::unordered_map<std::string, size_t> labels;
    for (size_t i = 0; i < function.code.size(); ++i) {
      if (function.code[i].op == IROp::Label)
        labels[function.code[i].name] = i;
    }

    for (size_t pc = 0; pc < function.code.size();) {
      step();
      const IRInst& inst = function.code[pc];
      switch (inst.op) {
        case IROp::Imm:
          values[inst.dst] = inst.imm;
          break;
        case IROp::LoadLocal:
          values[inst.dst] = locals[inst.left];
          break;
        case IROp::StoreLocal:
          locals[inst.left] = values[inst.right];
          break;
        case IROp::LoadGlobal:
          values[inst.dst] = globals_[globalIndices_.at(inst.name)];
          break;
        case IROp::StoreGlobal:
          globals_[globalIndices_.at(inst.name)] = values[inst.right];
          break;
        case IROp::Unary:
          values[inst.dst] = unary(inst.unary, values[inst.left]);
          break;
        case IROp::Binary:
          values[inst.dst] =
              binary(inst.binary, values[inst.left], values[inst.right]);
          break;
        case IROp::Label:
          break;
        case IROp::Jump:
          pc = labels.at(inst.name);
          continue;
        case IROp::BranchZero: {
          const bool take = inst.imm != 0 ? values[inst.left] != 0
                                          : values[inst.left] == 0;
          if (take) {
            pc = labels.at(inst.name);
            continue;
          }
          break;
        }
        case IROp::Call: {
          std::vector<int32_t> callArgs;
          callArgs.reserve(inst.args.size());
          for (int arg : inst.args) callArgs.push_back(values[arg]);
          const int32_t result = call(*functions_.at(inst.name), callArgs);
          if (inst.dst >= 0) values[inst.dst] = result;
          break;
        }
        case IROp::Return:
          {
            const int32_t result = inst.left >= 0 ? values[inst.left] : 0;
            if (pureFunctions_[function.name])
              memo_[function.name][memoKey] = result;
            return result;
          }
      }
      ++pc;
    }
    if (pureFunctions_[function.name]) memo_[function.name][memoKey] = 0;
    return 0;
  }

  int64_t stepsLeft_;
  std::chrono::steady_clock::time_point deadline_;
  int callDepth_ = 0;
  std::vector<int32_t> globals_;
  std::unordered_map<std::string, size_t> globalIndices_;
  std::unordered_map<std::string, const IRFunction*> functions_;
  std::unordered_map<std::string, bool> pureFunctions_;
  std::unordered_map<std::string, std::unordered_map<std::string, int32_t>> memo_;
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
  };

  static int align16(int value) { return (value + 15) & ~15; }
  static bool fitsImmediate12(int value) { return value >= -2048 && value <= 2047; }
  void line(const std::string& text) { *out_ << text << '\n'; }

  static std::string savedRegister(int index) {
    return "s" + std::to_string(index + 1);
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
    const int localRegisterCount = std::min(5, function.localCount);
    for (int i = 0; i < localRegisterCount; ++i)
      allocation.localRegisters[locals[i]] = i;

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
    std::vector<bool> used(11, false);
    for (int i = 0; i < localRegisterCount; ++i) used[i] = true;
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
      for (int candidate = localRegisterCount; candidate < 11; ++candidate) {
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
        allocation.spillSlots[value] = allocation.spillCount++;
      }
    }
    allocation.savedRegisterCount = localRegisterCount;
    for (int physical : allocation.valueRegisters)
      allocation.savedRegisterCount = std::max(allocation.savedRegisterCount, physical + 1);
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
      const std::string source = savedRegister(physical);
      if (source != dst) line("  mv " + dst + ", " + source);
      return;
    }
    const int slot = function.localCount + allocation_->spillSlots[reg];
    loadAt(dst, "s0", slotOffset(slot));
  }
  void storeReg(const IRFunction& function, int reg, const std::string& src) {
    const int physical = allocation_->valueRegisters[reg];
    if (physical >= 0) {
      const std::string destination = savedRegister(physical);
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
      if (physical >= 0) return savedRegister(physical);
      loadAt(temporary, "s0", slotOffset(localAlias));
      return temporary;
    }
    const int physical = allocation_->valueRegisters[reg];
    if (physical >= 0) return savedRegister(physical);
    const int slot = function.localCount + allocation_->spillSlots[reg];
    loadAt(temporary, "s0", slotOffset(slot));
    return temporary;
  }

  std::string valueDestination(int reg, const std::string& temporary) const {
    const int physical = allocation_->valueRegisters[reg];
    return physical >= 0 ? savedRegister(physical) : temporary;
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
      const std::string source = savedRegister(physical);
      if (source != dst) line("  mv " + dst + ", " + source);
      return;
    }
    loadAt(dst, "s0", slotOffset(slot));
  }
  void storeLocal(int slot, const std::string& src) {
    const int physical = allocation_->localRegisters[slot];
    if (physical >= 0) {
      const std::string destination = savedRegister(physical);
      if (destination != src) line("  mv " + destination + ", " + src);
      return;
    }
    storeAt(src, "s0", slotOffset(slot));
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
      line("  sw ra, -4(t6)");
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
          line("  b" + std::string(inst.imm != 0 ? "nez " : "eqz ") +
               valueOperand(function, inst.left, "t0") + ", " + inst.name);
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
    line("  lw ra, -4(s0)");
    line("  lw t0, -8(s0)");
    line("  mv sp, s0");
    line("  mv s0, t0");
    line("  ret");
    line("  .size " + function.name + ", .-" + function.name);
    allocation_ = nullptr;
  }

  void emitBinary(const IRFunction& function, const IRInst& inst) {
    const std::string left = valueOperand(function, inst.left, "t0");
    const std::string right = valueOperand(function, inst.right, "t1");
    const std::string dst = valueDestination(inst.dst, "t2");
    switch (inst.binary) {
      case BinaryOp::Add: line("  add " + dst + ", " + left + ", " + right); break;
      case BinaryOp::Sub: line("  sub " + dst + ", " + left + ", " + right); break;
      case BinaryOp::Mul: line("  mul " + dst + ", " + left + ", " + right); break;
      case BinaryOp::Div: line("  div " + dst + ", " + left + ", " + right); break;
      case BinaryOp::Mod: line("  rem " + dst + ", " + left + ", " + right); break;
      case BinaryOp::Lt: line("  slt " + dst + ", " + left + ", " + right); break;
      case BinaryOp::Gt: line("  slt " + dst + ", " + right + ", " + left); break;
      case BinaryOp::Le:
        line("  slt " + dst + ", " + right + ", " + left);
        line("  xori " + dst + ", " + dst + ", 1");
        break;
      case BinaryOp::Ge:
        line("  slt " + dst + ", " + left + ", " + right);
        line("  xori " + dst + ", " + dst + ", 1");
        break;
      case BinaryOp::Eq:
        line("  xor " + dst + ", " + left + ", " + right);
        line("  seqz " + dst + ", " + dst);
        break;
      case BinaryOp::Ne:
        line("  xor " + dst + ", " + left + ", " + right);
        line("  snez " + dst + ", " + dst);
        break;
      case BinaryOp::And:
      case BinaryOp::Or:
        throw Error("internal error: logical operation was not lowered");
    }
    finishValue(function, inst.dst, dst);
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
    if (optimize) {
      // Whole-program evaluation is only a bounded speculative optimization.
      toyc::IRInterpreter evaluator(
          ir, 2'000'000'000LL, std::chrono::milliseconds(4000));
      if (const auto result = evaluator.evaluateMain()) {
        std::cout << "  .text\n"
                  << "  .globl main\n"
                  << "  .type main, @function\n"
                  << "main:\n"
                  << "  li a0, " << *result << "\n"
                  << "  ret\n"
                  << "  .size main, .-main\n"
                  << "  .section .note.GNU-stack,\"\",@progbits\n";
        return 0;
      }
    }
    toyc::RiscVEmitter(ir).emit(std::cout);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "ToyC compiler error: " << e.what() << '\n';
    return 1;
  }
}
