
#ifndef __PARSER_AST_HPP
#define __PARSER_AST_HPP

#include <charconv>
#include <cstdint>
#include "hft.hpp"
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <memory>
#include <stdexcept>
#include "SQL_LEXER.hpp"
#include <filesystem> // Include for std::filesystem
#include <fstream>
#include "json.hpp"
#include "global.hpp"
#include "utility.hpp"
#include "generator.hpp"
#include "selectAstEvaluator.hpp"
#include "FastIndicators.hpp"
#include "initialLoad.hpp"

namespace fs = std::filesystem; // Shorthand for std::filesystem

bool fileExists(const std::string &filename)
{
    return fs::exists(filename);
}

// ==== Parser ====

class Parser
{
private:
    std::vector<Token *> tokens;
    size_t position = 0;

    Token *peek(int offset = 0)
    {
        if (position + offset >= tokens.size())
            return nullptr;
        return tokens[position + offset];
    }

    Token *current() { return peek(0); }

    Token *advance()
    {
        if (position < tokens.size())
            position++;
        return previous();
    }

    Token *previous()
    {
        if (position == 0)
            return nullptr;
        return tokens[position - 1];
    }

    void rewind()
    {
        if (position > 0)
            position--;
    }

    bool match(TokenType expected)
    {
        if (current() && current()->TYPE == expected)
        {
            advance();
            return true;
        }
        return false;
    }

    Token *expect(TokenType expected, const std::string &message)
    {
        if (match(expected))
            return previous();
        throw std::runtime_error("Parse error: " + message);
    }

public:
    std::string currentDb = "";
    Parser(const std::vector<Token *> &tokens) : tokens(tokens)
    {
        ensureCurrentDbFile("db/current_db.meta");
    }

    void ensureCurrentDbFile(const std::string &filePath)
    {
        fs::path parentDir = fs::path(filePath).parent_path();
        if (!parentDir.empty() && !fs::exists(parentDir))
        {
            std::error_code ec;
            if (fs::create_directories(parentDir, ec))
            {
                std::cout << "Created directory: " << parentDir << std::endl;
            }
            else
            {
                throw std::runtime_error("Error creating directory: " + ec.message());
            }
        }

        if (!fs::exists(filePath))
        {
            std::ofstream outfile(filePath);
            if (outfile.is_open())
            {
                outfile << "{\"current_db\":\"test\"}" << std::endl; // JSON-style default
                std::cout << "File '" << filePath << "' created and initialized." << std::endl;
                outfile.close();
                currentDatabase = "test";
            }
            else
            {
                throw std::runtime_error("Error: Could not create file '" + filePath + "'");
            }
        }
        else
        {
            std::ifstream in(filePath);
            std::stringstream buffer;
            buffer << in.rdbuf();
            in.close();
            std::string jsonContent = buffer.str();

            JSONParser jsonParser;
            if (!jsonParser.appendFromString(jsonContent))
            {
                throw std::runtime_error("Failed to parse current_db.meta JSON.");
            }
            JSONParser::JSONValue obj = jsonParser.getObject(0);

            if (std::holds_alternative<JSONParser::JSONObject>(obj.value))
            {
                auto jsonObj = std::get<JSONParser::JSONObject>(obj.value);
                if (jsonObj.find("current_db") != jsonObj.end())
                {
                    const auto &val = jsonObj["current_db"];
                    if (std::holds_alternative<std::string>(val.value))
                    {
                        this->currentDb = std::get<std::string>(val.value); // store it inside parser
                        currentDatabase = this->currentDb;
                    }
                }
            }
        }
    }

    std::unique_ptr<InsertStatement> parseInsertStatement() {
        expect(TokenType::INSERT, "Expected 'INSERT'");
        expect(TokenType::INTO, "Expected 'INTO'");

        Token *tableToken = expect(TokenType::IDENTIFIER, "Expected table name");
        std::unique_ptr<InsertStatement> stmt = std::make_unique<InsertStatement>();
        stmt->tableName = tableToken->VALUE;

        expect(TokenType::OPEN_PAREN, "Expected '(' before column list");

        // Parse columnsParse 
        do {
        Token *col = expect(TokenType::IDENTIFIER, "Expected column name");
        stmt->columns.push_back(col->VALUE);
        } while (match(TokenType::COMMA));

        expect(TokenType::CLOSE_PAREN, "Expected ')' after column list");

        expect(TokenType::VALUES, "Expected 'VALUES'");
        expect(TokenType::OPEN_PAREN, "Expected '(' before values");

        // Parse values
        do {
        if (match(TokenType::STRING) || match(TokenType::NUMBER)) {
            stmt->values.push_back(previous()->VALUE);
        } else {
            throw std::runtime_error("Expected a STRING in quotes or a NUMBER");
        }
        } while (match(TokenType::COMMA));

        expect(TokenType::CLOSE_PAREN, "Expected ')' after values");
        expect(TokenType::SEMICOLON, "Expected ';' at end");
        if (stmt->columns.size() != stmt->values.size()) {
        throw std::runtime_error("Number of columns and values do not match make "
                                "sure you does not pass the primary key column");
        }
        std::pair<bool, std::string> check =
            MyUtility::checkIfTableExist(stmt->tableName);
        if (!check.first)
        throw std::runtime_error(check.second);
        else {
        std::cout << "#### BEFORE COMMAND RUNNER #### \n";
        CommandRunner::generateInsertTableStatement(stmt);
        std::cout << "#### AFTER COMMAND RUNNER #### \n";
        }
        return stmt;
    }

    std::unique_ptr<UpdateStatement> parseUpdateStatement() {
        std::lock_guard<std::mutex> dbLock(dbMutex);

        auto stmt = std::make_unique<UpdateStatement>();

        // UPDATE
        expect(TokenType::UPDATE, "Expected UPDATE keyword");

        // table name
        Token* tableToken = expect(TokenType::IDENTIFIER, "Expected table name");
        stmt->tableName = tableToken->VALUE;

        // SET
        expect(TokenType::SET, "Expected SET keyword");

        // Parse assignments: col = value [, col = value]*
        while (true) {
            // column name
            Token* column = expect(TokenType::IDENTIFIER, "Expected column name");

            // =
            expect(TokenType::EQUAL, "Expected '=' in SET clause");

            // value
            Token* value;
            if (match(TokenType::STRING) || match(TokenType::NUMBER) || match(TokenType::IDENTIFIER)) {
                value = previous();
            } else {
                throw std::runtime_error("Invalid value in UPDATE SET clause");
            }

            stmt->assignments.push_back({
                column->VALUE,
                value->VALUE
            });

            if (!match(TokenType::COMMA))
                break;
        }

        // WHERE (mandatory)
        if (!match(TokenType::WHERE)) {
            throw std::runtime_error("UPDATE without WHERE is not allowed");
        }

        auto condition = parseExpression();
        stmt->where = std::make_unique<WhereClause>(std::move(condition));

        expect(TokenType::SEMICOLON, "Expected ';' after UPDATE statement");

        return stmt;
    }



    std::string parseUseStatement(){
        std::lock_guard<std::mutex> dbLock(dbMutex);
        expect(TokenType::USE, "Expected USE keyword");

        Token* dbName = expect(TokenType::IDENTIFIER, "Expected database name after USE");
        expect(TokenType::SEMICOLON, "Expected ';' after USE statement");

        std::string newDb = dbName->VALUE;
        std::stringstream filename;
        filename << "./db/" << newDb << ".shivam.db";

        if (!MyUtility::checkIfFileExist(filename.str()))
        {
            throw std::runtime_error("Database does not exist: " + newDb);
        }
        globalJsonCache.clear();
        globalTableCache.clear();
        dbBtrees.clear();
        currentDatabase = newDb;
        this->currentDb=newDb;
        MyUtility::changeCurrentDb(newDb);
        initialDatabseLoad();
        return newDb;
    }
    void parseDeleteStatement()
    {
        expect(TokenType::DELETE, "Expected DELETE keyword");
        expect(TokenType::FROM, "Expected FROM keyword");

        Token *table = expect(TokenType::IDENTIFIER, "Expected table name");
        std::string tableName = table->VALUE;

        // Create DELETE statement AST
        auto stmt = std::make_unique<DeleteStatement>();
        stmt->table = tableName;

        // Optional WHERE clause
        if (match(TokenType::WHERE))
        {
            auto condition = parseExpression();
            stmt->whereClause = std::make_unique<WhereClause>(std::move(condition));
        }

        expect(TokenType::SEMICOLON, "Expected ';' after DELETE statement");

        CommandRunner::handleDelete(stmt);
    }


    std::unique_ptr<CreateStatement> parseHFTCreateStatement(){
        if(currentDb.empty()){
             throw std::runtime_error("No database selected. Use USE <db_name>;");
        }
        
        std::unique_ptr<CreateStatement> stmt = std::make_unique<CreateStatement>();

        expect(TokenType::HFT, "Expected HFT KEYWORD");
        expect(TokenType::TABLE, "Expected TABLE KEYWORD");

        Token * tableName = expect(TokenType::IDENTIFIER, "Expected table name");
        stmt->name = tableName->VALUE;
         expect(TokenType::OPEN_PAREN, "Expected '(' after table name");

        while (!match(TokenType::CLOSE_PAREN)) {
            Token * colName = expect(TokenType::IDENTIFIER,"Expected column name");
            
            expect(TokenType::DOUBLE, "ONLY SUPPORT DOUBLE");
            Token * typeToken = previous(); // DOUBLE
           
            expect(TokenType::PRECISION, "use keyword precision after double");
            expect(TokenType::NUMBER, "expected bit precision to be a number");
            Token * bitToken = previous(); // BIT VALUE
            // std::cout<<"BIT TOKEN VALUE "<<bitToken->VALUE<<"\n";
            ColumnDefinition column(colName->VALUE,typeToken->VALUE,static_cast<int16_t>(std::stoi(bitToken->VALUE)));
            
            // column.print();

            stmt->columns.push_back(column);


                if (match(TokenType::COMMA))
                {
                    continue;
                }
                else if (peek()->TYPE == TokenType::CLOSE_PAREN)
                {
                    continue;
                }
                else
                {
                    throw std::runtime_error("Expected ',' or ')' in column list");
                }
            
        }

        expect(TokenType::SYMBOL, "use the symbol keyword\n");
        expect(TokenType::NUMBER, "the symbol should be a number");
        Token * sym = previous();
        int32_t symbol = static_cast<int>(std::stoi(sym->VALUE));
        if(symbol>(HFT::MAXHFTSYMBOL -1)){
            std::stringstream s; 
            s<<"the symbol value is not more than "<<(HFT::MAXHFTSYMBOL -1) <<"\n";
            std::runtime_error(s.str());
        }
        stmt->symbol = symbol;
        stmt->print();

        Token * curr = current();
        if(curr->TYPE != TokenType::SEMICOLON){
            expect(TokenType::TOP, "expected top variable for best bid and best ask price");
            stmt->top = true;
        }

        CommandRunner::generateHFTCreateStatement(stmt);
        
        return stmt;
    }

    std::unique_ptr<LISTStatement> parseListStatement(){
        rewind();
        std::unique_ptr<LISTStatement> statment = std::make_unique<LISTStatement>();
        expect(TokenType::LIST, "expect key word list");
        std::stringstream message;
        if(match(TokenType::STRATEGY)){
            for(auto it = HFT::InitalStorage::Indicators.begin(); it!=HFT::InitalStorage::Indicators.end();it++){
                std::string name = it->first;
                std::string file_path  = it->second.first;
                message << "indicator ";
                message <<GREEN<< name <<  RESET <<" file path is "<<GREEN<<file_path<< RESET << "\n";
                
            }
            statment->isStrategy = true;
            statment->message = message.str();
            expect(TokenType::SEMICOLON, "epect token type semi colon at end");
            return statment;
        }else if(match(TokenType::TABLE)){
            
        }

        throw std::runtime_error("error expect either strategy or TABLE TABLE_NAME with LIST");
    }


    std::unique_ptr<CreateStatement> parseCreateStatement()
    {
        expect(TokenType::CREATE, "Expected CREATE keyword");
        std::unique_ptr<CreateStatement> stmt = std::make_unique<CreateStatement>();

        if (match(TokenType::TABLE))
        {
            if (currentDb.empty()){
                throw std::runtime_error("No database selected. Use USE <db_name>;");
            }
            Token *tableName = expect(TokenType::IDENTIFIER, "Expected table name");
            stmt->name = tableName->VALUE;

            expect(TokenType::OPEN_PAREN, "Expected '(' after table name");

            while (!match(TokenType::CLOSE_PAREN))
            {
                Token *colName = expect(TokenType::IDENTIFIER, "Expected column name");

                Token *typeToken = current();
                if (match(TokenType::INT) || match(TokenType::VARCHAR))
                {

                    typeToken = previous();
                }
                else
                {
                    throw std::runtime_error("Parse error: Expected column type (int or varchar)");
                }

                ColumnDefinition column(colName->VALUE, typeToken->VALUE);

                // Handle VARCHAR(255) size syntax
                if (typeToken->TYPE == TokenType::VARCHAR && match(TokenType::OPEN_PAREN))
                {
                    Token *size = expect(TokenType::NUMBER, "Expected size in VARCHAR()");
                    expect(TokenType::CLOSE_PAREN, "Expected ')' after VARCHAR size");
                    column.type += "(" + size->VALUE + ")";

                    // std::cout<<"PARSER COLUMN TYPE \n" << "COLUMN TYPE "<< column.type <<"  size value  "<<size->VALUE<<"\n";
                }

                // Parse optional constraints
                while (true)
                {
                    if (match(TokenType::NOT))
                    {
                        expect(TokenType::NULL_T, "Expected NULL after NOT");
                        column.constraints.push_back(ColumnConstraint::NOT_NULL);
                    }
                    else if (match(TokenType::PRIMARY))
                    {
                        expect(TokenType::KEY, "Expected KEY after PRIMARY");
                        column.constraints.push_back(ColumnConstraint::PRIMARY_KEY);
                    }
                    else if (match(TokenType::AUTO_INCREMENT))
                    {
                        column.constraints.push_back(ColumnConstraint::AUTO_INCREMENT);
                    }
                    else if (match(TokenType::UNIQUE))
                    {
                        column.constraints.push_back(ColumnConstraint::UNIQUE);
                    }
                    else
                    {
                        break;
                    }
                }

                stmt->columns.push_back(column);

                if (match(TokenType::COMMA))
                {
                    continue;
                }
                else if (peek()->TYPE == TokenType::CLOSE_PAREN)
                {
                    continue;
                }
                else
                {
                    throw std::runtime_error("Expected ',' or ')' in column list");
                }
            }

            CommandRunner::generateCreateTableStatement(stmt);
        }
        else if (match(TokenType::DATABASE))
        {
            stmt->isDatabase = true;
            stmt->name = expect(TokenType::IDENTIFIER, "Expected database name")->VALUE;
            std::stringstream filename;
            filename << "./db/";
            filename << stmt->name;
            filename << ".shivam.db";

            if (MyUtility::checkIfFileExist(filename.str()))
            {
                throw std::runtime_error("Database already exists");
            }
            else
            {
                std::stringstream s;
                s << R"(
{
  "name": ")" << stmt->name
                  << R"(",
  "tables": []
}
)";
                MyUtility::createFile(filename.str(), s.str());
                currentDatabase = stmt->name;
                MyUtility::changeCurrentDb(currentDatabase);
            }
        }
        else
        {
            throw std::runtime_error("Expected TABLE or DATABASE keyword");
        }

        return stmt;
    }

    void parseMemoryStatement()
    {
        expect(TokenType::MEMORY, "Expected MEMORY keyword");

        std::string key;
        std::string value;
        int ttl = -1; // -1 means no expiry

        while (!match(TokenType::SEMICOLON))
        {
            if (match(TokenType::KEY))
            {
                expect(TokenType::EQUAL, "Expected '=' after KEY");

                if (match(TokenType::IDENTIFIER) || match(TokenType::STRING) || match(TokenType::NUMBER))
                {
                    key = previous()->VALUE;
                }
                else
                {
                    throw std::runtime_error("Expected identifier or string after KEY=");
                }
            }
            else if (match(TokenType::VALUES))
            {
                expect(TokenType::EQUAL, "Expected '=' after VALUE");

                if (match(TokenType::IDENTIFIER) || match(TokenType::STRING) || match(TokenType::NUMBER))
                {
                    value = previous()->VALUE;
                }
                else
                {
                    throw std::runtime_error("Expected identifier or string after VALUE=");
                }
            }
            else if (match(TokenType::TTL))
            {
                expect(TokenType::EQUAL, "Expected '=' after TTL");

                Token *num = expect(TokenType::NUMBER, "Expected number after TTL=");
                ttl = std::stoi(num->VALUE);
            }
            else
            {
                throw std::runtime_error(
                    "Unexpected token in MEMORY statement: " +
                    typeToString(current()->TYPE));
            }
        }

        if (key.empty())
            throw std::runtime_error("MEMORY command missing KEY");

        if (value.empty())
            throw std::runtime_error("MEMORY command missing VALUE");

        CommandRunner::memorySet(key, value, ttl);

        std::cout << "MEMORY SET: " << key << " = " << value;
        if (ttl >= 0)
            std::cout << " (TTL=" << ttl << "s)";
        std::cout << "\n";

    }

    void parseGetMemoryStatement()
    {
        expect(TokenType::MEMORY, "Expected MEMORY keyword");
        if (match(TokenType::GET))
        {
            expect(TokenType::KEY, "Expected KEY after MEMORY GET");
            expect(TokenType::EQUAL, "Expected '=' after KEY");

            Token* keyTok;
            if (match(TokenType::IDENTIFIER) || match(TokenType::STRING) || match(TokenType::NUMBER)) {
                keyTok = previous();
            } else {
                throw std::runtime_error("Expected identifier/string/number after KEY=");
            }
            
            expect(TokenType::SEMICOLON, "Expected ';'");

            const std::string& key = keyTok->VALUE;
            std::string value;
            if (CommandRunner::memoryGet(key, value)) {
                std::cout << "MEMORY GET: " << key << " = " << value << "\n";
            } else {
                std::cout << "MEMORY GET: key '" << key << "' not found or expired\n";
            }
        }

    }


    std::unique_ptr<DropStatement> parseDropStatement()
    {
        expect(TokenType::DROP, "Expected drop keyword");
        if (currentDb.empty()){
                throw std::runtime_error("No database selected. Use USE <db_name>;");
        }
        auto stmt = std::make_unique<DropStatement>();
        Token *token = advance();
        switch (token->TYPE)
        {
        case TokenType::TABLE:
        {
            Token *identifier = expect(TokenType::IDENTIFIER, "not a identifier\n");

            stmt->name = identifier->VALUE;
            stmt->istable = true;
            CommandRunner::generateDropStatement(stmt);
            break;
        }

        break;
        case TokenType::DATABASE:
        {
            Token *identifier = expect(TokenType::IDENTIFIER, "not a identifier\n");
            stmt->name = identifier->VALUE;
            stmt->istable = false;
            CommandRunner::generateDropStatement(stmt);
            globalJsonCache.clear();
            globalTableCache.clear();
            dbBtrees.clear();
            MyUtility::changeCurrentDb("");
            currentDatabase="";
            currentDb="";
            break;
        }

        default:
            throw std::runtime_error("error in drop ");
        }
        return stmt;
    }

    
    std::unique_ptr<AddIndicatorOnTableStatement> parseAddIndicatorOnTableStatement(){
            expect(TokenType::ADD, "expect Token type add");
            expect(TokenType::INDICATOR, "expect token type indictor");

            
            std::string indicatorName ;
            Token * indicator = expect(TokenType::STRING, "expect indicator name as string");
            indicatorName = indicator->VALUE;

            expect(TokenType::ON, "expect token ON after indicator");
            expect(TokenType::SYMBOL,"expect token type symbol");

            int64_t symbol ;
            Token * symBolToken = expect(TokenType::INT, "expect symbol to be int");
            std::from_chars(symBolToken->VALUE.data(),symBolToken->VALUE.data() +  symBolToken->VALUE.size(),symbol);

            expect(TokenType::COLUMN_NO, "expect token COLUMN_NO");
            int64_t columnNo;

            if(match(TokenType::MINUS)){
                Token *columnSymbol = expect(TokenType::NUMBER, "expect a number");
                columnNo = -1;
                //  std::from_chars(columnSymbol->VALUE.data(),columnSymbol->VALUE.data() +  columnSymbol->VALUE.size(),columnNo);
            }else{
                rewind();
                Token *columnSymbol = expect(TokenType::NUMBER, "expect a number");
                 std::from_chars(columnSymbol->VALUE.data(),columnSymbol->VALUE.data() +  columnSymbol->VALUE.size(),columnNo);
            }
            
            
            expect(TokenType::SEMICOLON, "expect token ;");

    }

    std::unique_ptr<SelectStatement> parseSelectStatement()
    {
        expect(TokenType::SELECT, "Expected SELECT keyword");

    if (currentDb.empty()){
            throw std::runtime_error("No database selected. Use USE <db_name>;");
        }


    auto stmt = std::make_unique<SelectStatement>();

        // Handle SELECT *
        if (match(TokenType::MULTIPLY))
        {
            stmt->columns.push_back("*");
        }
        else
        {
            // SELECT col1, col2, ...
            while (true)
            {
                Token *column = expect(TokenType::IDENTIFIER, "Expected column name");
                stmt->columns.push_back(column->VALUE);

                if (!match(TokenType::COMMA))
                    break;
            }
        }

        expect(TokenType::FROM, "Expected FROM keyword");
        Token *table = expect(TokenType::IDENTIFIER, "Expected table name");
        stmt->table = table->VALUE;

        // Optional WHERE clause
        if (match(TokenType::WHERE))
        {
            auto condition = parseExpression();
            stmt->whereClause = std::make_unique<WhereClause>(std::move(condition));
        }

        // Optional LIMIT clause
        if (match(TokenType::IDENTIFIER) && previous()->VALUE == "limit")
        {
            Token *limitValue = expect(TokenType::NUMBER, "Expected number after LIMIT");
            stmt->limitClause = std::make_unique<LimitClause>(std::stoi(limitValue->VALUE));
        }

        return stmt;
    }

    // =====================
    // Expression Parsing
    // =====================

    std::unique_ptr<Expression> parseExpression()
    {
        return parseLogical();
    }

    std::unique_ptr<Expression> parseLogical()
    {
        auto left = parseComparison();

        while (match(TokenType::AND) || match(TokenType::OR))
        {
            LogicalOperator op = (previous()->TYPE == TokenType::AND)
                                     ? LogicalOperator::AND
                                     : LogicalOperator::OR;

            auto right = parseComparison();
            left = std::make_unique<LogicalExpression>(std::move(left), op, std::move(right));
        }

        return left;
    }

    std::unique_ptr<Expression> parseComparison()
    {
        auto left = parsePrimary();

        if (match(TokenType::EQUAL) || match(TokenType::NOT_EQUAL) ||
            match(TokenType::GREATER) || match(TokenType::LESS) ||
            match(TokenType::GREATER_EQUAL) || match(TokenType::LESS_EQUAL))
        {
            TokenType opToken = previous()->TYPE;
            ComparisonOperator op;

            switch (opToken)
            {
            case TokenType::EQUAL:
                op = ComparisonOperator::EQUAL;
                break;
            case TokenType::NOT_EQUAL:
                op = ComparisonOperator::NOT_EQUAL;
                break;
            case TokenType::GREATER:
                op = ComparisonOperator::GREATER;
                break;
            case TokenType::LESS:
                op = ComparisonOperator::LESS;
                break;
            case TokenType::GREATER_EQUAL:
                op = ComparisonOperator::GREATER_EQUAL;
                break;
            case TokenType::LESS_EQUAL:
                op = ComparisonOperator::LESS_EQUAL;
                break;
            default:
                throw std::runtime_error("Invalid comparison operator");
            }

            auto right = parsePrimary();
            return std::make_unique<ComparisonExpression>(std::move(left), op, std::move(right));
        }

        return left;
    }

    std::unique_ptr<Expression> parsePrimary()
    {
        if (match(TokenType::OPEN_PAREN))
        {
            auto expr = parseExpression();
            expect(TokenType::CLOSE_PAREN, "Expected ')'");
            return std::make_unique<ParenthesizedExpression>(std::move(expr));
        }

        if (match(TokenType::IDENTIFIER))
        {
            std::string val = previous()->VALUE;
            if (val == "true" || val == "false")
                return std::make_unique<BoolLiteral>(val == "true");
            return std::make_unique<Identifier>(val);
        }

        if (match(TokenType::NUMBER))
        {
            return std::make_unique<IntLiteral>(std::stoi(previous()->VALUE));
        }

        if (match(TokenType::STRING))
        {
            return std::make_unique<StringLiteral>(previous()->VALUE);
        }

        throw std::runtime_error("Unexpected token in expression");
    }

    // =====================
    // AST Printing Utility
    // =====================

    void printExpression(const Expression *expr, int indent = 0)
    {
        auto pad = [indent]()
        { for (int i = 0; i < indent; ++i) std::cout << "  "; };

        if (!expr)
            return;

        switch (expr->getType())
        {
        case ASTNodeType::IDENTIFIER:
        {
            const auto *id = static_cast<const Identifier *>(expr);
            pad();
            std::cout << "Identifier: " << id->name << "\n";
            break;
        }
        case ASTNodeType::INT_LITERAL:
        {
            const auto *num = static_cast<const IntLiteral *>(expr);
            pad();
            std::cout << "IntLiteral: " << num->value << "\n";
            break;
        }
        case ASTNodeType::STRING_LITERAL:
        {
            const auto *str = static_cast<const StringLiteral *>(expr);
            pad();
            std::cout << "StringLiteral: \"" << str->value << "\"\n";
            break;
        }
        case ASTNodeType::BOOLEAN_LITERAL:
        {
            const auto *b = static_cast<const BoolLiteral *>(expr);
            pad();
            std::cout << "BoolLiteral: " << (b->value ? "true" : "false") << "\n";
            break;
        }
        case ASTNodeType::COMPARISON_EXPRESSION:
        {
            const auto *comp = static_cast<const ComparisonExpression *>(expr);
            pad();
            std::cout << "ComparisonExpression: ";
            switch (comp->op)
            {
            case ComparisonOperator::EQUAL:
                pad();
                std::cout << "==\n";
                break;
            case ComparisonOperator::NOT_EQUAL:
                std::cout << "!=\n";
                break;
            case ComparisonOperator::GREATER:
                std::cout << ">\n";
                break;
            case ComparisonOperator::LESS:
                std::cout << "<\n";
                break;
            case ComparisonOperator::GREATER_EQUAL:
                std::cout << ">=\n";
                break;
            case ComparisonOperator::LESS_EQUAL:
                std::cout << "<=\n";
                break;
            }
            printExpression(comp->left.get(), indent + 1);
            printExpression(comp->right.get(), indent + 1);
            break;
        }
        case ASTNodeType::LOGICAL_EXPRESSION:
        {
            const auto *log = static_cast<const LogicalExpression *>(expr);
            pad();
            std::cout << "LogicalExpression: " << (log->op == LogicalOperator::AND ? "AND" : "OR") << "\n";
            printExpression(log->left.get(), indent + 1);
            printExpression(log->right.get(), indent + 1);
            break;
        }
        case ASTNodeType::PARENTHESIZED_EXPRESSION:
        {
            const auto *paren = static_cast<const ParenthesizedExpression *>(expr);
            pad();
            std::cout << "ParenthesizedExpression:\n";
            printExpression(paren->expression.get(), indent + 1);
            break;
        }
        default:
            pad();
            std::cout << "Unknown Expression Type\n";
        }
    }

    std::string parse() {
        std::string r = "{\"suceess\" : true}";
        if (match(TokenType::CREATE) ) {
            if(match(TokenType::HFT)){
                // rewind();
                rewind();
                auto stmt = parseHFTCreateStatement();
                return r;
            }
        rewind(); // Go back one token to reprocess CREATE in parseCreateStatement
        try {
            auto stmt = parseCreateStatement();
            printCreateStatement(*stmt);
            return r;
        } catch (const std::exception &err) {
            std::stringstream e;
            e << "{" << "\"success\": false, " << "\"error\": \"\033[31m"
            << err.what() << "\033[0m\"" << "}";

            return e.str();
        }
        
        } 
        else if(match(TokenType::LIST)){
            try {
                std::unique_ptr<LISTStatement> statement = parseListStatement();
                return statement->message;
            } catch (const std::exception & err) {
                 std::stringstream e;
            e << "{" << "\"success\": false, " << "\"error\": \"\033[31m"
            << err.what() << "\033[0m\"" << "}";

            return e.str();
            }
        }
        else if (match(TokenType::INSERT)) {
        rewind();
        try {

            auto stmt = parseInsertStatement();
            printInsertStatement(*stmt);
            return r;
        } catch (const std::exception &err) {
            std::stringstream e;
            e << "{" << "\"success\": false, " << "\"error\": \"\033[31m"
            << err.what() << "\033[0m\"" << "}";

            return e.str();
        }
        } else if (match(TokenType::SELECT)) {
        try {
            rewind();
            auto stmt = parseSelectStatement();
            std::string json = SelectQueryHandler::handle(stmt);
            std::cout << "SELECT STATEMENT JSON \n";
            std::cout << json << "\n";
            printSelectStatement(*stmt);
            return json;
        } catch (const std::exception &err) {
            std::stringstream e;
            e << "{" << "\"success\": false, " << "\"error\": \"\033[31m"
            << err.what() << "\033[0m\"" << "}";

            return e.str();
        }
        } else if (match(TokenType::DROP)) {
        try {

            rewind();
            auto stmt = parseDropStatement();
            return r;
        } catch (const std::exception &err) {
            std::stringstream e;
            e << "{" << "\"success\": false, " << "\"error\": \"\033[31m"
            << err.what() << "\033[0m\"" << "}";

            return e.str();
        }
        } else if (match(TokenType::USE)) {
        try {

            rewind();
            auto stmt = parseUseStatement();
            return r;
        } catch (const std::exception &err) {
            std::stringstream e;
            e << "{" << "\"success\": false, " << "\"error\": \"\033[31m"
            << err.what() << "\033[0m\"" << "}";

            return e.str();
        }
        }else if (match(TokenType::DELETE)) {
        try {

            rewind();
            parseDeleteStatement();
            return r;
        } catch (const std::exception &err) {
            std::stringstream e;
            e << "{" << "\"success\": false, " << "\"error\": \"\033[31m"
            << err.what() << "\033[0m\"" << "}";

            return e.str();
        }
        } else if (match(TokenType::UPDATE)) {

        try {
            rewind();
            auto stmt = parseUpdateStatement();
            CommandRunner::handleUpdate(stmt);
            return r;
        } catch (const std::exception &err) {
            std::stringstream e;
            e << "{"
            << "\"success\": false, "
            << "\"error\": \"\033[31m"
            << err.what()
            << "\033[0m\""
            << "}";
            return e.str();
        }
        } else if (match(TokenType::MEMORY)) {
        try {

            rewind();
            if (peek(1) && peek(1)->TYPE == TokenType::GET) {
                parseGetMemoryStatement();
            } else {
                parseMemoryStatement();
            }
            return r;
        } catch (const std::exception &err) {
            std::stringstream e;
            e << "{" << "\"success\": false, " << "\"error\": \"\033[31m"
            << err.what() << "\033[0m\"" << "}";

            return e.str();
        }
        } 
        
        else if(match(TokenType::ADD)){
            if(match(TokenType::INDICATOR)){
                rewind();
                rewind();

                return r;   
            }
            rewind();
            rewind();
            try {
            std::unique_ptr<AddHftIndicatorStatement> statement = parseAddHftIndicatorStatement();
            statement->print();
            try {
            IndicatorHandler::parseIndicators(std::move(statement));
            } catch (const std::exception & err) {
                std::stringstream e;
                  e << "{" << "\"success\": false, " << "\"error\": \"\033[31m"
            << err.what() << "\033[0m\"" << "}";

            return e.str();
            }
            return r;
            } catch (const std::exception &err) {
                std::stringstream e;
                  e << "{" << "\"success\": false, " << "\"error\": \"\033[31m"
            << err.what() << "\033[0m\"" << "}";

            return e.str();
            }
        }else {
        throw std::runtime_error("Unsupported SQL statement or missing statement "
                                "type (CREATE, INSERT, SELECT, DROP, USE, MEMORY)");
        }
    }


    std::unique_ptr<AddHftIndicatorStatement> parseAddHftIndicatorStatement(){
        expect(TokenType::ADD, "expected token type add");
        expect(TokenType::HFT, "expect token type HFT");
        if(match(TokenType::INDICATOR)){
            std::string file_path;
            int64_t symbol;
            int64_t column_no = -1;
            expect(TokenType::FROM, "expect token type from");
            expect(TokenType::FILE, "expect token type file");

            Token * file = expect(TokenType::STRING, "expect a file path in string ");
            file_path = file->VALUE;

            // expect(TokenType::ON, "expected on keyword after file path");
            // expect(TokenType::SYMBOL, "expect tooken type symbol");

            // Token * symbolToken = expect(TokenType::NUMBER, "expected symbol to be a number");
            // std::from_chars(symbolToken->VALUE.data(),symbolToken->VALUE.data()+symbolToken->VALUE.size(),symbol);

            // if(match(TokenType::COLUMN_NO)){
            //     expect(TokenType::EQUAL, "expected token = after column no ");

            //     Token * columnToken = expect(TokenType::NUMBER, "expected column to be a number");
            //      std::from_chars(columnToken->VALUE.data(),columnToken->VALUE.data() + columnToken->VALUE.size(),column_no);
            // }else{
            //     expect(TokenType::SEMICOLON, "expected ;");
            // }

            expect(TokenType::SEMICOLON, "expected token ;");
            std::unique_ptr<AddHftIndicatorStatement> statement = std::make_unique<AddHftIndicatorStatement>();
            // statement->symbol = symbol;
            statement->file_path = file_path;
            // statement->column_no   = column_no;

            return std::move(statement);
        }
    }

    void printUseStatement(std::string &dbname){
        std::cout<<"USE"<<" ";
        std::cout<<dbname<<";"<<"\n";
    }

    void printDropStatement(const DropStatement &stmt){
        bool table=stmt.istable;
        std::cout<<"DROP"<<" ";
        if (table){
            std::cout<<"TABLE"<<" "<<stmt.name<<";";
        }else{
            std::cout<<"DATABASE"<<" "<<stmt.name<<";"<<"\n";
        }
    }

    void printSelectStatement(const SelectStatement &stmt, int indent = 0)
    {
        auto pad = [indent]()
        { for (int i = 0; i < indent; ++i) std::cout << "  "; };

        pad();
        std::cout << "SelectStatement\n";

        pad();
        std::cout << "  Columns:\n";
        for (const auto &col : stmt.columns)
        {
            pad();
            std::cout << "    - " << col << "\n";
        }

        pad();
        std::cout << "  From: " << stmt.table << "\n";

        if (stmt.whereClause)
        {
            pad();
            std::cout << "  Where:\n";
            printExpression(stmt.whereClause->condition.get(), indent + 2);
        }

        if (stmt.limitClause)
        {
            pad();
            std::cout << "  Limit: " << stmt.limitClause->limit << "\n";
        }
    }
    void printCreateStatement(const CreateStatement &stmt)
    {
        std::cout << "CREATE ";
        if (stmt.isDatabase)
        {
            std::cout << "DATABASE ";
        }
        else
        {
            std::cout << "TABLE ";
        }

        std::cout << stmt.name << "\n";

        if (!stmt.isDatabase)
        {
            for (const auto &col : stmt.columns)
            {
                std::cout << "  Column: " << col.name << " Type: " << col.type << "\n";

                for (const auto &constraint : col.constraints)
                {
                    std::string constraintStr;
                    switch (constraint)
                    {
                    case ColumnConstraint::NOT_NULL:
                        constraintStr = "NOT NULL";
                        break;
                    case ColumnConstraint::PRIMARY_KEY:
                        constraintStr = "PRIMARY KEY";
                        break;
                    case ColumnConstraint::AUTO_INCREMENT:
                        constraintStr = "AUTO_INCREMENT";
                        break;
                    case ColumnConstraint::UNIQUE:
                        constraintStr = "UNIQUE";
                        break;
                    default:
                        constraintStr = "UNKNOWN";
                        break;
                    }
                    std::cout << "    Constraint: " << constraintStr << "\n";
                }
            }
        }
    }

    void printInsertStatement(const InsertStatement &stmt)
    {
        std::cout << "INSERT INTO " << stmt.tableName << " (\n";
        for (const auto &col : stmt.columns)
        {
            std::cout << "  " << col << "\n";
        }
        std::cout << ") VALUES (\n";
        for (const auto &val : stmt.values)
        {
            std::cout << "  " << val << "\n";
        }
        std::cout << ");\n";
    }
};

#endif