#include <iostream>
#include <fstream>
#include <bits/stdc++.h>
using namespace std;

void logging(string text) {
    ofstream file("logging.txt");   

    if (!file) {
        cout << "Error opening file!" << endl;
    }

    file << text;

    file.close(); 
}

void clear_log(){
    ofstream file("logging.txt");
    file << ""; 
}

vector<string> exec_rem_sqls() {
    ifstream file("logging.txt");
    if (!file) return {};

    string content((istreambuf_iterator<char>(file)),
                    istreambuf_iterator<char>());
    file.close();

    if (content.find_first_not_of(" \n\t\r") == string::npos) {
        return {};
    }

    vector<string> leftoverSQLs;
    string curr;

    for (char c : content) {
        curr += c;
        if (c == ';') {
            leftoverSQLs.push_back(curr);
            curr.clear();
        }
    }

    if (!curr.empty() && curr.find_first_not_of(" \n\r\t") != string::npos) {
        leftoverSQLs.push_back(curr);
    }

    ofstream clear("logging.txt", ios::trunc);

    return leftoverSQLs;
}

