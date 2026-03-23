
#ifndef __INDICATOR_HANDLER
#define __INDICATOR_HANDLER

#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <format>
#include "utility.hpp"
#include "global.hpp"
#include "hft.hpp"
#include "utils/types.hpp"

// symbolAccessArray , tick, TableColumn of that symbol

namespace FastIndicators {
    using TickFn = void(*)(void*);
    
    struct IndicatorEntry{
        void * ptr;
        TickFn fn;
    };

};



namespace IndicatorHandler {
    namespace fs = std::filesystem;

    void parseIndicators(std::unique_ptr<AddHftIndicatorStatement>&&statement){
        std::cout<<"INDICATOR PARSING START\n";
        std::string file_path = statement->file_path;
        std::stringstream error;
        file_path.erase(std::remove(file_path.begin(), file_path.end(), '\r'), file_path.end());
        if(UNLIKELY(!MyUtility::checkIfFileExist(file_path))){
            error << std::format("the file {} not exist ",file_path);
            std::cout<<error.str();
            throw std::runtime_error(error.str());
            
        }

        std::string baseName = MyUtility::extractBaseName(file_path);
        std::pair<bool,std::string> data = MyUtility::readAFile(file_path);
        if(!data.first){
            error << std::format("error while opening the  file {}  ",file_path);
            std::cout<<error.str();
            throw std::runtime_error(error.str());
        }

        if(!HFT::InitalStorage::checkIndicatorExists(baseName)){
             error << std::format("indicator with base name  {}  already exist ",baseName);
            std::cout<<error.str();
            throw std::runtime_error(error.str());
        }

        std::cout<<"CONTENT PRINTING\n";
        std::string content = (data.second);
        std::cout<<content<<"\n";

        fs::path dir = "./fastindicator";

        fs::create_directories(dir);

        fs::path filePath = dir / (baseName + ".cpp");

        std::ofstream outFile(filePath);

        if (!outFile) {
            throw std::runtime_error("Failed to save the indicator  try again ");
        }

        outFile << content;

        outFile.close(); 
    }
};


#endif