#pragma once
#include "iostream"
#include "../utils//types.hpp"
#include "../hft.hpp"
#include "../FastIndicators.hpp"

class alignas(CACHELINE) OBI {

private:
    int64_t tick = 1;
    int64_t count = 0;
    int64_t obi_value = 0;

    HFT::TableColumn* tableColumn = nullptr;

    // scaled imbalance (to avoid float)

public:
    OBI(std::array<HFT::TableColumn, HFT::MAXHFTSYMBOL>& /*symbolAccessArr*/,
        int64_t tick,
        HFT::TableColumn& tableColumn,int64_t /*column_to_use*/ = -1)
    {
        this->tick = tick;
        this->count = 0;
        this->tableColumn = &tableColumn;
    }

    inline int64_t result() const {
        return obi_value;
    }

    void on_tick() {
        if (!tableColumn || !tableColumn->isTopOrderBook)
            return;

        count++;
        if (count != tick)
            return;

        count = 0;

        const int64_t* book = tableColumn->topOrderBook;

        const int64_t ask_qty = book[1];
        const int64_t bid_qty = book[3];

        const int64_t denom = ask_qty + bid_qty;

        if (UNLIKELY(denom == 0)) {
            obi_value = 0;
            return;
        }

        
        // range: [-1e6, +1e6]
        obi_value = ((bid_qty - ask_qty) * HFT::SCALINGFACTOR) / denom;
    }

    static inline void run(void* p) {
        static_cast<OBI*>(p)->on_tick();
    }

    void set_parameter(const std::vector<std::string> & /*params*/){
        
    }

    FastIndicators::IndicatorEntry create() {
         FastIndicators::IndicatorEntry e;
    e.checked = 1;
    e.ptr = this;
    e.fn = &OBI::run;
    e.indicatorIndex = -1;
    return e;
    }
};
