#pragma once
#include <stdint.h>

namespace lf { namespace sql{

    class Field {
    public:
        // 
        uint8_t *key_compare_encode();
    };

}} // end namespace
