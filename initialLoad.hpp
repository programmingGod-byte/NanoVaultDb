#ifndef __INITIAL_LOAD
#define __INITIAL_LOAD

#include <cstdint>
#include <unordered_map>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <memory>
#include <stdexcept>
#include <climits>
#include <string>
#include <thread>
#include <format>
#include <chrono>
#include <atomic>
#include <utility>
#include <vector>
#include "SQL_PARSER.hpp"
#include "hft.hpp"
#include "global.hpp"
#include "utility.hpp"
#include "batchWriter.hpp"
namespace fs = std::filesystem;

std::string getCurrentDatabase(std::string &metaFile)
{
    std::ifstream file(metaFile);
    if (!file.is_open())
        throw std::runtime_error("Unable to open meta file");

    std::string line;
    std::getline(file, line);

    size_t keyPos = line.find("\"current_db\"");
    if (keyPos == std::string::npos)
        throw std::runtime_error("Key 'current_db' not found");

    size_t colonPos = line.find(":", keyPos);
    if (colonPos == std::string::npos)
        throw std::runtime_error("Invalid format in meta file");

    size_t firstQuote = line.find("\"", colonPos);
    if (firstQuote == std::string::npos)
        throw std::runtime_error("Invalid format in meta file");

    size_t secondQuote = line.find("\"", firstQuote + 1);
    if (secondQuote == std::string::npos)
        throw std::runtime_error("Invalid format in meta file");

    return line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
}

void runVacuum() {
    std::lock_guard<std::mutex> dbLock(dbMutex);

    std::vector<std::pair<std::string, std::string>> tablesToVacuum;

    for (auto& [dbName, tables] : globalTableCache) {
        for (auto i=tables.begin();i!=tables.end();i++) {
            tablesToVacuum.emplace_back(dbName, i->first);
        }
    }

    for (auto& [db, table] : tablesToVacuum) {
        DEBUG_LOG(table);
        PagerHandler::vacuumTable(db, table);
    }
}


void vacuumScheduler() {
    while (!shuttingDown.load()) {
        for (int i = 0; i < 360; i++) { 
            if (shuttingDown.load()) return;
            std::this_thread::sleep_for(std::chrono::seconds(60));
        }
        
        if (shuttingDown.load()) return;
        runVacuum();
    }
}

void startVacuumThread() {
    vacuumThread = std::thread(vacuumScheduler);
}

bool checkDBexist(const std::string &name)
{
    std::stringstream file;
    file << dbDirectoryPath;
    file << name;
    file << ".shivam.db";
    return MyUtility::checkIfFileExist(file.str());
}

void initialDatabseLoad()
{
    std::string currentDbMeta = currentDbPath;
    if (!fs::is_directory(dbDirectoryPath)){
        fs::create_directories(tableDirectory);
        std::ofstream file(currentDbPath);
        if (file.is_open()){
            file << "{\"current_db\":\"\"}";
            file.close();
        }else{
            DEBUG_LOG("Failed to create meta file");
        }
    }
    std::string dbName = getCurrentDatabase(currentDbMeta);
    if (dbName == "") return;
    currentDatabase = dbName;
    
    std::string filename = dbName + ".shivam.db";
    std::string fullPath = dbDirectoryPath + "/" + filename;

    if (!fs::exists(fullPath)) {
        DEBUG_LOG("Database file does not exist: " << fullPath);
        return;
    }

    std::string dbname = dbName;
    std::shared_ptr<PythonLikeJSONParser> parser = std::make_shared<PythonLikeJSONParser>();

    // Store parser in global cache
    globalJsonCache[dbname] = parser;

    DEBUG_LOG("dbname " << dbname);
    DEBUG_LOG("FULL PATH " << fullPath);
    if (!parser->loadFromFile(fullPath))
    {
        DEBUG_LOG("Failed to load file: " << fullPath);
        std::stringstream err;
        err << "Failed to load file: " << fullPath;
        throw std::runtime_error(err.str());
    }

    try
    {
        JSONArrayWrapper tablesArray = (*parser)[0][std::string("tables")].asArray();
        for (int64_t i = 0; i < tablesArray.size(); ++i)
        {
            std::string tableName = tablesArray[i][std::string("name")].getString();
            std::string indexFileName = tableDirectory + "/" + dbname + "/" + tableName + ".data"; 
            std::unique_ptr<IoUringQueue> io_queue = std::make_unique<IoUringQueue>(indexFileName);
            std::string symbolString = std::to_string(tablesArray[i][std::string("symbol")].getInt());
            std::string  topString = std::to_string(tablesArray[i][std::string("top")].getInt());
            DEBUG_LOG(symbolString << " " << topString);
            bool isSymbol = false;
            bool isTop = false;
            HFT_DEBUG_FILE("error.txt", std::format("the tableName is {} and top string is {}",symbolString,topString));
            if(!symbolString.empty() && symbolString != "-1") isSymbol = true;
            if(topString.length() > 0 && topString[0]=='1') isTop = true;
            JSONArrayWrapper columnsArray = tablesArray[i][std::string("columns")].asArray();

            std::vector<std::shared_ptr<TableGlobalColumnNode>> columnNodes;
            std::vector<int64_t> precisions;
            for (int64_t j = 0; j < columnsArray.size(); ++j)
            {
                std::shared_ptr<TableGlobalColumnNode> node = std::make_shared<TableGlobalColumnNode>();

                std::string columnDataName = columnsArray[j][std::string("name")].getString();
                std::string columnDataType = columnsArray[j][std::string("type")].getString();
                JSONArrayWrapper constraintArray = columnsArray[j][std::string("constraints")].asArray();
                int64_t precision = columnsArray[j][std::string(std::string("precision"))].getInt();
                precisions.push_back(precision);

                DEBUG_LOG("the precision is " << precision);
                int length = INT_MAX;
                bool isUnique = false;
                bool isPrimary = false;
                bool autoIncrement = false;
                bool createIndex = false;

                for (int64_t k = 0; k < constraintArray.size(); ++k)
                {
                    std::string constraint = constraintArray[k].getString();
                    if (constraint == "primary_key")
                        isPrimary = true;
                    if (constraint == "auto_increment")
                        autoIncrement = true;
                    if (constraint == "unique")
                        isUnique = true;
                    if (constraint == "create_index")
                        createIndex = true;
                }

                if (columnsArray[j].hasKey(std::string("length")))
                {
                    length = columnsArray[j][std::string("length")];
                }

                node->constraint = constraintArray.toStringVector();
                node->length = length;
                node->precision = precision;
                node->name = columnDataName;
                node->type = columnDataType;
                node->autoIncrement = autoIncrement;
                node->isUnique = isUnique;
                node->createIndex = createIndex;
                node->isPrimary = isPrimary;

                columnNodes.push_back(node);

                if (isPrimary || isUnique)
                {
                    TreeVariant tree = std::make_shared<BPlusTree<int64_t, IndexNode>>();
                    dbBtrees[dbname][tableName][columnDataName] = std::make_pair(tree, 0);
                }
            }
            
            int ticks = -1;
            if (tablesArray[i].hasKey(std::string("ticks"))) {
                ticks = tablesArray[i][std::string("ticks")].getInt();
            }

            // Save table columns in globalTableCache
            auto it=std::move(columnNodes);
            globalTableCache[dbname][tableName] = {symbolString,it};
            if(isSymbol){
                int64_t symbol = static_cast<int64_t>(std::stoi(symbolString));

                HFT_DEBUG_FILE("error.txt", std::format("the table name is {} symbol is {} isTop is {}",tableName,symbol,isTop));
                HFT::symbolAccessArray[symbol].init(precisions,columnsArray.size(),isTop?1:0,symbol);
                if (ticks > 0) {
                    HFT::symbolAccessArray[symbol].storageTicks = ticks;
                }
                
                auto it_f = batchWriterFileMap.find(symbol);
                if (it_f == batchWriterFileMap.end() || !it_f->second) {
                    batchWriterFileMap[symbol] = std::move(io_queue);
                } else {
                    // Reuse existing writer
                    DEBUG_LOG("[INITIAL_LOAD] Reusing existing writer for symbol " << symbol);
                }
            }

            DEBUG_LOG("Loaded table: " << tableName << " from DB: " << dbname);
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error accessing 'tables' in " << fullPath << ": " << e.what() << std::endl;
    }
}

void loadAllNodesOfBtreeForPrimaryKey(TreeVariant &tree,std::string dbName, int64_t size, std::string tableName)
{
    std::stringstream indexFileName;
    indexFileName << tableDirectory << "/" << dbName << "/" << tableName << ".index";
    DEBUG_LOG("INDEX FILE NAME " << indexFileName.str());
    if (MyUtility::checkIfFileExist(indexFileName.str()))
    {

        std::fstream indexFile(indexFileName.str(), std::ios::in | std::ios::out | std::ios::binary);
        if (indexFile.fail())
        {
            throw std::runtime_error("Failed to read primary key  from index file");
        }

        int64_t indexFileSize = PagerHandler::getFileSize(indexFileName.str());
        std::cout << "index file size is " << indexFileSize << "\n";
        int64_t divider = 8 * (2 * size - 1);
        int64_t getTotalNoOfRows = (int64_t)(indexFileSize / divider);
        std::cout << "Total No Of rows " << getTotalNoOfRows << "\n";
        while (getTotalNoOfRows--)
        {
            int64_t id, start, end;
            std::streampos pos = indexFile.tellg();

            if (pos != std::streampos(-1))
            {
                int64_t offset = static_cast<int64_t>(pos);
                start = offset;
            }
            else
            {
                break;
            }

            indexFile.read(reinterpret_cast<char *>(&id), sizeof(int64_t));

            // indexFile.seekg(sizeof(int64_t),std::ios::cur);

            // indexFile.read(reinterpret_cast<char *>(&start), sizeof(int64_t));

            indexFile.seekg((2 * (size - 1)) * sizeof(int64_t), std::ios::cur);

            std::streampos endpos = indexFile.tellg();

            if (endpos != std::streampos(-1))
            {
                int64_t offset = static_cast<int64_t>(endpos);
                end = offset;
            }
            else
            {
                break;
            }

            IndexNode node{start, static_cast<int16_t>(end)};
            DEBUG_LOG("id " << id << " start " << start << " end " << end);
            std::visit([id, node](auto &treePtr)
                       {
            using TreeType = std::decay_t<decltype(*treePtr)>;
            if constexpr (std::is_same_v<TreeType, BPlusTree<int64_t, IndexNode>>) {
                treePtr->insert(id, node);
            } else if constexpr (std::is_same_v<TreeType, BPlusTree<std::string, IndexNode>>) {
                treePtr->insert(std::to_string(id), node);
            } }, tree);
        }
    }
    else
    {
        throw std::runtime_error("the table does not exist");
    }
}

void loadAllNodesOfBtreeForUniqueKey(TreeVariant &tree,std::string dbName, int64_t size, std::string tableName,std::string columnName, const std::vector<std::string> &sortedColumnsVector, const std::string &dataType)
{
    std::stringstream indexFileName;
    std::stringstream dataFileName;
    indexFileName << tableDirectory << "/" << dbName << "/" << tableName << ".index";
    dataFileName << tableDirectory << "/" << dbName << "/" << tableName << ".data";

    int64_t columnNameIndexInSortedOrder = -1;
    DEBUG_LOG("### --- startedSorted---");
    for(auto ele:sortedColumnsVector){
        DEBUG_LOG(ele);
    }
    
    for (int i = 0; i < sortedColumnsVector.size(); i++)
    {
        DEBUG_LOG(sortedColumnsVector[i] << " ");
        if (columnName == sortedColumnsVector[i])
        {
            columnNameIndexInSortedOrder = i + 1;
            break;
        }
    }
    std::cout<<"\n";

    if (columnNameIndexInSortedOrder == -1)
    {

        std::stringstream err;
        err << "unique key " << columnName << " does not exist in table " << tableDirectory;
        throw std::runtime_error(err.str());
    }

    DEBUG_LOG("INDEX FILE NAME " << indexFileName.str());
    DEBUG_LOG("DATATYPE IS " << dataType);
    if (MyUtility::checkIfFileExist(indexFileName.str()))
    {

        std::fstream indexFile(indexFileName.str(), std::ios::in | std::ios::out | std::ios::binary);
        std::fstream dataFile(dataFileName.str(), std::ios::in | std::ios::out | std::ios::binary);
        if (indexFile.fail())
        {
            throw std::runtime_error("Failed to read primary key  from index file");
        }

        if (dataFile.fail())
        {
            throw std::runtime_error("Failed to read data   from dataFile file");
        }

        int64_t indexFileSize = PagerHandler::getFileSize(indexFileName.str());
        DEBUG_LOG("index file size is " << indexFileSize);
        int64_t divider = 8 * (2 * size - 1);
        int64_t getTotalNoOfRows = (int64_t)(indexFileSize / divider);
        DEBUG_LOG("Total No Of rows " << getTotalNoOfRows);
        DEBUG_LOG("columnNameIndexSortedOrder " << columnNameIndexInSortedOrder);
        while (getTotalNoOfRows--)
        {
            std::variant<int64_t, std::string> id;
            int64_t start, end;
            std::streampos pos = indexFile.tellg();

            if (pos != std::streampos(-1))
            {
                int64_t offset = static_cast<int64_t>(pos);
                start = offset;
            }
            else
            {
                break;
            }

            indexFile.seekg(sizeof(int64_t), std::ios::cur);
            indexFile.seekg(2 * (columnNameIndexInSortedOrder - 2) * sizeof(int64_t), std::ios::cur);

            int64_t uniqueReadStartIndex, uniqueReadEndIndex;
            indexFile.read(reinterpret_cast<char *>(&uniqueReadStartIndex), sizeof(int64_t));
            indexFile.read(reinterpret_cast<char *>(&uniqueReadEndIndex), sizeof(int64_t));

            indexFile.seekg(-2 * sizeof(int64_t), std::ios::cur);
            indexFile.seekg(-2 * (columnNameIndexInSortedOrder - 2) * sizeof(int64_t), std::ios::cur);
            indexFile.seekg(-1 * sizeof(int64_t), std::ios::cur);

            dataFile.seekg(uniqueReadStartIndex, std::ios::beg);
            try
            {
                if (dataType == "int")
            {
                // Data file stores values as ASCII strings, not binary int64_t.
                // Read the string and convert to int64_t.
                uint64_t strSize = uniqueReadEndIndex - uniqueReadStartIndex;
                std::string strValue(strSize, '\0');
                dataFile.read(strValue.data(), strSize);
                DEBUG_LOG("VALUE READ (int) " << strValue);
                int64_t value = std::stoll(strValue);
                id = value;
            }
            else
            {
                uint64_t strSize = uniqueReadEndIndex - uniqueReadStartIndex;
                DEBUG_LOG("start " << uniqueReadStartIndex << " and end " << uniqueReadEndIndex);
                std::string value(strSize, '\0');
                dataFile.read(value.data(), strSize);
                DEBUG_LOG("VALUE READ " << value);
                id = value;
            }
            }
            catch(const std::exception& e)
            {
                std::cerr<<"erro at dataFileRead"<<"\n";
                std::cerr << e.what() << '\n';
            }
            

            // indexFile.seekg(sizeof(int64_t),std::ios::cur);

            // indexFile.read(reinterpret_cast<char *>(&start), sizeof(int64_t));
             indexFile.seekg(sizeof(int64_t), std::ios::cur);
            indexFile.seekg((2 * (size - 1)) * sizeof(int64_t), std::ios::cur);

            std::streampos endpos = indexFile.tellg();

            if (endpos != std::streampos(-1))
            {
                int64_t offset = static_cast<int64_t>(endpos);
                end = offset;
            }
            else
            {
                break;
            }

            IndexNode node{start, static_cast<int16_t>(end)};
            DEBUG_LOG("id ");
            std::visit([](auto &&value)
                       { DEBUG_LOG(value); }, id);
            DEBUG_LOG(" start " << start << " end " << end);

            std::visit([&](auto &treePtr, auto &&val)
                       {
                           using TreeType = std::decay_t<decltype(*treePtr)>;
                           using ValType = std::decay_t<decltype(val)>;
                           if constexpr (std::is_same_v<TreeType, BPlusTree<int64_t, IndexNode>>) {
                               if constexpr (std::is_same_v<ValType, int64_t>) {
                                   treePtr->insert(val, node);
                               } else {
                                   // convert string to int64_t 
                                   int64_t parsed = std::stoll(val);
                                   treePtr->insert(parsed, node);
                               }
                           } else if constexpr (std::is_same_v<TreeType, BPlusTree<std::string, IndexNode>>) {
                               if constexpr (std::is_same_v<ValType, std::string>) {
                                   treePtr->insert(val, node);
                               } else {
                                   treePtr->insert(std::to_string(val), node);
                               }
                           } },
                       tree, id);
        }
    }
    else
    {
        throw std::runtime_error("the table does not exist");
    }
}

void initializePrimaryIndexBtrees(std::string tabName,bool first)
{
    for (const auto &dbPair : globalTableCache)
    {
        const std::string &dbName = dbPair.first;
        const auto &tables = dbPair.second;

        for (const auto &tablePair : tables)
        {
            const std::string &tableName = tablePair.first;
            if (!first && tabName!=tableName) continue;
            if (tablePair.second.first!="-1") continue;
            const auto &columns = tablePair.second.second;
            int64_t noOfColumns = columns.size();

            std::vector<std::string> sortedColumnVector;
            for (const auto &columnPtr : columns)
            {             
                    sortedColumnVector.emplace_back(columnPtr->name);   
            }
            sort(sortedColumnVector.begin(), sortedColumnVector.end());

            for (const auto &columnPtr : columns)
            {
                DEBUG_LOG(columnPtr->name << " " << columnPtr->isPrimary << "hh");
                if (columnPtr->isPrimary)
                {
                    const std::string &columnName = columnPtr->name;
                    const std::string &type = columnPtr->type;

                    TreeVariant tree;

                    if (type == "int")
                    {
                        tree = std::make_shared<BPlusTree<int64_t, IndexNode>>();
                    }
                    else
                    {
                        std::cerr << "Unsupported primary key type: " << type
                                  << " for column: " << columnName << std::endl;
                        continue;
                    }
                    dbBtrees[dbName][tableName][columnName] = std::make_pair(std::move(tree), noOfColumns);
                    DEBUG_LOG("btreecalled");
                    loadAllNodesOfBtreeForPrimaryKey(dbBtrees[dbName][tableName][columnName].first,dbName, noOfColumns, tableName);

                    DEBUG_LOG("Initialized B+ Tree for " << dbName
                              << "." << tableName << "." << columnName << " " << "and size is " << noOfColumns);
                }
                else if (columnPtr->isUnique)
                {
                    std::cout << "for unique columns " << columnPtr->name << columnPtr->isUnique << "\n";
                    const std::string &columnName = columnPtr->name;
                    const std::string &type = columnPtr->type;

                    TreeVariant tree;

                    if (type == "int")
                    {
                        tree = std::make_shared<BPlusTree<int64_t, IndexNode>>();
                    }
                    else if (type == "varchar")
                    {
                        tree = std::make_shared<BPlusTree<std::string, IndexNode>>();
                    }

                    else
                    {
                        DEBUG_LOG("Unsupported Unique key type: " << type
                                  << " for column: " << columnName);
                        continue;
                    }

                    dbBtrees[dbName][tableName][columnName] = std::make_pair(std::move(tree), noOfColumns);
                    loadAllNodesOfBtreeForUniqueKey(dbBtrees[dbName][tableName][columnName].first,dbName, noOfColumns, tableName,columnName, sortedColumnVector, type);

                    DEBUG_LOG("Initialized B+ Tree for Unique Key" << dbName
                              << "." << tableName << "." << columnName << " " << "and size is " << noOfColumns);
                }
            }
        }
    }
}

#endif // __INITIAL_LOAD