#ifndef MYUTILITY_UTILITY_HPP
#define MYUTILITY_UTILITY_HPP

#include <iostream>
#include <sstream>
#include <string>
#include <memory>
#include <stdexcept>
#include <filesystem> // Include for std::filesystem
#include <fstream>
#include <string_view>
#include <sys/stat.h>
#include "json.hpp"   // Assuming this is a necessary include
#include "global.hpp" // Assuming this is a necessary include
#include <utility>
#include "databaseSchemaReader.hpp"
namespace MyUtility
{                                   // Define a namespace called MyUtility
    namespace fs = std::filesystem; // Shorthand for std::filesystem


    std::string extractBaseName(const std::string& filename) {
    fs::path p(filename);
    std::string stem = p.stem().string();  // first .stem() call removes ".db"
    while (fs::path(stem).extension() != "") {
        stem = fs::path(stem).stem().string();  // repeat to strip ".something"
    }
    return stem;  // returns "hello"
}

    
    void appendToFile(const std::string_view & filePath,const std::string_view &cntent){
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
            std::ofstream outfile(filePath.data(),std::ios::app);
            outfile<<"\n";
            outfile<<cntent.data()<<"\n";
            outfile.close();

    }
    void createFile(const std::string &filePath, const std::string &content)
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

        std::ofstream outfile(filePath); // Create and open the file
        if (outfile.is_open())
        {
            outfile << content; // Write the content to the file
            outfile.close();    // Close the file
        }
        else
        {
            throw std::runtime_error("Error: Could not create or open file '" + filePath + "' for writing");
        }
    }

    bool checkIfFileExist(std::string filePath)
{
    filePath.erase(std::remove(filePath.begin(), filePath.end(), '\r'), filePath.end());
    filePath.erase(std::remove(filePath.begin(), filePath.end(), '\n'), filePath.end());

    std::error_code ec;
    bool ok = std::filesystem::exists(filePath, ec);

    if (ec)
        std::cout << "FS ERROR: " << ec.message() << "\n";

    return ok;
}

    std::pair<bool,std::string> readAFile(const std::string & filePath){
        if(checkIfFileExist(filePath)){
            std::ifstream file(filePath);

            if(!file.is_open()){
                return {false,nullptr};
            }
            std::stringstream buffer;
            buffer<<file.rdbuf();

            std::string content  =buffer.str();
            file.close();

            return {true,content    };
        }
        return {false,nullptr};
    }

    void changeCurrentDb(const std::string &newDbName)
    {

        std::ofstream outfile(currentDbPath);
        if (outfile.is_open())
        {
            std::string content = "{\"current_db\":\"" + newDbName + "\"}";
            outfile << content;
            outfile.close();
        }
        else
        {
            throw std::runtime_error("Error: Could not open file '" + currentDbPath);
        }
    }

    std::pair<bool, std::string> checkIfTableExist(const std::string &table)
    {
        std::stringstream s;
        s << "./db/";
        s << currentDatabase;
        s << ".shivam.db";
        PythonLikeJSONParser parser;

        if (checkIfFileExist(s.str()))
        {
            std::cout << "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n";
            parser.loadFromFile(s.str());
            JSONArrayWrapper columns = parser[0][std::string("tables")].asArray();
            for(size_t i = 0;i<columns.size();i++){
                std::string tableName = columns[i][std::string("name")];
                if( tableName== table){
                    return std::make_pair(true,"");
                }
            }
            
        }
        std::stringstream err;
        err << "the table name "<<table <<" not exist";
        return std::make_pair(false, err.str());
    }

} // namespace MyUtility



namespace PagerHandler
{

    struct RowIndex
    {
        int64_t row_start, row_end;
    };

    std::mutex tableIndexMutex;
    std::mutex tableDataMutex;
    int64_t getFileSize(const std::string &filename)
    {
        struct stat st;
        if (stat(filename.c_str(), &st) != 0)
            return 0;
        return st.st_size;
    }

    
    void vacuumTable(const std::string& db, const std::string& table)
    {
        std::lock_guard<std::mutex> lock(tableLocks[table]);

        std::string base = tableDirectory + "/" + db + "/" + table;

        std::fstream oldIndex(base + ".index", std::ios::in | std::ios::binary);
        std::fstream oldData (base + ".data",  std::ios::in | std::ios::binary);
        std::fstream oldDel  (base + ".delete",std::ios::in | std::ios::binary);

        std::cout<<"old files opened"<<"\n";

        if (oldIndex.fail() || oldData.fail() || oldDel.fail())
            return; // table deleted / corrupted → skip safely

        std::fstream newIndex(base + ".index.new", std::ios::out | std::ios::binary);
        std::fstream newData (base + ".data.new",  std::ios::out | std::ios::binary);
        std::fstream newDel  (base + ".delete.new",std::ios::out | std::ios::binary);
        std::cout<<"new file created"<<"\n";

        auto& cols = globalTableCache[db][table];
        std::cout<<"globaltable cache"<<"\n";
        size_t colCount = cols.size();

        int64_t rowSize =
            sizeof(int64_t) + (colCount - 1) * sizeof(PagerHandler::RowIndex);

        int64_t indexSize = PagerHandler::getFileSize(base + ".index");
        int64_t rowCount  = indexSize / rowSize;

        int64_t newDataOffset = 0;

        for (int64_t row = 0; row < rowCount; row++) {
            uint8_t deleted;
            oldDel.seekg(row);
            oldDel.read(reinterpret_cast<char*>(&deleted), 1);

            oldIndex.seekg(row * rowSize);

            if (deleted == 1) {
                continue;
            }
            std::cout<<"deleted skippped"<<"\n";
            // ---- copy ID ----
            int64_t id;
            oldIndex.read(reinterpret_cast<char*>(&id), sizeof(int64_t));
            newIndex.write(reinterpret_cast<char*>(&id), sizeof(int64_t));

            // ---- copy columns ----
            for (size_t c = 1; c < colCount; c++) {
                int64_t start, end;
                oldIndex.read(reinterpret_cast<char*>(&start), sizeof(int64_t));
                oldIndex.read(reinterpret_cast<char*>(&end), sizeof(int64_t));

                std::cout<<"old index read"<<"\n";

                int64_t len = end - start;
                std::string buf(len, '\0');

                oldData.seekg(start);
                oldData.read(buf.data(), len);

                std::cout<<"old data read"<<"\n";

                int64_t newStart = newDataOffset;
                int64_t newEnd   = newStart + len;

                newData.write(buf.data(), len);
                std::cout<<"new data written"<<"\n";
                newIndex.write(reinterpret_cast<char*>(&newStart), sizeof(int64_t));
                newIndex.write(reinterpret_cast<char*>(&newEnd),   sizeof(int64_t));
                std::cout<<"new index written"<<"\n";
                newDataOffset = newEnd;
            }

            uint8_t alive = 0;
            newDel.write(reinterpret_cast<char*>(&alive), 1);
        }

        oldIndex.close(); oldData.close(); oldDel.close();
        newIndex.close(); newData.close(); newDel.close();
        std::cout<<"old files closed"<<"\n";
        auto replaceFile = [](const std::string& newF, const std::string& oldF) {
            if (std::filesystem::exists(oldF))
                std::filesystem::remove(oldF);
            std::filesystem::rename(newF, oldF);
        };

        replaceFile(base + ".index.new",  base + ".index");
        std::cout<<"base index replace"<<"\n";
        replaceFile(base + ".data.new",   base + ".data");
        std::cout<<"base data replace"<<"\n";
        replaceFile(base + ".delete.new", base + ".delete");
        std::cout<<"base delete replace"<<"\n";
    }


    void insertRow(std::string primaryName, std::vector<std::pair<std::string, std::pair<std::string, bool>>> data, std::string tableName)
    {
        if (data.empty())
        {
            throw std::runtime_error("Cannot insert empty row data");
        }

        std::stringstream indexFileName;
        indexFileName << tableDirectory << "/" << currentDatabase << "/" << tableName << ".index";

        std::stringstream dataFileName;
        dataFileName << tableDirectory << "/" << currentDatabase << "/" << tableName << ".data";

        if (!MyUtility::checkIfFileExist(indexFileName.str()))
        {
            throw std::runtime_error("Table " + tableName + " does not exist. Create it first.");
        }

        if (!MyUtility::checkIfFileExist(dataFileName.str()))
        {
            throw std::runtime_error("Data file for table " + tableName + " does not exist.");
        }

        int64_t currentIndexFileSize = getFileSize(indexFileName.str());
        int64_t colsize = data.size();

        std::lock_guard<std::mutex> indexLock(tableIndexMutex);
        std::lock_guard<std::mutex> dataLock(tableDataMutex);

        std::fstream indexFile(indexFileName.str(), std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
        std::fstream dataFile(dataFileName.str(), std::ios::in | std::ios::out | std::ios::binary | std::ios::app);

        if (!indexFile.is_open())
        {
            throw std::runtime_error("Failed to open index file: " + indexFileName.str());
        }
        if (!dataFile.is_open())
        {
            throw std::runtime_error("Failed to open data file: " + dataFileName.str());
        }

        int64_t newRowId;

        if (currentIndexFileSize == 0)
        {
            newRowId = 1;
            std::cout << "Inserting first row (ID: " << newRowId << ")\n";
        }
        else
        {
            int64_t rowEntrySize = sizeof(int64_t) + colsize * sizeof(RowIndex);
            int64_t lastRowIdPos = currentIndexFileSize - rowEntrySize;

            indexFile.seekg(lastRowIdPos);
            int64_t lastRowId;
            indexFile.read(reinterpret_cast<char *>(&lastRowId), sizeof(int64_t));

            if (indexFile.fail())
            {
                throw std::runtime_error("Failed to read last row ID from index file");
            }

            newRowId = lastRowId + 1;
            std::cout << "Inserting new row (ID: " << newRowId << ")\n";
        }

        indexFile.seekp(0, std::ios::end);
        dataFile.seekp(0, std::ios::end);
        std::stringstream deleteFileName;
        deleteFileName << tableDirectory << "/" << currentDatabase << "/" << tableName << ".delete";

        std::fstream deleteFile(
            deleteFileName.str(),
            std::ios::in | std::ios::out | std::ios::binary
        );

        // if file doesn't exist yet, create it
        if (!deleteFile.is_open()) {
            deleteFile.open(deleteFileName.str(),
                std::ios::out | std::ios::binary);
            deleteFile.close();
            deleteFile.open(deleteFileName.str(),
                std::ios::in | std::ios::out | std::ios::binary);
        }
        uint8_t alive = 0;

        deleteFile.seekp(newRowId - 1, std::ios::beg);
        deleteFile.write(reinterpret_cast<char*>(&alive), 1);

        deleteFile.flush();
        indexFile.write(reinterpret_cast<const char *>(&newRowId), sizeof(int64_t));
        if (indexFile.fail())
        {
            throw std::runtime_error("Failed to write row ID to index file");
        }

        for (size_t i = 0; i < data.size(); i++)
        {
            const std::string &columnData = data[i].second.first;

            int64_t start = dataFile.tellp();

            dataFile.write(columnData.c_str(), columnData.size());
            if (dataFile.fail())
            {
                throw std::runtime_error("Failed to write column data to data file");
            }

            int64_t end = dataFile.tellp();

            RowIndex entry{start, end};
            indexFile.write(reinterpret_cast<const char *>(&entry), sizeof(RowIndex));
            if (indexFile.fail())
            {
                throw std::runtime_error("Failed to write index entry");
            }

            std::cout << "Column " << i << ": '" << columnData
                      << "' stored at [" << start << "-" << end << "]\n";
        }

        dataFile.flush();
        indexFile.flush();

        std::cout << "Successfully inserted row with ID: " << newRowId << "\n";
    }

};

// for this does not cahnge the function name just make it thread safe
#endif