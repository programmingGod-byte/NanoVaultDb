#ifndef GLOBALS_HPP
#define GLOBALS_HPP

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <memory>
#include <utility>
#include <variant>
#include <vector>
#include <climits>
#include <chrono>
#include <queue>
#include <shared_mutex>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>

#include "databaseSchemaReader.hpp"
#include "storageTree.hpp"

#define RESET "\033[0m"
#define GREEN "\033[1;32m"
#define YELLOW "\033[1;33m"
#define CYAN "\033[1;36m"

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

    bool operator>(const ExpiryNode& other) const {
        return expiry > other.expiry;
    }
};

extern std::priority_queue<
    ExpiryNode,
    std::vector<ExpiryNode>,
    std::greater<>
> expiryHeap;

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
struct TableGlobalColumnNode
{
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

// --- JSON Parser Cache ---
// db_name -> JSON parser
std::unordered_map<std::string, std::shared_ptr<PythonLikeJSONParser>> globalJsonCache;
std::unordered_map<std::string, MemoryEntry> memoryStore;
std::shared_mutex memoryMutex;
std::unordered_map<std::string, std::mutex> tableLocks;
std::mutex dbMutex;

std::atomic<bool> shuttingDown{false};
std::thread vacuumThread;

std::priority_queue<
    ExpiryNode,
    std::vector<ExpiryNode>,
    std::greater<>
> expiryHeap;

std::mutex expiryMutex;
std::condition_variable expiryCV;
std::atomic<bool> memorySchedulerRunning{true};

// --- Table Metadata Cache ---
// db_name -> table_name -> vector of column definitions
std::unordered_map<
    std::string,
    std::unordered_map<
        std::string,
        std::vector<std::shared_ptr<TableGlobalColumnNode>>>>
    globalTableCache;








struct IndexNode
{
    int64_t start;
    int16_t end;
};

// --- B+ Tree Variant for different key types ---
// only int64_t and string call be index in the b+ trees
using TreeVariant = std::variant<
    std::shared_ptr<BPlusTree<int64_t, IndexNode>>,
    std::shared_ptr<BPlusTree<std::string, IndexNode>>>;

// --- B+ Tree Cache ---
// db_name -> table_name -> column_name -> (B+ Tree, no of columns)
std::unordered_map<
    std::string,
    std::unordered_map<
        std::string,
        std::unordered_map<
            std::string,
            std::pair<TreeVariant, int64_t>>>>
    dbBtrees;

enum class ASTNodeType
{
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



    // HFT_STATEMENT
    ADD_HFT_INDICATOR_STATEMENT,
    ADD_HFT_INDICATOR_ON_TABLE_STATEMENT
};

enum class LogicalOperator
{
    AND,
    OR
};

enum class ComparisonOperator
{
    EQUAL,
    NOT_EQUAL,
    GREATER,
    LESS,
    GREATER_EQUAL,
    LESS_EQUAL
};
enum class ColumnConstraint
{
    NONE,
    NOT_NULL,
    PRIMARY_KEY,
    UNIQUE,
    AUTO_INCREMENT
};

// ==== AST Nodes ====

struct ASTNode
{
    virtual ~ASTNode() = default;
    virtual ASTNodeType getType() const = 0;
};

struct Expression : public ASTNode
{
    virtual ~Expression() = default;
};

using Value = std::variant<int64_t, std::string, bool>;
struct Row
{
    std::unordered_map<std::string, Value> columns;
};

struct Identifier : public Expression
{
    std::string name;
    Identifier(const std::string &name) : name(name) {}
    ASTNodeType getType() const override { return ASTNodeType::IDENTIFIER; }
};

struct IntLiteral : public Expression
{
    int value;
    IntLiteral(int value) : value(value) {}
    ASTNodeType getType() const override { return ASTNodeType::INT_LITERAL; }
};

struct StringLiteral : public Expression
{
    std::string value;
    StringLiteral(const std::string &value) : value(value) {}
    ASTNodeType getType() const override { return ASTNodeType::STRING_LITERAL; }
};

struct BoolLiteral : public Expression
{
    bool value;
    BoolLiteral(bool value) : value(value) {}
    ASTNodeType getType() const override { return ASTNodeType::BOOLEAN_LITERAL; }
};

struct ComparisonExpression : public Expression
{
    std::unique_ptr<Expression> left;
    ComparisonOperator op;
    std::unique_ptr<Expression> right;
    ComparisonExpression(std::unique_ptr<Expression> left, ComparisonOperator op, std::unique_ptr<Expression> right)
        : left(std::move(left)), op(op), right(std::move(right)) {}
    ASTNodeType getType() const override { return ASTNodeType::COMPARISON_EXPRESSION; }
};

struct LogicalExpression : public Expression
{
    std::unique_ptr<Expression> left;
    LogicalOperator op;
    std::unique_ptr<Expression> right;
    LogicalExpression(std::unique_ptr<Expression> left, LogicalOperator op, std::unique_ptr<Expression> right)
        : left(std::move(left)), op(op), right(std::move(right)) {}
    ASTNodeType getType() const override { return ASTNodeType::LOGICAL_EXPRESSION; }
};

struct ParenthesizedExpression : public Expression
{
    std::unique_ptr<Expression> expression;
    ParenthesizedExpression(std::unique_ptr<Expression> expr)
        : expression(std::move(expr)) {}
    ASTNodeType getType() const override { return ASTNodeType::PARENTHESIZED_EXPRESSION; }
};

struct WhereClause : public ASTNode
{
    std::unique_ptr<Expression> condition;
    WhereClause(std::unique_ptr<Expression> condition) : condition(std::move(condition)) {}
    ASTNodeType getType() const override { return ASTNodeType::WHERE_CLAUSE; }
};

struct LimitClause : public ASTNode
{
    size_t limit;
    LimitClause(size_t limit) : limit(limit) {}
    ASTNodeType getType() const override { return ASTNodeType::LIMIT_CLAUSE; }
};

struct SelectStatement : public ASTNode
{
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

struct DropStatement : public ASTNode
{
    bool istable;
    std::string name;

    ASTNodeType getType() const override { return ASTNodeType::DROP_STATEMENT; }
};

struct ColumnDefinition
{
    std::string name;
    std::string type;
    int precision = 0;
    std::vector<ColumnConstraint> constraints;

    ColumnDefinition(const std::string &name, const std::string &type,int16_t bit = 0)
        : name(name), type(type),precision(bit) {}
    void print(){
        std::cout<<name<<" "<<type<<" "<<precision<<"\n";
    }
};

struct DeleteStatement : public ASTNode
{
    std::string table;
    std::unique_ptr<WhereClause> whereClause = nullptr;

    ASTNodeType getType() const override
    {
        return ASTNodeType::DELETE_STATEMENT;
    }
};


struct CreateStatement : public ASTNode
{
    bool isDatabase = false;
    std::string name;
    int32_t symbol = -1;
    bool top = false;
    std::vector<ColumnDefinition> columns;

    ASTNodeType getType() const override { return ASTNodeType::CREATE_STATEMENT; }
    void print(){
        for(auto e:columns){
            e.print();
        }
        std::cout<<"SYMBOL is "<<symbol<<"\n";
    }
};


struct AddHftIndicatorStatement : public ASTNode{

    // int64_t symbol;
    std::string file_path;
    // int64_t column_no;
    ASTNodeType getType() const override { return ASTNodeType::ADD_HFT_INDICATOR_STATEMENT; }
    void print(){
        std::cout<<" the file path is "<<file_path <<"\n";
    }
}; 


struct AddIndicatorOnTableStatement: public ASTNode{

    std::pair<std::string,std::string> indicator;
    int64_t column_no = -1;
    int64_t symbol  = -1;
    ASTNodeType getType() const override { return ASTNodeType::ADD_HFT_INDICATOR_ON_TABLE_STATEMENT; }
    
    
};

struct LISTStatement: public ASTNode{

    bool isStrategy = false;
    bool isTable = false;
    std::string tableName = "";
    std::string message;
    ASTNodeType getType() const override { return ASTNodeType::LIST_STATEMENT; }
    void print(){
        // std::cout<<"the symobl is "<<symbol<<" the file path is "<<file_path <<" the column no is "<<column_no<<"\n";
    }
};

struct InsertStatement
{
    std::string tableName;
    std::vector<std::string> columns;
    std::vector<std::string> values;
};

#endif
