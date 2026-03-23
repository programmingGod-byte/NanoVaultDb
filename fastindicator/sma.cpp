#include "iostream"
#include "../utils//types.hpp"
#include "../hft.hpp"
#include "../FastIndicators.hpp"
#include <cstdint>

 class SMA {

    private:    
        alignas(CACHELINE) int64_t tick = 0;
        alignas(CACHELINE) int64_t count = 0;
        alignas(CACHELINE) int64_t window = 1;
        alignas(CACHELINE) int64_t column_to_use = -1;
        alignas(CACHELINE) int64_t running_sum = 0;

        alignas(CACHELINE) HFT::ColumnRing* columnRing = nullptr;
        alignas(CACHELINE) HFT::TableColumn* tableColumn = nullptr;
        alignas(CACHELINE) std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL>* symbolAccessArr;
        alignas(CACHELINE) const int64_t* precision = nullptr;

    public:
        SMA(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL>& symbolAccessArr,
            int64_t tick,
            HFT::TableColumn& tableColumn,
            int64_t column_to_use = -1)
        {
            this->symbolAccessArr = &symbolAccessArr;
            this->tick = tick;
            this->count = 0;
            this->tableColumn = &tableColumn;
            if (LIKELY(column_to_use >= 0 && column_to_use < HFT::MAXCOLUMN))
                this->columnRing = &tableColumn.history[column_to_use];
            else
                this->columnRing = nullptr;

            this->precision = tableColumn.precisions;
        }

        void set_parameter(int64_t window,int64_t column_to_use) {
            this->window = window;
            this->running_sum = 0;
            this->column_to_use = column_to_use;

            if (LIKELY(column_to_use >= 0 && column_to_use < HFT::MAXCOLUMN))
                this->columnRing = &this->tableColumn->history[column_to_use];
            else
                this->columnRing = nullptr;
        }

        inline int64_t result() const {
            return running_sum / window;
        }

        
        void on_tick() {
            if (!columnRing) return;

            count++;

            if (count != tick)
                return;

            count = 0;

            const int64_t latest = *columnRing->latest_ptr();

            int32_t head = columnRing->head;
            int32_t old_idx = (head - window - 1) & HFT::MAXRINGMASK;

            int64_t old_val = columnRing->get(old_idx);

            running_sum += latest;

            if (window <= HFT::MAXRINGSIZE)
                running_sum -= old_val;

            

        }
        // without static a member function pointer 
        static void run(void *p){
            static_cast<SMA*>(p)->on_tick();
        }


        FastIndicators::IndicatorEntry create(){
            return FastIndicators::IndicatorEntry{
                static_cast<void*>(this),
                &SMA::run
            };
        }
    };


