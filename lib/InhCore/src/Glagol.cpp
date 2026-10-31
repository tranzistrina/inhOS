#include "Glagol.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <set>
#include <sstream>
namespace inh {
static void require(bool b, const std::string &s) {
  if (!b)
    throw std::runtime_error(s);
}
bool Value::truth() const {
  return kind == Number   ? number != 0
         : kind == String ? !text.empty()
                          : !array->empty();
}
double Value::num() const {
  require(kind == Number, "нужно число");
  return number;
}
std::string Value::str() const {
  if (kind == String)
    return text;
  if (kind == Array)
    return "[массив: " + std::to_string(array->size()) + "]";
  std::ostringstream s;
  s << std::setprecision(12) << number;
  return s.str();
}
namespace {
struct Token {
  std::string s;
  bool quoted = false;
};
std::vector<Token> lex(const std::string &s) {
  std::vector<Token> r;
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = s[i];
    if (c == ' ' || c == '\r') {
      i++;
      continue;
    }
    if (c == '#')
      break;
    if (c == '"' || c == '\'') {
      char q = c;
      std::string v;
      i++;
      bool closed = false;
      while (i < s.size()) {
        char a = s[i++];
        if (a == q) {
          closed = true;
          break;
        }
        if (a == '\\') {
          require(i < s.size(), "незавершённая строка");
          a = s[i++];
          if (a == 'n')
            a = '\n';
          else if (a == 't')
            a = '\t';
          else
            require(a == '\\' || a == '\'' || a == '"',
                    "неизвестное экранирование");
        }
        v += a;
        require(v.size() <= 2048, "строка слишком велика");
      }
      require(closed, "незавершённая строка");
      r.push_back({v, true});
    } else if (std::isdigit(c) || (c == '.' && i + 1 < s.size() &&
                                   std::isdigit((unsigned char)s[i + 1]))) {
      size_t st = i;
      bool dot = false;
      while (i < s.size() &&
             (std::isdigit((unsigned char)s[i]) || (!dot && s[i] == '.'))) {
        if (s[i] == '.')
          dot = true;
        i++;
      }
      r.push_back({s.substr(st, i - st), false});
    } else if (std::isalpha(c) || c == '_' || c >= 128) {
      size_t st = i++;
      while (i < s.size()) {
        unsigned char a = s[i];
        if (!(std::isalnum(a) || a == '_' || a >= 128))
          break;
        i++;
      }
      r.push_back({s.substr(st, i - st), false});
    } else {
      std::string two = s.substr(i, 2);
      if (two == "==" || two == "!=" || two == "<=" || two == ">=" ||
          two == "//" || two == "**" || two == "+=" || two == "-=" ||
          two == "*=" || two == "/=") {
        r.push_back({two, false});
        i += 2;
      } else {
        require(std::string("+-*/%()[],=:<>").find(c) != std::string::npos,
                "неизвестный знак");
        r.push_back({std::string(1, c), false});
        i++;
      }
    }
    require(r.size() <= 512, "слишком много знаков в строке");
  }
  return r;
}
bool identifier(const Token &t) {
  if (t.quoted || t.s.empty())
    return false;
  unsigned char c = t.s[0];
  return std::isalpha(c) || c == '_' || c >= 128;
}
struct Line {
  int indent = 0, num = 0;
  std::vector<Token> t;
};
struct Compiler {
  Program p;
  std::vector<Line> lines;
  size_t pos = 0;
  int line = 1;
  unsigned hidden = 0;
  struct Loop {
    std::vector<size_t> breaks, continues;
  };
  std::vector<Loop> loops;
  size_t emit(Op::Code c, std::string name = {}, int arg = 0, Value v = {}) {
    require(p.ops.size() < 8192, "программа слишком велика");
    p.ops.push_back({c, std::move(name), std::move(v), arg, line});
    return p.ops.size() - 1;
  }
  void push(double n) { emit(Op::Push, "", 0, Value(n)); }
  struct Expr {
    Compiler &c;
    const std::vector<Token> &t;
    size_t i = 0, end;
    unsigned depth = 0;
    Expr(Compiler &a, const std::vector<Token> &b, size_t from, size_t to)
        : c(a), t(b), i(from), end(to) {}
    bool eat(const std::string &s) {
      if (i < end && !t[i].quoted && t[i].s == s) {
        i++;
        return true;
      }
      return false;
    }
    int prec(const std::string &s) {
      if (s == "или")
        return 1;
      if (s == "и")
        return 2;
      if (s == "==" || s == "!=" || s == "<" || s == ">" || s == "<=" ||
          s == ">=")
        return 3;
      if (s == "+" || s == "-")
        return 4;
      if (s == "*" || s == "/" || s == "//" || s == "%")
        return 5;
      if (s == "**")
        return 7;
      return 0;
    }
    void expression(int minimum = 1) {
      require(++depth <= 24, "выражение слишком глубоко");
      atom();
      while (i < end && !t[i].quoted && prec(t[i].s) >= minimum) {
        std::string op = t[i++].s;
        int level = prec(op);
        if (op == "и" || op == "или") {
          c.emit(Op::Dup);
          size_t j = c.emit(op == "и" ? Op::JFalse : Op::JTrue);
          c.emit(Op::Drop);
          expression(level + 1);
          c.p.ops[j].arg = c.p.ops.size();
        } else {
          expression(level + (op == "**" ? 0 : 1));
          c.emit(Op::Binary, op);
        }
      }
      depth--;
    }
    void atom() {
      require(i < end, "нужно выражение");
      Token a = t[i++];
      if (!a.quoted && (a.s == "-" || a.s == "+" || a.s == "не")) {
        expression(a.s == "не" ? 3 : 6);
        c.emit(Op::Unary, a.s);
      } else if (!a.quoted && a.s == "(") {
        expression();
        require(eat(")"), "нужна )");
      } else if (!a.quoted && a.s == "[") {
        int n = 0;
        if (!eat("]")) {
          do {
            expression();
            require(++n <= 1024, "массив слишком велик");
          } while (eat(","));
          require(eat("]"), "нужна ]");
        }
        c.emit(Op::MakeArray, "", n);
      } else if (a.quoted)
        c.emit(Op::Push, "", 0, Value(a.s));
      else if (a.s == "истина" || a.s == "ложь")
        c.push(a.s == "истина");
      else if (identifier(a)) {
        if (eat("(")) {
          int n = 0;
          if (!eat(")")) {
            do {
              expression();
              require(++n <= 8, "слишком много аргументов");
            } while (eat(","));
            require(eat(")"), "нужна )");
          }
          c.emit(Op::Call, a.s, n);
        } else
          c.emit(Op::Load, a.s);
      } else {
        char *e = nullptr;
        double n = strtod(a.s.c_str(), &e);
        require(e && *e == 0 && e != a.s.c_str() && std::isfinite(n),
                "нужно число или имя");
        c.push(n);
      }
      while (eat("[")) {
        expression();
        require(eat("]"), "нужна ]");
        c.emit(Op::Get);
      }
    }
    void all() {
      expression();
      require(i == end, "лишние знаки в выражении");
    }
  };
  void expr(const std::vector<Token> &t, size_t a, size_t b) {
    Expr(*this, t, a, b).all();
  }
  void child(int indent) {
    require(pos < lines.size() && lines[pos].indent == indent + 4,
            "после : нужен блок с отступом 4 пробела");
    block(indent + 4);
  }
  void block(int indent) {
    require(indent <= 64, "слишком много вложенных блоков");
    while (pos < lines.size() && lines[pos].indent >= indent) {
      Line l = lines[pos];
      line = l.num;
      require(l.indent == indent, "неверный отступ");
      const auto &t = l.t;
      const std::string &head = t[0].s;
      if (head == "инако")
        return;
      if (head == "аще" || head == "дондеже" || head == "для") {
        require(t.size() >= 3 && t.back().s == ":" && !t.back().quoted,
                "нужно :");
        pos++;
        if (head == "аще") {
          expr(t, 1, t.size() - 1);
          auto no = emit(Op::JFalse);
          child(indent);
          if (pos < lines.size() && lines[pos].indent == indent &&
              lines[pos].t[0].s == "инако") {
            auto end = emit(Op::Jump);
            p.ops[no].arg = p.ops.size();
            require(lines[pos].t.size() == 2 && lines[pos].t[1].s == ":",
                    "инако:");
            pos++;
            child(indent);
            p.ops[end].arg = p.ops.size();
          } else
            p.ops[no].arg = p.ops.size();
        } else {
          loops.push_back({});
          std::string items, idx;
          size_t start;
          if (head == "для") {
            require(t.size() >= 6 && identifier(t[1]) && t[2].s == "въ",
                    "для имя въ ряд(...):");
            items = "@items" + std::to_string(hidden);
            idx = "@index" + std::to_string(hidden++);
            expr(t, 3, t.size() - 1);
            emit(Op::Store, items);
            push(0);
            emit(Op::Store, idx);
          }
          start = p.ops.size();
          if (head == "дондеже")
            expr(t, 1, t.size() - 1);
          else {
            emit(Op::Load, idx);
            emit(Op::Load, items);
            emit(Op::Call, "длина", 1);
            emit(Op::Binary, "<");
          }
          auto end = emit(Op::JFalse);
          if (head == "для") {
            emit(Op::Load, items);
            emit(Op::Load, idx);
            emit(Op::Get);
            emit(Op::Store, t[1].s);
          }
          child(indent);
          auto advance = p.ops.size();
          if (head == "для") {
            emit(Op::Load, idx);
            push(1);
            emit(Op::Binary, "+");
            emit(Op::Store, idx);
          }
          emit(Op::Jump, "", start);
          p.ops[end].arg = p.ops.size();
          for (auto j : loops.back().breaks)
            p.ops[j].arg = p.ops.size();
          for (auto j : loops.back().continues)
            p.ops[j].arg = advance;
          loops.pop_back();
        }
      } else {
        pos++;
        if (head == "прерви" || head == "продолжи") {
          require(t.size() == 1 && !loops.empty(), "команда вне цикла");
          auto j = emit(Op::Jump);
          (head == "прерви" ? loops.back().breaks : loops.back().continues)
              .push_back(j);
          continue;
        }
        size_t eq = t.size();
        int level = 0;
        for (size_t j = 0; j < t.size(); j++) {
          if (t[j].quoted)
            continue;
          if (t[j].s == "(" || t[j].s == "[")
            level++;
          if (t[j].s == ")" || t[j].s == "]")
            level--;
          if (!level && (t[j].s == "=" || t[j].s == "+=" || t[j].s == "-=" ||
                         t[j].s == "*=" || t[j].s == "/=")) {
            eq = j;
            break;
          }
        }
        if (eq == t.size()) {
          expr(t, 0, t.size());
          emit(Op::Drop);
          continue;
        }
        require(eq > 0 && identifier(t[0]), "нужно имя переменной");
        if (eq == 1) {
          if (t[eq].s != "=")
            emit(Op::Load, t[0].s);
          expr(t, eq + 1, t.size());
          if (t[eq].s != "=")
            emit(Op::Binary, t[eq].s.substr(0, 1));
          emit(Op::Store, t[0].s);
        } else {
          require(t[1].s == "[" && t[eq - 1].s == "]" && t[eq].s == "=",
                  "индекс: имя[число] = значение");
          emit(Op::Load, t[0].s);
          expr(t, 2, eq - 1);
          expr(t, eq + 1, t.size());
          emit(Op::Set);
        }
      }
    }
  }
};
} // namespace
Program compile(const std::string &source) {
  require(source.size() <= 32768, "приложение больше 32 КиБ");
  require(source.find('\0') == std::string::npos, "нулевой байт в коде");
  Compiler c;
  std::istringstream input(source);
  std::string s;
  int number = 0;
  try {
    while (std::getline(input, s)) {
      number++;
      if (number == 1 && s.compare(0, 3, "\xef\xbb\xbf") == 0)
        s.erase(0, 3);
      require(number <= 1024, "слишком много строк");
      int indent = 0;
      while (indent < (int)s.size() && s[indent] == ' ')
        indent++;
      require(s.find('\t') == std::string::npos,
              "табуляция запрещена; используйте пробелы");
      auto t = lex(s.substr(indent));
      if (t.empty())
        continue;
      require(indent % 4 == 0, "отступ должен быть кратен 4");
      c.lines.push_back({indent, number, std::move(t)});
    }
    c.block(0);
    require(c.pos == c.lines.size(), "инако без аще");
    return c.p;
  } catch (const std::exception &e) {
    throw std::runtime_error("строка " +
                             std::to_string(c.lines.empty() ? number : c.line) +
                             ": " + e.what());
  }
}
void VM::start(Program p) {
  stop();
  program_ = std::move(p);
  pc_ = 0;
  running_ = true;
  error_.clear();
  executed_ = 0;
}
void VM::stop() {
  running_ = false;
  stack_.clear();
  vars_.clear();
  program_.ops.clear();
  pc_ = 0;
}
Value VM::pop() {
  require(!stack_.empty(), "пустой стек");
  auto v = std::move(stack_.back());
  stack_.pop_back();
  return v;
}
void VM::memoryCheck() {
  require(stack_.size() <= 128 && vars_.size() <= 80, "лимит стека/переменных");
  size_t bytes = 0;
  std::set<const void *> arrays;
  auto add = [&](const Value &v) {
    if (v.kind == Value::String) {
      require(v.text.size() <= 2048, "строка больше 2048 байт");
      bytes += v.text.size();
    }
    if (v.kind == Value::Array && arrays.insert(v.array.get()).second) {
      require(v.array->size() <= 1024, "массив больше 1024 элементов");
      bytes += v.array->size() * sizeof(double);
    }
  };
  for (const auto &v : vars_)
    add(v.second);
  for (const auto &v : stack_)
    add(v);
  require(bytes <= 48 * 1024, "лимит памяти приложения 48 КиБ");
}
Value VM::call(const std::string &n, const std::vector<Value> &a) {
  auto argc = [&](size_t k) {
    require(a.size() == k, "неверное число аргументов: " + n);
  };
  auto integer = [&](size_t i) {
    double v = a[i].num();
    require(std::isfinite(v) && std::floor(v) == v && std::abs(v) <= 1000000,
            "нужно целое в пределах миллиона");
    return int(v);
  };
  if (n == "длина") {
    argc(1);
    return Value(double(a[0].kind == Value::Array    ? a[0].array->size()
                        : a[0].kind == Value::String ? a[0].text.size()
                                                     : 0));
  }
  if (n == "строка") {
    argc(1);
    return Value(a[0].str());
  }
  if (n == "число") {
    argc(1);
    if (a[0].kind == Value::Number)
      return a[0];
    require(a[0].kind == Value::String, "нужно число или строка");
    char *e;
    double v = strtod(a[0].text.c_str(), &e);
    require(e != a[0].text.c_str() && *e == 0 && std::isfinite(v), "не число");
    return Value(v);
  }
  if (n == "ряд") {
    require(a.size() >= 1 && a.size() <= 3,
            "ряд(конец), ряд(начало, конец, шаг)");
    int start = a.size() == 1 ? 0 : integer(0),
        end = integer(a.size() == 1 ? 0 : 1),
        step = a.size() == 3 ? integer(2) : 1;
    require(step != 0, "нулевой шаг");
    std::vector<double> r;
    for (int i = start; step > 0 ? i < end : i > end; i += step) {
      require(r.size() < 1024, "ряд больше 1024 элементов");
      r.push_back(i);
    }
    return Value(std::move(r));
  }
  if (n == "массив") {
    require(a.size() == 1 || a.size() == 2, "массив(длина, значение)");
    int k = integer(0);
    require(k >= 0 && k <= 1024, "неверная длина массива");
    return Value(std::vector<double>(k, a.size() == 2 ? a[1].num() : 0));
  }
  if (n == "синус" || n == "косинус" || n == "тангенс" || n == "корень" ||
      n == "модуль" || n == "пол" || n == "округли") {
    argc(1);
    double x = a[0].num(), v = 0;
    if (n == "синус")
      v = sin(x);
    if (n == "косинус")
      v = cos(x);
    if (n == "тангенс")
      v = tan(x);
    if (n == "корень")
      v = sqrt(x);
    if (n == "модуль")
      v = fabs(x);
    if (n == "пол")
      v = floor(x);
    if (n == "округли")
      v = round(x);
    require(std::isfinite(v), "математическая ошибка");
    return Value(v);
  }
  if (n == "степень") {
    argc(2);
    double v = pow(a[0].num(), a[1].num());
    require(std::isfinite(v), "математическая ошибка");
    return Value(v);
  }
  if (n == "пи") {
    argc(0);
    return Value(3.14159265358979323846);
  }
  require(bool(host_), "неизвестное слово: " + n);
  return host_(n, a);
}
bool VM::tick(unsigned budget) {
  if (!running_)
    return false;
  unsigned line = 0;
  try {
    while (budget-- && running_) {
      if (pc_ >= program_.ops.size()) {
        running_ = false;
        break;
      }
      const auto &o = program_.ops[pc_++];
      line = o.line;
      executed_++;
      switch (o.code) {
      case Op::Push:
        stack_.push_back(o.value);
        break;
      case Op::Load: {
        auto it = vars_.find(o.name);
        require(it != vars_.end(), "нет переменной: " + o.name);
        stack_.push_back(it->second);
        break;
      }
      case Op::Store: {
        auto v = pop();
        vars_[o.name] = std::move(v);
        break;
      }
      case Op::Drop:
        pop();
        break;
      case Op::Dup:
        require(!stack_.empty(), "пустой стек");
        stack_.push_back(stack_.back());
        break;
      case Op::MakeArray: {
        std::vector<double> a(o.arg);
        for (int j = o.arg - 1; j >= 0; j--)
          a[j] = pop().num();
        stack_.push_back(Value(std::move(a)));
        break;
      }
      case Op::Get:
      case Op::Set: {
        Value v;
        if (o.code == Op::Set)
          v = pop();
        double n = pop().num();
        auto arr = pop();
        require(arr.kind == Value::Array, "нужен массив");
        require(n >= 0 && n < arr.array->size() && floor(n) == n,
                "индекс вне массива");
        if (o.code == Op::Set)
          (*arr.array)[size_t(n)] = v.num();
        else
          stack_.push_back(Value((*arr.array)[size_t(n)]));
        break;
      }
      case Op::Unary: {
        auto v = pop();
        stack_.push_back(o.name == "не"
                             ? Value(!v.truth())
                             : Value(o.name == "-" ? -v.num() : v.num()));
        break;
      }
      case Op::Binary: {
        auto b = pop(), a = pop();
        Value r;
        const auto &n = o.name;
        if (n == "+" && (a.kind == Value::String || b.kind == Value::String)) {
          auto x = a.str(), y = b.str();
          require(x.size() + y.size() <= 2048, "строка слишком велика");
          r = Value(x + y);
        } else if (n == "==" || n == "!=") {
          bool eq = a.kind == b.kind &&
                    (a.kind == Value::Number   ? a.number == b.number
                     : a.kind == Value::String ? a.text == b.text
                                               : *a.array == *b.array);
          r = Value(n == "==" ? eq : !eq);
        } else {
          double x = a.num(), y = b.num(), v = 0;
          if (n == "+")
            v = x + y;
          else if (n == "-")
            v = x - y;
          else if (n == "*")
            v = x * y;
          else if (n == "/" || n == "//" || n == "%") {
            require(y != 0, "деление на ноль");
            v = n == "/"    ? x / y
                : n == "//" ? floor(x / y)
                            : x - floor(x / y) * y;
          } else if (n == "**")
            v = pow(x, y);
          else if (n == "<")
            v = x < y;
          else if (n == ">")
            v = x > y;
          else if (n == "<=")
            v = x <= y;
          else if (n == ">=")
            v = x >= y;
          else
            throw std::runtime_error("неизвестный оператор");
          require(std::isfinite(v), "математическая ошибка");
          r = Value(v);
        }
        stack_.push_back(std::move(r));
        break;
      }
      case Op::Call: {
        std::vector<Value> a(o.arg);
        for (int j = o.arg - 1; j >= 0; j--)
          a[j] = pop();
        stack_.push_back(call(o.name, a));
        break;
      }
      case Op::Jump:
        pc_ = o.arg;
        break;
      case Op::JFalse:
      case Op::JTrue: {
        bool b = pop().truth();
        if (b == (o.code == Op::JTrue))
          pc_ = o.arg;
        break;
      }
      }
      require(pc_ <= program_.ops.size(), "неверный переход");
      memoryCheck();
    }
  } catch (const std::exception &e) {
    error_ = "строка " + std::to_string(line) + ": " + e.what();
    running_ = false;
    stack_.clear();
  }
  return running_;
}
} // namespace inh
