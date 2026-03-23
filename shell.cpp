#include <iostream>
#include <string>
#include <vector>
#include <termios.h>
#include <unistd.h>

#include "SQL_LEXER.hpp"
#include "SQL_PARSER.hpp"
#include "initialLoad.hpp"
#include "logging.hpp"
using namespace std;

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

      input.push_back(c);
        cout << c;
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return input;
}

int main()
{
    initialDatabseLoad();
    vector<string> history;
    
    vector<string>rem_sqls=exec_rem_sqls();
    for (string s:rem_sqls){
        try {
            Lexer lexer(s);
            vector<Token*> tokens = lexer.tokenize();
            Parser parser(tokens);
            std::string output =  parser.parse();
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
