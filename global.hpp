#ifndef GLOBALS_HPP
#define GLOBALS_HPP

#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include "utils/spsc.hpp"

#include "io_uring_queue.hpp"
#include "databaseSchemaReader.hpp"
#include "storageTree.hpp"

#define RESET "\033[0m"
#define GREEN "\033[1;32m"
#define YELLOW "\033[1;33m"
#define CYAN "\033[1;36m"

#include "debug_macros.hpp"

// for binance
extern std::unordered_map<std::string, std::string> API;


// extern std::thread vacuumThread;

// Memory structures

struct MemoryEntry {
  std::string value;
  std::chrono::steady_clock::time_point expiry;
};

// Main store
// extern std::unordered_map<std::string, MemoryEntry> memoryStore;

// // Reader-writer lock
// extern std::shared_mutex memoryMutex;
// extern std::unordered_map<std::string, std::mutex> tableLocks;
// extern std::mutex dbMutex;
// TTL scheduler heap
struct ExpiryNode {
  std::chrono::steady_clock::time_point expiry;
  std::string key;

  bool operator>(const ExpiryNode &other) const {
    return expiry > other.expiry;
  }
};

extern std::priority_queue<ExpiryNode, std::vector<ExpiryNode>, std::greater<>>
    expiryHeap;

extern std::mutex expiryMutex;
extern std::condition_variable expiryCV;
extern std::atomic<bool> memorySchedulerRunning;

// --- File Paths ---

inline std::string currentDbPath = "./db/current_db.meta";
inline std::string dbDirectoryPath = "./db";
// inline std::string allTableDataDirectory = "./db/data";
inline std::string currentDatabase = "";
inline std::string tableDirectory = "./db/tables";
// --- Schema Node Structure ---
struct TableGlobalColumnNode {
  std::string type;
  std::string name;
  std::vector<std::string> constraint;
  int precision = 0;
  bool autoIncrement = false;
  bool isUnique = false;
  bool isPrimary = false;
  bool createIndex = false;
  int length = INT_MAX;
};

struct  web_socket_Packet{
    bool valid = false;
    int64_t symbol;
    int64_t strategyIndex;  
};
// --- JSON Parser Cache ---
// db_name -> JSON parser
extern std::unordered_map<std::string, std::shared_ptr<PythonLikeJSONParser>>
    globalJsonCache;
extern std::unordered_map<std::string, MemoryEntry> memoryStore;
extern std::shared_mutex memoryMutex;
extern std::unordered_map<std::string, std::mutex> tableLocks;
extern std::unordered_map<int64_t, std::unique_ptr<IoUringQueue>> batchWriterFileMap;
extern std::mutex dbMutex;

extern std::atomic<bool> shuttingDown;
extern std::thread vacuumThread;

extern std::priority_queue<ExpiryNode, std::vector<ExpiryNode>, std::greater<>>
    expiryHeap;

extern std::mutex expiryMutex;
extern std::condition_variable expiryCV;
extern std::atomic<bool> memorySchedulerRunning;
extern SPSCQueue<web_socket_Packet,1024>web_socket_queue;

// --- Table Metadata Cache ---
// db_name -> table_name -> vector of column definitions
extern std::unordered_map<
    std::string,
    std::unordered_map<std::string,
                       std::pair<std::string,std::vector<std::shared_ptr<TableGlobalColumnNode>>>>>
    globalTableCache;

struct IndexNode {
  int64_t start;
  int16_t end;
};

inline std::ostream& operator<<(std::ostream& os, const IndexNode& node) {
    os << "{start=" << node.start << ", end=" << node.end << "}";
    return os;
}

using TreeVariant =
    std::variant<std::shared_ptr<BPlusTree<int64_t, IndexNode>>,
                 std::shared_ptr<BPlusTree<std::string, IndexNode>>>;

// --- B+ Tree Cache ---
// db_name -> table_name -> column_name -> (B+ Tree, no of columns)
extern std::unordered_map<
    std::string,
    std::unordered_map<
        std::string,
        std::unordered_map<std::string, std::pair<TreeVariant, int64_t>>>>
    dbBtrees;

enum class ASTNodeType {
  STATEMENT,
  SELECT_STATEMENT,
  INSERT_STATEMENT,
  UPDATE_STATEMENT,
  DELETE_STATEMENT,
  EXPRESSION,
  IDENTIFIER,
  INT_LITERAL,
  STRING_LITERAL,
  BOOLEAN_LITERAL,
  COMPARISON_EXPRESSION,
  LOGICAL_EXPRESSION,
  PARENTHESIZED_EXPRESSION,
  LIMIT_CLAUSE,
  WHERE_CLAUSE,
  DROP_STATEMENT,
  LIST_STATEMENT,
  CREATE_STATEMENT,
  ENABLE_STATEMENT,
  STATISTICS_STATEMENT,
  DISABLE_STATEMENT,
  // HFT_STATEMENT
  ADD_HFT_INDICATOR_STATEMENT,
  ADD_HFT_INDICATOR_ON_TABLE_STATEMENT,
  FETCH_INDICATOR_STATEMENT,
  ADD_HFT_STRATEGY_STATEMENT,
  ADD_HFT_STRATEGY_ON_TABLE_STATEMENT,
  WEBSOCKET_STATEMENT,
  BINANCE_ORDER_BOOK_STATEMENT,
  BINANCE_API_KEY_STATEMENT,
  BINANCE_LIVE_ORDERS_STATEMENT,
  BINANCE_LIVE_OHLC_STATEMENT
  
};

enum class LogicalOperator { AND, OR };

enum class ComparisonOperator {
  EQUAL,
  NOT_EQUAL,
  GREATER,
  LESS,
  GREATER_EQUAL,
  LESS_EQUAL
};
enum class ColumnConstraint {
  NONE,
  NOT_NULL,
  PRIMARY_KEY,
  UNIQUE,
  AUTO_INCREMENT
};

// ==== AST Nodes ====

struct ASTNode {
  virtual ~ASTNode() = default;
  virtual ASTNodeType getType() const = 0;
};

struct Expression : public ASTNode {
  virtual ~Expression() = default;
};

using Value = std::variant<int64_t, std::string, bool>;
struct Row {
  std::unordered_map<std::string, Value> columns;
};

struct Identifier : public Expression {
  std::string name;
  Identifier(const std::string &name) : name(name) {}
  ASTNodeType getType() const override { return ASTNodeType::IDENTIFIER; }
};

struct IntLiteral : public Expression {
  int value;
  IntLiteral(int value) : value(value) {}
  ASTNodeType getType() const override { return ASTNodeType::INT_LITERAL; }
};

struct StringLiteral : public Expression {
  std::string value;
  StringLiteral(const std::string &value) : value(value) {}
  ASTNodeType getType() const override { return ASTNodeType::STRING_LITERAL; }
};

struct BoolLiteral : public Expression {
  bool value;
  BoolLiteral(bool value) : value(value) {}
  ASTNodeType getType() const override { return ASTNodeType::BOOLEAN_LITERAL; }
};

struct ComparisonExpression : public Expression {
  std::unique_ptr<Expression> left;
  ComparisonOperator op;
  std::unique_ptr<Expression> right;
  ComparisonExpression(std::unique_ptr<Expression> left, ComparisonOperator op,
                       std::unique_ptr<Expression> right)
      : left(std::move(left)), op(op), right(std::move(right)) {}
  ASTNodeType getType() const override {
    return ASTNodeType::COMPARISON_EXPRESSION;
  }
};

struct LogicalExpression : public Expression {
  std::unique_ptr<Expression> left;
  LogicalOperator op;
  std::unique_ptr<Expression> right;
  LogicalExpression(std::unique_ptr<Expression> left, LogicalOperator op,
                    std::unique_ptr<Expression> right)
      : left(std::move(left)), op(op), right(std::move(right)) {}
  ASTNodeType getType() const override {
    return ASTNodeType::LOGICAL_EXPRESSION;
  }
};
struct DisableStatement : ASTNode {
  int64_t ticks;
  std::string tableName;
  ASTNodeType getType() const override { return ASTNodeType::DISABLE_STATEMENT; }
  void print() {
    std::cout << std::format("the ticks is {} and tableName is {}", ticks,
                             tableName);
  }
};

struct ParenthesizedExpression : public Expression {
  std::unique_ptr<Expression> expression;
  ParenthesizedExpression(std::unique_ptr<Expression> expr)
      : expression(std::move(expr)) {}
  ASTNodeType getType() const override {
    return ASTNodeType::PARENTHESIZED_EXPRESSION;
  }
};

struct WhereClause : public ASTNode {
  std::unique_ptr<Expression> condition;
  WhereClause(std::unique_ptr<Expression> condition)
      : condition(std::move(condition)) {}
  ASTNodeType getType() const override { return ASTNodeType::WHERE_CLAUSE; }
};

struct LimitClause : public ASTNode {
  size_t limit;
  LimitClause(size_t limit) : limit(limit) {}
  ASTNodeType getType() const override { return ASTNodeType::LIMIT_CLAUSE; }
};

struct SelectStatement : public ASTNode {
  std::vector<std::string> columns;
  std::string table;
  std::unique_ptr<WhereClause> whereClause = nullptr;
  std::unique_ptr<LimitClause> limitClause = nullptr;

  ASTNodeType getType() const override { return ASTNodeType::SELECT_STATEMENT; }
};

struct UpdateStatement {
  std::string tableName;
  std::vector<std::pair<std::string, std::string>> assignments;
  std::unique_ptr<WhereClause> where;
};

struct DropStatement : public ASTNode {
  bool istable;
  std::string name;

  ASTNodeType getType() const override { return ASTNodeType::DROP_STATEMENT; }
};

struct StatisticsStatement : public ASTNode {
  std::string tableName;
  std::string colName;
  std::string type;
  std::unique_ptr<WhereClause> whereClause = nullptr;
  ASTNodeType getType() const override {
    return ASTNodeType::STATISTICS_STATEMENT;
  }
};

struct ColumnDefinition {
  std::string name;
  std::string type;
  int precision = 0;
  std::vector<ColumnConstraint> constraints;

  ColumnDefinition(const std::string &name, const std::string &type,
                   int16_t bit = 0)
      : name(name), type(type), precision(bit) {}
  void print() { std::cout << name << " " << type << " " << precision << "\n"; }
};

struct DeleteStatement : public ASTNode {
  std::string table;
  std::unique_ptr<WhereClause> whereClause = nullptr;

  ASTNodeType getType() const override { return ASTNodeType::DELETE_STATEMENT; }
};

struct Aggregates
{
    std::string type;  
    int64_t time;
    int32_t col_idx;      
    int64_t threshold;
    Aggregates(const std::string &type, int64_t time, int32_t col_idx, int64_t threshold){  
        this->type = type;
        this->time = time;
        this->col_idx = col_idx;
        this->threshold = threshold;
    }
}; 

struct CreateStatement : public ASTNode {
  bool isDatabase = false;
  std::string name;
  int32_t symbol = -1;
  bool top = false;
  std::vector<Aggregates>aggregates;
  std::vector<ColumnDefinition> columns;

  ASTNodeType getType() const override { return ASTNodeType::CREATE_STATEMENT; }
  void print() {
    for (auto e : columns) {
      e.print();
    }
    std::cout << "SYMBOL is " << symbol << "\n";
  }
};

struct AddHftIndicatorStatement : public ASTNode {

  // int64_t symbol;
  std::string file_path;
  // int64_t column_no;
  ASTNodeType getType() const override {
    return ASTNodeType::ADD_HFT_INDICATOR_STATEMENT;
  }
  void print() { std::cout << " the file path is " << file_path << "\n"; }
};

struct AddHftStrategyStatement : public ASTNode {

  // int64_t symbol;
  std::string file_path;
  // int64_t column_no;
  ASTNodeType getType() const override {
    return ASTNodeType::ADD_HFT_STRATEGY_STATEMENT;
  }
  void print() { std::cout << " the file path is " << file_path << "\n"; }
};


struct AddIndicatorOnTableStatement : public ASTNode {

  // name and path
  std::pair<std::string, std::string> indicator;
  std::vector<std::string> paramas; // parameter
  int64_t column_no = -1;
  int64_t symbol = -1;
  int64_t ticks = 1;
  ASTNodeType getType() const override {
    return ASTNodeType::ADD_HFT_INDICATOR_ON_TABLE_STATEMENT;
  }

  void print() {
    std::stringstream print;
    print << std::format("the column no is {} the symbol is {} the indicator "
                         "name is path is {} \n",
                         column_no, symbol, indicator.first, indicator.second);
    std::cout << print.str() << "\n";
  }
};


struct AddStrategyOnTableStatement : public ASTNode {

  // name and path
  std::pair<std::string, std::string> strategy;
  std::vector<std::string> paramas; // parameter
  int64_t symbol = -1;
  int64_t ticks = 1;
  ASTNodeType getType() const override {
    return ASTNodeType::ADD_HFT_STRATEGY_ON_TABLE_STATEMENT;
  }

  void print() {
    std::stringstream print;
    print << std::format(" the symbol is {} the strategy "
                         "name is path is {} \n",
                          symbol, strategy.first, strategy.second);
    std::cout << print.str() << "\n";
  }
};


struct LISTStatement : public ASTNode {

  bool isStrategy = false;
  bool isTable = false;
  std::string tableName = "";
  std::string message;
  ASTNodeType getType() const override { return ASTNodeType::LIST_STATEMENT; }
  void print() {
    // std::cout<<"the symobl is "<<symbol<<" the file path is "<<file_path <<"
    // the column no is "<<column_no<<"\n";
  }
};

struct InsertStatement {
  std::string tableName;
  std::vector<std::string> columns;
  std::vector<std::string> values;
};

struct EnableStatement : ASTNode {
  int64_t ticks;
  std::string tableName;
  ASTNodeType getType() const override { return ASTNodeType::ENABLE_STATEMENT; }
  void print() {
    std::cout << std::format("the ticks is {} and tableName is {}", ticks,
                             tableName);
  }
};

struct FetchIndicatorStatement:ASTNode{
  int64_t symbol;
  ASTNodeType getType() const override { return ASTNodeType::FETCH_INDICATOR_STATEMENT; }
  void print(){
    std::cout<<std::format("the symbol is {}",symbol);
  }
};


struct WebSocketCommand:ASTNode{
  std::string url;
  std::string strategyName;
  int64_t symbol;
  int64_t strategy_index;


   ASTNodeType getType() const override { return ASTNodeType::WEBSOCKET_STATEMENT; }
 

  void print(){
    std::cout<<std::format("the symbol is {}",symbol);
  }

};







/////// BINANCE ORDER BOOK

struct BinanceOrderBookStatement : ASTNode{
  int64_t tableSymbol;
  std::string binance_symbol ;


  void print(){
    std::cout << std::format("the tableSymbol is {} and binance symbol is {} \n",tableSymbol,binance_symbol) << std::endl;
  }
  
  ASTNodeType getType() const override { return ASTNodeType::BINANCE_ORDER_BOOK_STATEMENT; }
 
};


struct  BinanceAPIKEYStatement:ASTNode{
  std::string key;
  void print(){
    std::cout << std::format("the key is {} \n",key) << std::endl;
  }
   ASTNodeType getType() const override { return ASTNodeType::BINANCE_API_KEY_STATEMENT; }
 

};


struct Binance_LIVE_Orders_Statement:ASTNode{
  int64_t tableSymbol;
  std::string tradeSymbol;
  void print(){
    std::cout << std::format("the table symbol is {} \n",tableSymbol) << std::endl;
  }
   ASTNodeType getType() const override { return ASTNodeType::BINANCE_LIVE_ORDERS_STATEMENT; }
 

};

struct Binance_LIVE_OHLC_STATEMENT:ASTNode{
  int64_t tableSymbol;
  std::string time;
  std::string trade_symbol;

  void print(){
    std::cout << std::format("the table symbol is {} and the time is {} ",tableSymbol,time) << std::endl;
  }
   ASTNodeType getType() const override { return ASTNodeType::BINANCE_LIVE_OHLC_STATEMENT; }
 
};


#endif