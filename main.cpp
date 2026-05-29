#include "Btrees_testing.hpp"
#include "IndicatorHandler.hpp"
#include "SQL_LEXER.hpp"
#include "SQL_PARSER.hpp"
#include "UDPReceiver.hpp"
#include "hft.hpp"
#include "initialLoad.hpp"
#include "logging.hpp"
#include "strategyHandler.hpp"
#include "tradePackets.hpp"
#include "utility.hpp"
#include "utils/cpu_affinity.hpp"
#include "web_socks_og.hpp"
#include <iostream>
#include <string>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>
using namespace std;
std::string typeToString(TokenType TYPE);

static std::vector<std::jthread> worker_threads;

void setup() {
  worker_threads.emplace_back(NetFeed::run_receiver, 0);
  worker_threads.emplace_back(NetFeed::run_packet_parser, 1);
  worker_threads.emplace_back(NetFeed::run_strategy_parser, 2);
  worker_threads.emplace_back(init_web_sockets, 3);
  worker_threads.emplace_back(tradeHandler::run_trade_handler, 4);
}
std::string sha256(const string& input) {
    unsigned char hash[256];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()),input.size(),hash);
    stringstream ss;
    for (int i=0;i<256;i++) ss<<hex<<setw(2)<<setfill('0')<<(int)hash[i];
    return ss.str();
}
std::string get_id(){
    std::ifstream f("/sys/class/dmi/id/product_uuid");
    std::string uuid;
    f>>uuid;
    uuid=sha256(uuid);
    return uuid;
}

string readLineWithHistory(vector<string> &history, int &historyIndex) {
  termios oldt, newt;
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);

  string input;
  vector<char> buffer;

  while (true) {
    char c = getchar();

    if (c == '\n') {
      cout << "\n";
      break;
    }

    if (c == 27) {
      char c1 = getchar();
      char c2 = getchar();

      if (c1 == '[') {
        if (c2 == 'A') {
          if (!history.empty() && historyIndex > 0) {
            historyIndex--;

            cout << "\33[2K\rnanoVaultDb> ";
            input = history[historyIndex];
            cout << input;
          }
        } else if (c2 == 'B') {
          if (!history.empty() && historyIndex < (int)history.size() - 1) {
            historyIndex++;
            cout << "\33[2K\rnanoVaultDb> ";
            input = history[historyIndex];
            cout << input;
          } else {
            historyIndex = history.size();
            cout << "\33[2K\rnanoVaultDb> ";
            input.clear();
          }
        }
      }
      continue;
    }
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

int main(int argc, char const *argv[]) {
  std::string db_path = "";
  for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--db-path" || arg == "-d") {
          if (i + 1 < argc) {
              db_path = argv[++i];
          } else {
              std::cerr << "Error: --db-path requires a path argument." << std::endl;
              return 1;
          }
      } else if (arg.rfind("--", 0) != 0 && arg.rfind("-", 0) != 0) {
          db_path = arg;
      }
  }

  if (!db_path.empty()) {
      if (db_path.back() == '/' || db_path.back() == '\\') {
          db_path.pop_back();
      }
      dbDirectoryPath = db_path;
      currentDbPath = dbDirectoryPath + "/current_db.meta";
      tableDirectory = dbDirectoryPath + "/tables";
      std::cout << "Using database directory: " << dbDirectoryPath << std::endl;
  }
  // std::string msg=get_id();
  // cout<<msg<<endl;
  // if (msg!=get_msg_from_server) throw runtime_error("Firstly run download.exe file with sudo permissions");
  ExchangeHelper::load_api_keys();
  initialDatabseLoad();
  HFT::InitalStorage::initialIndicatorLoad();
  HFT::InitalStorage::initialStrategyLoad();
  // runVacuum();
  initializePrimaryIndexBtrees("abcd", true);
  cout << "\n";
  test_b_trees();

  std::cout << "Finished b+\n";

  setup();
  std::vector<std::string> testSQLs = {
      // R"(
      //     CREATE DATABASE test10;
      // )",
      // R"(
      // CREATE TABLE StudentRolls (
      //     id INT PRIMARY KEY AUTO_INCREMENT,
      //     roll_no INT NOT NULL UNIQUE
      // );
      // // // // // // // // // // // )",
      // R"(
      // INSERT INTO StudentRolls (roll_no)
      // VALUES (10);
      // )",
      // R"(
      //   INSERT INTO StudentRolls (roll_no)
      //   VALUES (15);
      // )",
      // R"(
      // UPDATE StudentRolls SET roll_no="WH" WHERE roll_no="Hey";
      // )",
      // R"(
      // DELETE FROM StudentRolls WHERE roll_no=12;
      // )",
      // R"(
      // INSERT INTO StudentRolls (roll_no)
      // VALUES ("Woho");
      // )",
      // R"(
      // SELECT * FROM StudentRolls;
      // )",
      // R"(
      // STATISTICS MEAN FROM StudentRolls ON roll_no WHERE roll_no="Woho";
      // )",
      // R"(
      // STATISTICS COUNT FROM StudentRolls ON roll_no WHERE roll_no="W";
      // )" ,
      R"(
      CREATE HFT TABLE testing_hft_table (
          timestamp  DOUBLE PRECISION 0,
          price      DOUBLE PRECISION 10,
          volume     DOUBLE PRECISION 2,
          side       DOUBLE PRECISION 0
      ) SYMBOL 6 TOP;
      )",

      // R"(
      // ADD INDICATOR "sma"  ( "10" ) ON SYMBOL 6 COLUMN_NO 2 ticks 100;
      // )",
      //  R"(
      // ENABLE STRATEGY "again" ("10") on symbol 6 column_no 0 ticks 10;
      // )",
      // R"(
      // ENABLE BATCH WRITING ON TABLE "testing_hft_table" TICKS 100;
      // )",
      // R"(
      // DISABLE BATCH WRITING ON TABLE "testing_hft_table";
      // )",
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

  // for (int i = 1; i <= 10; i++)g++ -std=c++20 -fsanitize=address -g -O0 -Wall
  // -Wextra main.cpp -o main
  // {
  //     std::string insertSQL =
  //         "INSERT INTO testing (rollno, name, age) VALUES (" +
  //         std::to_string(i) + ", \"Student" +
  //         std::to_string(i) + "\", " +
  //         std::to_string(18 + i) + ");";

  //     testSQLs.push_back(insertSQL);
  // }

  // initialDatabseLoad();
  IndicatorHandler::registerAllIndicators(IndicatorHandler::indicatorRegistry);
  StrategyHandler::registerAllStrategy(StrategyHandler::strategyRegistry);
  for (const auto &sql : testSQLs) {
    cout << "\n=============================\n";
    cout << "SQL:\n" << sql << endl;
    cout << "=============================\n";

    try {
      Lexer lexer(sql);
      vector<Token *> tokens = lexer.tokenize();

      // Debug: Print tokens
      cout << "Tokens:\n";
      for (Token *token : tokens) {
        cout << typeToString(token->TYPE) << " : " << token->VALUE << endl;
      }

      Parser parser(tokens);
      parser.parse(); // Par/se the SQL
    } catch (const std::exception &e) {
      cerr << "Error: " << e.what() << endl;
    }
    cout << "\n";
  }
  // NetFeed::run_receiver();

  // --- Start Shell REPL below ---
  vector<string> history;
  vector<string> rem_sqls = exec_rem_sqls();
  for (string s : rem_sqls) {
    try {
      Lexer lexer(s);
      vector<Token *> tokens = lexer.tokenize();
      for (Token *token : tokens) {
        cout << typeToString(token->TYPE) << " : " << token->VALUE << endl;
      }

      Parser parser(tokens);
      std::string output = parser.parse();
      std::cout << output << "\n";
    } catch (const std::exception &e) {
      cerr << "Warning: Failed to execute recovered SQL -> " << e.what()<< endl;
    }
  }

  int historyIndex = 0;
  while (true) {
    cout << "nanoVaultDb> ";
    string sql = readLineWithHistory(history, historyIndex);

    if (sql == "exit" || sql == "quit")
      break;

    if (!sql.empty()) {
      history.push_back(sql);
      historyIndex = history.size();
    }

    while (sql.find(';') == string::npos) {
      cout << " ...> ";
      string more = readLineWithHistory(history, historyIndex);

      if (!more.empty()) {
        history.push_back(more);
        historyIndex = history.size();
      }

      sql += "\n" + more;
    }

    try {
      // logging(sql);
      Lexer lexer(sql);
      vector<Token *> tokens = lexer.tokenize();
      for (Token *token : tokens) {
        cout << typeToString(token->TYPE) << " : " << token->VALUE << endl;
      }
      Parser parser(tokens);
      std::string output = parser.parse();
      std::cout << output << "\n";
      clear_log();
    } catch (const std::exception &e) {
      cerr << "Error: " << e.what() << endl;
    }
  }

  return 0;
}