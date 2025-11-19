#ifndef __LEXER_H
#define __LEXER_H

#include <unordered_map>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <stdexcept>

enum class TokenType
{
    // Keywords
    SELECT,
    FROM,
    WHERE,
    INSERT,
    INTO,
    VALUES,
    CREATE,
    TABLE,
    DELETE,
    UPDATE,
    SET,
    AND,
    OR,
    NOT,
    ORDER,
    BY,
    DROP,
    DATABASE,
    ORDERBY,
    AUTO_INCREMENT,
    NULL_T,UNIQUE,
    
     INT, VARCHAR, PRIMARY, KEY,

    // Literals
    IDENTIFIER,
    STRING,
    NUMBER,
    STAR,

    // Operators
    PLUS,
    MINUS,
    MULTIPLY,
    SLASH,
    PERCENT,
    EQUAL,
    GREATER,
    LESS,
    GREATER_EQUAL,
    LESS_EQUAL,
    NOT_EQUAL,
    DOUBLE_EQUAL,

    // Punctuation
    COMMA,
    SEMICOLON,
    DOT,
    OPEN_PAREN,
    CLOSE_PAREN,

    // End of file
    END_OF_FILE
};

// Make these static const to avoid multiple definition errors
static const std::unordered_map<std::string, TokenType> keywords = {
    {"select", TokenType::SELECT},
    {"from", TokenType::FROM},
    {"where", TokenType::WHERE},
    {"insert", TokenType::INSERT},
    {"into", TokenType::INTO},
    {"values", TokenType::VALUES},
    {"create", TokenType::CREATE},
    {"table", TokenType::TABLE},
    {"delete", TokenType::DELETE},
    {"update", TokenType::UPDATE},
    {"set", TokenType::SET},
    {"and", TokenType::AND},
    {"or", TokenType::OR},
    {"not", TokenType::NOT},
    {"order", TokenType::ORDER},
    {"by", TokenType::BY},
    {"drop",TokenType::DROP},
    {"database",TokenType::DATABASE},
    {"orderby",TokenType::ORDERBY},
    {"table",TokenType::TABLE},
    {"int",TokenType::INT},
    {"varchar",TokenType::VARCHAR},
    {"primary",TokenType::PRIMARY},
    {"key",TokenType::KEY},
    {"auto_increment",TokenType::AUTO_INCREMENT},
    {"null",TokenType::NULL_T},
    {"unique",TokenType::UNIQUE}
};

static const std::unordered_map<char, TokenType> singleCharTokens = {
    {'+', TokenType::PLUS},
    {'-', TokenType::MINUS},
    {'*', TokenType::MULTIPLY},
    {'/', TokenType::SLASH},
    {'%', TokenType::PERCENT},
    {'(', TokenType::OPEN_PAREN},
    {')', TokenType::CLOSE_PAREN},
    {',', TokenType::COMMA},
    {';', TokenType::SEMICOLON},
    {'.', TokenType::DOT}
};
std::string typeToString(TokenType TYPE)
{
    switch (TYPE)
    {
    // Keywords
    case TokenType::SELECT: return "SELECT";
    case TokenType::FROM: return "FROM";
    case TokenType::WHERE: return "WHERE";
    case TokenType::INSERT: return "INSERT";
    case TokenType::INTO: return "INTO";
    case TokenType::VALUES: return "VALUES";
    case TokenType::CREATE: return "CREATE";
    case TokenType::TABLE: return "TABLE";
    case TokenType::DELETE: return "DELETE";
    case TokenType::UPDATE: return "UPDATE";
    case TokenType::SET: return "SET";
    case TokenType::AND: return "AND";
    case TokenType::OR: return "OR";
    case TokenType::NOT: return "NOT";
    case TokenType::ORDER: return "ORDER";
    case TokenType::BY: return "BY";
    case TokenType::DROP: return "DROP";           // Added this
    case TokenType::DATABASE: return "DATABASE";   // Added this
    case TokenType::ORDERBY: return "ORDERBY";     // Added this
    case TokenType::AUTO_INCREMENT: return "AUTO_INCREMENT";
    case TokenType::NULL_T : return "NULL";
    case TokenType::UNIQUE : return "UNIQUE";
    
    // Data types
    case TokenType::INT: return "INT";             // Added this
    case TokenType::VARCHAR: return "VARCHAR";     // Added this
    case TokenType::PRIMARY: return "PRIMARY";     // Added this
    case TokenType::KEY: return "KEY";             // Added this

    // Literals
    case TokenType::IDENTIFIER: return "IDENTIFIER";
    case TokenType::STRING: return "STRING";
    case TokenType::NUMBER: return "NUMBER";
    case TokenType::STAR: return "STAR";

    // Operators
    case TokenType::PLUS: return "PLUS";
    case TokenType::MINUS: return "MINUS";
    case TokenType::MULTIPLY: return "MULTIPLY";
    case TokenType::SLASH: return "SLASH";
    case TokenType::PERCENT: return "PERCENT";
    case TokenType::EQUAL: return "EQUAL";
    case TokenType::GREATER: return "GREATER";
    case TokenType::LESS: return "LESS";
    case TokenType::GREATER_EQUAL: return "GREATER_EQUAL";
    case TokenType::LESS_EQUAL: return "LESS_EQUAL";
    case TokenType::NOT_EQUAL: return "NOT_EQUAL";
    case TokenType::DOUBLE_EQUAL: return "DOUBLE_EQUAL";

    // Punctuation
    case TokenType::COMMA: return "COMMA";
    case TokenType::SEMICOLON: return "SEMICOLON";
    case TokenType::DOT: return "DOT";
    case TokenType::OPEN_PAREN: return "OPEN_PAREN";
    case TokenType::CLOSE_PAREN: return "CLOSE_PAREN";
    

    // End of file
    case TokenType::END_OF_FILE: return "END_OF_FILE";

    default: return "UNKNOWN";
    }
}
struct Token
{
    TokenType TYPE;
    std::string VALUE;
};

class Lexer
{
private:
    std::string source;
    int cursor;
    int size;
    char current;
    int lineNumber;
    int characterNumber;
    

public:
    std::vector<Token *> tokens;
    Lexer(std::string sourceCode)
    {
        source = sourceCode;
        cursor = 0;
        size = sourceCode.length();
        current = (size > 0) ? sourceCode.at(cursor) : '\0';
        lineNumber = 1;
        characterNumber = 1;
        

    };
    ~Lexer(){
        for(auto token : tokens){
            delete token;
        }
    }

    std::pair<bool, Token *> checkSingleCharToken()
    {
        auto check = singleCharTokens.find(current);

        if (check != singleCharTokens.end())
        {
            Token *newToken = new Token();
            newToken->VALUE = current;
            newToken->TYPE = check->second;
            advance();
            return {true, newToken};
        }
        return {false, nullptr};
    }

    char seek(int offset)
    {
        if (cursor + offset >= size)
            return 0;
        else
            return source[cursor + offset];
    }

    char advance()
    {
        if (cursor < size)
        {
            char temp = current;
            cursor++;
            characterNumber++;
            current = (cursor < size) ? source[cursor] : '\0';
            if (temp == '\n')
            {
                lineNumber++;
                characterNumber = 1;
            }
            return temp;
        }
        else
            return '\0';
    }

    void skipWhitespace()
    {
        while (current == ' ' || current == '\t' || current == '\r' || current == '\n')
        {
            advance();
        }
    }

    bool isAlphaNumWrapper()
    {
        return isalpha(current) || current == '_' || isdigit(current);
    }
    
    Token *tokenizeKeyword()
    {
        std::stringstream buffer;
        
        if (isalpha(current) || current == '_')
        {
            buffer << advance();
        }
        
        while (isalnum(current) || current == '_')
        {
            buffer << advance();
        }

        Token *newToken = new Token();
        std::string value = buffer.str();
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        newToken->VALUE = value;
        
        auto it = keywords.find(value);
        if (it != keywords.end())
        {
            newToken->TYPE = it->second;
        }
        else
        {
            newToken->TYPE = TokenType::IDENTIFIER;
        }

        return newToken;
    }

    Token *addToken(enum TokenType token, std::string value)
    {
        Token *newToken = new Token();
        newToken->TYPE = token;
        newToken->VALUE = value;
        return newToken;
    }

    Token *tokenizeNumber()
    {
        std::stringstream buffer;

        while (isdigit(current))
        {
            buffer << advance();
        }

        if (current == '.')
        {
            buffer << advance();
            while (isdigit(current))
            {
                buffer << advance();
            }
        }

        Token *token = new Token();
        token->TYPE = TokenType::NUMBER;
        token->VALUE = buffer.str();
        return token;
    }

    Token *tokenizeString()
    {
        char quote = advance(); 
        std::stringstream buffer;

        while (current != quote && current != '\0')
        {
            if (current == '\n')
            {
                throw std::runtime_error("Unterminated string at line " + std::to_string(lineNumber));
            }
            buffer << advance();
        }

        if (current == quote)
        {
            advance(); 
        }
        else
        {
            throw std::runtime_error("Unterminated string at line " + std::to_string(lineNumber));
        }

        Token *token = new Token();
        token->TYPE = TokenType::STRING;
        token->VALUE = buffer.str();
        return token;
    }

    std::vector<Token *> tokenize()
    {

        while (cursor < size)
        {
            skipWhitespace();
            
            if (cursor >= size) break;

            if (isalpha(current) || current == '_')
            {
                tokens.push_back(tokenizeKeyword());
                continue;
            }

            if (current == '\'' || current == '\"')
            {
                tokens.push_back(tokenizeString());
                continue;
            }

            if (isdigit(current))
            {
                tokens.push_back(tokenizeNumber());
                continue;
            }

            
            switch (current)
            {
            case '!':
                if (seek(1) == '=')
                {
                    advance();
                    advance();
                    tokens.push_back(addToken(TokenType::NOT_EQUAL, "!="));
                }
                else
                {
                    throw std::runtime_error("Parsing error at line " + std::to_string(lineNumber) + " at character " + std::to_string(characterNumber));
                }
                break;

            case '=':
                if (seek(1) == '=')
                {
                    advance();
                    advance();
                    tokens.push_back(addToken(TokenType::DOUBLE_EQUAL, "=="));
                }
                else
                {
                    advance();
                    tokens.push_back(addToken(TokenType::EQUAL, "="));
                }
                break;

            case '<':
                if (seek(1) == '=')
                {
                    advance();
                    advance();
                    tokens.push_back(addToken(TokenType::LESS_EQUAL, "<="));
                }
                else if (seek(1) == '>')
                {
                    advance();
                    advance();
                    tokens.push_back(addToken(TokenType::NOT_EQUAL, "<>"));
                }
                else
                {
                    advance();
                    tokens.push_back(addToken(TokenType::LESS, "<"));
                }
                break;

            case '>':
                if (seek(1) == '=')
                {
                    advance();
                    advance();
                    tokens.push_back(addToken(TokenType::GREATER_EQUAL, ">="));
                }
                else
                {
                    advance();
                    tokens.push_back(addToken(TokenType::GREATER, ">"));
                }
                break;

            default:
                
                std::pair<bool, Token *> singleCharacterCheck = checkSingleCharToken();
                if (singleCharacterCheck.first)
                {
                    tokens.push_back(singleCharacterCheck.second);
                }
                else
                {
                    throw std::runtime_error("Line -" + std::to_string(lineNumber) + "cannot be parsed at character " + std::to_string(characterNumber) + " - unexpected character: " + current);
                }
                break;
            }
        }
        
        tokens.push_back(addToken(TokenType::END_OF_FILE, " "));
        return tokens;
    }
};

#endif