#include "core/model/instrument.h"

#include "core/model/units.h"

bool tr_instrument_validate(const tr_instrument_t *ins) {
    if (ins == 0) {
        return false;
    }
    if (ins->instrument_id == 0) {
        return false;
    }
    if (ins->broker_code[0] == '\0') {
        return false;
    }
    if (ins->market == TR_MARKET_UNKNOWN || ins->product_type == TR_PRODUCT_UNKNOWN) {
        return false;
    }
    if (!tr_scale_is_valid(ins->price_scale) || !tr_scale_is_valid(ins->qty_scale)) {
        return false;
    }
    if (ins->multiplier <= 0) {
        return false;
    }
    return true;
}
