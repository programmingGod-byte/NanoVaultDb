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
#include "hft.hpp"
#include "global.hpp"
#include "utility.hpp"

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
        for (auto& [tableName, _] : tables) {
            tablesToVacuum.emplace_back(dbName, tableName);
        }
    }

    for (auto& [db, table] : tablesToVacuum) {
        std::cout<<table<<"\n";
        PagerHandler::vacuumTable(db, table);
    }
}


void vacuumScheduler() {
    while (!shuttingDown.load()) {
        runVacuum();

        for (int i = 0; i < 360; i++) { 
            if (shuttingDown.load()) return;
            std::this_thread::sleep_for(std::chrono::seconds(60));
        }
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
    std::string currentDbMeta = "./db/current_db.meta";
    std::string dbName = getCurrentDatabase(currentDbMeta);
    if (dbName=="") return;
    currentDatabase = dbName;
    for (const auto &entry : fs::directory_iterator(dbDirectoryPath))
    {
        if (fs::is_regular_file(entry.status()))
        {
            std::string filename = entry.path().filename().string();

            if (filename.find(".db") != std::string::npos)
            {
                std::string dbname = MyUtility::extractBaseName(filename);
                std::cout << "dbname " << dbname << "\n";
                std::shared_ptr<PythonLikeJSONParser> parser = std::make_shared<PythonLikeJSONParser>();

                // Store parser in global cache
                globalJsonCache[dbname] = parser;

                std::string fullPath = dbDirectoryPath + "/" + filename;
                std::cout << "FULL PATH " << fullPath << "\n";
                if (!parser->loadFromFile(fullPath))
                {
                    std::cerr << "Failed to load file: " << fullPath << std::endl;
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
                        std::string symbolString = std::to_string(tablesArray[i][std::string("symbol")].getInt());
                        std::string  topString = std::to_string(tablesArray[i][std::string("top")].getInt());
                        bool isSymbol = false;
                        bool isTop = false;
                        MyUtility::appendToFile("error.txt", std::format("the tableName is {} and top string is {}",symbolString,topString));
                        if(!symbolString.empty()) isSymbol = true;
                        if(topString[0]=='1') isTop = true;
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

                            std::cout<<"the precision is "<<precision<<"\n";
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
                                dbBtrees[currentDatabase][tableName][columnDataName] = std::make_pair(tree, 0);
                            }
                        }
                        
                        // Save table columns in globalTableCache
                        globalTableCache[dbname][tableName] = std::move(columnNodes);
                        if(isSymbol){
                            int64_t symbol = static_cast<int64_t>(std::stoi(symbolString));

                            MyUtility::appendToFile("error.txt", std::format("the table name is {} symbol is {} isTop is {}",tableName,symbol,isTop));
                            HFT::symbolAccessArray[symbol].init(precisions,columnsArray.size(),isTop?1:0,symbol);
                        }

                        std::cout << "Loaded table: " << tableName << " from DB: " << dbname << std::endl;
                    }
                }
                catch (const std::exception &e)
                {
                    std::cerr << "Error accessing 'tables' in " << fullPath << ": " << e.what() << std::endl;
                }
            }
        }
    }
}

void loadAllNodesOfBtreeForPrimaryKey(TreeVariant &tree, int64_t size, std::string tableName)
{
    std::stringstream indexFileName;
    indexFileName << tableDirectory << "/" << currentDatabase << "/" << tableName << ".index";
    std::cout << "INDEX FILE NAME " << indexFileName.str() << "\n";
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

            IndexNode node{start, end};
            std::cout << "id " << id << " start " << start << "end " << end << "\n";
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

void loadAllNodesOfBtreeForUniqueKey(TreeVariant &tree, int64_t size, std::string tableName,std::string columnName, const std::vector<std::string> &sortedColumnsVector, const std::string &dataType)
{
    std::stringstream indexFileName;
    std::stringstream dataFileName;
    indexFileName << tableDirectory << "/" << currentDatabase << "/" << tableName << ".index";
    dataFileName << tableDirectory << "/" << currentDatabase << "/" << tableName << ".data";

    int64_t columnNameIndexInSortedOrder = -1;
    std::cout<<"### --- startedSorted---\n";
    for(auto ele:sortedColumnsVector){
        std::cout<<ele<<"\n";
    }
    
    for (int i = 0; i < sortedColumnsVector.size(); i++)
    {
        std::cout<<sortedColumnsVector[i]<<" ";
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

    std::cout << "INDEX FILE NAME " << indexFileName.str() << "\n";
    std::cout<<"DATATYPE IS "<<dataType<<"\n";
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
        std::cout << "index file size is " << indexFileSize << "\n";
        int64_t divider = 8 * (2 * size - 1);
        int64_t getTotalNoOfRows = (int64_t)(indexFileSize / divider);
        std::cout << "Total No Of rows " << getTotalNoOfRows << "\n";
        std::cout << "columnNameIndexSortedOrder " << columnNameIndexInSortedOrder << "\n";
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
                int64_t value;
                dataFile.read(reinterpret_cast<char *>(&value), sizeof(int64_t));
                id = value;
            }
            else
            {
                // read string: assume fixed length or length-prefixed
                uint64_t strSize = uniqueReadEndIndex - uniqueReadStartIndex + 1;
                std::cout<<"start "<<uniqueReadStartIndex<<" and end "<<uniqueReadEndIndex<<"\n";
                std::string value(strSize, '\0');
                dataFile.read(value.data(), strSize);
                std::cout<<"VALUE READ "<<value<<"\n";
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

            IndexNode node{start, end};
            std::cout << "id ";
            std::visit([](auto &&value)
                       { std::cout << value; }, id);
            std::cout << " start " << start << " end " << end << "\n";

            // Insert into the appropriate typed B+ tree by visiting both the tree variant and the id variant.
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

void initializePrimaryIndexBtrees()
{
    for (const auto &dbPair : globalTableCache)
    {
        const std::string &dbName = dbPair.first;
        const auto &tables = dbPair.second;

        for (const auto &tablePair : tables)
        {
            const std::string &tableName = tablePair.first;
            const auto &columns = tablePair.second;
            int64_t noOfColumns = columns.size();

            std::vector<std::string> sortedColumnVector;
            for (const auto &columnPtr : columns)
            {
               
                
                    sortedColumnVector.emplace_back(columnPtr->name);
                
            }
            sort(sortedColumnVector.begin(), sortedColumnVector.end());

            for (const auto &columnPtr : columns)
            {
                std::cout << columnPtr->name << " " << columnPtr->isPrimary << "hh\n";
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
                    std::cout<<"btreecalled"<<"\n";
                    loadAllNodesOfBtreeForPrimaryKey(dbBtrees[dbName][tableName][columnName].first, noOfColumns, tableName);

                    std::cout << "Initialized B+ Tree for " << dbName
                              << "." << tableName << "." << columnName << " " << "and size is " << noOfColumns << std::endl;
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
                        std::cerr << "Unsupported Unique key type: " << type
                                  << " for column: " << columnName << std::endl;
                        continue;
                    }

                    dbBtrees[dbName][tableName][columnName] = std::make_pair(std::move(tree), noOfColumns);
                    loadAllNodesOfBtreeForUniqueKey(dbBtrees[dbName][tableName][columnName].first, noOfColumns, tableName,columnName, sortedColumnVector, type);

                    std::cout << "Initialized B+ Tree for Unique Key" << dbName
                              << "." << tableName << "." << columnName << " " << "and size is " << noOfColumns << std::endl;
                }
            }
        }
    }
}

#endif // __INITIAL_LOAD