#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace inh {
struct Value {
  enum Kind { Number, String, Array } kind = Number;
  double number = 0;
  std::string text;
  std::shared_ptr<std::vector<double>> array;
  Value() = default;
  Value(double n) : number(n) {}
  Value(std::string s) : kind(String), text(std::move(s)) {}
  Value(std::vector<double> a)
      : kind(Array),
        array(std::make_shared<std::vector<double>>(std::move(a))) {}
  bool truth() const;
  double num() const;
  std::string str() const;
};
struct Op {
  enum Code {
    Push,
    Load,
    Store,
    Get,
    Set,
    MakeArray,
    Unary,
    Binary,
    Call,
    Drop,
    Dup,
    Jump,
    JFalse,
    JTrue
  } code;
  std::string name;
  Value value;
  int arg = 0;
  int line = 0;
};
struct Program {
  std::vector<Op> ops;
};
Program compile(const std::string &source);
class VM {
public:
  using Host =
      std::function<Value(const std::string &, const std::vector<Value> &)>;
  explicit VM(Host host = {}) : host_(std::move(host)) {}
  void start(Program p);
  void stop();
  bool tick(unsigned budget = 100);
  bool running() const { return running_; }
  const std::string &error() const { return error_; }
  const std::map<std::string, Value> &variables() const { return vars_; }
  uint64_t instructions() const { return executed_; }

private:
  Program program_;
  size_t pc_ = 0;
  bool running_ = false;
  std::string error_;
  Host host_;
  std::vector<Value> stack_;
  std::map<std::string, Value> vars_;
  uint64_t executed_ = 0;
  Value pop();
  Value call(const std::string &, const std::vector<Value> &);
  void memoryCheck();
};
} // namespace inh
