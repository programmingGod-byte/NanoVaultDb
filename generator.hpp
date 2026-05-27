#ifndef __GENERATOR

#define __GENERATOR

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

#include <filesystem>
#include <fstream>
#include <thread>
#include "json.hpp"
#include "utility.hpp"
#include "initialLoad.hpp"
#include "global.hpp"
#include "selectAstEvaluator.hpp"
namespace fs = std::filesystem;


namespace CommandRunner
{

    void generateDropStatement(const std::unique_ptr<DropStatement> &stmt) {
        using namespace std;
        std::lock_guard<std::mutex> dbLock(dbMutex);

        bool is_table = stmt->istable;
        std::string name = stmt->name;

        std::unique_ptr<std::lock_guard<std::mutex>> tableLock;
        if (is_table) {
            tableLock = std::make_unique<std::lock_guard<std::mutex>>(tableLocks[name]);
        }

        if (is_table) {

            if (currentDatabase.empty()) {
                throw std::runtime_error("No database selected");
            }

            auto it_db = globalTableCache.find(currentDatabase);
            if (it_db == globalTableCache.end()) {
                throw std::runtime_error("Database not found: " + currentDatabase);
            }

            auto it_table = it_db->second.find(name);
            if (it_table == it_db->second.end()) {
                throw std::runtime_error("Table '" + name + "' does not exist");
            }

            std::string base = tableDirectory + "/" + currentDatabase + "/";
            std::string dataFile  = base + name + ".data";
            std::string indexFile = base + name + ".index";
            std::string deleteFile = base + name + ".delete";
            
            if (fs::exists(deleteFile)) fs::remove(deleteFile);
            if (fs::exists(dataFile))  fs::remove(dataFile);
            if (fs::exists(indexFile)) fs::remove(indexFile);

            globalTableCache[currentDatabase].erase(name);

            std::string filePath = dbDirectoryPath + "/" + currentDatabase + ".shivam.db";
            JSONParser parser(filePath);

            if (!parser.loadFromFile())
                throw std::runtime_error("Failed to load DB file: " + filePath);

            JSONParser::JSONValue root = parser.getObject(0);
            auto &dbObj = std::get<JSONParser::JSONObject>(root.value);

            if (dbObj.find("tables") != dbObj.end()) {
                auto &tables = std::get<JSONParser::JSONArray>(dbObj["tables"].value);

                for (size_t i = 0; i < tables.size(); i++) {
                    const auto &tblVal = tables[i];
                    const auto &tblObj = std::get<JSONParser::JSONObject>(tblVal.value);

                    std::string tblName = std::get<std::string>(tblObj.at("name").value);

                    if (tblName == name) {
                        tables.erase(tables.begin() + i);
                        break;
                    }
                }
            }

            parser.clear();
            parser.appendValue(JSONParser::JSONValue(dbObj));
            if (!parser.saveToFile()) {
                throw std::runtime_error("Failed to save updated DB metadata");
            }

            // std::cout << "Table '" << name << "' dropped successfully.\n";
        }
        else {
            std::string metaFile = dbDirectoryPath + "/" + name + ".shivam.db";

            if (!fs::exists(metaFile)) {
                throw std::runtime_error("Database '" + name + "' does not exist.");
            }

            std::string dbDir = tableDirectory + "/" + name;

            if (fs::exists(dbDir)) {
                fs::remove_all(dbDir);
            }

            fs::remove(metaFile);

            if (globalTableCache.find(name) != globalTableCache.end()) {
                globalTableCache.erase(name);
            }

            if (currentDatabase == name) {
                currentDatabase.clear();
            }

            // std::cout << "Database '" << name << "' dropped successfully.\n";
        }
    }
    
    void generateInsertTableStatement(const std::unique_ptr<InsertStatement> &stmt)
    {
        std::string tableName = stmt->tableName;
        std::lock_guard<std::mutex> lock(tableLocks[tableName]);
        // column , <value isUnique>
        std::vector<std::pair<std::string, std::pair<std::string,bool>>> column;
        std::vector<std::pair<std::string,bool>>actualColumn ;
        std::string primaryColName = "";
        for (int i = 0; i < stmt->columns.size(); i++)
        {

            column.push_back(make_pair(stmt->columns[i], make_pair(stmt->values[i],false)));
        }

        std::sort(column.begin(), column.end(), [](const auto &a, const auto &b)
                  { return a.first < b.first; });

        auto it_db = globalTableCache.find(currentDatabase);
        if (it_db != globalTableCache.end())
        {
            auto it_table = it_db->second.find(tableName);
            if (it_table != it_db->second.end())
            {
                std::vector<std::shared_ptr<TableGlobalColumnNode>> &columns = it_table->second.second;

                // Use `columns` here
                for ( auto &col : columns)
                {
                    if(!col->isPrimary){

                        actualColumn.push_back(make_pair(col->name,col->isUnique));
                    }else{
                        primaryColName = col->name;
                    }
                    // Example: assuming TableGlobalColumnNode has a `name` field

                    //// std::cout << col->name << '\n';
                }

                sort(actualColumn.begin(),actualColumn.end(),[](const auto &a, const auto &b)
                  { return a.first < b.first; });
                if(actualColumn.size()!=column.size()){
                    throw std::runtime_error("the given column size does not match with the actual column size");
                }
                for(int i = 0;i<actualColumn.size();i++){

                    if(actualColumn[i].first!=column[i].first){
                        std::stringstream s;
                        s<<"error "<<" get column Name "<<column[i].first<<" instead of  "<<actualColumn[i].first<<"\n";
                        throw std::runtime_error(s.str());
                    }
                    else{
                        if(actualColumn[i].second){
                            column[i].second.second = true;
                        }
                    }
                }

                PagerHandler::insertRow(std::move(primaryColName),std::move(column),std::move(tableName));
            }
            else
            {
                std::stringstream s ;
                s << "Table not found: " << tableName << '\n';
                throw std::runtime_error(s.str());
            }
        }
        else
        {
            std::stringstream s;
            s << "Database not found: " << currentDatabase << '\n';
            throw std::runtime_error(s.str());
        }
    }
    static std::string valueToStorageString(const Value& v)
    {
        if (std::holds_alternative<long int>(v))
            return std::to_string(std::get<long int>(v));

        if (std::holds_alternative<std::string>(v))
            return std::get<std::string>(v);

        if (std::holds_alternative<bool>(v))
            return std::get<bool>(v) ? "1" : "0";

        throw std::runtime_error("Unsupported Value type");
    }


    void handleUpdate(const std::unique_ptr<UpdateStatement>& stmt)
    {
        const std::string& tableName = stmt->tableName;
        std::lock_guard<std::mutex> lock(tableLocks[tableName]);

        if (currentDatabase.empty())
            throw std::runtime_error("No database selected");

        auto& tableCols = globalTableCache[currentDatabase][tableName].second;

        std::string primaryKey;
        for (auto& col : tableCols) {
            if (col->isPrimary) {
                primaryKey = col->name;
                break;
            }
        }

        for (auto& [col, _] : stmt->assignments) {
            if (col == primaryKey)
                throw std::runtime_error("UPDATE of PRIMARY KEY is not allowed");
        }
        std::string base = tableDirectory + "/" + currentDatabase + "/" + tableName;

        std::fstream indexFile(base + ".index", std::ios::in | std::ios::binary);
        std::fstream dataFile (base + ".data",  std::ios::in | std::ios::binary);
        std::fstream delFile  (base + ".delete",std::ios::in | std::ios::out | std::ios::binary);

        if (indexFile.fail() || dataFile.fail() || delFile.fail())
            throw std::runtime_error("Failed to open table files");

        std::vector<std::string> columnNames;
        for (auto& col : tableCols)
            columnNames.push_back(col->name);

        std::sort(columnNames.begin(), columnNames.end());

        int64_t rowSize =
            sizeof(int64_t) + (columnNames.size() - 1) * sizeof(PagerHandler::RowIndex);

        int64_t rowCount =
            PagerHandler::getFileSize(base + ".index") / rowSize;

        int updatedCount = 0;

        for (int64_t row = 0; row < rowCount; row++)
        {
            uint8_t deleted;
            delFile.seekg(row);
            delFile.read(reinterpret_cast<char*>(&deleted), 1);
            if (deleted == 1) continue;

            indexFile.seekg(row * rowSize);

            Row oldRow;
            int64_t id;
            indexFile.read(reinterpret_cast<char*>(&id), sizeof(int64_t));
            oldRow.columns[columnNames[0]] = std::to_string(id);

            for (size_t i = 1; i < columnNames.size(); i++) {
                int64_t start, end;
                indexFile.read(reinterpret_cast<char*>(&start), sizeof(int64_t));
                indexFile.read(reinterpret_cast<char*>(&end), sizeof(int64_t));

                std::string val(end - start, '\0');
                dataFile.seekg(start);
                dataFile.read(val.data(), val.size());

                oldRow.columns[columnNames[i]] = val;
            }

            if (!AstParser::evaluateWhere(stmt->where.get(), oldRow))
                continue;

            Row newRow = oldRow;
            for (auto& [col, val] : stmt->assignments) {
                newRow.columns[col] = val;
            }

            uint8_t dead = 1;
            delFile.seekp(row);
            delFile.write(reinterpret_cast<char*>(&dead), 1);

            if (dbBtrees.find(currentDatabase) != dbBtrees.end() && 
                dbBtrees[currentDatabase].find(tableName) != dbBtrees[currentDatabase].end()) {
                
                auto& tableBtrees = dbBtrees[currentDatabase][tableName];
                for (auto& [colName, treePair] : tableBtrees) {
                    TreeVariant& treeVar = treePair.first;
                    std::string colValueStr = valueToStorageString(oldRow.columns[colName]);
                    
                    std::visit([&](auto& treePtr) {
                        if (treePtr) {
                            using TreePtr = std::decay_t<decltype(treePtr)>;
                            using K = typename TreePtr::element_type::key_type;
                            if constexpr (requires { { std::stoll(colValueStr) } -> std::same_as<int64_t>; } && std::is_same_v<K, int64_t>) {
                                int64_t val = 0;
                                try { val = std::stoll(colValueStr); } catch (...) {}
                                treePtr->remove(val);
                            } else if constexpr (std::is_same_v<K, std::string>) {
                                treePtr->remove(colValueStr);
                            }
                        }
                    }, treeVar);
                }
            }

            std::vector<std::pair<std::string,
                std::pair<std::string, bool>>> insertCols;


            for (auto& col : tableCols) {
                if (col->isPrimary) continue;

                insertCols.push_back({
                    col->name,
                    {
                        valueToStorageString(newRow.columns[col->name]),
                        col->isUnique
                    }
                });
            }


            PagerHandler::insertRow(
                primaryKey,
                std::move(insertCols),
                tableName
            );

            updatedCount++;
        }

        delFile.flush();

        // std::cout << "UPDATE affected rows: " << updatedCount << "\n";
    }


    void handleDelete(const std::unique_ptr<DeleteStatement>& stmt)
    {
        const std::string& tableName = stmt->table;
        std::lock_guard<std::mutex> lock(tableLocks[tableName]);

        std::stringstream indexfilename, deletefilename, datafilename;
        indexfilename  << tableDirectory << "/" << currentDatabase << "/" << tableName << ".index";
        deletefilename << tableDirectory << "/" << currentDatabase << "/" << tableName << ".delete";
        datafilename << tableDirectory << "/" << currentDatabase << "/" << tableName << ".data";

        std::fstream indexFile(indexfilename.str(), std::ios::in | std::ios::binary);
        std::fstream deleteFile(deletefilename.str(), std::ios::in | std::ios::out | std::ios::binary);
        std::fstream dataFile(datafilename.str(), std::ios::in | std::ios::binary);
        if (dataFile.fail())
            throw std::runtime_error("Failed to open data file");
        if (indexFile.fail())
            throw std::runtime_error("Failed to open index file");
        if (deleteFile.fail())
            throw std::runtime_error("Failed to open delete file");
        std::vector<std::string> allColumnNames;
        for (auto& col : globalTableCache[currentDatabase][tableName].second)
            allColumnNames.push_back(col->name);

        sort(allColumnNames.begin(), allColumnNames.end());
        int64_t rowSize;
        int64_t rowCount;
        {
            int64_t indexSize = PagerHandler::getFileSize(indexfilename.str());
            int64_t cols = allColumnNames.size();
            rowSize = sizeof(int64_t) + (allColumnNames.size() - 1) * sizeof(PagerHandler::RowIndex);
            rowCount = indexSize / rowSize;
        }
        int count=0;
        for (int64_t row = 0; row < rowCount; row++)
        {
            // --- sync delete file ---
            uint8_t alive;
            deleteFile.seekg(row * sizeof(uint8_t), std::ios::beg);
            deleteFile.read(reinterpret_cast<char*>(&alive), 1);

            // --- sync index file ---
            indexFile.seekg(row * rowSize, std::ios::beg);

            if (alive == 1)
                continue;

            Row r;
            int64_t id;
            indexFile.read(reinterpret_cast<char*>(&id), sizeof(int64_t));
            r.columns[allColumnNames[0]] = std::to_string(id);

            for (size_t i = 1; i < allColumnNames.size(); i++)
            {
                int64_t start, end;
                indexFile.read(reinterpret_cast<char*>(&start), sizeof(int64_t));
                indexFile.read(reinterpret_cast<char*>(&end), sizeof(int64_t));
                int64_t len = end - start;

                std::string value(len, '\0');
                dataFile.seekg(start, std::ios::beg);
                dataFile.read(&value[0], len);

                r.columns[allColumnNames[i]] = value;
            }

            if (!stmt->whereClause || AstParser::evaluateWhere(stmt->whereClause.get(), r))
            {
                uint8_t dead = 1;
                deleteFile.seekp(row * sizeof(uint8_t), std::ios::beg);
                deleteFile.write(reinterpret_cast<char*>(&dead), 1);
                count++;

                if (dbBtrees.find(currentDatabase) != dbBtrees.end() && 
                    dbBtrees[currentDatabase].find(tableName) != dbBtrees[currentDatabase].end()) {
                    
                    auto& tableBtrees = dbBtrees[currentDatabase][tableName];
                    for (auto& [colName, treePair] : tableBtrees) {
                        TreeVariant& treeVar = treePair.first;
                        std::string colValueStr = valueToStorageString(r.columns[colName]);
                        
                        std::visit([&](auto& treePtr) {
                            if (treePtr) {
                                using TreePtr = std::decay_t<decltype(treePtr)>;
                                using K = typename TreePtr::element_type::key_type;
                                if constexpr (requires { { std::stoll(colValueStr) } -> std::same_as<int64_t>; } && std::is_same_v<K, int64_t>) {
                                    int64_t val = 0;
                                    try { val = std::stoll(colValueStr); } catch (...) {}
                                    treePtr->remove(val);
                                } else if constexpr (std::is_same_v<K, std::string>) {
                                    treePtr->remove(colValueStr);
                                }
                            }
                        }, treeVar);
                    }
                }
            }
        }

        // std::cout<<"Delete affected rows "<<count<<"\n";
        deleteFile.flush();
    }

    void generateCreateTableStatement(const std::unique_ptr<CreateStatement> &stmt)
    {
        std::lock_guard<std::mutex> dbLock(dbMutex);
        JSONParser::JSONArray columnArray;
        std::vector<std::shared_ptr<TableGlobalColumnNode>> newTableCache;

        for (const auto &col : stmt->columns)
        {
            std::shared_ptr<TableGlobalColumnNode> node = std::make_shared<TableGlobalColumnNode>();
            node->name = col.name;
            node->type = col.type;
            node->precision=0;

            int length = INT_MAX;
            bool isUnique = false;
            bool isPrimary = false;
            bool autoIncrement = false;
            bool createIndex = false;
            int symbol=-1;
            int top=-1;
            std::vector<std::string> ActualTableConstraint;
            
            JSONParser::JSONObject colJson = {
                {"name", JSONParser::JSONValue(col.name)},
                {"precision",JSONParser::JSONValue(col.precision)},
                {"type", JSONParser::JSONValue(col.type)}};

            // std::cout<<"getting col type\n";
            // std::cout<<col.type<<"\n";
            if (col.type.find("varchar(") != std::string::npos)
            {
                size_t start = col.type.find("(") + 1;
                size_t end = col.type.find(")");
                if (start != std::string::npos && end != std::string::npos && end > start)
                {
                    std::string lengthStr = col.type.substr(start, end - start);
                    try
                    {
                        int length = std::stoi(lengthStr);
                        node->length = length;
                        colJson["length"] = JSONParser::JSONValue(length);
                        colJson["type"] = JSONParser::JSONValue("varchar");
                        node->type = "varchar"; 
                    }
                    catch (...)
                    {
                        throw std::runtime_error("Invalid VARCHAR length");
                    }
                }
            }

            JSONParser::JSONArray constraintArray;
            for (const auto &c : col.constraints)
            {
                switch (c)
                {
                case ColumnConstraint::NOT_NULL:
                    constraintArray.push_back(JSONParser::JSONValue("not_null"));
                    ActualTableConstraint.push_back("not_null");
                    break;
                case ColumnConstraint::PRIMARY_KEY:
                    constraintArray.push_back(JSONParser::JSONValue("primary_key"));
                    isPrimary = true;
                    ActualTableConstraint.push_back("primary_key");
                    break;
                case ColumnConstraint::UNIQUE:
                    constraintArray.push_back(JSONParser::JSONValue("unique"));
                    isUnique = true;
                    ActualTableConstraint.push_back("unique");
                    break;
                case ColumnConstraint::AUTO_INCREMENT:
                    autoIncrement = true;
                    ActualTableConstraint.push_back("auto_increment");
                    constraintArray.push_back(JSONParser::JSONValue("auto_increment"));
                    
                    break;
                default:
                    break;
                }
            }

            colJson["constraints"] = JSONParser::JSONValue(constraintArray);
            columnArray.push_back(JSONParser::JSONValue(colJson));
            node->autoIncrement = autoIncrement;
            node->isUnique = isUnique;
            node->createIndex = createIndex;
            node->isPrimary = isPrimary;
            newTableCache.push_back(node);

        }

        if (globalTableCache[currentDatabase].find(stmt->name) != globalTableCache[currentDatabase].end())
        {
            throw std::runtime_error(" Table '" + stmt->name + "' already exists in DB '" + currentDatabase + "'");
        }

        JSONParser::JSONObject tableJson = {
            {"name", JSONParser::JSONValue(stmt->name)},
            {"symbol", JSONParser::JSONValue(stmt->symbol)},
            {"top", JSONParser::JSONValue(stmt->top)},
            {"columns", JSONParser::JSONValue(columnArray)}};

        std::string filePath = dbDirectoryPath + "/" + currentDatabase + ".shivam.db";
        JSONParser parser(filePath);

        if (!parser.loadFromFile())
        {
            throw std::runtime_error(" Failed to load DB file: " + filePath);
        }

        JSONParser::JSONValue root = parser.getObject(0);
        if (!std::holds_alternative<JSONParser::JSONObject>(root.value))
        {
            throw std::runtime_error("Root of DB JSON must be an object");
        }

        auto &dbObj = std::get<JSONParser::JSONObject>(root.value);

        if (dbObj.find("tables") != dbObj.end() &&
            std::holds_alternative<JSONParser::JSONArray>(dbObj["tables"].value))
        {

            auto &tables = std::get<JSONParser::JSONArray>(dbObj["tables"].value);
            tables.push_back(JSONParser::JSONValue(tableJson));
        }
        else
        {
            dbObj["tables"] = JSONParser::JSONValue(JSONParser::JSONArray{
                JSONParser::JSONValue(tableJson)});
        }

        parser.clear();
        parser.appendValue(JSONParser::JSONValue(dbObj));

        if (!parser.saveToFile())
        {
            throw std::runtime_error(" Failed to save DB JSON file");
        }
        
        globalTableCache[currentDatabase][stmt->name].first = std::to_string(stmt->symbol);
        globalTableCache[currentDatabase][stmt->name].second = newTableCache; 
        tableLocks[stmt->name]; 
                      
        // std::cout << " Table '" << stmt->name << "' added to DB '" << currentDatabase << "' successfully.\n";
        std::string tablename = stmt->name;
        std::stringstream indexFile, dataFile, delFile;

        indexFile << tableDirectory << "/" << currentDatabase << "/" << tablename << ".index";
        dataFile << tableDirectory << "/" << currentDatabase << "/" << tablename << ".data";
        delFile << tableDirectory << "/" << currentDatabase << "/" << tablename << ".delete";
        MyUtility::createFile(indexFile.str(), "");
        MyUtility::createFile(dataFile.str(), "");
        MyUtility::createFile(delFile.str(), "");
        initializePrimaryIndexBtrees(tablename,false);
        tableLocks[stmt->name].unlock();
    }

    void generateHFTCreateStatement(const std::unique_ptr<CreateStatement> &stmt)
    {
        std::lock_guard<std::mutex> dbLock(dbMutex);
        JSONParser::JSONArray columnArray;
        std::vector<std::shared_ptr<TableGlobalColumnNode>> newTableCache;

        for (const auto &col : stmt->columns)
        {
            std::shared_ptr<TableGlobalColumnNode> node = std::make_shared<TableGlobalColumnNode>();
            node->name = col.name;
            node->type = col.type;
            JSONParser::JSONArray constraintArray;

            JSONParser::JSONObject colJson = {
                {"name", JSONParser::JSONValue(col.name)},
                {"type", JSONParser::JSONValue(col.type)},
                {"precision", JSONParser::JSONValue(static_cast<int>(col.precision))},
                {"constraints", JSONParser::JSONValue(constraintArray)}
            };
            
            columnArray.push_back(JSONParser::JSONValue(colJson));
            
            node->autoIncrement = false;
            node->isUnique = false;
            node->createIndex = false;
            node->isPrimary = false;
            node->precision = col.precision;
            newTableCache.push_back(node);
        }
        if (globalTableCache[currentDatabase].find(stmt->name) != globalTableCache[currentDatabase].end())
        {
            throw std::runtime_error(" Table '" + stmt->name + "' already exists in DB '" + currentDatabase + "'");
        }


        JSONParser::JSONObject tableJson = {
            {"name", JSONParser::JSONValue(stmt->name)},
            {"symbol",JSONParser::JSONValue(stmt->symbol)},
            {"top",JSONParser::JSONValue(stmt->top ? 1 : 0)},
            {"columns", JSONParser::JSONValue(columnArray)}
        };

        std::string filePath = dbDirectoryPath + "/" + currentDatabase + ".shivam.db";
        JSONParser parser(filePath);

        if (!parser.loadFromFile())
        {
            throw std::runtime_error(" Failed to load DB file: " + filePath);
        }

        JSONParser::JSONValue root = parser.getObject(0);
        if (!std::holds_alternative<JSONParser::JSONObject>(root.value))
        {
            throw std::runtime_error("Root of DB JSON must be an object");
        }

        auto &dbObj = std::get<JSONParser::JSONObject>(root.value);

        if (dbObj.find("tables") != dbObj.end() &&
            std::holds_alternative<JSONParser::JSONArray>(dbObj["tables"].value))
        {
            auto &tables = std::get<JSONParser::JSONArray>(dbObj["tables"].value);
            tables.push_back(JSONParser::JSONValue(tableJson));
        }
        else
        {
            dbObj["tables"] = JSONParser::JSONValue(JSONParser::JSONArray{
                JSONParser::JSONValue(tableJson)});
        }

        parser.clear();
        parser.appendValue(JSONParser::JSONValue(dbObj));

        if (!parser.saveToFile())
        {
            throw std::runtime_error(" Failed to save DB JSON file");
        }
        globalTableCache[currentDatabase][stmt->name].first=std::to_string(stmt->symbol);
        globalTableCache[currentDatabase][stmt->name].second = newTableCache; 
        tableLocks[stmt->name];  // initialize mutex

        // Initialize symbolAccessArray for the new HFT table immediately
        std::vector<int64_t> precisions;
        for (const auto &col : stmt->columns) {
            precisions.push_back(col.precision);
        }
        HFT::symbolAccessArray[stmt->symbol].init(precisions, stmt->columns.size(), stmt->top ? 1 : 0, stmt->symbol);
                      
        // std::cout << " HFT Table '" << stmt->name << "' added to DB '" << currentDatabase << "' successfully.\n";
        
        std::string tablename = stmt->name;
        std::stringstream indexFile, dataFile, delFile;

        indexFile << tableDirectory << "/" << currentDatabase << "/" << tablename << ".index";
        dataFile << tableDirectory << "/" << currentDatabase << "/" << tablename << ".data";
        delFile << tableDirectory << "/" << currentDatabase << "/" << tablename << ".delete";
        
        MyUtility::createFile(indexFile.str(), "");
        MyUtility::createFile(dataFile.str(), "");
        MyUtility::createFile(delFile.str(), "");
    }
    void memorySet(const std::string& key,
               const std::string& value,
               int ttlSeconds)
        {
            auto expiry = (ttlSeconds < 0)
                ? std::chrono::steady_clock::time_point::max()
                : std::chrono::steady_clock::now() + std::chrono::seconds(ttlSeconds);

            {
                std::unique_lock<std::shared_mutex> lock(memoryMutex);
                memoryStore[key] = {value, expiry};
            }

            if (ttlSeconds >= 0) {
                {
                    std::lock_guard<std::mutex> lock(expiryMutex);
                    expiryHeap.push({expiry, key});
                }
                expiryCV.notify_one();
            }
        }


        bool memoryGet(const std::string& key,
                    std::string& outValue)
        {
            {
                std::shared_lock<std::shared_mutex> lock(memoryMutex);
                auto it = memoryStore.find(key);
                if (it == memoryStore.end())
                    return false;

                if (it->second.expiry > std::chrono::steady_clock::now()) {
                    outValue = it->second.value;
                    return true;
                }
            }

            {
                std::unique_lock<std::shared_mutex> lock(memoryMutex);
                memoryStore.erase(key);
            }
            return false;
        }

        static void memoryScheduler()
        {
            while (memorySchedulerRunning) {
                std::unique_lock<std::mutex> lock(expiryMutex);

                if (expiryHeap.empty()) {
                    expiryCV.wait(lock);
                    continue;
                }

                auto nextExpiry = expiryHeap.top().expiry;
                expiryCV.wait_until(lock, nextExpiry);

                auto now = std::chrono::steady_clock::now();

                while (!expiryHeap.empty() &&
                    expiryHeap.top().expiry <= now) {

                    auto node = expiryHeap.top();
                    expiryHeap.pop();

                    std::unique_lock<std::shared_mutex> memLock(memoryMutex);
                    auto it = memoryStore.find(node.key);
                    if (it != memoryStore.end() &&
                        it->second.expiry == node.expiry) {
                        memoryStore.erase(it);
                    }
                }
            }
        }

        void startMemoryScheduler()
        {
            std::thread(memoryScheduler).detach();
        }

        void stopMemoryScheduler()
        {
            memorySchedulerRunning = false;
            expiryCV.notify_all();
        }
};
#endif