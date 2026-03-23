// g++ -c db_engine.cpp -o db_engine.o
// ar rcs libdb_engine.a db_engine.o


#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <termios.h>
#include <unistd.h>
#include "UDPReceiver.hpp"
#include "SQL_LEXER.hpp"
#include "SQL_PARSER.hpp"
#include "hft.hpp"
#include "initialLoad.hpp"
#include "utils/cpu_affinity.hpp"
#include "logging.hpp"

using namespace std;
std::string typeToString(TokenType TYPE);



void setup(){
std::thread packet_receiver(NetFeed::run_receiver);
        std::thread packet_parser(NetFeed::run_packet_parser);

        pin_thread_to_cpu(packet_receiver,0);
        pin_thread_to_cpu(packet_parser,1);

        packet_receiver.detach();
        packet_parser.detach();
}

string readLineWithHistory(vector<string>& history, int& historyIndex)
{
    termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    string input;
    vector<char> buffer;

    while (true)
    {
        char c = getchar();

        if (c == '\n')
        {
            cout << "\n";
            break;
        }

       
        if (c == 27)
        {
            char c1 = getchar();
            char c2 = getchar();

            if (c1 == '[')
            {
                if (c2 == 'A')  
                {
                    if (!history.empty() && historyIndex > 0)
                    {
                        historyIndex--;
                        
                        cout << "\33[2K\rnanoVaultDb> ";
                        input = history[historyIndex];
                        cout << input;
                    }
                }
                else if (c2 == 'B') 
                {
                    if (!history.empty() && historyIndex < (int)history.size() - 1)
                    {
                        historyIndex++;
                        cout << "\33[2K\rnanoVaultDb> ";
                        input = history[historyIndex];
                        cout << input;
                    }
                    else
                    {
                        historyIndex = history.size();
                        cout << "\33[2K\rnanoVaultDb> ";
                        input.clear();
                    }
                }
            }
            continue;
        }

        // Handle backspace properly (127 for DEL, 8 for BS)
        if (c == 127 || c == 8) {
            if (!input.empty()) {
                input.pop_back();
                cout << "\b \b";
            }
            continue;
        }

        input.push_back(c);
        cout << c;
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return input;
}


int main(int argc, char const *argv[])
{
    initialDatabseLoad();
    HFT::InitalStorage::initialIndicatorLoad();
    // runVacuum();
    
    try
    {
        initializePrimaryIndexBtrees();
    }
    catch(const std::exception& e)
    {}

    std::cout<<"#### \n\n\n finished b+ \n\n\n\n ###";


    std::vector<std::string> testSQLs = {
// R"(
//     CREATE DATABASE school;
// )",
// R"(
// CREATE TABLE StudentRolls (
//     id INT PRIMARY KEY AUTO_INCREMENT,
//     roll_no INT NOT NULL
// );
// )",
// R"(
// INSERT INTO StudentRolls (roll_no)
// VALUES (2);
// )",
// R"(
// INSERT INTO StudentRolls (roll_no)
// VALUES (5);
// )",
// R"(
// UPDATE StudentRolls SET roll_no=12 WHERE roll_no<5;
// )",
// R"(
// DELETE FROM StudentRolls WHERE roll_no=12;
// )",
// R"(
// CREATE HFT TABLE eth_ticks (
//     timestamp  DOUBLE PRECISION 0,          
//     price      DOUBLE PRECISION 10,           
//     volume     DOUBLE PRECISION 2,            
//     side       DOUBLE PRECISION 0              
// ) SYMBOL 2 TOP;
// )",
R"(
ADD INDICATOR "INDICATOR_NAME" ON SYMBOL 2 COLUMN_NO = -3;
)",
// R"(
// DROP TABLE StudentRolls;
// )",
// R"(
// DROP DATABASE test;
// )",
// R"(
// USE school;
// )",
// R"(
// MEMORY KEY=a VALUES=123 TTL=5;
// )",
// R"(
// MEMORY GET KEY=a;
// )",
};


// for (int i = 1; i <= 10; i++)
// {
//     std::string insertSQL =
//         "INSERT INTO testing (rollno, name, age) VALUES (" +
//         std::to_string(i) + ", \"Student" +
//         std::to_string(i) + "\", " +
//         std::to_string(18 + i) + ");";

//     testSQLs.push_back(insertSQL);
// }


    for (const auto &sql : testSQLs)
    {
        cout << "\n=============================\n";
        cout << "SQL:\n"
             << sql << endl;
        cout << "=============================\n";

        try
        {
            Lexer lexer(sql);
            vector<Token *> tokens = lexer.tokenize();

            // Debug: Print tokens
            cout << "Tokens:\n";
            for (Token *token : tokens)
            {
                cout << typeToString(token->TYPE) << " : " << token->VALUE << endl;
            }

            Parser parser(tokens);
            parser.parse(); // Par/se the SQL
        }
        catch (const std::exception &e)
        {
            cerr << "Error: " << e.what() << endl;
        }
        cout << "\n";
    }

    // setup();
        // NetFeed::run_receiver();
        
    // --- Start Shell REPL below ---
    vector<string> history;
    vector<string>rem_sqls=exec_rem_sqls();
    for (string s:rem_sqls){
        try {
            Lexer lexer(s);
            vector<Token*> tokens = lexer.tokenize();
            Parser parser(tokens);
            std::string output =  parser.parse();
            std::cout<<output<<"\n";
        } catch(const std::exception& e) {
            cerr << "Warning: Failed to execute recovered SQL -> " << e.what() << endl;
        }
    }

    int historyIndex = 0;

    while (true)
    {
        cout << "nanoVaultDb> ";
        string sql = readLineWithHistory(history, historyIndex);

        if (sql == "exit" || sql == "quit")
            break;

        if (!sql.empty())
        {
            history.push_back(sql);
            historyIndex = history.size();
        }

        while (sql.find(';') == string::npos)
        {
            cout << " ...> ";
            string more = readLineWithHistory(history, historyIndex);

            if (!more.empty())
            {
                history.push_back(more);
                historyIndex = history.size();
            }

            sql += "\n" + more;
        }

        try
        {
            logging(sql);
            Lexer lexer(sql);
            vector<Token*> tokens = lexer.tokenize();
            Parser parser(tokens);
            std::string output = parser.parse();
            std::cout<<output<<"\n";
            clear_log();
        }
        catch (const std::exception& e)
        {
            cerr << "Error: " << e.what() << endl;
        }
    }

    return 0;
}
