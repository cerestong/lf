#include "lf/logger.hh"

int32_t rough_log2(int64_t input)
{
    int32_t c = 0;
    if (input >= 0)
    {
        while ((input >> c) && (c < sizeof(int64_t)*8)) c++;
    }
    else
    {
        while(((input >> c) < -1) && (c < sizeof(int64_t)*8)) c++;
    }
    return c;
}

int main(int argc, char **argv)
{
    lf::g_stdout_logger_on = true;

    lf::log("rough_log2(1) %d", rough_log2(1));
    lf::log("rough_log2(10) %d", rough_log2(10));    
    lf::log("rough_log2(100) %d", rough_log2(100));    
    lf::log("rough_log2(1001) %d", rough_log2(1001));    
    lf::log("rough_log2(11111) %d", rough_log2(11111));    

    lf::log("rough_log2(-1) %d", rough_log2(-1));
    lf::log("rough_log2(-10) %d", rough_log2(-10));    
    lf::log("rough_log2(-100) %d", rough_log2(-100));    
    lf::log("rough_log2(-1001) %d", rough_log2(-1001));    
    lf::log("rough_log2(-11111) %d", rough_log2(-11111));

    lf::log("-10 %x", -10);
    lf::log("-10 >> 1 %x", -10 >> 1);    
        
    return 0;
}
